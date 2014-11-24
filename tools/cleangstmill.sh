#!/bin/sh

rm -rf /var/run/gstreamill/gstreamill.pid
mount -t mqueue none /var/run/gstreamill/mq
mkdir -p /var/run/gstreamill/mq
rm -rf /var/run/gstreamill/mq/*
umount /var/run/gstreamill/mq
rm -rf /dev/shm/*
