#!/bin/sh

aclocal
libtoolize --force
autoheader
automake --foreign --add-missing --copy
autoconf
