Write and use Jobs
******************

Test job example from examples directory in source::

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

JOB file structure::

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

The following table define the properties of the job file:

    ================ ======= ==========================
    Property name    Type    Description
    ================ ======= ==========================
    name             string  job name
    debug            string  gstreamer debug
    is-live          boolean job type is live event?
    log-path         string  log file save path
    source           object  source description
    encoders         array   encoders description array
    m3u8streaming    object  hls streaming parameters
    dvr_duration     number  dvr duration in seconds
    ================ ======= ==========================

Source structure::

    "source" : {
        "elements" : {
            ...
        },
        "bins" : [
            ...
        ]
    }

The following table define the properties of source:

    ================ ======= ====================================
    Property name    Type    Description
    ================ ======= ====================================
    elements         object  descript elements be used in bins
    bins             array   bins array, descript source pipeline
    ================ ======= ====================================

Elements structure::

    "elements" : {
        "element" : {
            "property" : {
                ...
            },
            "caps" : "..."
        },
        ...
    }

The following table define the properties of elements:

    ================ ======= ====================================================
    Property name    Type    Description
    ================ ======= ====================================================
    element          string  this property name is the element name, e.g. x264enc
    property         object  properties of the element
    caps             string  element sink pad caps
    ================ ======= ====================================================

bins structure::

    "bins" : [
        bin,
        bin,
        ...
    ]

bins is an array of bin, syntax of bin is like gst-launch, for example::

    [
        "udpsrc ! queue ! tsdemux name=demuxer",
        "demuxer.video ! queue ! mpeg2dec ! queue ! appsink name = video",
        "demuxer.audio ! mpegaudioparse ! queue ! mad ! queue ! appsink name = audio" 
    ]

in this example, first bin with tsdemux has sometimes pads, second and third bin link with first bin: demuxer.video and demuxer.audio. second bin with appsink named video and third bin with appsink named audio. source bins must have bin with appsink that is corespond endoders' source.

encoders structure::

    "encoders" : [
        encoder,
        ...
    ]

encoders is an array of encoder, encoder is an object, every encoder correspneds an encode output, that means gstreamill support mutilty bitrate output.

encoder structure::

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

m3u8streaming is hls output, it's optional::

    "m3u8streaming" : {
        "version" : 3,
        "window-size" : 10,
        "segment-duration" : 3.00,
    }
    
The following table define the properties of m3u8streaming:

    ================ ======= ====================================================
    Property name    Type    Description
    ================ ======= ====================================================
    version          number  hls version 
    window-size      number  windows size
    segment-duration number  segment duration
    ================ ======= ====================================================
