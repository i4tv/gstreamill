Gstreamill install
******************

Gstreamill have been tested on Ubuntu 16.04.2 and CentOS 7.0.

Install gstreamill from source on ubuntu 14.04
==============================================

Install the prerequistites::

    sudo apt-get install autoconf automake libtool libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libaugeas-dev

get lastest release tarball::

    wget https://github.com/i4tv/gstreamill/archive/v0.8.5.tar.gz

unpack tarball::

    tar xvf v0.8.5.tar.gz

Build and install gstreamill::

    cd gstreamill-x.y.z
    ./autogen.sh
    ./configure
    make
    make install

Start gstreamill::

    sudo service gstreamill start
