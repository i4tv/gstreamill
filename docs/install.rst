Gstreamill install
******************

Gstreamill have been tested on Ubuntu 14.04 and CentOS 7.0.

Install gstreamill from source
==============================

Prerequisites
-------------

* gnome-common
* autoconf
* automake
* libtool
* libgstreamer1.0
* libgstreamer-plugins-base1.0
* libaugeas

Compile and install
-------------------

Build and install gstreamill::

    ./autogen.sh
    ./configure (--help)
    make
    make install
