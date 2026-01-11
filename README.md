# HouseWiz

A House web service to control Philips WiZ devices (lights, plugs..)

## Overview

This is a web server to give access to Philips WiZ WiFi devices. This server can sense the current status and control the state of each device. The web API is meant to be compatible with the [House control API](https://github.com/pascal-fb-martin/houseportal/blob/master/controlapi.md).

This service is not really meant to be accessed directly by end-users: these should use [houselights](https://github.com/pascal-fb-martin/houselights) to control WiZ devices.

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/wiz.json (see below)

Otherwise installing [houselights](https://github.com/pascal-fb-martin/houselights) is recommended, but not necessarily on the same computer.

## Configuration

The preferred method is to configure the WiZ devices from the Configure web page.
The configuration is stored in file /etc/house/wiz.json. A typical example of configuration is:

```
{
    "wiz" : {
        "devices" : [
            {
                "name" : "light1",
                "address" : "wiz_eeeeee",
                "description" : "a WiZ light"
            },
            {
                "name" : "light2",
                "address" : "wiz_ffffff",
                "description" : "another WiZ light"
            }
        ]
    }
}
```

## Device Setup

Each device must be setup using the WiZ Connected phone app. The protocol for setting up devices has not been reverse engineered at that time.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
make debian-package
```

