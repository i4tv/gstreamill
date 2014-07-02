/*
 * gstreamill job scheduler.
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <unistd.h>
#include <sys/wait.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "gstreamill.h"
#include "parson.h"
#include "jobdesc.h"
#include "m3u8playlist.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        GSTREAMILL_PROP_0,
        GSTREAMILL_PROP_LOGDIR,
        GSTREAMILL_PROP_DAEMON,
};

static GObject *gstreamill_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties);
static void gstreamill_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gstreamill_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void gstreamill_dispose (GObject *obj);
static void gstreamill_finalize (GObject *obj);
static gchar *create_job_process (Job *job);

static void gstreamill_class_init (GstreamillClass *gstreamillclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (gstreamillclass);
        GParamSpec *param;

        g_object_class->constructor = gstreamill_constructor;
        g_object_class->set_property = gstreamill_set_property;
        g_object_class->get_property = gstreamill_get_property;
        g_object_class->dispose = gstreamill_dispose;
        g_object_class->finalize = gstreamill_finalize;

        param = g_param_spec_boolean (
                "daemon",
                "daemon",
                "run in background",
                TRUE,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, GSTREAMILL_PROP_DAEMON, param);

        param = g_param_spec_string (
                "log_dir",
                "log_dir",
                "log directory",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, GSTREAMILL_PROP_LOGDIR, param);
}

static void gstreamill_init (Gstreamill *gstreamill)
{
        GstDateTime *start_time;

        gstreamill->stop = FALSE;
        gstreamill->system_clock = gst_system_clock_obtain ();
        g_object_set (gstreamill->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
        start_time = gst_date_time_new_now_local_time ();
        gstreamill->start_time = gst_date_time_to_iso8601_string (start_time);
        gst_date_time_unref (start_time);
        g_mutex_init (&(gstreamill->job_list_mutex));
        gstreamill->job_list = NULL;
}

static GObject * gstreamill_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
        GObject *obj;
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        obj = parent_class->constructor (type, n_construct_properties, construct_properties);

        return obj;
}

static void gstreamill_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_GSTREAMILL (obj));

        switch (prop_id) {
        case GSTREAMILL_PROP_DAEMON:
                GSTREAMILL (obj)->daemon = g_value_get_boolean (value);
                break;

        case GSTREAMILL_PROP_LOGDIR:
                GSTREAMILL (obj)->log_dir = (gchar *)g_value_dup_string (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void gstreamill_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        Gstreamill  *gstreamill = GSTREAMILL (obj);

        switch (prop_id) {
        case GSTREAMILL_PROP_DAEMON:
                g_value_set_boolean (value, gstreamill->daemon);
                break;

        case GSTREAMILL_PROP_LOGDIR:
                g_value_set_string (value, gstreamill->log_dir);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void gstreamill_dispose (GObject *obj)
{
        Gstreamill *gstreamill = GSTREAMILL (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        if (gstreamill->log_dir != NULL) {
                g_free (gstreamill->log_dir);
                gstreamill->log_dir = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void gstreamill_finalize (GObject *obj)
{
        Gstreamill *gstreamill = GSTREAMILL (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
        g_slist_free (gstreamill->job_list);
        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

GType gstreamill_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (GstreamillClass), /* class size */
                NULL, /* base initializer */
                NULL, /* base finalizer */
                (GClassInitFunc) gstreamill_class_init, /* class init */
                NULL, /* class finalize */
                NULL, /* class data */
                sizeof (Gstreamill),
                0, /* instance size */
                (GInstanceInitFunc) gstreamill_init, /* instance init */
                NULL /* value table */
        };
        type = g_type_register_static (G_TYPE_OBJECT, "Gstreamill", &info, 0);

        return type;
}

static void rotate_log (Gstreamill *gstreamill, gchar *log_path, pid_t pid)
{
        GStatBuf st;
        gchar *name;
        glob_t pglob;
        gint i;

        g_stat (log_path, &st);
        if (st.st_size > LOG_SIZE) {
                name = g_strdup_printf ("%s-%lu", log_path, gst_clock_get_time (gstreamill->system_clock));
                g_rename (log_path, name);
                g_free (name);
                GST_INFO ("log rotate %s, process pid %d.", log_path, pid);
                kill (pid, SIGUSR1); /* reopen log file. */
                name = g_strdup_printf ("%s-*", log_path);
                glob (name, 0, NULL, &pglob);
                if (pglob.gl_pathc > LOG_ROTATE) {
                        for (i = 0; i < pglob.gl_pathc - LOG_ROTATE; i++) {
                                g_remove (pglob.gl_pathv[i]);
                        }
                }
                globfree (&pglob);
                g_free (name);
        }
}

static void
log_rotate (Gstreamill *gstreamill)
{
        gchar *log_path;
        Job *job;
        GSList *list;

        /* gstreamill log rotate. */
        log_path = g_build_filename (gstreamill->log_dir, "gstreamill.log", NULL);
        rotate_log (gstreamill, log_path, getpid ());
        g_free (log_path);

        /* jobs log rotate. */
        list = gstreamill->job_list;
        while (list != NULL) {
                job = list->data;
                if ((job->worker_pid == 0) || !(job->is_live)) {
                        /* pid == 0 and non-live job, do not care about job which is stoped */
                        list = list->next;
                        continue;
                }
                log_path = g_build_filename (gstreamill->log_dir, job->name, "gstreamill.log", NULL);
                rotate_log (gstreamill, log_path, job->worker_pid);
                g_free (log_path);
                list = list->next;
        }
}

static void clean_job_list (Gstreamill *gstreamill)
{
        gboolean done;
        GSList *list;
        Job *job;

        done = FALSE;
        while (!done) {
                list = gstreamill->job_list;
                while (list != NULL) {
                        job = list->data;

                        /* clean live job */
                        if (job->is_live && (*(job->output->state) == GST_STATE_NULL && job->current_access == 0)) {
                                GST_WARNING ("Remove live job: %s.", job->name);
                                gstreamill->job_list = g_slist_remove (gstreamill->job_list, job);
                                g_object_unref (job);
                                break;
                        }

                        /* clean non live job */
                        if (!job->is_live && job->eos) {
                                GST_WARNING ("Remove non-live job: %s.", job->name);
                                gstreamill->job_list = g_slist_remove (gstreamill->job_list, job);
                                g_object_unref (job);
                                break;
                        }

                        list = list->next;
                }
                if (list == NULL) {
                        /* all list item have been checked */
                        done = TRUE;
                }
        }
}

static gint stop_job (Job *job, gint sig)
{
        if (job->worker_pid != 0) {
                if (sig == SIGUSR2) {
                        /* normally stop */
                        *(job->output->state) = GST_STATE_PAUSED;
                        GST_WARNING ("Stop job %s, pid %d.", job->name, job->worker_pid);

                } else {
                        /* unexpect stop, restart job */
                        GST_WARNING ("Restart job %s, pid %d.", job->name, job->worker_pid);
                }
                kill (job->worker_pid, sig);

                return 0;

        } else {
                return 1; /* stop a stoped job */
        }
}

static void source_check (Gstreamill *gstreamill, Job *job)
{
        gint i;
        GstClockTimeDiff time_diff;
        GstClockTime now;

        /* log source timestamp. */
        for (i = 0; i < job->output->source.stream_count; i++) {
                GST_INFO ("%s.source.%s timestamp %" GST_TIME_FORMAT,
                                job->name,
                                job->output->source.streams[i].name,
                                GST_TIME_ARGS (job->output->source.streams[i].current_timestamp));
        }

        /* non live job, don't check heartbeat */
        if (!job->is_live) {
                return;
        }

        /* source heartbeat check */
        for (i = 0; i < job->output->source.stream_count; i++) {
                /* check video and audio */
                if (!g_str_has_prefix (job->output->source.streams[i].name, "video") &&
                    !g_str_has_prefix (job->output->source.streams[i].name, "audio")) {
                        continue;
                }

                now = gst_clock_get_time (gstreamill->system_clock);
                time_diff = GST_CLOCK_DIFF (job->output->source.streams[i].last_heartbeat, now);
                if ((time_diff > HEARTBEAT_THRESHHOLD) && gstreamill->daemon) {
                        GST_ERROR ("%s.source.%s heart beat error %lu, restart job.",
                                        job->name,
                                        job->output->source.streams[i].name,
                                        time_diff);
                        /* restart job. */
                        stop_job (job, SIGKILL);
                        return;

                } else {
                        GST_INFO ("%s.source.%s heartbeat %" GST_TIME_FORMAT,
                                        job->name,
                                        job->output->source.streams[i].name,
                                        GST_TIME_ARGS (job->output->source.streams[i].last_heartbeat));
                }
        }
}

static void encoders_check (Gstreamill *gstreamill, Job *job)
{
        gint j, k;
        GstClockTimeDiff time_diff;
        GstClockTime now;

        /* log encoder current timestamp. */
        for (j = 0; j < job->output->encoder_count; j++) {
                for (k = 0; k < job->output->encoders[j].stream_count; k++) {
                        GST_INFO ("%s.encoders.%s.%s timestamp %" GST_TIME_FORMAT,
                                        job->name,
                                        job->output->encoders[j].name,
                                        job->output->encoders[j].streams[k].name,
                                        GST_TIME_ARGS (job->output->encoders[j].streams[k].current_timestamp));
                }
        }

        /* non live job, don't check heartbeat */
        if (!job->is_live) {
                return;
        }

        /* encoder heartbeat check */
        for (j = 0; j < job->output->encoder_count; j++) {
                for (k = 0; k < job->output->encoders[j].stream_count; k++) {
                        if (!g_str_has_prefix (job->output->encoders[j].streams[k].name, "video") &&
                            !g_str_has_prefix (job->output->encoders[j].streams[k].name, "audio")) {
                                continue;
                        }

                        now = gst_clock_get_time (gstreamill->system_clock);
                        time_diff = GST_CLOCK_DIFF (job->output->encoders[j].streams[k].last_heartbeat, now);
                        if ((time_diff > HEARTBEAT_THRESHHOLD) && gstreamill->daemon) {
                                GST_ERROR ("%s.encoders.%s.%s heartbeat error %lu, restart",
                                                job->name,
                                                job->output->encoders[j].name,
                                                job->output->encoders[j].streams[k].name,
                                                time_diff);
                                /* restart job. */
                                stop_job (job, SIGKILL);
                                return;

                        } else {
                                GST_INFO ("%s.encoders.%s.%s heartbeat %" GST_TIME_FORMAT,
                                                job->name,
                                                job->output->encoders[j].name,
                                                job->output->encoders[j].streams[k].name,
                                                GST_TIME_ARGS (job->output->encoders[j].streams[k].last_heartbeat));
                        }
                }
        }

        /* encoder job->output heartbeat check. */
        for (j = 0; j < job->output->encoder_count; j++) {
                now = gst_clock_get_time (gstreamill->system_clock);
                time_diff = GST_CLOCK_DIFF (*(job->output->encoders[j].heartbeat), now);
                if ((time_diff > ENCODER_OUTPUT_HEARTBEAT_THRESHHOLD) && gstreamill->daemon) {
                        GST_ERROR ("%s.encoders.%s job->output heart beat error %lu, restart",
                                        job->name,
                                        job->output->encoders[j].name,
                                        time_diff);
                        /* restart job. */
                        stop_job (job, SIGKILL);
                        return;

                } else {
                        GST_INFO ("%s.encoders.%s job->output heartbeat %" GST_TIME_FORMAT,
                                        job->name,
                                        job->output->encoders[j].name,
                                        GST_TIME_ARGS (*(job->output->encoders[j].heartbeat)));
                }
        }
}

static void sync_check (Gstreamill *gstreamill, Job *job)
{
        gint j;
        GstClockTimeDiff time_diff;
        GstClockTime now, min, max;

        min = GST_CLOCK_TIME_NONE;
        max = 0;
        for (j = 0; j < job->output->source.stream_count; j++) {
                if (!g_str_has_prefix (job->output->source.streams[j].name, "video") &&
                    !g_str_has_prefix (job->output->source.streams[j].name, "audio")) {
                        continue;
                }

                if (min > job->output->source.streams[j].current_timestamp) {
                        min = job->output->source.streams[j].current_timestamp;
                }

                if (max < job->output->source.streams[j].current_timestamp) {
                        max = job->output->source.streams[j].current_timestamp;
                }
        }
        time_diff = GST_CLOCK_DIFF (min, max);
        if ((time_diff > SYNC_THRESHHOLD) && gstreamill->daemon){
                GST_ERROR ("%s sync error %lu", job->name, time_diff);
                job->output->source.sync_error_times += 1;
                if (job->output->source.sync_error_times == 3) {
                        GST_ERROR ("sync error times %ld, restart %s", job->output->source.sync_error_times, job->name);
                        /* restart job. */
                        stop_job (job, SIGKILL);
                        return;
                }

        } else {
                job->output->source.sync_error_times = 0;
        }
}

static void job_check_func (gpointer data, gpointer user_data)
{
        Job *job = (Job *)data;
        Gstreamill *gstreamill = (Gstreamill *)user_data;

        if (gstreamill->stop) {
                GST_ERROR ("waitting %s stopped", job->name);
                return;
        }

        /* stat report. */
        if (gstreamill->daemon && (job->worker_pid != 0)) {
                job_stat_update (job);
                GST_INFO ("Job %s's average cpu: %d%%, cpu: %d%%, rss: %lu",
                                job->name,
                                job->cpu_average,
                                job->cpu_current,
                                job->memory);
        }

        if (*(job->output->state) != GST_STATE_PLAYING) {
                return;
        }

        source_check (gstreamill, job);

        encoders_check (gstreamill, job);

        if (job->is_live) {
                sync_check (gstreamill, job);
        }

        /* check non live job eos */
        if (!job->is_live) {
                gint i;
                gboolean eos = TRUE;

                for (i = 0; i < job->output->encoder_count; i++) {
                        if (!(*(job->output->encoders[i].eos))) {
                                eos = FALSE;
                                break;

                        } else {
                                /* add #EXT-X-ENDLIST to playlist */
                                gchar *location, *property, *playlist1, *playlist2;

                                property = g_strdup_printf ("encoder.%d.elements.hlssink.property.playlist-location", i);
                                location = jobdesc_element_property_value (job->description, property);
                                g_file_get_contents (location, &playlist1, NULL, NULL);
                                playlist2 = g_strdup_printf ("%s#EXT-X-ENDLIST\n",  playlist1);
                                g_file_set_contents (location, playlist2, strlen(playlist2), NULL);
                                g_free (playlist1);
                                g_free (playlist2);
                                g_free (property);
                        }
                }

                if (eos) {
                        stop_job (job, SIGUSR2);
                        job->eos = TRUE;
                }
        }
}

static gboolean gstreamill_monitor (GstClock *clock, GstClockTime time, GstClockID id, gpointer user_data)
{
        GstClockID nextid;
        GstClockReturn ret;
        GstClockTime now;
        Gstreamill *gstreamill;
        GSList *list;

        gstreamill = (Gstreamill *)user_data;

        g_mutex_lock (&(gstreamill->job_list_mutex));

        /* remove stoped job from job list */
        clean_job_list (gstreamill);

        /* stop? */
        if (gstreamill->stop && g_slist_length (gstreamill->job_list) == 0) {
                GST_ERROR ("streamill stopped");
                exit (0);
        }

        /* check job stat */
        if (!gstreamill->stop) {
                list = gstreamill->job_list;
                g_slist_foreach (list, job_check_func, gstreamill);
        }

        /* log rotate. */
        if (gstreamill->daemon) {
                log_rotate (gstreamill);
        }

        g_mutex_unlock (&(gstreamill->job_list_mutex));

        /* register streamill monitor */
        now = gst_clock_get_time (gstreamill->system_clock);
        nextid = gst_clock_new_single_shot_id (gstreamill->system_clock, now + 2000 * GST_MSECOND);
        ret = gst_clock_id_wait_async (nextid, gstreamill_monitor, gstreamill, NULL);
        gst_clock_id_unref (nextid);
        if (ret != GST_CLOCK_OK) {
                GST_WARNING ("Register gstreamill monitor failure");
                return FALSE;
        }

        return TRUE;
}

/**
 * gstreamill_start:
 * @gstreamill: (in): gstreamill to be starting
 *
 * start gstreamill
 *
 * Returns: 0 on success.
 */
gint gstreamill_start (Gstreamill *gstreamill)
{
        GstClockID id;
        GstClockTime t;
        GstClockReturn ret;

        /* regist gstreamill monitor */
        t = gst_clock_get_time (gstreamill->system_clock)  + 5000 * GST_MSECOND;
        id = gst_clock_new_single_shot_id (gstreamill->system_clock, t); 
        ret = gst_clock_id_wait_async (id, gstreamill_monitor, gstreamill, NULL);
        gst_clock_id_unref (id);
        if (ret != GST_CLOCK_OK) {
                GST_WARNING ("Regist gstreamill monitor failure");
                return 1;
        }

        return 0;
}

/**
 * gstreamill_stop:
 * @gstreamill: (in): gstreamill to be stop
 *
 * stop gstreamill, stop job first before stop gstreamill.
 *
 * Returns: none
 */
void gstreamill_stop (Gstreamill *gstreamill)
{
        Job *job;
        GSList *list;

        gstreamill->stop = TRUE;
        g_mutex_lock (&(gstreamill->job_list_mutex));
        list = gstreamill->job_list;
        while (list != NULL) {
                job = list->data;
                stop_job (job, SIGUSR2);
                list = list->next;
        }
        g_mutex_unlock (&(gstreamill->job_list_mutex));

        return;
}

/**
 * gstreamill_get_start_time:
 * @gstreamill: (in): gstreamill
 *
 * get start time of the gstreamill
 *
 * Returns: start time.
 */
gchar * gstreamill_get_start_time (Gstreamill *gstreamill)
{
        return gstreamill->start_time;
}

static void child_watch_cb (GPid pid, gint status, Job *job)
{
        /* Close pid */
        g_spawn_close_pid (pid);
        job->age += 1;
        job->worker_pid = 0;

        if (WIFEXITED (status) && (WEXITSTATUS (status) == 0)) {
                GST_ERROR ("Job with pid %d normaly exit, status is %d", pid, WEXITSTATUS (status));
                *(job->output->state) = GST_STATE_NULL;
                job->eos = TRUE;
        }

        if (WIFEXITED (status) && (WEXITSTATUS (status) != 0)) {
                GST_ERROR ("Job with pid %d abnormaly exit, status is %d", pid, WEXITSTATUS (status));
                *(job->output->state) = GST_STATE_NULL;
                job->eos = TRUE;
        }

        if (WIFSIGNALED (status)) {
                gchar *ret;

                if (*(job->output->state) == GST_STATE_PAUSED) {
                        GST_ERROR ("Job with pid %d exit on an unhandled signal and paused, stopping gstreamill...", pid);
                        *(job->output->state) = GST_STATE_NULL;
                        return;
                }

                GST_ERROR ("Job with pid %d exit on an unhandled signal, restart.", pid);
                job_reset (job);
                ret = create_job_process (job);
                if (g_str_has_suffix (ret, "failure")) {
                        /* create process failure, clean from job list */
                        *(job->output->state) = GST_STATE_NULL;
                }
                GST_ERROR ("create_job_process: %s", ret);
                g_free (ret);
        }

        return;
}

static gchar * create_job_process (Job *job)
{
        GError *error = NULL;
        gchar *argv[16], path[512], *p;
        GPid pid;
        gint i, j;

        memset (path, '\0', sizeof (path));
        if (readlink ("/proc/self/exe", path, sizeof (path)) == -1) {
                GST_ERROR ("Read /proc/self/exe error.");
                return g_strdup ("create live job failure");
        }
        i = 0;
        argv[i++] = g_strdup (path);
        argv[i++] = g_strdup ("-l");
        argv[i++] = g_strdup (job->log_dir);
        argv[i++] = g_strdup ("-n");
        argv[i++] = g_strdup_printf ("%s", job->name);
        argv[i++] = g_strdup ("-q");
        argv[i++] = g_strdup_printf ("%ld", strlen (job->description));
        p = jobdesc_get_debug (job->description);
        if (p != NULL) {
                argv[i++] = g_strdup_printf ("--gst-debug=%s", p);
                g_free (p);
        }
        argv[i++] = NULL;
        if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
                GST_ERROR ("Start job %s error, reason: %s.", job->name, error->message);
                for (j = 0; j < i; j++) {
                        if (argv[j] != NULL) {
                                g_free (argv[j]);
                        }
                }
                g_error_free (error);
                return g_strdup ("create live job process failure");
        }

        for (j = 0; j < i; j++) {
                if (argv[j] != NULL) {
                        g_free (argv[j]);
                }
        }
        job->worker_pid = pid;
        g_child_watch_add (pid, (GChildWatchFunc)child_watch_cb, job);

        return g_strdup ("create live job process success");
}

static Job * get_job (Gstreamill *gstreamill, gchar *name)
{
        Job *job;
        GSList *list;

        g_mutex_lock (&(gstreamill->job_list_mutex));
        list = gstreamill->job_list;
        if (list == NULL) {
                g_mutex_unlock (&(gstreamill->job_list_mutex));
                return NULL;
        }

        while (list != NULL) {
                job = list->data;
                if (g_strcmp0 (job->name, name) == 0) {
                        break;

                } else {
                        job = NULL;
                }
                list = list->next;
        }
        g_mutex_unlock (&(gstreamill->job_list_mutex));

        return job;
}

/**
 * gstreamill_job_start:
 * @job: (in): json type of job description.
 *
 * Returns: json type of job execution result. 
 */
gchar * gstreamill_job_start (Gstreamill *gstreamill, gchar *job_desc)
{
        gchar *p, *name;
        Job *job;

        if (!jobdesc_is_valid (job_desc)) {
                p = g_strdup ("Invalid job");
                return p;
        }

        if (jobdesc_is_live (job_desc)) {
                GST_ERROR ("live job arrived");

        } else {
                GST_ERROR ("transcode job arrived");
        }

        /* create job object */
        name = jobdesc_get_name (job_desc);
        if (get_job (gstreamill, name) != NULL) {
                GST_ERROR ("start live job failure, duplicated name %s.", name);
                p = g_strdup_printf ("start live job failure, duplicated name %s.", name);
                g_free (name);
                return p;
        }
        job = job_new ("job", job_desc, "name", name, NULL);
        g_free (name);

        /* job initialize */
        job->log_dir = gstreamill->log_dir;
        g_mutex_init (&(job->access_mutex));
        job->is_live = jobdesc_is_live (job_desc);
        job->eos = FALSE;
        job->current_access = 0;
        job->age = 0;
        job->last_start_time = NULL;
        if (job_initialize (job, gstreamill->daemon) != 0) {
                p = g_strdup ("initialize job failure");
                g_object_unref (job);
                return p;
        }

        /* reset and start job */
        job_reset (job);
        if (gstreamill->daemon) {
                p = create_job_process (job);
                GST_ERROR ("%s: %s", p, job->name);
                if (g_str_has_suffix (p, "success")) {
                        g_mutex_lock (&(gstreamill->job_list_mutex));
                        gstreamill->job_list = g_slist_append (gstreamill->job_list, job);
                        g_mutex_unlock (&(gstreamill->job_list_mutex));

                } else {
                        g_object_unref (job);
                }

        } else {
                if (job_start (job) == 0) {
                        g_mutex_lock (&(gstreamill->job_list_mutex));
                        gstreamill->job_list = g_slist_append (gstreamill->job_list, job);
                        g_mutex_unlock (&(gstreamill->job_list_mutex));
                        p = g_strdup ("success");

                } else {
                        p = g_strdup ("failure");
                }
        }

        return p;
}

/**
 * gstreamill_job_stop:
 * @name: (in): job name to be stoped
 *
 * Returns: plain text.
 */
gchar * gstreamill_job_stop (Gstreamill *gstreamill, gchar *name)
{
        Job *job;

        job = get_job (gstreamill, name);
        if (job != NULL) {
                stop_job (job, SIGUSR2);
                return g_strdup ("ok");

        } else {
                return g_strdup ("job not found");
        }
}

/**
 * gstreamill_get_job:
 * @uri: (in): access uri, e.g. /live/test/encoder/0
 *
 * Get the Job by access uri.
 *
 * Returns: job
 */
Job *gstreamill_get_job (Gstreamill *gstreamill, gchar *uri)
{
        Job *job = NULL;
        GRegex *regex;
        GMatchInfo *match_info;
        gchar *name = NULL;

        regex = g_regex_new ("^/live/(?<name>[^/]*)/.*", G_REGEX_OPTIMIZE, 0, NULL);
        match_info = NULL;
        g_regex_match (regex, uri, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
        }

        if (name != NULL) {
                job = get_job (gstreamill, name);
                g_free (name);
        }
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }

        return job;
}

gint gstreamill_job_number (Gstreamill *gstreamill)
{
        gint number;

        g_mutex_lock (&(gstreamill->job_list_mutex));
        number = g_slist_length (gstreamill->job_list);
        g_mutex_unlock (&(gstreamill->job_list_mutex));

        return number;
}

/**
 * gstreamill_get_encoder_output:
 * @uri: (in): access uri, e.g. /live/test/encoder/0
 *
 * Get the EncoderOutput by access uri.
 *
 * Returns: the encoder output
 */
EncoderOutput * gstreamill_get_encoder_output (Gstreamill *gstreamill, gchar *uri)
{
        Job *job;
        guint index;
        GRegex *regex = NULL;
        GMatchInfo *match_info = NULL;
        gchar *e;

        index = -1;
        regex = g_regex_new ("^/live/.*/encoder/(?<encoder>[0-9]+).*", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, uri, 0, &match_info);
        if (g_match_info_matches (match_info)) {
                e = g_match_info_fetch_named (match_info, "encoder");
                index = g_ascii_strtoll (e, NULL, 10);
                g_free (e);
        }
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (regex != NULL) {
                g_regex_unref (regex);
        }
        if (index == -1) {
                GST_INFO ("Not a encoder uri: %s", uri);
                return NULL;
        }
        job = gstreamill_get_job (gstreamill, uri);
        if (job == NULL) {
                GST_ERROR ("Job %s not found.", uri);
                return NULL;
        }
        if (index >= job->output->encoder_count) {
                GST_ERROR ("Encoder %s not found.", uri);
                return NULL;
        }
        g_mutex_lock (&(job->access_mutex));
        job->current_access += 1;
        g_mutex_unlock (&(job->access_mutex));

        return &job->output->encoders[index];
}

/**
 * gstreamill_get_m3u8playlist:
 * @encoder_output: (in): the encoder output to get its m3u8 playlist
 *
 * Get EncoderOutput' m3u8 playlist.
 *
 * Returns: m3u8 playlist
 */
gchar * gstreamill_get_m3u8playlist (Gstreamill *gstreamill, EncoderOutput *encoder_output)
{
        gchar *m3u8playlist;

        if (encoder_output->m3u8_playlist == NULL) {
                return g_strdup ("not fount");
        }

        g_mutex_lock (&(encoder_output->m3u8_playlist_mutex));
        m3u8playlist = m3u8playlist_render (encoder_output->m3u8_playlist);
        g_mutex_unlock (&(encoder_output->m3u8_playlist_mutex));

        return m3u8playlist;
}

/**
 * gstreamill_get_master_m3u8playlist:
 * @uri: (in): job uri
 *
 * Get Job's master playlist.
 *
 * Returns: master m3u8 playlist
 */
gchar * gstreamill_get_master_m3u8playlist (Gstreamill *gstreamill, gchar *uri)
{
        Job *job;
        gchar *master_m3u8_playlist;

        job = gstreamill_get_job (gstreamill, uri);
        if (job == NULL) {
                GST_ERROR ("Job %s not found.", uri);
                return NULL;
        }

        if (job->output->master_m3u8_playlist == NULL) {
                master_m3u8_playlist = NULL;

        } else {
                master_m3u8_playlist = g_strdup (job->output->master_m3u8_playlist);
        }

        return master_m3u8_playlist;
}

/**
 * gstreamill_unaccess:
 * @uri: (in): job access uri.
 *
 * current_access minus 1.
 *
 * Returns: none
 */
void gstreamill_unaccess (Gstreamill *gstreamill, gchar *uri)
{
        Job *job;

        job = gstreamill_get_job (gstreamill, uri);
        if (job == NULL) {
                GST_ERROR ("Job %s not found.", uri);
                return;
        }
        g_mutex_lock (&(job->access_mutex));
        job->current_access -= 1;
        g_mutex_unlock (&(job->access_mutex));

        return;
}

/**
 * gstreamill_stat:
 * @gstreamill: (bin): the gstreamill.
 *
 * Returns: json type stat:
 * {
 *     version:
 *     builddate:
 *     buildtime:
 *     starttime:
 *     job: [job1, job2]
 * }
 *
 */
gchar * gstreamill_stat (Gstreamill *gstreamill)
{
        gchar *template = "{\n"
                          "    \"version\": \"%s\",\n"
                          "    \"builddate\": \"%s\",\n"
                          "    \"buildtime\": \"%s\",\n"
                          "    \"starttime\": \"%s\",\n"
                          "    \"job\": %s]\n}\n";
        gchar *stat, *jobarray, *p;
        GSList *list;
        Job *job;

        jobarray = g_strdup_printf ("[");
        g_mutex_lock (&(gstreamill->job_list_mutex));
        list = gstreamill->job_list;
        while (list != NULL) {
                job = list->data;
                p = jobarray;
                jobarray = g_strdup_printf ("%s\"%s\"", p, job->name);
                g_free (p);
                list = list->next;
                if (list != NULL) {
                        p = jobarray;
                        jobarray = g_strdup_printf ("%s,", p);
                        g_free (p);
                }
        }
        g_mutex_unlock (&(gstreamill->job_list_mutex));
        stat = g_strdup_printf (template,
                                VERSION,
                                __DATE__,
                                __TIME__,
                                gstreamill->start_time,
                                jobarray);
        g_free (jobarray);

        return stat;
}

static gchar * source_streams_stat (Job *job)
{
        SourceStreamState *stat;
        GstDateTime *time;
        gint i;
        gchar *source_streams, *p1, *p2;
        gchar *template_source_stream = "            {\n"
                                        "                \"name\": \"%s\",\n"
                                        "                \"timestamp\": %lu,\n"
                                        "                \"heartbeat\": \"%s\"\n"
                                        "            }";

        stat = &(job->output->source.streams[0]);
        time = gst_date_time_new_from_unix_epoch_local_time (stat->last_heartbeat/GST_SECOND);
        p1 = gst_date_time_to_iso8601_string (time);
        gst_date_time_unref (time);
        source_streams = g_strdup_printf (template_source_stream,
                                          stat->name,
                                          stat->current_timestamp,
                                          p1);
        g_free (p1);
        for (i = 1; i < job->output->source.stream_count; i++) {
                stat = &(job->output->source.streams[i]);
                time = gst_date_time_new_from_unix_epoch_local_time (stat->last_heartbeat/GST_SECOND);
                p1 = gst_date_time_to_iso8601_string (time);
                gst_date_time_unref (time);
                p2 = g_strdup_printf (template_source_stream,
                                      stat->name,
                                      stat->current_timestamp,
                                      p1);
                g_free (p1);
                p1 = source_streams;
                source_streams = g_strdup_printf ("%s,\n%s", p1, p2);
                g_free (p1);
                g_free (p2);
        }

        return source_streams;
}

static gchar * encoder_stat (EncoderOutput *encoder)
{
        EncoderStreamState *stat;
        gint i;
        GstDateTime *time;
        gchar *encoder_stat, *encoder_streams, *p1, *p2;
        gchar *template_encoder_stream = "                {\n"
                                         "                    \"name\": \"%s\",\n"
                                         "                    \"timestamp\": %lu,\n"
                                         "                    \"heartbeat\": \"%s\"\n"
                                         "                }";
        gchar *template_encoder = "        {\n"
                                  "            \"name\": \"%s\",\n"
                                  "            \"heartbeat\": \"%s\",\n"
                                  "            \"count\": %ld,\n"
                                  "            \"streamcount\": %ld,\n"
                                  "            \"streams\": [\n"
                                  "%s\n"
                                  "            ]\n"
                                  "        }";
       
        stat = &(encoder->streams[0]);
        time = gst_date_time_new_from_unix_epoch_local_time (stat->last_heartbeat/GST_SECOND);
        p1 = gst_date_time_to_iso8601_string (time);
        gst_date_time_unref (time);
        encoder_streams = g_strdup_printf (template_encoder_stream,
                                           stat->name,
                                           stat->current_timestamp,
                                           p1);
        g_free (p1);
        for (i = 1; i < encoder->stream_count; i++) {
                stat = &(encoder->streams[i]);
                time = gst_date_time_new_from_unix_epoch_local_time (stat->last_heartbeat/GST_SECOND);
                p1 = gst_date_time_to_iso8601_string (time);
                gst_date_time_unref (time);
                p2 = g_strdup_printf (template_encoder_stream,
                                      stat->name,
                                      stat->current_timestamp,
                                      p1);
                g_free (p1);
                p1 = encoder_streams;
                encoder_streams = g_strdup_printf ("%s,\n%s", p1, p2);
                g_free (p1);
                g_free (p2);
        }
        time = gst_date_time_new_from_unix_epoch_local_time (*(encoder->heartbeat)/GST_SECOND);
        p1 = gst_date_time_to_iso8601_string (time);
        gst_date_time_unref (time);
        encoder_stat = g_strdup_printf (template_encoder,
                                        encoder->name,
                                        p1,
                                        *(encoder->total_count),
                                        encoder->stream_count,
                                        encoder_streams);
        g_free (p1);

        return encoder_stat;
}

static gchar * encoders_stat (Job *job)
{
        EncoderOutput *encoder;
        gint i;
        gchar *encoders, *p1, *p2;

        encoder = &(job->output->encoders[0]);
        encoders = encoder_stat (encoder);
        for (i = 1; i < job->output->encoder_count; i++) {
                p1 = encoders;
                encoder = &(job->output->encoders[i]);
                p2 = encoder_stat (encoder);
                encoders = g_strdup_printf ("%s,\n%s", p1, p2);
                g_free (p1);
                g_free (p2);
        }

        return encoders;
}

/**
 * gstreamill_job_stat
 * @name: (in): job name
 *
 * Returns: json type of stat of the name job:
 * {
 *     name:
 *     state:
 *     cpu:
 *     memory:
 *     source: {
 *         state:
 *         streamcount:
 *         streams: [
 *             {
 *                 caps:
 *                 timestamps:
 *                 heartbeat:
 *             },
 *             ...
 *         ]
 *     }
 *     encoder_count:
 *     encoders: [
 *         {
 *             state:
 *             heartbeat:
 *             count:
 *             streamcount:
 *             streams: [
 *                 {
 *                     timestamps:
 *                 }
 *             ]
 *         },
 *         ...
 *     ]
 * }
 */
gchar * gstreamill_job_stat (Gstreamill *gstreamill, gchar *uri)
{
        gchar *template = "{\n"
                          "    \"name\": \"%s\",\n"
                          "    \"age:\": %d,\n"
                          "    \"last_start_time\": \"%s\",\n"
                          "    \"current_access\": %d, \n"
                          "    \"cpu_average\": %d,\n"
                          "    \"cpu_current\": %d,\n"
                          "    \"memory\": %d,\n"
                          "    \"source\": {\n"
                          "        \"sync_error_times\": %ld,\n"
                          "        \"stream_count\": %ld,\n"
                          "        \"streams\": [\n"
                          "%s\n"
                          "        ]\n"
                          "    },\n"
                          "    \"encoder_count\": %ld,\n"
                          "    \"encoders\": [\n"
                          "%s\n"
                          "    ]\n"
                          "}\n";
        gchar *stat, *name, *source_streams, *encoders;
        Job *job;
        GRegex *regex = NULL;
        GMatchInfo *match_info = NULL;

        regex = g_regex_new ("/job/(?<name>[^/]*).*", G_REGEX_OPTIMIZE, 0, NULL);
        match_info = NULL;
        g_regex_match (regex, uri, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
        }
        if (name == NULL) {
                GST_ERROR ("wrong uri");
                return g_strdup ("wrong uri");
        }
        job = get_job (gstreamill, name);
        if (job == NULL) {
                GST_ERROR ("uri %s not found.", uri);
                return g_strdup ("not found");
        }
        source_streams = source_streams_stat (job);
        encoders = encoders_stat (job);
        stat = g_strdup_printf (template,
                                job->name,
                                job->age,
                                job->last_start_time,
                                job->current_access,
                                job->cpu_average,
                                job->cpu_current,
                                job->memory,
                                job->output->source.sync_error_times,
                                job->output->source.stream_count,
                                source_streams,
                                job->output->encoder_count,
                                encoders);
        g_free (source_streams);
        g_free (encoders);

        return stat;
}

gchar * gstreamill_gstreamer_stat (Gstreamill *gstreamill, gchar *uri)
{
        gchar *std_out, *cmd;
        GError *error = NULL;
        gchar buf[128];

        cmd = NULL;
        std_out = NULL;
        if (sscanf (uri, "/stat/gstreamer/%s$", buf) != EOF) {
                cmd = g_strdup_printf ("gst-inspect-1.0 %s", buf);

        } else if (g_strcmp0 (uri, "/stat/gstreamer") == 0 || g_strcmp0 (uri, "/stat/gstreamer/") == 0) {
                cmd = g_strdup ("gst-inspect-1.0");
        }

        if ((cmd != NULL) && !g_spawn_command_line_sync (cmd, &std_out, NULL, NULL, &error)) {
                GST_ERROR ("gst-inspect error, reason: %s.", error->message);
                g_error_free (error);
        }

        if (cmd != NULL) {
                g_free (cmd);
        }

        if (std_out == NULL) {
                std_out = g_strdup ("output is null, maybe gst-inspect-1.0 command not found.");
        }

        return std_out;
}
