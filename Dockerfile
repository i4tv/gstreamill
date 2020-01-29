FROM ubuntu:18.04

MAINTAINER Zhang Ping, dqzhangp@163.com

# install required packages
RUN apt-get update && \
        apt-get install -y git autoconf automake libtool pkg-config autopoint gettext liborc-0.4-dev make libglib2.0-dev flex bison && \
        git clone git://anongit.freedesktop.org/gstreamer/gstreamer && \
        cd gstreamer && \
        git checkout 1.16.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install && \
	cd / && rm -rf gstreamer
RUN apt-get install -y libpango1.0-dev libasound2-dev && \
        git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-base && \
        cd gst-plugins-base && \
        git checkout 1.16.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install && \
	cd / && rm -rf gst-plugins-base
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-good && \
        cd gst-plugins-good && \
        git checkout 1.16.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install && \
	cd / && rm -rf gst-plugins-good
RUN apt-get install -y libmpeg2-4-dev libmad-ocaml-dev libmp3lame-dev liba52-0.7.4-dev libx264-dev && \
        git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-ugly && \
        cd gst-plugins-ugly && \
        git checkout 1.16.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-fatal-warnings && \
        make && \
        make install && \
	cd / && rm -rf gst-plugins-ugly
RUN apt-get install -y libvoaacenc-ocaml-dev libsoup2.4-dev librtmp-dev libx265-dev --fix-missing && \
        git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-bad && \
        cd gst-plugins-bad && \
        git checkout 1.16.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gl && \
        make && \
        make install && \
	cd / && rm -rf gst-plugins-bad
RUN apt-get install -y yasm && \
        git clone git://anongit.freedesktop.org/gstreamer/gst-ffmpeg && \
        cd gst-ffmpeg && \
        git checkout 1.16.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-fatal-warnings && \
        make && \
        make install && \
	cd / && rm -rf gst-libav

# install gsreamill
RUN apt-get install -y libaugeas-dev && \
        git clone https://github.com/i4tv/gstreamill.git
RUN     cd gstreamill && \
        git pull && \
        git checkout v1.0.0 && \
        ./autogen.sh && \
        ./configure --prefix=/usr && \
        make && \
        make install && \
	cd / && rm -rf gstreamill

CMD mount -o remount -o size=10240M /dev/shm && gstreamill -d 

EXPOSE 20118
EXPOSE 20119
