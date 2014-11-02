## Makefile.am -- Process this file with automake to produce Makefile.in
SUBDIRS = src usr etc

EXTRA_DIST = examples tools test etc

install-exec-hook:
	id gstreamill || test "$$?" -eq 9 || { \
	    useradd gstreamill; \
	}
	usermod -a -G video,audio gstreamill;
	chmod u+s $(bindir)/gstreamill
	test -s /var/lib/gstreamill || { \
	    mkdir -p /var/lib/gstreamill/in /var/lib/gstreamill/out; \
	}
	chown -R root:gstreamill /var/lib/gstreamill;
	chmod -R g+w /var/lib/gstreamill;
	test -s /var/log/gstreamill || { \
	    mkdir -p /var/log/gstreamill; \
	}
	chown -R root:gstreamill /var/log/gstreamill;
	chmod -R g+w /var/log/gstreamill;
	test -s /usr/share/gstreamill && { \
	    chown -R root:gstreamill /usr/share/gstreamill; \
	    chmod -R g+w /usr/share/gstreamill; \
	}
	test -s /usr/lib/systemd/system/gstreamill.service && { \
	    systemctl enable gstreamill; \
	    install -c -m 755 tools/startjobs.sh '/usr/local/bin/startjobs.sh'; \
	} || true
