INCLUDES = $(gstreamer_CFLAGS) $(gstreamerapp_CFLAGS) $(gstreamerpluginsbase_CFLAGS)

bin_PROGRAMS = gstreamill

gstreamill_LDADD = $(gstreamer_LIBS) $(gstreamerapp_LIBS) $(gstreamerpluginsbase_LIBS) -lrt -lpthread -lgstvideo-1.0

gstreamill_SOURCES = main.c gstreamill.c httpserver.c source.c encoder.c livejob.c log.c httpstreaming.c httpmgmt.c parson.c jobdesc.c m3u8playlist.c

