FROM ubuntu:14.04.2

MAINTAINER Zhang Ping, dqzhangp@163.com
# install required packages
RUN apt-get update
RUN apt-get install -y git autoconf automake libtool pkg-config autopoint gettext liborc-0.4-dev make libglib2.0-dev flex bison
RUN git clone git://anongit.freedesktop.org/gstreamer/gstreamer
RUN     cd gstreamer && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install
RUN apt-get install -y libpango1.0-dev
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-base
RUN     cd gst-plugins-base && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-good
RUN     cd gst-plugins-good && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install
RUN apt-get install -y libmpeg2-4-dev libmad-ocaml-dev libmp3lame-dev liba52-0.7.4-dev libx264-dev
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-ugly
RUN cd gst-plugins-ugly && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-fatal-warnings && \
        make && \
        make install
RUN apt-get update
RUN apt-get install -y libvoaacenc-ocaml-dev --fix-missing
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-bad
RUN     cd gst-plugins-bad && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gl && \
        make && \
        make install
RUN apt-get install -y yasm
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-libav
RUN     cd gst-libav && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-fatal-warnings && \
        make && \
        make install
RUN apt-get install -y libaugeas-dev
RUN git clone https://github.com/i4tv/gstreamill.git
RUN     cd gstreamill && \
        ./autogen.sh && \
        ./configure --prefix=/usr && \
        make && \
        make install

EXPOSE 20118
EXPOSE 20119
