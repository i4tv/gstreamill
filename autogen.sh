#!/bin/sh

which gnome-autogen.sh || {
    echo "You need to install gnome-common!"
    exit 1
}

USE_GNOME2_MACROS=1 . gnome-autogen.sh
