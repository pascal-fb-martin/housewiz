# HouseWiz
A House web service to read and control Philips Wiz devices (lights, plugs..)
## Overview
This is a web server to give access to philips Wiz WiFi devices. This server can sense the current status and control the state of each device. The web API is meant to be compatible with the House control API (e.g. the same web API as [houserelays](https://github.com/pascal-fb-martin/houserelays)).
This service is not really meant to be accessed directly by end-users: these should use [houselights](https://github.com/pascal-fb-martin/houselights) to control WiZ devices.
## Installation
* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/housewiz.json (see below)

Otherwise installing [houselights](https://github.com/pascal-fb-martin/houselights) is recommended, but not necessarily on the same computer.
## Configuration
The preferred method is to configure the Wiz devices from the Configure web page.
The configuration is stored in file /etc/house/wiz.json. A typical example of configuration is:
```
{
    "wiz" : {
        "devices" : [
            {
                "name" : "light1",
                "address" : "wiz_eeeeee",
                "description" : "a Wiz light"
            },
            {
                "name" : "light2",
                "address" : "wiz_ffffff",
                "description" : "another Wiz light"
            }
        ]
    }
}
```
## Device Setup
Each device must be setup using the Wiz Connected phone app. The protocol for setting up devices has not been reverse engineered at that time.

