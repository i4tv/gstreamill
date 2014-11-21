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

On successful, returns a response body with the following structur::

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

On successful, returns a response body as following, more details reference gst-inspect man page::

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

On successful, returns a response body with the following structure::

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

On successful, returns a response body with the following structure::

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

**response**

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

Media Managment Interface
=========================
