#!/bin/sh

autoreconf --verbose --force --install --make || {
         echo 'autogen.sh failed';
          exit 1;
}
