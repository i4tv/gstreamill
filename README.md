# INTRODUCTION

## Overview

gstreamill is an open source, GPL licensed "stream mill" based on gstreamer-1.0. To send query to a gstreamill server to provoke a live encode, transcode or recoder job. query is carried in http post, post body is the job to be provoke, job is descripted in json.

gstreamill is under active development and isn't production ready.

## Highlight

   * hls, http progressive streaminig, udp output.
   * Multi-Rate with GOP Alignment.
   * RESTful management interface, allowing easy integration into operator environment.
   * Job is descript in json.
   * Job run in subprocess, and auto restart on error.
   * Base on gstreamer and easy to extend.

## Application

       IP --------+ 
                  |                      +------- UDP
       CVBS ------+    +------------+    |
                  +----+ gstreamill +----+------ M3U8
       SDI -------+    +------------+    |
                  |                      +------ HTTP
       LIVE ------+

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

* invoke a job

default management port is 20118, invoke test job as flowing:

    curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/start

* invode a job in foreground:

    gstreamill -j job_descript_file

* access output use vlc

    http://localhost:20119/live/test/encoder/0

or

    http://localhost:20119/live/test/playlist.m3u8

## management interface

* start a job over http use curl

    curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/start

test.job is job description in json, can be found in examples directory.

* stop a job

    curl http://localhost:20118/stop/job_name

job_name is value of the "name" field in job description.

* query gstreamill stat:

    curl http://localhost:20118/stat/gstreamill
    curl http://localhost:20118/stat/gstreamill/livejob/test

* query gstreamer information:

    curl http://localhost:20118/stat/gstreamer[/plugin]

## output

* http progressive streaming

    http://localhost:20119/live/job name/encoder/encoder_index

* hls

    http://localhost:20119/live/job name/encoder/encoder_index/playlist.m3u8

* udp

    udp://@ip:port

## JOB DESCRIPTION

    {
        "name" : "test",
        "debug" : "gstreamill:3",
        "source" : {
            "elements" : {
                "videotestsrc" : {
                    "caps" : "video/x-raw,width=720,height=576,framerate=25/1"
                },
                "audiotestsrc" : {
                    "property" : {
                        "wave" : 8
                    }
                }
            },
            "bins" : [
                "videotestsrc ! appsink name=video",
                "audiotestsrc ! appsink name=audio"
            ]
        },
        "encoders" : [
            {
                "elements": {
                    "appsrc" : {
                        "property" : {
                            "format" : 3,
                            "is-live" : true
                        }
                    },
                    "x264enc" : {
                        "property" : {
                            "bitrate" : 1000,
                            "byte-stream" : true,
                            "threads" : 4,
                            "bframes" : 3
                        }
                    },
                    "faac" : {
                        "property" : {
                            "name" : "faac",
                            "outputformat" : 1
                        }
                    },
                    "appsink" : {
                        "property" : {
                            "sync" : false
                        }
                    }
                },
                "bins" : [
                    "appsrc name=video ! queue ! x264enc ! queue ! muxer.",
                    "appsrc name=audio ! audioconvert ! audioresample ! voaacenc ! muxer.",
                    "mpegtsmux name=muxer ! queue ! appsink"
                ]
            }
        ],
        "m3u8streaming" : {
            "version" : 3,
            "window-size" : 10,
            "segment-duration" : 3.00
        }
    }

There are more examples in examples directory of source.

Talk about gstreamill on [gstreamill](https://groups.google.com/forum/#!forum/gstreamill) or report a bug on [issues](https://github.com/zhangping/gstreamill/issues) page.
