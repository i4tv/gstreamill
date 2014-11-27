Management Interfaces
*********************

Gstreamill managment API subject to RESTful, allowing easy integration into operator environment.

State Interface
===============

System stat
-----------

Get hardware and system software infomation of the server

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/stat/system

**Response**

On success, returns a response body with the following structure::

    {
        "CPU_Model": "Intel(R) Xeon(R) CPU E5-2620 0 @ 2.00GHz",
        "CPU_Count": 24,
        "Threads_per_Core": 2,
        "Core_per_Socket": 6,
        "Sockets_Count": 2,
        "CPU_MHz": 2300
    }

The following table define the properties

    ================ ====== ======================
    Property name    Type   Description
    ================ ====== ======================
    CPU_Model        string The model of the CPU
    CPU_Count        number The Count of CPU cores
    Threads_per_Core number Threads per core
    Core per Socket  number Core per socket
    Sockets Count    number The count of Sockets
    CPU_MHz          number CPU frequence
    ================ ====== ======================

Gstreamer stat
--------------

Get gst-inspect output without any parameters of the server

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/stat/gstreamer

**Response**

On success, returns a response body as following, more details reference gst-inspect man page::

    nice:  nicesink: ICE sink
    x264:  x264enc: x264enc
    mpeg2dec:  mpeg2dec: mpeg1 and mpeg2 video decoder
    mad:  mad: mad mp3 decoder
    realmedia:  pnmsrc: PNM packet receiver
    realmedia:  rtspreal: RealMedia RTSP Extension
    realmedia:  rdtmanager: RTP Decoder
    realmedia:  rdtdepay: RDT packet parser
    realmedia:  rademux: RealAudio Demuxer
    realmedia:  rmdemux: RealMedia Demuxer 

Gstreamill stat
---------------

Get current stat of gstreamill server.

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/stat/gstreamill

**Response**

On success, returns a response body with the following structure::

    {
        "version": "0.5.3",
        "builddate": "Nov 20 2014",
        "buildtime": "13:27:11",
        "starttime": "2014-11-20T13:30:13+0800",
        "jobcount": 5
    }

The following table defines the properties

    ============= ====== =================================
    Property name Type   Description
    ============= ====== =================================
    version       string The version of the gstreamill
    builddate     string The build date of the gstreamill
    buildtime     string The build time of the gstreamill
    starttime     string Start time of the gstreamill
    jobcount      number Current count of jobs
    ============= ====== =================================

Job stat
--------

Get job stat.

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/stat/gstreamill/job/CCTV-1

CCTV-1 is job name

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success",
        "data": {
            "name": "CCTV-1",
            "age": 0,
            "last_start_time": "2014-11-20T13:30:14+0800",
            "state": "JOB_STATE_PLAYING",
            "current_access": 0,
            "cpu_average": 9,
            "cpu_current": 8,
            "memory": 545214464,
            "source": {
                "duration": 0,
                "sync_error_times": 0,
                "stream_count": 2,
                "streams": [
                    {
                        "name": "video",
                        "timestamp": 77140338135406,
                        "heartbeat": "2014-11-21T10:55:54+0800"
                    },
                    {
                        "name": "audio",
                        "timestamp": 77140023661978,
                        "heartbeat": "2014-11-21T10:55:54+0800"
                    }
                ]
            },
            "encoder_count": 2,
            "encoders": [
                {
                    "name": "CCTV-1.encoder.0",
                    "heartbeat": "2014-11-21T10:55:54+0800",
                    "count": 4209958636,
                    "streamcount": 2,
                    "streams": [
                        {
                            "name": "video",
                            "timestamp": 77140298135968,
                            "heartbeat": "2014-11-21T10:55:54+0800"
                        },
                        {
                            "name": "audio",
                            "timestamp": 77139999661978,
                            "heartbeat": "2014-11-21T10:55:54+0800"
                        }
                    ]
                },
                {
                    "name": "CCTV-1.encoder.1",
                    "heartbeat": "2014-11-21T10:55:54+0800",
                    "count": 10996259496,
                    "streamcount": 2,
                    "streams": [
                        {
                            "name": "video",
                            "timestamp": 77140258139283,
                            "heartbeat": "2014-11-21T10:55:54+0800"
                        },
                        {
                            "name": "audio",
                            "timestamp": 77139999661978,
                            "heartbeat": "2014-11-21T10:55:54+0800"
                        }
                    ]
                }
            ]
        }
    }

The following table defines the properties

**response json**

    ================ =========== ====================================================
    Property name    Type        Description
    ================ =========== ====================================================
    result           string      Request result, success or failure
    data             json object Job stat
    ================ =========== ====================================================

**data**

    ================ =========== ====================================================
    Property name    Type        Description
    ================ =========== ====================================================
    name             string      Job name
    age              number      Restart times, for live job only
    last_start_time  string      The time of most recently restart, for live job only
    stat             string      Current stat of the job
    current_access   number      Count of concurrent access
    cpu_average      number      Average CPU usage
    cpu_current      number      Current CPU usage
    memory           number      Memory usage
    source           json object Source of the job
    encoder_count    number      Count of encoders output
    encoders         array       encoders array
    ================ =========== ====================================================

**source**

    ================ =========== ====================================================
    Property name    Type        Description
    ================ =========== ====================================================
    duration         number      media duration, for transcode job only
    sync_error_times number      use for audio video sync check
    stream_count     number      streams count
    streams          array       streams array
    ================ =========== ====================================================

**encoder**

    ================ =========== ====================================================
    Property name    Type        Description
    ================ =========== ====================================================
    name             string      encoder name
    heartbeat        string      encoder heart beat
    count            number      encoder output bytes count
    streamcount      number      streams count
    streams          array       streams array
    ================ =========== ====================================================

**streams**

    ================ =========== ====================================================
    Property name    Type        Description
    ================ =========== ====================================================
    name             string      name of the stream
    timestamp        string      stream timestamp
    heartbeat        string      output or input heart beat of the stream
    ================ =========== ====================================================

Administrator Interface
=======================

start job
---------

Start a job.

**Request**

HTTP Request::

    POST http://gstreamill.server.addr:20118/admin/start

Request body::

    Json type of job

**Response**

On success, returns a response body with the following structure::

    {
        "name": "CCTV-1",
        "result": "success"
    }

On failure, returns a response body with the following structure::

    {
        "result": "failure",
        "reason": "initialize job failure"
    }

stop job
--------

Stop a running job

**Request**

HTTP Request::

    GET http://localhost:20118/admin/stop/CCTV-1

CCTV-1 is the name of job

**Response**

On success, returns a response body with the following structure::

    {
        "name": "CCTV-1",
        "result": "success"
    }

On failure, returns a response body with the following structure::

    {
        "result": "failure",
        "reason": "job not found"
    }

get network devices
-------------------

Get network devices of the server.

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/getnetworkdevices

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success",
        "data": [
            "em3",
            "em4",
            "em2",
            "em1"
        ]
    }

get network interfaces
----------------------

Get current network interfaces configuration

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/getnetworkinterfaces

**Response**

On success, returns a response body with the following structure::

    [
        {
            "name": "em4",
            "family": "inet",
            "method": "static",
            "address": "192.168.7.40",
            "netmask": "255.255.0.0",
            "network": "192.168.0.0",
            "broadcast": "192.168.255.255",
            "gateway": "192.168.88.1",
            "dns-nameservers": "192.168.88.1"
        },
        {
            "name": "em2",
            "family": "inet",
            "method": "static",
            "address": "192.169.0.254",
            "netmask": "255.255.255.0",
            "network": "192.169.0.0",
            "broadcast": "192.169.0.255"
        },
        {
            "name": "em3",
            "family": "inet",
            "method": "static",
            "address": "192.167.1.109",
            "netmask": "255.255.255.0",
            "network": "192.167.0.0",
            "broadcast": "192.167.0.255"
        }
    ]

set network interfaces
----------------------

Set network interfaces

**Request**

HTTP Request::

    POST http://gstreamill.server.addr:20118/admin/setnetworkinterfaces

Request body

The same structure as get network interfaces response body

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success"
    }

On failure, returns a response body with the following structure::

    {
        "result": "failure",
        "reason": "invalid data"
    }

get audio devices
-----------------

get video devices
-----------------

get conf
--------

Get web admin configure

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/getconf

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success",
        "data": {
            "language": "English"
        }
    }

put conf
--------

Set web admin configure

**Request**

HTTP Request::

    POST http://gstreamill.server.addr:20118/admin/putconf

Request body::

    {
        "language": "English"
    }

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success"
    }

list live jobs
--------------

List live jobs

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/listlivejob

**Response**

On success, returns a response body with the following structure::

    ["CCTV-1","CCTV-2","CCTV-3","CCTV-4","CCTV-5"]

list nonlive jobs
-----------------

List nonlive jobs

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/listnonlivejob

**Response**

On success, returns a response body with the following structure::

    ["Film"]

new job
-------

Create a live job

**Request**

HTTP Request::

    POST http://gstreamill.server.addr:20118/admin/putjob

Request body::

    Json type of job

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success"
    }

On failure, returns a response body with the following structure::

    {
        "result": "failure",
        "reason": "invalid job"
    }

get job
-------

Get a live job

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/getjob/CCTV-1

CCTV-1 is name of the job

**Response**

On success, returns a response body that is the job

set job
-------

Set a live job

**Request**

HTTP Request::

    POST http://gstreamill.server.addr:20118/admin/setjob

Request body::

    Json type of job

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success"
    }

On failure, returns a response body with the following structure::

    {
        "result": "failure",
        "reason": "invalid job"
    }

remove job
----------

Remove a live job

HTTP Request::

    GET http://gstreamill.server.addr:20118/admin/rmjob/CCTV-1

CCTV-1 is name of the job to be removed

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success"
    }

On failure, returns a response body with the following structure::

    {
        "result": "failure",
        "reason": "No such file or directory"
    }

Media Managment Interface
=========================

media upload
------------

Get method upload request, for Resumable use, more detail reference https://github.com/23/resumable.js 

media upload
------------

Post method upload request, for Resumable use, more detail reference https://github.com/23/resumable.js 

media download
--------------

Download media, now only trancode in and out media file can be downloaded.

HTTP Request::

    http://gstreamill.server.addr:20118/media/download/transcode/out/crazystone.mp4/640x360_300bps.mp4

transcode in list
-----------------

List transcode in

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/media/transcodeinlist

**Response**

On success, returns a response body with the following structure::

    [
        "【20140103】电竞世界877期炉石版-DC沐沐Ks炉石传说互动.wmv",
        "【20140312】电竞世界939期300英雄.wmv",
        "【20140313】电竞世界940期DOTA2版沐沐KS互动.wmv",
        "【20140320】电竞世界944期300英雄版牌哥沐沐轩轩互动.wmv",
        "【2014CFPLS5】004：汉宫-易游.wmv",
        "【游戏进行时】20140515逸凡沐沐《龙魂传说》互动.wmv",
        "战逗高校新春特辑（上）高清网络版 .mp4",
        "战逗高校第3期高清网络带台标版-盲僧：我秀的是智商.mp4"
    ]

transcode out list
------------------

List transcode out

HTTP Request::

    GET http://gstreamill.server.addr:20118/media/transcodeoutlist

**Response**

On success, returns a response body with the following structure::

    {
        "战逗高校第3期高清网络带台标版-盲僧：我秀的是智商.mp4": [
            "640x360_300bps.mp4",
            "720x576_800bps.mp4"
        ],
        "战逗高校新春特辑（上）高清网络版 .mp4": [
            "1280x720_1500bps.mp4",
            "640x360_300bps.mp4",
            "720x576_1000bps.mp4",
            "720x576_800bps.mp4"
        ],
        "【2014CFPLS5】004：汉宫-易游.wmv": [
            "640x360_300bps.mp4"
        ],
        "【20140103】电竞世界877期炉石版-DC沐沐Ks炉石传说互动.wmv": [
            "640x360_300bps.mp4"
        ],
    }
    
remove transcode media
----------------------

Remove transcode in or out media

**Request**

HTTP Request::

     GET http://gstreamill.server.addr:20118/media/rm/transcode/in/Apple.wmv

**Response**

On success, returns a response body with the following structure::

    {
        "result": "success"
    }

get media dir
-------------

Get media root directory

**Request**

HTTP Request::

    GET http://gstreamill.server.addr:20118/media/getmediadir

**Response**

Returns a response body with the following structure::

    {
        "media_dir": "/var/lib/gstreamill"
    }
