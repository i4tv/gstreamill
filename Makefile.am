## Makefile.am -- Process this file with automake to produce Makefile.in
SUBDIRS = src etc usr

EXTRA_DIST = examples tools test

install-exec-hook:
	id gstreamill || test "$$?" -eq 9 || { \
	    useradd -G video,audio gstreamill; \
	}
	chmod u+s $(bindir)/gstreamill
	test -s /var/gstreamill || { \
	    mkdir -p /var/gstreamill/in /var/gstreamill/out; \
	    chown -R root:gstreamill /var/gstreamill; \
	    chmod -R g+w /var/gstreamill; \
	}
	test -s /var/log/gstreamill || { \
	    mkdir -p /var/log/gstreamill; \
	    chown -R root:gstreamill /var/log/gstreamill; \
	    chmod -R g+w /var/log/gstreamill; \
	}
	test -s /var/run/gstreamill || { \
	    mkdir -p /var/run/gstreamill; \
	    chown -R root:gstreamill /var/run/gstreamill; \
	    chmod -R g+w /var/run/gstreamill; \
	}
	test -s /usr/local/share/gstreamill && { \
	    chown -R root:gstreamill /usr/local/share/gstreamill; \
	    chmod -R g+w /usr/local/share/gstreamill; \
	}
	test -s /etc/gstreamill.d || { \
	    mkdir -p /etc/gstreamill.d; \
	    chown -R root:gstreamill /etc/gstreamill.d; \
	    chmod -R g+w /etc/gstreamill.d; \
	}
	test -s /usr/lib/systemd/system/gstreamill.service && { \
	    systemctl enable gstreamill; \
	    install -c -m 755 tools/startjobs.sh '/usr/local/bin/startjobs.sh'; \
	} || true
