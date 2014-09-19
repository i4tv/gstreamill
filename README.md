# INTRODUCTION

## Overview

gstreamill is an open source, GPL licensed "stream mill" based on gstreamer-1.0. To send query to a gstreamill server to provoke a live encode, transcode or recoder job. query is carried in http post, post body is the job to be provoke, job is descripted in json.

If you would like to donate to help support gstreamill development use [Gittip](https://www.gittip.com/zhangping/)

## Highlight

   * hls, http progressive streaminig, udp output.
   * hls push mode output via webdav.
   * Multi-Rate with GOP Alignment.
   * RESTful management interface, allowing easy integration into operator environment.
   * Job is descript in json.
   * Job run in subprocess, and auto restart on error.
   * Base on gstreamer and easy to extend.
   * B/S Management.

## Application

    IP --------+ 
               |                      +------- UDP
    CVBS ------+    +------------+    |               +---- http put
               +----+ gstreamill +----+------ M3U8----+
    SDI -------+    +------+-----+    |               +---- http get
               |           |          +------ HTTP
    LIVE ------+           |
                           |
             REST interface (json over http post)

# INSTALL

gstreamill have been tested in ubuntu 13.10.

## Prerequisites

   * gnome-common
   * autoconf
   * automake
   * libtool
   * libgstreamer1.0-dev
   * libgstreamer-plugins-base1.0-dev
   * libaugeas-dev

## Compile and install

    ./autogen.sh
    ./configure (--help)
    make
    make install

# USING GSTREAMILL

## Prerequisites

   * gstreamer1.0-plugins-ugly
   * gstreamer1.0-plugins-bad
   * gstreamer1.0-plugins-good
   * gstreamer1.0-plugins-base

## B/S Management

http://gstreamill.ip:20118/admin/

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

default management port is 20118, invoke test job: ```curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/admin/start```

* invode a job in foreground:

    gstreamill -j job_descript_file

* access output use vlc

    http progressive download:
    
        http://host.name.or.ip:20119/live/test/encoder/0

    m3u8:
    
        http://host.name.or.ip:20119/live/test/playlist.m3u8

## management interface

* start a job over http use curl

    curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/admin/start

test.job is job description in json, can be found in examples directory.

* stop a job

    curl http://host.name.or.ip:20118/admin/stop/job_name

job_name is value of the "name" field in job description.

* query gstreamill stat:

    curl http://host.name.or.ip:20118/stat/gstreamill
    curl http://host.name.or.ip:20118/stat/gstreamill/job/test

* query gstreamer information:

    curl http://host.name.or.ip:20118/stat/gstreamer[/plugin]

## output

job name is the value of name of job description.

* http progressive streaming

    http://host.name.or.ip:20119/live/job name/encoder/encoder_index

* hls

    http://host.name.or.ip:20119/live/job name/playlist.m3u8

    http://host.name.or.ip:20119/live/job name/encoder/encoder_index/playlist.m3u8

* udp

    udp://@ip:port

## JOB DESCRIPTION

JOB file structure:

    {
        'name' : 'cctv2',
        'debug' : '3',
        'is-live' : false,
        'log-path' : '/home/zhangping/tmp/cctv2',
        'source' : {
            ...
        },
        'encoders' : [
            ...
        ],
        'm3u8streaming' : {
            ...
        }
    }

name : job name

debug : debug option, reference gst-launch gst-debug, optional.

is-live : true for live source, false otherwise, for example transcode, default is true.

log-path : dont log to default log direcotry for non-live source, log to log-path if it is presented.

source : source of encoders.

encoders : encoders.

m3u8streaming : m3u8 streaming

structure of source:

    "source" : {
        "elements" : {
            ...
        },
        "bins" : [
            ...
        ]
    }

structure of elements:

    "elements" : {
        "element" : {
            "property" : {
                ...
            },
            "caps" : "..."
        },
        ...
    }

structure of bins:

    "bins" : [
        bin,
        bin,
        ...
    ]

bins is an array of bin, syntax of bin is like gst-launch, for example:

    "udpsrc ! queue ! tsdemux name=demuxer",
    "demuxer.video ! queue ! mpeg2dec ! queue ! appsink name = video",
    "demuxer.audio ! mpegaudioparse ! queue ! mad ! queue ! appsink name = audio" 

in this example, first bin with tsdemux has sometimes pads, second and third bin link with first bin: demuxer.video and demuxer.audio. second bin with appsink named video and third bin with appsink named audio. source bins must have bin with appsink that is corespond endoders' source.

structure of encoders:

    "encoders" : [
        encoder,
        ...
    ]

encoders is an array of encoder, encoder is an object, every encoder correspneds an encode output, that means gstreamill support mutilty bitrate output.

structure of encoder:

    {
        "elements" : {
            ...
        },
        "bins" : [
            ...
        ],
        "udpstreaming" : "uri"
    }

elements and bins is just the same as source structure in syntax, the differnce is encoder bins must have bins with appsrc element, appsrc must have name property, the value of name is the same as appsink name value in source bins. udpstreaming uri is udp streaming output uri, it's optional.

m3u8streaming is hls output, it's optional:

    "m3u8streaming" : {
        "version" : 3,
        "window-size" : 10,
        "segment-duration" : 3.00,
        "push-server-uri" : "http://192.168.56.3/test"
    }

push-server-uri, push m3u8 to web server use http webdav. If you use nginx, note that the webdav module of nginx is not built by default, it should be enabled with the --with-http_dav_module configuration parameter. nginx conf examples:

    #user  nobody;
    worker_processes  1;
    
    #error_log  logs/error.log;
    #error_log  logs/error.log  notice;
    #error_log  logs/error.log  info;
    
    #pid        logs/nginx.pid;
    
    
    events {
        worker_connections  1024;
    }
    
    
    http {
    
        client_max_body_size 20M;

        server {
            location / {
                root /home/zhangping/publish;
                client_body_temp_path /home/zhangping/tmp;
    
                dav_methods  PUT DELETE MKCOL COPY MOVE;
    
                create_full_put_path   on;
                dav_access             group:rw  all:r;
    
                limit_except  GET {
                    allow  192.168.0.0/16;
                    deny   all;
                }
            }
        }
    }

There are examples in examples directory of source.

Talk about gstreamill on [gstreamill](https://groups.google.com/forum/#!forum/gstreamill) or report a bug on [issues](https://github.com/zhangping/gstreamill/issues) page.
