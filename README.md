# Table of Contents

* [INTRODUCTION](#introduction)
  * [Overview](#overview)
  * [Highlight](#highlight)
  * [Application](#application)
* [INSTALL](#install)
  * [Prerequisites](#prerequisites)
  * [Compile and install](#compile-and-install)
* [USING GSTREAMILL](#using-gstreamill)
  * [Prerequisites](#prerequisites)
  * [B/S Management URL](#b/s-management-url)
  * [Command line](#command-line)
  * [Management interface](#management-interface)
  * [Output](#output)
* [JOB DESCRIPTION](#job-description)

# INTRODUCTION

## Overview

gstreamill is an open source, based on gstreamer-1.0 with dvr support and hls output. 

## Highlight

   * hls, http progressive streaminig, udp output.
   * Multi-Rate with GOP Alignment.
   * RESTful management interface, allowing easy integration into operator environment.
   * Job is descript in json.
   * Job run in subprocess, and auto restart on error.
   * Base on gstreamer and easy to extend.
   * B/S Management.
   * Time Shift and Catchup.

## Application

    IP >-----+ 
             |   +------------+   +----> UDP
    CVBS >---+   | gstreamill |   |                 +-- live
             +---+  with dvr  +---+---> M3U8(HLS) --+-- time shift (?timeshift=nnnn)
    SDI >----+   +------+-----+   |                 +-- catch up (?start=yyyymmddhhmmss&end=yyyymmddhhmmss)
             |          |         +---> HTTP
    LIVE >---+          |
                        |
         REST interface (json over http post)

## Third-party Applications

   * Parson
   * JQuery
   * BootStrap
   * BootBox
   * i18next
   * Resumable

# INSTALL

gstreamill have been tested in ubuntu 16.04.2.

## get source

    git clone https://github.com/i4tv/gstreamill.git

## install prerequisites

    sudo apt-get install autoconf automake libtool libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev libaugeas-dev

## Compile and install

    ./autogen.sh
    ./configure (--help)
    make
    make install

## dist package

    ./make dist

# USING GSTREAMILL

## install prerequisites

    sudo apt-get install gstreamer1.0-plugins-ugly gstreamer1.0-plugins-bad gstreamer1.0-plugins-good gstreamer1.0-plugins-base

## B/S Management URL

    http://gstreamill.ip:20118/admin/

## Command line

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

    service gstreamill start

* stop gstreamill

    service gstreamill stop

* invoke a job

default management port is 20118, use curl to invoke test job via management interface for example:

        curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/admin/start

* invoke a job in foreground, the debug mode:

        gstreamill -j job_descript_file

* access output via http progressive download:
    
        http://host.name.or.ip:20119/test/encoder/0

* access output use via m3u8:
    
        http://host.name.or.ip:20119/test/playlist.m3u8 (live)
        http://host.name.or.ip:20119/test/playlist.m3u8?timeshift=3600 (time shift)
        http://host.name.or.ip:20119/test/playlist.m3u8?position=1486556428 (time shift Wed Feb  8 20:20:28 CST 2017)
        http://host.name.or.ip:20119/test/playlist.m3u8?start=201506060606&end=20150606070600 (callback)

## Management interface

* start a job over http

        curl -H "Content-Type: application/json" --data @examples/test.job http://localhost:20118/admin/start

        or

        http http://localhost:20118/admin/start < tssegment.job

test.job is job description in json, can be found in examples directory.

* stop a job

        curl http://host.name.or.ip:20118/admin/stop/job_name

job_name is value of the "name" field in job description.

* query gstreamill stat:

        curl http://host.name.or.ip:20118/stat/gstreamill
        curl http://host.name.or.ip:20118/stat/gstreamill/job/test

* query gstreamer information:

        curl http://host.name.or.ip:20118/stat/gstreamer[/plugin]

## Output

job name is the value of name of job description.

* http progressive streaming

        http://host.name.or.ip:20119/job name/encoder/encoder_index

* hls

        http://host.name.or.ip:20119/job name/playlist.m3u8
        http://host.name.or.ip:20119/job name/encoder/encoder_index/playlist.m3u8

* udp

        udp://@ip:port

## JOB DESCRIPTION

Job example:
```json
/*
 * test job, videotestsrc and audiotestsrc
 */
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
                "appsrc name=video ! queue ! timeoverlay ! queue ! x264enc ! queue ! muxer.",
                "appsrc name=audio ! audioconvert ! audioresample ! voaacenc ! muxer.",
                "mpegtsmux name=muxer ! queue ! appsink"
            ]
        }
    ],
    "m3u8streaming" : {
        "version" : 3,
        "window-size" : 10,
        "segment-duration" : 3.00
    },
    "dvr_duration": 86400
}
```

JOB file structure:

```json
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
    },
    "dvr_duration": 86400
}
```

name : job name

debug : debug option, reference gst-launch gst-debug, optional.

is-live : true for live source, false otherwise, for example transcode, default is true.

log-path : dont log to default log direcotry for non-live source, log to log-path if it is presented.

source : source of encoders.

encoders : encoders.

m3u8streaming : m3u8 streaming

dvr_duration : dvr duration, seconds, it's optional.

structure of source:
```json
"source" : {
    "elements" : {
        ...
    },
    "bins" : [
        ...
    ]
}
```
structure of elements:
```json
"elements" : {
    "element" : {
        "property" : {
            ...
        },
        "caps" : "..."
    },
    ...
}
```

structure of bins:
```json
"bins" : [
    bin,
    bin,
    ...
]
```

bins is an array of bin, syntax of bin is like gst-launch, for example:
```json
[
    "udpsrc ! queue ! tsdemux name=demuxer",
    "demuxer.video ! queue ! mpeg2dec ! queue ! appsink name = video",
    "demuxer.audio ! mpegaudioparse ! queue ! mad ! queue ! appsink name = audio" 
]
```
in this example, first bin with tsdemux has sometimes pads, second and third bin link with first bin: demuxer.video and demuxer.audio. second bin with appsink named video and third bin with appsink named audio. source bins must have bin with appsink that is corespond endoders' source.

structure of encoders:
```json
"encoders" : [
    encoder,
    ...
]
```

encoders is an array of encoder, encoder is an object, every encoder correspneds an encode output, that means gstreamill support mutilty bitrate output.

structure of encoder:
```json
{
    "elements" : {
        ...
    },
    "bins" : [
        ...
    ],
    "udpstreaming" : "uri"
}
```

elements and bins is just the same as source structure in syntax, the differnce is encoder bins must have bins with appsrc element, appsrc must have name property, the value of name is the same as appsink name value in source bins. udpstreaming uri is udp streaming output uri, it's optional.

m3u8streaming is hls output, it's optional:
```json
"m3u8streaming" : {
    "version" : 3,
    "window-size" : 10,
    "segment-duration" : 3.00,
}
```

There are examples in examples directory of source.

Talk about gstreamill on [gstreamill](https://groups.google.com/forum/#!forum/gstreamill) or report a bug on [issues](https://github.com/zhangping/gstreamill/issues) page.

## License

gstreamill is available under GPL v3. See the LICENSE file for more info. 
