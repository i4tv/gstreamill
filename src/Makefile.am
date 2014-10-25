INCLUDES = $(gstreamer_CFLAGS) $(gstreamerapp_CFLAGS) $(gstreamerpluginsbase_CFLAGS) $(augeas_CFLAGS)

bin_PROGRAMS = gstreamill

gstreamill_LDADD = $(gstreamer_LIBS) $(gstreamerapp_LIBS) $(gstreamerpluginsbase_LIBS) $(augeas_LIBS) $(gio_LIBS) -lrt -lpthread -lgstvideo-1.0

gstreamill_CFLAGS = -Wall -DDATADIR='"$(datadir)"'

gstreamill_SOURCES = utils.c main.c gstreamill.c httpserver.c source.c encoder.c job.c log.c httpstreaming.c httpmgmt.c mediaman.c parson.c jobdesc.c m3u8playlist.c

include_HEADERS = encoder.h gstreamill.h httpmgmt.h httpserver.h httpstreaming.h jobdesc.h job.h log.h m3u8playlist.h mediaman.h parson.h source.h utils.h
