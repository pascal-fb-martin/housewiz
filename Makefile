
OBJS= housewiz_device.o housewiz.o
LIBOJS=

SHARE=/usr/local/share/house

all: housewiz

clean:
	rm -f *.o *.a housewiz

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housewiz: $(OBJS)
	gcc -g -O -o housewiz $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

install:
	if [ -e /etc/init.d/housewiz ] ; then systemctl stop housewiz ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housewiz /etc/init.d/housewiz
	cp housewiz /usr/local/bin
	cp init.debian /etc/init.d/housewiz
	chown root:root /usr/local/bin/housewiz /etc/init.d/housewiz
	chmod 755 /usr/local/bin/housewiz /etc/init.d/housewiz
	mkdir -p $(SHARE)/public/wiz
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/wiz
	cp public/* $(SHARE)/public/wiz
	chown root:root $(SHARE)/public/wiz/*
	chmod 644 $(SHARE)/public/wiz/*
	touch /etc/default/housewiz
	systemctl daemon-reload
	systemctl enable housewiz
	systemctl start housewiz

uninstall:
	systemctl stop housewiz
	systemctl disable housewiz
	rm -f /usr/local/bin/housewiz /etc/init.d/housewiz
	systemctl daemon-reload

purge: uninstall
	rm -rf /etc/house/wiz.config /etc/default/housewiz

