#!/bin/sh

set -x
aclocal
libtoolize --force
automake --foreign --add-missing --copy
autoconf
autoheader
