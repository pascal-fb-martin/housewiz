# HouseWiz - A simple home web server for control of Philips Wiz devices.
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

HAPP=housewiz
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS= housewiz_device.o housewiz.o
LIBOJS=

all: housewiz

clean:
	rm -f *.o *.a housewiz

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housewiz: $(OBJS)
	gcc -Os -o housewiz $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

# Distribution agnostic file installation -----------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housewiz
	cp housewiz $(HROOT)/bin
	chown root:root $(HROOT)/bin/housewiz
	chmod 755 $(HROOT)/bin/housewiz
	mkdir -p $(SHARE)/public/wiz
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/wiz
	cp public/* $(SHARE)/public/wiz
	chown root:root $(SHARE)/public/wiz/*
	chmod 644 $(SHARE)/public/wiz/*
	touch /etc/default/housewiz

uninstall-app:
	rm -rf $(SHARE)/public/wiz
	rm -f $(HROOT)/bin/housewiz

purge-app:

purge-config:
	rm -rf /etc/house/wiz.config /etc/default/housewiz

# System installation. ------------------------------------------

include $(SHARE)/install.mak


