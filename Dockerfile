FROM alpine:edge

MAINTAINER Zhang Ping, dqzhangp@163.com

# install required packages
RUN apk add --update && \
        apk add git autoconf automake libtool gettext make flex bison pkgconfig && \
        apk add gstreamer-dev=1.12.1-r2 && \
        apk add gst-plugins-base-dev=1.12.1-r2 && \
        apk add gst-plugins-bad-dev=1.12.1-r2

# install gsreamill
RUN apk add libaugeas-dev && \
        git clone https://github.com/i4tv/gstreamill.git && \
        cd gstreamill && \
        git checkout v0.9.1 && \
        ./autogen.sh && \
        ./configure --prefix=/usr && \
        make && \
        make install && \
	cd / && rm -rf gstreamill

CMD mount -o remount -o size=10240M /dev/shm && gstreamill -d 

EXPOSE 20118
EXPOSE 20119
