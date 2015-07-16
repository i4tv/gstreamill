/*
 * gstreamill scheduler
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#ifndef __GSTREAMILL_H__
#define __GSTREAMILL_H__

#include <gst/gst.h>

#include "config.h"
#include "job.h"

#define SYNC_THRESHHOLD 3000000000 /* 1000ms */
#define HEARTBEAT_THRESHHOLD 7000000000 /* 7000ms */
#define NONLIVE_HEARTBEAT_THRESHHOLD 60000000000 /* 60s */
#define ENCODER_OUTPUT_HEARTBEAT_THRESHHOLD 30000000000 /* 30s */

#define LOG_SIZE 4*1024*1024
#define LOG_ROTATE 100

typedef struct _Gstreamill      Gstreamill;
typedef struct _GstreamillClass GstreamillClass;

struct _Gstreamill {
        GObject parent;

        gchar *exe_path;
        gboolean stop; /* gstreamill exit if stop == TURE */
        gboolean daemon; /* run as daemon? */
        GstClock *system_clock;
        gchar *start_time;
        GThread *msg_thread;
        gchar *log_dir;
        guint64 last_dvr_clean_time;

        GMutex job_list_mutex;
        GSList *job_list;
};

struct _GstreamillClass {
        GObjectClass parent;
};

#define TYPE_GSTREAMILL           (gstreamill_get_type())
#define GSTREAMILL(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GSTREAMILL, Gstreamill))
#define GSTREAMILL_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_GSTREAMILL, GstreamillClass))
#define IS_GSTREAMILL(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GSTREAMILL))
#define IS_GSTREAMILL_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_GSTREAMILL))
#define GSTREAMILL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_GSTREAMILL, GstreamillClass))
#define gstreamill_new(...)       (g_object_new(TYPE_GSTREAMILL, ## __VA_ARGS__, NULL))

GType gstreamill_get_type (void);
gint gstreamill_start (Gstreamill *gstreamill);
void gstreamill_stop (Gstreamill *gstreamill);
gchar * gstreamill_get_start_time (Gstreamill *gstreamill);
gchar * gstreamill_job_start (Gstreamill *gstreamill, gchar *job_desc);
gchar * gstreamill_job_stop (Gstreamill *gstreamill, gchar *name);
gchar * gstreamill_stat (Gstreamill *gstreamill);
gchar * gstreamill_list_jobs (Gstreamill *gstreamill);
gchar * gstreamill_job_stat (Gstreamill *gstreamill, gchar *name);
gchar * gstreamill_gstreamer_stat (Gstreamill *gstreamill, gchar *uri);
void gstreamill_unaccess (Gstreamill *gstreamill, gchar *uri);
Job * gstreamill_get_job (Gstreamill *gstreamill, gchar *uri);
gint gstreamill_job_number (Gstreamill *gstreamill);
EncoderOutput * gstreamill_get_encoder_output (Gstreamill *gstreamill, gchar *uri);
gchar * gstreamill_get_master_m3u8playlist (Gstreamill *gstreamill, gchar *uri);

#endif /* __GSTREAMILL_H__ */
