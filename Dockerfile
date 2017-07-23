FROM alpine:edge

MAINTAINER Zhang Ping, dqzhangp@163.com

# install required packages
RUN apk add --update && \
        apk add git autoconf automake libtool gettext make flex bison pkgconfig && \
        apk add gstreamer=1.12.1-r2 && \
        apk add gstreamer-tools=1.12.1-r2 && \
        apk add gstreamer-dev=1.12.1-r2 && \
        apk add gst-plugins-base-dev=1.12.1-r2 && \
        apk add gst-plugins-bad-dev=1.12.1-r2 && \
        apk add gst-plugins-base=1.12.1-r2 && \
        apk add gst-plugins-good=1.12.1-r2 && \
        apk add gst-plugins-bad=1.12.1-r2 && \
        apk add gst-plugins-ugly=1.12.1-r2 && \
        apk add gst-libav=1.12.1-r2

# install gsreamill
RUN apk add gcc=6.4.0-r4 musl-dev=1.1.16-r15 augeas-dev=1.8.0-r1
#        git clone https://github.com/i4tv/gstreamill.git && \
ADD . gstreamill
RUN cd gstreamill && \
#        git checkout v0.9.1 && \
        ./autogen.sh && \
        ./configure --prefix=/usr && \
        make && \
        make install && \
	cd / && rm -rf gstreamill

CMD mount -o remount -o size=10240M /dev/shm && gstreamill -d 

EXPOSE 20118
EXPOSE 20119
