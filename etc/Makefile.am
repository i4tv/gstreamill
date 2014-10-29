
gstreamillconfdir = /etc/gstreamill.d
gstreamillconf_DATA = \
        gstreamill.d/gstreamill.conf

install-exec-hook:
	test -s /etc/init && { \
	    install -c -m 755 init/gstreamill.conf '/etc/init/gstreamill.conf'; \
	} || true
	test -s /etc/gstreamill.d || { \
	    mkdir -p /etc/gstreamill.d/jobs.d; \
	    chown -R root:gstreamill /etc/gstreamill.d; \
	    chmod -R g+w /etc/gstreamill.d; \
	}
