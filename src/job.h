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

#define MEDIA_LOCATION "/var/lib/gstreamill"

typedef struct _Job Job;
typedef struct _JobClass JobClass;

/** job state
 * JOB_STATE_VOID_PENDING: no pending state.
 * JOB_STATE_READY: creating job process, subsequent state is JOB_STATE_PLAYLING or JOB_STATE_START_FAILURE
 * JOB_STATE_PLAYLING: playing state, subprocess running.
 * JOB_STATE_START_FAILURE: subprocess start failure.
 * JOB_STATE_STOPING: stoping state, subprocess is being stop.
 * JOB_STATE_STOPED: stoped state, subprocess finished.
 */
typedef enum {
        JOB_STATE_VOID_PENDING = 0,
        JOB_STATE_READY = 1,
        JOB_STATE_PLAYING = 2,
        JOB_STATE_START_FAILURE = 3,
        JOB_STATE_STOPING = 4,
        JOB_STATE_STOPED = 5
} JobState;

typedef struct _JobOutput {
        gchar *job_description;
        gchar *semaphore_name;
        sem_t *semaphore; /* access of job output should be exclusive */
        guint64 *state;
        SourceState source;
        guint64 sequence;
        gint64 encoder_count;
        EncoderOutput *encoders;

        gchar *master_m3u8_playlist;
} JobOutput;

struct _Job {
        GObject parent;

        gchar *description;
        gchar *exe_path;
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
        guint64 memory;

        Source *source; 
        GArray *encoder_array;
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

gchar * job_state_get_name (guint64 state);
gint job_initialize (Job *job, gboolean daemon);
gint job_output_initialize (Job *job);
gint job_encoders_output_initialize (Job *job);
void job_reset (Job *job);
gint job_stat_update (Job *job);
gint job_start (Job *job);
gint job_stop (Job *job, gint sig);

#endif /* __JOB_H__ */
