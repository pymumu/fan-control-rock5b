# MIT License

# Copyright (c) 2022 Nick Peng

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

PKG_CONFIG := pkg-config
DESTDIR :=
PREFIX := /usr
SBINDIR := $(PREFIX)/sbin
SYSCONFDIR := /etc
RUNSTATEDIR := /var/run
SYSTEMDSYSTEMUNITDIR := $(shell ${PKG_CONFIG} --variable=systemdsystemunitdir systemd)
FAN_CONTROL_SYSTEMD = systemd/fan-control.service
VER := "1.0.0"

.PHONY: all clean install FAN_CONTROL_BIN package
all: package 

FAN_CONTROL_BIN: $(FAN_CONTROL_SYSTEMD)
	$(MAKE) $(MFLAGS) -C src all 

$(FAN_CONTROL_SYSTEMD): systemd/fan-control.service.in
	cp $< $@
	sed -i 's|@SBINDIR@|$(SBINDIR)|' $@
	sed -i 's|@SYSCONFDIR@|$(SYSCONFDIR)|' $@
	sed -i 's|@RUNSTATEDIR@|$(RUNSTATEDIR)|' $@

package: FAN_CONTROL_BIN
	cd package && ./make.sh	-o $(shell pwd) --ver ${VER}

clean:
	$(MAKE) $(MFLAGS) -C src clean  
	$(RM) $(FAN_CONTROL_SYSTEMD)
	$(RM) fan-control*.deb

install: FAN_CONTROL_BIN 
	install -v -m 0755 -D -t $(DESTDIR)$(SYSCONFDIR)/init.d etc/init.d/fan-control
	install -v -m 0640 -D -t $(DESTDIR)$(SYSCONFDIR) etc/fan-control.json
	install -v -m 0755 -D -t $(DESTDIR)$(SBINDIR) src/fan-control
	install -v -m 0644 -D -t $(DESTDIR)$(SYSTEMDSYSTEMUNITDIR) systemd/fan-control.service

