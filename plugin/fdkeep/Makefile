# This optional warmctl plugin is built and shipped by the fork release.
CC ?= cc
ROOT ?= ../..
PREFIX ?= /opt/warmctl/dmtcp
CFLAGS ?= -O2 -Wall

.PHONY: all install clean
all: libdmtcp_fdkeep.so

libdmtcp_fdkeep.so: fdkeep.c raw-syscalls.h
	$(CC) $(CFLAGS) -shared -fPIC -I$(ROOT)/include -o $@ fdkeep.c

install: all
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/share/doc/dmtcp/fdkeep
	install -m 0755 libdmtcp_fdkeep.so $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 LICENSE README.md $(DESTDIR)$(PREFIX)/share/doc/dmtcp/fdkeep/

clean:
	rm -f libdmtcp_fdkeep.so
