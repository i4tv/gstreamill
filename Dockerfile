FROM ubuntu:14.04.2

MAINTAINER Zhang Ping, dqzhangp@163.com
# install required packages
RUN apt-get update
RUN apt-get install -y git autoconf automake libtool pkg-config autopoint gettext liborc-0.4-dev make libglib2.0-dev flex bison
RUN git clone git://anongit.freedesktop.org/gstreamer/gstreamer && \
        cd gstreamer && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gst-debug && \
        make && \
        make install
RUN apt-get install -y libpango1.0-dev
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-base && \
        cd gst-plugins-base && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gst-debug && \
        make && \
        make install
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-good && \
        cd gst-plugins-good && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gst-debug && \
        make && \
        make install
RUN apt-get install -y libmpeg2-4-dev libmad-ocaml-dev libmp3lame-dev liba52-0.7.4-dev libx264-dev
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-ugly
ADD ./752604.patch /gst-plugins-ugly/
RUN cd gst-plugins-ugly && \
        git checkout 1.5.2 && \
        patch -p1 < 752604.patch && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc && \
        make && \
        make install
RUN apt-get install -y libvoaacenc-ocaml-dev
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-plugins-bad && \
        cd gst-plugins-bad && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gst-debug --disable-gl && \
        make && \
        make install
RUN apt-get install -y yasm
RUN git clone git://anongit.freedesktop.org/gstreamer/gst-libav && \
        cd gst-libav && \
        git checkout 1.5.2 && \
        ./autogen.sh --prefix=/usr --disable-gtk-doc-pdf --disable-gtk-doc --disable-gst-debug && \
        make && \
        make install
