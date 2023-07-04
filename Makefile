
OBJS= housewiz_device.o housewiz.o
LIBOJS=

SHARE=/usr/local/share/house

# Local build ---------------------------------------------------

all: housewiz

clean:
	rm -f *.o *.a housewiz

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housewiz: $(OBJS)
	gcc -Os -o housewiz $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

# Distribution agnostic file installation -----------------------

install-files:
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housewiz
	cp housewiz /usr/local/bin
	chown root:root /usr/local/bin/housewiz
	chmod 755 /usr/local/bin/housewiz
	mkdir -p $(SHARE)/public/wiz
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/wiz
	cp public/* $(SHARE)/public/wiz
	chown root:root $(SHARE)/public/wiz/*
	chmod 644 $(SHARE)/public/wiz/*
	touch /etc/default/housewiz

uninstall-files:
	rm -rf $(SHARE)/public/wiz
	rm -f /usr/local/bin/housewiz

purge-config:
	rm -rf /etc/house/wiz.config /etc/default/housewiz

# Distribution agnostic systemd support -------------------------

install-systemd:
	cp systemd.service /lib/systemd/system/housewiz.service
	chown root:root /lib/systemd/system/housewiz.service
	systemctl daemon-reload
	systemctl enable housewiz
	systemctl start housewiz

uninstall-systemd:
	if [ -e /etc/init.d/housewiz ] ; then systemctl stop housewiz ; systemctl disable housewiz ; rm -f /etc/init.d/housewiz ; fi
	if [ -e /lib/systemd/system/housewiz.service ] ; then systemctl stop housewiz ; systemctl disable housewiz ; rm -f /lib/systemd/system/housewiz.service ; systemctl daemon-reload ; fi

stop-systemd: uninstall-systemd

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-files install-systemd

uninstall-debian: uninstall-systemd uninstall-files

purge-debian: uninstall-debian purge-config

# Void Linux install --------------------------------------------

install-void: install-files

uninstall-void: uninstall-files

purge-void: uninstall-void purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

