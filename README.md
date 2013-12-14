# INTRODUCTION

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

# INSTALL

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

# USING GSTREAMILL

## command line

* help

    gstreamill -h

    Usage:
      gstreamill [OPTION...]

      Help Options:
        -h, --help                        Show help options
        --help-all                        Show all help options
        --help-gst                        Show GStreamer Options

      Application Options:
        -j, --job                         -j /full/path/to/job.file: Specify a job file, full path is must.
        -l, --log                         -l /full/path/to/log: Specify log path, full path is must.
        -m, --httpmgmt                    -m http managment service address.
        -a, --httpstreaming               -a http streaming service address.
        -s, --stop                        Stop gstreamill.
        -v, --version                     display version information and exit.

* start gstreamill

    gstreamill

* stop gstreamill

    gstreamill -s

* debug job descript

    gstreamill -j job_descript_file

## management interface

* start a job over http use curl

    curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/start

test.job is job description in json, can be found in examples directory.

* stop a job

    curl http://localhost:20118/stop/job_name

* query gstreamill stat:

    curl http://localhost:20118/stat/gstreamill[/sub/item]

* query gstreamer information:

    curl http://localhost:20118/stat/gstreamer[/plugin]

## output

* http progressive streaming

    http://localhost:20119/live/job name/encoder/encoder_index

* hls

    http://localhost:20119/live/job name/encoder/encoder_index/playlist.m3u8

* udp

    udp://@ip:port

