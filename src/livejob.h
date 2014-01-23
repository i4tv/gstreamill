/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __LIVEJOB_H__
#define __LIVEJOB_H__

#include "source.h"
#include "encoder.h"

#define SHM_SIZE 64*1024*1024

typedef struct _LiveJob LiveJob;
typedef struct _LiveJobClass LiveJobClass;

typedef struct _m3u8Segment {
        EncoderOutput *encoder;
        GstClockTime timestamp;
} m3u8Segment;

typedef struct _LiveJobOutput {
        gchar *job_description;
        guint64 *state;
        SourceState source;
        gint64 encoder_count;
        EncoderOutput *encoders;

        gchar *master_m3u8_playlist;
} LiveJobOutput;

struct _LiveJob {
        GObject parent;

        gchar *job;
        gchar *name; /* same as the name in livejob config file */
        gint id;
        gchar *log_dir;
        GstClock *system_clock;
        gsize output_size;
        gint output_fd;
        LiveJobOutput *output; /* Interface for producing */
        gint64 age; /* (re)start times of the livejob */
        gchar *last_start_time; /* last start up time */
        pid_t worker_pid;

        GMutex access_mutex; /* current_access access should be mutex */
        gint current_access; /* number of current access client */

        guint64 last_utime; /* last process user time */
        guint64 last_stime; /* last process system time */
        guint64 last_ctime; /* last process cpu time */
        guint64 start_ctime; /* cpu time at process start */
        gint cpu_average;
        gint cpu_current;
        gint memory;

        Source *source; 
        GArray *encoder_array;

        GThreadPool *m3u8push_thread_pool;
};

struct _LiveJobClass {
        GObjectClass parent;
};

#define TYPE_LIVEJOB           (livejob_get_type())
#define LIVEJOB(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_LIVEJOB, LiveJob))
#define LIVEJOB_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_LIVEJOB, LiveJobClass))
#define IS_LIVEJOB(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_LIVEJOB))
#define IS_LIVEJOB_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_LIVEJOB))
#define LIVEJOB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_LIVEJOB, LiveJobClass))
#define livejob_new(...)       (g_object_new(TYPE_LIVEJOB, ## __VA_ARGS__, NULL))

GType livejob_get_type (void);

gint livejob_initialize (LiveJob *livejob, gboolean daemon);
void livejob_reset (LiveJob *livejob);
void livejob_stat_update (LiveJob *livejob);
gint livejob_start (LiveJob *livejob);
gchar * livejob_get_master_m3u8_playlist (LiveJob *livejob);

#endif /* __LIVEJOB_H__ */
