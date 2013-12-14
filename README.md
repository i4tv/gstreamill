# Introduction

## Overview

gstreamill is an open source, GPL licensed "stream mill" based on gstreamer-1.0. To send query to a gstreamill server to provoke a live encode, transcode or recoder job. query is carried in http post, post body is the job to be provoke, job is descripted in json.

## Highlight

   * support hls, http progressive streaminig, udp multicast, udp unicast output.
   * based on gstreamer, support all codecs and containers that gstreamer suppport.
   * can be extented to support new codec, container and protocol by new gstreamer plugins.
   * RESTful management interface, allowing easy integration into operator environment.
   * live job run in subprocess
   * live job subprocess can be restarted on error

## Application

   * dvb to ip gateway in ott

# Install from source

gstreamill have been tested in ubuntu 13.10.

## Prerequisites

   * gnome-common
   * autoconf
   * automake
   * libtool
   * gstreamer-devel
   * gstreamer-plugins-base-devel

## Compile and install

    ./autogen.sh
    ./configure (--help)
    make
    make install

# Start using gstreamill

## help

    gstreamill -h

## start gstreamill

    gstreamill [--gst-debug=gstreamill:x]

for --gst-debug, please reference gst-launch

## stop gstreamill

    gstreamill -s

## start a job over http use curl

    curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/start

test.job is job description in json, can be found in examples directory.

## stop a job

    curl http://localhost:20118/stop/job_name

## output

* http progressive streaming

    http://localhost:20119/live/job name/encoder/encoder_index

* hls

    http://localhost:20119/live/job name/encoder/encoder_index/playlist.m3u8

* udp

    udp://@ip:port

## query stat

query gstreamill stat:

    curl http://localhost:20118/stat/gstreamill[/sub/item]

query gstreamer information:

    curl http://localhost:20118/stat/gstreamer[/plugin]

## debug job descript

    gstreamill -j job_descript_file
