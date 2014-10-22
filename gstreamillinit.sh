#!/bin/sh

useradd -G video,audio gstreamill

# /var/log/gstreamill, log directory
mkdir -p /var/log/gstreamill
chown -R root:gstreamill /var/log/gstreamill
chmod -R g+w /var/log/gstreamill

# /var/gstreamill, media directory
mkdir -p /var/gstreamill/in /var/gstreamill/out
chown -R root:gstreamill /var/gstreamill
chmod -R g+w /var/gstreamill

# /etc/gstreamill.d/ jobs directory
mkdir -p /etc/gstreamill.d
chown -R root:gstreamill /etc/gstreamill.d
chmod -R g+w /etc/gstreamill.d

# /usr/local/share/gstreamill, managment directory
mkdir -p /usr/local/share/gstreamill
chown -R root:gstreamill /usr/local/share/gstreamill
chmod -R g+r /usr/local/share/gstreamill

# /var/run/gstreamill, pid file directory
mkdir -p /var/run/gstreamill
chown -R root:gstreamill /var/run/gstreamill
chmod -R g+w /var/run/gstreamill
