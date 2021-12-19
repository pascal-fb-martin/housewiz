/* HouseWiz - A simple home web server for control of Philips Wiz WiFi Devices
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housewiz_device.c - Control a Philips Wiz WiFi Device.
 *
 * SYNOPSYS:
 *
 * const char *housewiz_device_initialize (int argc, const char **argv);
 *
 *    Initialize this module at startup.
 *
 * int housewiz_device_changed (void);
 *
 *    Indicate if the configuration was changed due to discovery, which
 *    means it must be saved.
 *
 * const char *housewiz_device_live_config (char *buffer, int size);
 *
 *    Recover the current live config, typically to save it to disk after
 *    a change has been detected.
 *
 * const char *housewiz_device_refresh (const char *reason);
 *
 *    Re-evaluate the configuration after it changed.
 *
 * int housewiz_device_count (void);
 *
 *    Return the number of configured devices available.
 *
 * const char *housewiz_device_name (int point);
 *
 *    Return the name of a wiz device.
 *
 * const char *housewiz_device_failure (int point);
 *
 *    Return a string describing the failure, or a null pointer if healthy.
 *
 * int    housewiz_device_commanded (int point);
 * time_t housewiz_device_deadline (int point);
 *
 *    Return the last commanded state, or the command deadline, for
 *    the specified wiz device.
 *
 * int housewiz_device_get (int point);
 *
 *    Get the actual state of the device.
 *
 * int housewiz_device_set (int point, int state, int pulse);
 *
 *    Set the specified point to the on (1) or off (0) state for the pulse
 *    length specified. The pulse length is in seconds. If pulse is 0, the
 *    device is maintained to the requested state until a new state is issued.
 *
 *    Return 1 on success, 0 if the device is not known and -1 on error.
 *
 * void housewiz_device_periodic (void);
 *
 *    This function must be called every second. It runs the Wiz device
 *    discovery and ends the expired pulses.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <net/if.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "echttp.h"
#include "echttp_json.h"
#include "houselog.h"

#include "housewiz_device.h"
#include "housewiz_config.h"


// This offset is used to "sign" an ID that contains a device index.
// This is more for debug _and simulation_ purpose: this module does
// not uses the value of the ID from received messages. A simulator
// might however use it to identify the context.
//
#define WIZ_ID_OFFSET 12000

struct DeviceMap {
    char name[32];
    char macaddress[16];
    char description[256];
    struct sockaddr_in ipaddress;
    time_t detected;
    int status;
    int commanded;
    time_t pending;
    time_t deadline;
    time_t last_sense;
};

static int DeviceListChanged = 0;

static struct DeviceMap *Devices;
static int DevicesCount = 0;
static int DevicesSpace = 0;

static int WizDevicePort = 38899;
static int WizStatusPort = 38900;

static int WizSocket = -1;
static struct sockaddr_in WizBroadcast;


struct NetworkMap {
    char name[32];
    char ip[20];
    char mac[16];
};

#define NETWORKS_MAX 8
static struct NetworkMap Networks[NETWORKS_MAX];
static int NetworksCount = 0;


static void safecpy (char *dest, const char *src, int limit) {
    strncpy (dest, src, limit-1);
    dest[limit-1] = 0;
}

int housewiz_device_count (void) {
    return DevicesCount;
}

int housewiz_device_changed (void) {
    if (DeviceListChanged) {
        DeviceListChanged = 0;
        return 1;
    }
    return 0;
}

const char *housewiz_device_name (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].name;
}

int housewiz_device_commanded (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].commanded;
}

time_t housewiz_device_deadline (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].deadline;
}

const char *housewiz_device_failure (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    if (!Devices[point].detected) return "silent";
    return 0;
}

int housewiz_device_get (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].status;
}

static void housewiz_device_socket (void) {

    WizBroadcast.sin_family = AF_INET;
    WizBroadcast.sin_port = htons(WizStatusPort);
    WizBroadcast.sin_addr.s_addr = INADDR_ANY;

    WizSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (WizSocket < 0) {
        houselog_trace (HOUSE_FAILURE, "DEVICE",
                        "cannot open UDP socket: %s", strerror(errno));
        exit(1);
    }

    if (bind(WizSocket,
             (struct sockaddr *)(&WizBroadcast),
             sizeof(WizBroadcast)) < 0) {
        houselog_trace (HOUSE_FAILURE, "SOCKET",
                        "cannot bind to UDP port %d: %s",
                        WizStatusPort, strerror(errno));
        exit(1);
    }

    int value = 1;
    if (setsockopt(WizSocket, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) < 0) {
        houselog_trace (HOUSE_FAILURE, "SOCKET",
                        "cannot broadcast: %s", strerror(errno));
        exit(1);
    }

    WizBroadcast.sin_port = htons(WizDevicePort);
    WizBroadcast.sin_addr.s_addr = INADDR_BROADCAST;

    houselog_trace (HOUSE_INFO, "DEVICE", "UDP port %d is now open", WizStatusPort);
}

static void housewiz_device_send (const struct sockaddr_in *a, const char *d) {
    if (echttp_isdebug()) {
        long ip = ntohl((long)(a->sin_addr.s_addr));
        int port = ntohs(a->sin_port);
        printf ("Sending packet to %d.%d.%d.%d(port %d): %s\n",
                (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, ip&0xff, port, d);
    }
    int sent = sendto (WizSocket, d, strlen(d)+1, 0,
                       (struct sockaddr *)a, sizeof(struct sockaddr_in));
    if (sent < 0)
        houselog_trace
            (HOUSE_FAILURE, "DEVICE", "sendto() error: %s", strerror(errno));
}

static void housewiz_device_sense (const struct sockaddr_in *a, int id) {
    int i;
    char buffer[256];
    for (i = 0; i < NetworksCount; i++) {
        snprintf (buffer, sizeof(buffer),
                  "{\"method\": \"registration\", \"id\": %d, \"params\":"
                      "{\"phoneIp\": \"%s\", \"register\":true, "
                      "\"phoneMac\":\"%s\"}}",
                  id, Networks[i].ip, Networks[i].mac);
        housewiz_device_send (a, buffer);
    }
}

static void housewiz_device_control (int device, int state) {
    char buffer[256];
    snprintf (buffer, sizeof(buffer),
          "{\"method\": \"setPilot\", \"id\": %d, \"env\":\"pro\", \"params\": {\"state\": %s}}",
          WIZ_ID_OFFSET+device, state?"true":"false");
    housewiz_device_send (&(Devices[device].ipaddress), buffer);
}

int housewiz_device_set (int device, int state, int pulse) {

    const char *namedstate = state?"on":"off";
    time_t now = time(0);

    if (device < 0 || device > DevicesCount) return 0;

    if (echttp_isdebug()) {
        if (pulse) fprintf (stderr, "set %s to %s at %ld (pulse %ds)\n", Devices[device].name, namedstate, time(0), pulse);
        else       fprintf (stderr, "set %s to %s at %ld\n", Devices[device].name, namedstate, time(0));
    }

    if (pulse > 0) {
        Devices[device].deadline = now + pulse;
        houselog_event ("DEVICE", Devices[device].name, "SET",
                        "%s FOR %d SECONDS", namedstate, pulse);
    } else {
        Devices[device].deadline = 0;
        houselog_event ("DEVICE", Devices[device].name, "SET", "%s", namedstate);
    }
    Devices[device].commanded = state;
    Devices[device].pending = now + 5;

    // Only send a command if we detected the device on the network.
    //
    if (Devices[device].detected) {
        housewiz_device_control (device, state);
    }
    return 1;
}

static int housewiz_device_enumerate_add (const char *name) {
    int i;
    for (i = 0; i < NetworksCount; i++) {
        if (!strcmp (name, Networks[i].name)) return i;
    }
    // Not found: add new entry if there is room, or else overwrite the last.
    if (NetworksCount >= NETWORKS_MAX) NetworksCount = NETWORKS_MAX - 1;
    safecpy (Networks[NetworksCount].name, name, sizeof(Networks[0].name));
    Networks[NetworksCount].ip[0] = 0;
    Networks[NetworksCount].mac[0]= 0;
    return NetworksCount++;
}

static void housewiz_device_enumerate (void) {

    static char bin2hex[] = "0123456789abcdef";
    int i;
    struct ifaddrs *interfaces;
    struct ifaddrs *cursor;

    NetworksCount = 0; // Erase the previous list.

    if (getifaddrs(&interfaces) < 0) return;
    for (cursor = interfaces; cursor; cursor = cursor->ifa_next) {
        if (cursor->ifa_flags & IFF_LOOPBACK) continue;
        if (!cursor->ifa_addr) continue;
        if (cursor->ifa_addr->sa_family == AF_INET) {
            int idx = housewiz_device_enumerate_add (cursor->ifa_name);
            struct sockaddr_in *ia = (struct sockaddr_in *) (cursor->ifa_addr);
            long ip = ntohl((long)(ia->sin_addr.s_addr));
            snprintf (Networks[idx].ip, sizeof(Networks[0].ip),
                      "%d.%d.%d.%d",
                      (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, ip&0xff);
        } else if (cursor->ifa_addr->sa_family == AF_PACKET) {
            struct sockaddr_ll *mac = (struct sockaddr_ll*) (cursor->ifa_addr);
            if (mac->sll_halen >= sizeof(Networks[0].mac)) continue;
            int idx = housewiz_device_enumerate_add (cursor->ifa_name);
            for (i = 0; i < mac->sll_halen; i++) {
                Networks[idx].mac[i*2] = bin2hex[(mac->sll_addr[i])/16];
                Networks[idx].mac[i*2+1] = bin2hex[(mac->sll_addr[i])&15];
            }
            Networks[idx].mac[i*2] = 0;
        }
    }
    if (echttp_isdebug()) {
        for (i = 0; i < NetworksCount; i++) {
            fprintf (stderr, "Interface %s: IP %s, MAC %s\n",
                     Networks[i].name, Networks[i].ip, Networks[i].mac);
        }
    }
}

static void housewiz_device_reset (int i, int status) {
    Devices[i].commanded = Devices[i].status = status;
    Devices[i].pending = Devices[i].deadline = 0;
}

void housewiz_device_periodic (time_t now) {

    static time_t LastRetry = 0;
    static time_t LastSense = 0;
    int i;

    if (now >= LastSense + 60) {
        housewiz_device_enumerate();
        housewiz_device_sense(&WizBroadcast, 1);
        LastSense = now;
    }

    if (now < LastRetry + 5) return;
    LastRetry = now;

    for (i = 0; i < DevicesCount; ++i) {

        if (now >= Devices[i].last_sense + 35) {
            housewiz_device_sense(&(Devices[i].ipaddress), WIZ_ID_OFFSET+i);
            Devices[i].last_sense = now;
        }

        // If we did not detect a device for 3 senses, consider it failed.
        if (Devices[i].detected > 0 && Devices[i].detected < now - 100) {
            houselog_event ("DEVICE", Devices[i].name, "SILENT",
                            "MAC ADDRESS %s", Devices[i].macaddress);
            housewiz_device_reset (i, 0);
            Devices[i].detected = 0;
        }

        if (Devices[i].deadline > 0 && now >= Devices[i].deadline) {
            houselog_event ("DEVICE", Devices[i].name, "RESET", "END OF PULSE");
            Devices[i].commanded = 0;
            Devices[i].pending = now + 5;
            Devices[i].deadline = 0;
        }
        if (Devices[i].status != Devices[i].commanded) {
            if (Devices[i].pending > now) {
                if (Devices[i].detected) {
                    const char *state = Devices[i].commanded?"on":"off";
                    houselog_event ("DEVICE", Devices[i].name, "RETRY", state);
                    housewiz_device_control (i, Devices[i].commanded);
                }
            } else {
                // The ongoing command timed out, forget and cleanup.
                if (Devices[i].pending)
                    houselog_event ("DEVICE", Devices[i].name, "TIMEOUT", "");
                housewiz_device_reset (i, Devices[i].status);
            }
        }
    }
}

const char *housewiz_device_refresh (const char *reason) {

    int i;
    int devices;

    houselog_event ("CONFIG", "wiz", "ACTIVATING", "%s", reason);

    for (i = 0; i < DevicesCount; ++i) {
        Devices[i].name[0] = 0;
        Devices[i].macaddress[0] = 0;
        Devices[i].description[0] = 0;
        Devices[i].deadline = 0;
        Devices[i].pending = 0;
        Devices[i].detected = 0;
    }
    DevicesCount = 0;

    if (housewiz_config_size() > 0) {
        devices = housewiz_config_array (0, ".wiz.devices");
        if (devices < 0) return "cannot find devices array";

        DevicesCount = housewiz_config_array_length (devices);
        if (echttp_isdebug()) fprintf (stderr, "found %d devices\n", DevicesCount);
    }
    DevicesSpace = DevicesCount + 32;

    Devices = calloc(sizeof(struct DeviceMap), DevicesSpace);
    if (!Devices) return "no more memory";

    for (i = 0; i < DevicesCount; ++i) {
        int device;
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        device = housewiz_config_object (devices, path);
        if (device <= 0) continue;
        const char *name = housewiz_config_string (device, ".name");
        if (name)
            safecpy (Devices[i].name, name, sizeof(Devices[i].name));
        const char *mac = housewiz_config_string (device, ".address");
        if (mac)
            safecpy (Devices[i].macaddress, mac, sizeof(Devices[i].macaddress));
        const char *desc = housewiz_config_string (device, ".description");
        if (desc)
            safecpy (Devices[i].description, desc,
                     sizeof(Devices[i].description));

        if (echttp_isdebug())
            fprintf (stderr, "load device %s, MAC address %s (%s)\n",
                     Devices[i].name, Devices[i].macaddress,
                     desc?desc:"no description");
        housewiz_device_reset (i, Devices[i].status); // Use last status known.
    }
    return 0;
}

const char *housewiz_device_live_config (char *buffer, int size) {

    static char pool[65537];
    static ParserToken token[1024];
    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));

    int i;

    int root = echttp_json_add_object (context, 0, 0);
    int top = echttp_json_add_object (context, root, "wiz");
    int items = echttp_json_add_array (context, top, "devices");

    for (i = 0; i < DevicesCount; ++i) {
        if (Devices[i].name[0] == 0 || Devices[i].macaddress[0] == 0) continue;
        int device = echttp_json_add_object (context, items, 0);
        echttp_json_add_string (context, device, "name", Devices[i].name);
        echttp_json_add_string (context, device, "address", Devices[i].macaddress);
        echttp_json_add_string
            (context, device, "description", Devices[i].description);
    }
    return echttp_json_export (context, buffer, size);
}

static int housewiz_device_mac_search (const char *macaddress) {
    int i;
    for (i = 0; i < DevicesCount; ++i) {
        if (!strcasecmp(macaddress, Devices[i].macaddress)) return i;
    }
    return -1;
}

static void housewiz_device_receive (int fd, int mode) {

    int status;
    int manual;
    char data[256];
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    int size = recvfrom (WizSocket, data, sizeof(data), 0,
                         (struct sockaddr *)(&addr), &addrlen);
    if (size > 0) {
        data[size] = 0;
        if (echttp_isdebug()) fprintf (stderr, "Received: %s\n", data);

        ParserToken json[256];
        int jsoncount = 256;

        // We need to copy to preserve the original data (JSON decoding is
        // destructive).
        //
        char buffer[256];
        safecpy (buffer, data, sizeof(buffer));

        const char *error = echttp_json_parse (buffer, json, &jsoncount);
        if (error) {
            houselog_trace (HOUSE_FAILURE, "DEVICE", "%s: %s", error, data);
            return;
        }

        // Retrieve MAC address and current device state from the JSON data.
        //
        int method = echttp_json_search (json, ".method");
        if ((method < 0) || (json[method].type != PARSER_STRING)) {
            houselog_trace (HOUSE_FAILURE,
                            "DEVICE", "no valid method in: %s", data);
            return;
        }
        if (strcmp (json[method].value.string, "firstBeat") &&
            strcmp (json[method].value.string, "syncPilot")) return;

        int macaddr = echttp_json_search (json, ".params.mac");
        if ((macaddr < 0) || (json[macaddr].type != PARSER_STRING)) {
            houselog_trace (HOUSE_FAILURE,
                            "DEVICE", "no valid MAC address in: %s", data);
            return;
        }
        const char *mac = json[macaddr].value.string;
        int device = housewiz_device_mac_search (mac);

        if (!strcmp (json[method].value.string, "firstBeat")) {
            manual = 1; // Someone just turned it on manually, do not fight.
        } else {
            int state = echttp_json_search (json, ".params.state");
            if ((state < 0) || (json[state].type != PARSER_BOOL)) {
                houselog_trace (HOUSE_FAILURE,
                                "DEVICE", "no valid state in: %s", data);
                return;
            }
            status = json[state].value.bool;
            manual = 0;
        }

        if (device < 0 && DevicesCount < DevicesSpace) {
            if (echttp_isdebug()) fprintf (stderr, "new device %s\n", mac);
            DeviceListChanged = 1;
            if (DevicesCount >= DevicesSpace) return;
            device = DevicesCount++;
            snprintf (Devices[device].name, sizeof(Devices[0].name), "wiz%d", device+1);
            snprintf (Devices[device].macaddress, sizeof(Devices[0].macaddress),
                      "%s", mac);
            snprintf (Devices[device].description, sizeof(Devices[0].description),
                      "autogenerated");
            houselog_event ("DEVICE", Devices[device].name, "ADDED",
                            "MAC ADDRESS %s", mac);
            Devices[device].detected = time(0); // Skip the "DETECTED" event.
            Devices[device].last_sense = 0;
        }
        if (device >= 0) {
            if (!Devices[device].detected)
                houselog_event ("DEVICE", Devices[device].name, "DETECTED",
                                "MAC ADDRESS %s", mac);
            Devices[device].detected = time(0);

            if (manual) {
                Devices[device].commanded = status = 1;
                Devices[device].pending = 0;
                housewiz_device_control (device, status); // Acknowledge.
            }
            if (Devices[device].status != status) {
                if (Devices[device].pending) {
                    if (status == Devices[device].commanded) {
                        houselog_event ("DEVICE", Devices[device].name,
                                        "CONFIRMED", "FROM %s TO %s",
                                        Devices[device].status?"on":"off",
                                        status?"on":"off");
                        Devices[device].pending = 0; // Command complete.
                    }
                } else {
                    houselog_event ("DEVICE", Devices[device].name,
                                    manual?"OPERATED":"CHANGED",
                                    "FROM %s TO %s",
                                    Devices[device].status?"on":"off",
                                    status?"on":"off");
                    Devices[device].commanded = status; // By someone else.
                }
                Devices[device].status = status;
            }

            memcpy (&(Devices[device].ipaddress),
                    &addr, sizeof(Devices[device].ipaddress));
            Devices[device].ipaddress.sin_port = htons(WizDevicePort);
        }
    }
}

const char *housewiz_device_initialize (int argc, const char **argv) {
    housewiz_device_socket ();
    echttp_listen (WizSocket, 1, housewiz_device_receive, 0);
    return housewiz_device_refresh ("ON STARTUP");
}

