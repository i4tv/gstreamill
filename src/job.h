/*
 *  job
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __JOB_H__
#define __JOB_H__

#include "config.h"
#include "source.h"
#include "encoder.h"

#define SHM_SIZE 64*1024*1024

#define HTTP_PUT "PUT %s HTTP/1.1\r\n" \
                 "User-Agent: %s-%s\r\n" \
                 "Host: %s\r\n" \
                 "Accept: */*\r\n" \
                 "Content-Type: application/octet-stream\r\n" \
                 "Content-Length: %lu\r\n\r\n" 

#define HTTP_DELETE "DELETE %s HTTP/1.1\r\n" \
                    "User-Agent: %s-%s\r\n" \
                    "Host: %s\r\n" \
                    "Accept: */*\r\n\r\n"

typedef struct _Job Job;
typedef struct _JobClass JobClass;

typedef struct _m3u8PushRequest {
        gchar *rm_segment;
        EncoderOutput *encoder_output;
        GstClockTime timestamp;
        guint64 sequence_number;
} m3u8PushRequest;

typedef struct _JobOutput {
        gchar *job_description;
        guint64 *state;
        SourceState source;
        gint64 encoder_count;
        EncoderOutput *encoders;

        gchar *master_m3u8_playlist;
} JobOutput;

struct _Job {
        GObject parent;

        gchar *description;
        gchar *name; /* same as the name in job config file */
        gboolean is_live;
        gboolean eos;
        gint id;
        gchar *log_dir;
        GstClock *system_clock;
        gsize output_size;
        gint output_fd;
        JobOutput *output; /* Interface for producing */
        gint64 age; /* (re)start times of the job */
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

        gchar *m3u8push_uri;
        gchar m3u8push_host[256];
        gchar m3u8push_path[128];
        guint16 m3u8push_port;
        GThreadPool *m3u8push_thread_pool;
};

struct _JobClass {
        GObjectClass parent;
};

#define TYPE_JOB           (job_get_type())
#define JOB(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_JOB, Job))
#define JOB_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_JOB, JobClass))
#define IS_JOB(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_JOB))
#define IS_JOB_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_JOB))
#define JOB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_JOB, JobClass))
#define job_new(...)       (g_object_new(TYPE_JOB, ## __VA_ARGS__, NULL))

GType job_get_type (void);

gint job_initialize (Job *job, gboolean daemon);
void job_reset (Job *job);
void job_stat_update (Job *job);
gint job_start (Job *job);

#endif /* __JOB_H__ */
