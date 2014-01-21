/*
 *  source
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __SOURCE_H__
#define __SOURCE_H__

#include <gst/gst.h>
#include <semaphore.h>
#include <mqueue.h>

#include "log.h"
#include "m3u8playlist.h"

#define SOURCE_RING_SIZE 250
#define STREAM_NAME_LEN 32

typedef struct _Source Source;
typedef struct _SourceClass SourceClass;

typedef struct _Link {
        GstElement *src;
        GstElement *sink;
        gchar *src_name;
        gchar *sink_name;
        gchar *src_pad_name;
        gchar *sink_pad_name;
        gchar *caps;
} Link;

typedef struct _Bin {
        gchar *name;
        GSList *elements;
        GstElement *first;
        GstElement *last;
        GSList *links;
        Link *previous;
        Link *next;
        gulong signal_id;
} Bin;

typedef struct _SourceStreamState {
        gchar name[STREAM_NAME_LEN];
        GstClockTime current_timestamp;
        GstClockTime last_heartbeat;
} SourceStreamState;

typedef struct _SourceState {
        /*
         *  sync error cause sync_error_times inc, 
         *  sync normal cause sync_error_times reset to zero,
         *  sync_error_times == 5 cause the livejob restart.
         */
        guint64 sync_error_times;
        gint64 stream_count;
        SourceStreamState *streams;
} SourceState;

typedef struct _SourceStream {
        gchar *name;
        GstSample *ring[SOURCE_RING_SIZE];
        gint current_position; /* current source output position */
        GstClock *system_clock;
        GArray *encoders;

        SourceStreamState *state;
} SourceStream;

struct _Source {
        GObject parent;

        gchar *name;
        GstClock *system_clock;
        GstState state; /* state of the pipeline */
        GSList *bins;
        GstElement *pipeline;

        GArray *streams;
};

struct _SourceClass {
        GObjectClass parent;
};

#define TYPE_SOURCE           (source_get_type())
#define SOURCE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_SOURCE, Source))
#define SOURCE_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_SOURCE, SourceClass))
#define IS_SOURCE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_SOURCE))
#define IS_SOURCE_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_SOURCE))
#define SOURCE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_SOURCE, SourceClass))
#define source_new(...)       (g_object_new(TYPE_SOURCE, ## __VA_ARGS__, NULL))

GType source_get_type (void);

gboolean bus_callback (GstBus *bus, GstMessage *msg, gpointer user_data);
GSList * bins_parse (gchar *job, gchar *pipeline);
Source * source_initialize (gchar *job, SourceState source_stat);

#endif /* __SOURCE_H__ */
