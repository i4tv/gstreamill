/*
 *  encoder
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __ENCODER_H__
#define __ENCODER_H__

#include <semaphore.h>
#include <mqueue.h>

typedef struct _Encoder Encoder;
typedef struct _EncoderClass EncoderClass;

typedef struct _EncoderStreamState {
        gchar name[STREAM_NAME_LEN];
        GstClockTime current_timestamp;
        GstClockTime last_heartbeat;
} EncoderStreamState;

typedef struct _EncoderOutput {
        gchar name[STREAM_NAME_LEN];
        sem_t *semaphore; /* access of encoder output should be exclusive */
        GstClockTime *heartbeat;
        gint cache_fd;
        gchar *cache_addr;
        guint64 cache_size;
        guint64 *total_count; /* total output packet counts */
        guint64 *head_addr;
        guint64 *tail_addr;
        guint64 *last_rap_addr; /* last random access point address */
        gint64 stream_count;
        EncoderStreamState *streams;

        /* m3u8 streaming */
        GMutex m3u8_playlist_mutex;
        GThreadPool *m3u8push_thread_pool;
        mqd_t mqdes;
        guint sequence_number;
        M3U8Playlist *m3u8_playlist;
        GstClockTime last_timestamp; /* last segment timestamp */
} EncoderOutput;

typedef struct _EncoderStream {
        gchar *name;
        SourceStream *source;
        GstClock *system_clock;
        gint current_position; /* encoder position */
        EncoderStreamState *state;
        Encoder *encoder;
} EncoderStream;

struct _Encoder {
        GObject parent;

        gchar *name;
        gint id;
        GstClock *system_clock;
        GstState state; /* state of the pipeline */
        GstClockTime *output_heartbeat;
        GSList *bins;
        GstElement *pipeline;
        GArray *streams;
        EncoderOutput *output;

        /* udp streaming */
        GstElement *udpstreaming;
        GstElement *appsrc;
        GstBuffer *cache_7x188;
        gsize cache_size;

        /* gop size */
        guint force_key_count; /* downstream force key unit count */
        GstClockTime segment_duration; /* force key interval */
        GstClockTime duration_accumulation; /* current segment duration accumulation */

        /* m3u8 playlist */
        mqd_t mqdes;
        GstClockTime last_segment_duration;
        GstClockTime last_running_time;
};

struct _EncoderClass {
        GObjectClass parent;
};

#define TYPE_ENCODER           (encoder_get_type())
#define ENCODER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_ENCODER, Encoder))
#define ENCODER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_ENCODER, EncoderClass))
#define IS_ENCODER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_ENCODER))
#define IS_ENCODER_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_ENCODER))
#define ENCODER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_ENCODER, EncoderClass))
#define encoder_new(...)       (g_object_new(TYPE_ENCODER, ## __VA_ARGS__, NULL))

GType encoder_get_type (void);

guint encoder_initialize (GArray *earray, gchar *job, EncoderOutput *encoders, Source *source);
GstClockTime encoder_output_rap_timestamp (EncoderOutput *encoder_output, guint64 rap_addr);
guint64 encoder_output_rap_next (EncoderOutput *encoder_output, guint64 rap_addr);
guint64 encoder_output_gop_size (EncoderOutput *encoder_output, guint64 rap_addr);

#endif /* __ENCODER_H__ */
