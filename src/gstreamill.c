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

#include "utils.h"
#include "gstreamill.h"
#include "parson.h"
#include "jobdesc.h"
#include "m3u8playlist.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        GSTREAMILL_PROP_0,
        GSTREAMILL_PROP_LOGDIR,
        GSTREAMILL_PROP_EXEPATH,
        GSTREAMILL_PROP_DAEMON,
};

static GObject *gstreamill_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties);
static void gstreamill_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gstreamill_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void gstreamill_dispose (GObject *obj);
static void gstreamill_finalize (GObject *obj);

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

        param = g_param_spec_string (
                "exe_path",
                "exe_path",
                "exe path",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, GSTREAMILL_PROP_EXEPATH, param);
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
        gstreamill->last_dvr_clean_time = g_get_real_time ();
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

        case GSTREAMILL_PROP_EXEPATH:
                GSTREAMILL (obj)->exe_path = (gchar *)g_value_dup_string (value);
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

        case GSTREAMILL_PROP_EXEPATH:
                g_value_set_string (value, gstreamill->exe_path);
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

        if (gstreamill->exe_path != NULL) {
                g_free (gstreamill->exe_path);
                gstreamill->exe_path = NULL;
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
                GST_DEBUG ("log rotate %s, process pid %d.", log_path, pid);
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
                        if (job->is_live && (*(job->output->state) == JOB_STATE_NULL && job->current_access == 0)) {
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
                if (sig == SIGTERM) {
                        /* normally stop */
                        *(job->output->state) = JOB_STATE_PAUSED;
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
                GST_DEBUG ("%s timestamp %" GST_TIME_FORMAT,
                                job->output->source.streams[i].name,
                                GST_TIME_ARGS (job->output->source.streams[i].current_timestamp));
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
                if (((time_diff > HEARTBEAT_THRESHHOLD) && gstreamill->daemon && job->is_live) ||
                    ((time_diff > NONLIVE_HEARTBEAT_THRESHHOLD) && gstreamill->daemon && !job->is_live)) {
                        GST_WARNING ("%s heart beat error %lu, restart job.",
                                        job->output->source.streams[i].name,
                                        time_diff);
                        /* restart job. */
                        stop_job (job, SIGKILL);
                        return;

                } else {
                        GST_DEBUG ("%s heartbeat %" GST_TIME_FORMAT,
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
                        GST_DEBUG ("%s.%s timestamp %" GST_TIME_FORMAT,
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
                                GST_WARNING ("%s.%s heartbeat error %lu, restart",
                                                job->output->encoders[j].name,
                                                job->output->encoders[j].streams[k].name,
                                                time_diff);
                                /* restart job. */
                                stop_job (job, SIGKILL);
                                return;

                        } else {
                                GST_DEBUG ("%s.%s heartbeat %" GST_TIME_FORMAT,
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
                        GST_WARNING ("%s job->output heart beat error %lu, restart",
                                        job->output->encoders[j].name,
                                        time_diff);
                        /* restart job. */
                        stop_job (job, SIGKILL);
                        return;

                } else {
                        GST_DEBUG ("%s job->output heartbeat %" GST_TIME_FORMAT,
                                        job->output->encoders[j].name,
                                        GST_TIME_ARGS (*(job->output->encoders[j].heartbeat)));
                }
        }
}

static void sync_check (Gstreamill *gstreamill, Job *job)
{
        gint j;
        GstClockTimeDiff time_diff;
        GstClockTime min, max;

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
                GST_WARNING ("%s sync error %lu", job->name, time_diff);
                job->output->source.sync_error_times += 1;
                if (job->output->source.sync_error_times == 3) {
                        GST_WARNING ("sync error times %ld, restart %s", job->output->source.sync_error_times, job->name);
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
                GST_INFO ("waitting %s stopped", job->name);
                return;
        }

        /* stat report. */
        if (gstreamill->daemon && (job->worker_pid != 0)) {
                job_stat_update (job);
                GST_DEBUG ("Job %s's average cpu: %d%%, cpu: %d%%, rss: %lu",
                                job->name,
                                job->cpu_average,
                                job->cpu_current,
                                job->memory);
        }

        if (sem_wait (job->output->semaphore) == -1) {
                GST_WARNING ("%s job_check_func sem_trywait failure: %s", job->name, g_strerror (errno));
                return;
        }

        if (*(job->output->state) != JOB_STATE_PLAYING) {
                sem_post (job->output->semaphore);
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
                                /* add #EXT-X-ENDLIST to playlist if output is m3u8 */
                                gchar *location, *property, *playlist1, *playlist2;

                                property = g_strdup_printf ("encoder.%d.elements.hlssink.property.playlist-location", i);
                                location = jobdesc_element_property_value (job->description, property);
                                g_free (property);
                                if (location != NULL) {
                                        g_file_get_contents (location, &playlist1, NULL, NULL);
                                        playlist2 = g_strdup_printf ("%s#EXT-X-ENDLIST\n",  playlist1);
                                        g_file_set_contents (location, playlist2, strlen(playlist2), NULL);
                                        g_free (playlist1);
                                        g_free (playlist2);
                                        g_free (location);
                                }
                        }
                }

                if (eos) {
                        stop_job (job, SIGTERM);
                        job->eos = TRUE;
                }
        }
        sem_post (job->output->semaphore);
}

static void dvr_clean (Gstreamill *gstreamill)
{
        guint64 now, time;
        gint i, j;
        gchar *pattern;
        glob_t pglob;
        GSList *list;
        Job *job;

        now = g_get_real_time ();
        if (now - gstreamill->last_dvr_clean_time < 600000000) {
                return;
        }

        list = gstreamill->job_list;
        while (list != NULL) {
                job = list->data;
                /* non live job need not clean dvr */
                if (!job->is_live) {
                        list = list->next;
                        continue;
                }
                for (i = 0; i < job->output->encoder_count; i++) {
                        if (job->output->encoders[i].record_path == NULL) {
                                continue;
                        }
                        time = (now / 1000000 - job->output->encoders[i].dvr_duration) / 10;
                        while (time > 11) {
                                pattern = g_strdup_printf ("%s/%lu*.ts", job->output->encoders[i].record_path, time - 1);
                                GST_DEBUG ("%s", pattern);
                                glob (pattern, 0, NULL, &pglob);
                                for (j = 0; j < pglob.gl_pathc; j++) {
                                        g_remove (pglob.gl_pathv[j]);
                                }
                                globfree (&pglob);
                                g_free (pattern);
                                time = time / 10;
                        }
                }
                list = list->next;
        }
        gstreamill->last_dvr_clean_time = now;
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
                GST_INFO ("streamill stopped");
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
                dvr_clean (gstreamill);
        }

        g_mutex_unlock (&(gstreamill->job_list_mutex));

        /* register streamill monitor */
        now = gst_clock_get_time (gstreamill->system_clock);
        nextid = gst_clock_new_single_shot_id (gstreamill->system_clock, now + 2000 * GST_MSECOND);
        ret = gst_clock_id_wait_async (nextid, gstreamill_monitor, gstreamill, NULL);
        gst_clock_id_unref (nextid);
        if (ret != GST_CLOCK_OK) {
                GST_ERROR ("Register gstreamill monitor failure");
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
                GST_ERROR ("Regist gstreamill monitor failure");
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

        GST_INFO ("Stop gstreamill ...");
        gstreamill->stop = TRUE;
        g_mutex_lock (&(gstreamill->job_list_mutex));
        list = gstreamill->job_list;
        while (list != NULL) {
                job = list->data;
                if (gstreamill->daemon) {
                        stop_job (job, SIGTERM);

                } else {
                        *(job->output->state) = JOB_STATE_NULL;
                }
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

static void child_watch_cb (GPid pid, gint status, Job *job);

static guint64 create_job_process (Job *job)
{
        GError *error = NULL;
        gchar *argv[16], *p;
        GPid pid;
        gint i, j;

        i = 0;
        argv[i++] = g_strdup (job->exe_path);
        argv[i++] = g_strdup ("-l");
        argv[i++] = g_strdup (job->log_dir);
        argv[i++] = g_strdup ("-n");
        argv[i++] = unicode_file_name_2_shm_name (job->name);
        argv[i++] = g_strdup ("-q");
        argv[i++] = g_strdup_printf ("%ld", strlen (job->description));
        p = jobdesc_get_debug (job->description);
        if (p != NULL) {
                argv[i++] = g_strdup_printf ("--gst-debug=%s", p);
                g_free (p);
        }
        argv[i++] = NULL;
        if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
                GST_WARNING ("Start job %s error, reason: %s.", job->name, error->message);
                for (j = 0; j < i; j++) {
                        if (argv[j] != NULL) {
                                g_free (argv[j]);
                        }
                }
                g_error_free (error);
                return JOB_STATE_START_FAILURE;
        }

        for (j = 0; j < i; j++) {
                if (argv[j] != NULL) {
                        g_free (argv[j]);
                }
        }
        job->worker_pid = pid;
        g_child_watch_add (pid, (GChildWatchFunc)child_watch_cb, job);

        while ((*(job->output->state) == JOB_STATE_READY) || (*(job->output->state) == JOB_STATE_VOID_PENDING)) {
                GST_DEBUG ("waiting job process creating ... state: %s", job_state_get_name (*(job->output->state)));
                g_usleep (50000);
        }

        return *(job->output->state);
}

static void child_watch_cb (GPid pid, gint status, Job *job)
{
        /* Close pid */
        g_spawn_close_pid (pid);
        job->age += 1;
        job->worker_pid = 0;

        if (WIFEXITED (status) && (WEXITSTATUS (status) == 0)) {
                GST_WARNING ("Job %s normaly exit, status is %d", job->name, WEXITSTATUS (status));
                *(job->output->state) = JOB_STATE_NULL;
                job->eos = TRUE;
                return;
        }

        if (WIFEXITED (status) && (WEXITSTATUS (status) != 0)) {
                if (WEXITSTATUS (status) < 100) {
                        GST_WARNING ("Start job failure: subprocess return %d and don't restart it.", WEXITSTATUS (status));
                        return;

                } else {
                        if (!job->is_live) {
                                GST_WARNING ("Nonlive job %s exit on critical error return %d.", job->name, WEXITSTATUS (status));
                                *(job->output->state) = JOB_STATE_NULL;
                                job->eos = TRUE;
                                return;
                        }

                        GST_INFO ("Job %s exit on critical error return %d, restart ...", job->name, WEXITSTATUS (status));
                        job_reset (job);
                        if (create_job_process (job) == JOB_STATE_PLAYING) {
                                GST_INFO ("Restart job %s success", job->name);

                        } else {
                                /* create process failure, clean from job list */
                                GST_WARNING ("Restart job %s failure", job->name);
                                *(job->output->state) = JOB_STATE_NULL;
                        }
                }
        }

        if (WIFSIGNALED (status)) {
                if (*(job->output->state) == JOB_STATE_PAUSED) {
                        GST_INFO ("Job %s exit on signal and paused, stopping gstreamill...", job->name);
                        *(job->output->state) = JOB_STATE_NULL;
                        return;
                }

                if (!job->is_live) {
                        GST_INFO ("Nonlive job %s exit on an unhandled signal.", job->name);
                        *(job->output->state) = JOB_STATE_NULL;
                        job->eos = TRUE;
                        return;
                }

                GST_INFO ("Live job %s exit on an unhandled signal, restart.", job->name);
                job_reset (job);
                if (create_job_process (job) == JOB_STATE_PLAYING) {
                        GST_INFO ("Restart job %s success", job->name);

                } else {
                        /* create process failure, clean from job list */
                        GST_WARNING ("Restart job %s failure", job->name);
                        *(job->output->state) = JOB_STATE_NULL;
                }
        }

        return;
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
                p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid job\"\n}");
                return p;
        }

        if (jobdesc_is_live (job_desc)) {
                GST_INFO ("live job arrived:\n%s", job_desc);

        } else {
                GST_INFO ("transcode job arrived:\n%s", job_desc);
        }

        /* create job object */
        name = jobdesc_get_name (job_desc);
        if (get_job (gstreamill, name) != NULL) {
                GST_WARNING ("start job failure, duplicated name %s.", name);
                p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"duplicated name\"\n}");
                g_free (name);
                return p;
        }
        job = job_new ("job", job_desc, "name", name, "exe_path", gstreamill->exe_path, NULL);
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
                p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"initialize job failure\"\n}");
                g_object_unref (job);
                return p;
        }

        if (job->is_live && (job_output_initialize (job) != 0)) {
                p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"initialize job output failure\"\n}");
                g_object_unref (job);
                return p;
        }

        /* reset and start job */
        job_reset (job);
        if (gstreamill->daemon) {
                guint64 stat;

                stat = create_job_process (job);
                if (stat == JOB_STATE_PLAYING) {
                        GST_INFO ("Start job %s success", job->name);
                        g_mutex_lock (&(gstreamill->job_list_mutex));
                        gstreamill->job_list = g_slist_append (gstreamill->job_list, job);
                        g_mutex_unlock (&(gstreamill->job_list_mutex));
                        p = g_strdup_printf ("{\n    \"name\": \"%s\",\n\"result\": \"success\"\n}", job->name);

                } else {
                        GST_WARNING ("Start job %s failure, return stat: %s", job->name, job_state_get_name (stat));
                        g_object_unref (job);
                        p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"create process failure\"\n}");
                }

        } else {
                job_encoders_output_initialize (job);
                if (job_start (job) == 0) {
                        g_mutex_lock (&(gstreamill->job_list_mutex));
                        gstreamill->job_list = g_slist_append (gstreamill->job_list, job);
                        g_mutex_unlock (&(gstreamill->job_list_mutex));
                        p = g_strdup ("{\n    \"result\": \"success\"\n}");

                } else {
                        p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"unknown\"\n}");
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
                stop_job (job, SIGTERM);
                return g_strdup_printf ("{\n    \"name\": \"%s\",\n    \"result\": \"success\"\n}", name);

        } else {
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"job not found\"\n}");
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

        regex = g_regex_new ("^/(live|dvr)/(?<name>[^/]*)/.*", G_REGEX_OPTIMIZE, 0, NULL);
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
        regex = g_regex_new ("^/(live|dvr)/.*/encoder/(?<encoder>[0-9]+).*", G_REGEX_OPTIMIZE, 0, NULL);
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
                GST_WARNING ("Job %s not found.", uri);
                return NULL;
        }
        if (*(job->output->state) != JOB_STATE_PLAYING) {
                GST_WARNING ("FATAL: Job %s state is not playing", job->name);
                return NULL;
        }
        if (index >= job->output->encoder_count) {
                GST_WARNING ("Encoder %s not found.", uri);
                return NULL;
        }
        g_mutex_lock (&(job->access_mutex));
        job->current_access += 1;
        g_mutex_unlock (&(job->access_mutex));

        return &job->output->encoders[index];
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
                GST_WARNING ("Job %s not found.", uri);
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
                GST_WARNING ("Job %s not found.", uri);
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
 *     jobcount:
 * }
 *
 */
gchar * gstreamill_stat (Gstreamill *gstreamill)
{
        JSON_Value *value;
        JSON_Object *object;
        guint jobcount;
        gchar *stat;

        value = json_value_init_object ();
        object = json_value_get_object (value);
        json_object_set_string (object, "version", VERSION);
        json_object_set_string (object, "builddate", __DATE__);
        json_object_set_string (object, "buildtime", __TIME__);
        json_object_set_string (object, "starttime", gstreamill->start_time);
        jobcount = gstreamill_job_number (gstreamill);
        json_object_set_number (object, "jobcount", jobcount);
        stat = json_serialize_to_string (value);
        json_value_free (value);

        return stat;
}

/**
 * gstreamill_list_nonlive_job:
 * @gstreamill: (bin): the gstreamill.
 *
 * Returns: nonlive type job array, ["job1", ... "jobn"]
 */
gchar * gstreamill_list_nonlive_job (Gstreamill *gstreamill)
{
        gchar *jobarray, *p;
        GSList *list;
        Job *job;
 
        jobarray = g_strdup_printf ("[");
        g_mutex_lock (&(gstreamill->job_list_mutex));
        list = gstreamill->job_list;
        while (list != NULL) {
                job = list->data;
                if (!job->is_live) {
                    p = jobarray;
                    jobarray = g_strdup_printf ("%s\"%s\",", p, job->name);
                    g_free (p);
                }
                list = list->next;
        }
        g_mutex_unlock (&(gstreamill->job_list_mutex));
        if (strlen (jobarray) == 1) {
                p = jobarray;
                jobarray = g_strdup_printf ("%s]", p);
                g_free (p);

        } else {
                jobarray[strlen (jobarray) - 1] = ']';
        }

        return jobarray;
}

static JSON_Value * source_stat (Job *job)
{
        JSON_Value *value_source, *value_streams, *value_stream;
        JSON_Array *array_streams;
        JSON_Object *object_source, *object_stream;
        SourceStreamState *stat;
        GstDateTime *time;
        gint i;
        guint64 timestamp;
        gchar *heartbeat;

        value_source = json_value_init_object ();
        object_source = json_value_get_object (value_source);
        json_object_set_number (object_source, "duration", *(job->output->source.duration));
        json_object_set_number (object_source, "sync_error_times", job->output->source.sync_error_times);
        json_object_set_number (object_source, "stream_count", job->output->source.stream_count);
        value_streams = json_value_init_array ();
        array_streams = json_value_get_array (value_streams);
        for (i = 0; i < job->output->source.stream_count; i++) {
                value_stream = json_value_init_object ();
                object_stream = json_value_get_object (value_stream);
                stat = &(job->output->source.streams[i]);
                if (*(job->output->state) == JOB_STATE_PLAYING) {
                        timestamp = stat->current_timestamp;
                        time = gst_date_time_new_from_unix_epoch_local_time (stat->last_heartbeat/GST_SECOND);
                        heartbeat = gst_date_time_to_iso8601_string (time);
                        gst_date_time_unref (time);

                } else {
                        timestamp = 0;
                        heartbeat = g_strdup ("0");
                }
                json_object_set_string (object_stream, "name", stat->name);
                json_object_set_number (object_stream, "timestamp", timestamp);
                json_object_set_string (object_stream, "heartbeat", heartbeat);
                g_free (heartbeat);
                json_array_append_value (array_streams, value_stream);
        }
        json_object_set_value (object_source, "streams", value_streams);

        return value_source;
}

static JSON_Value * encoder_stat (EncoderOutput *encoder_output, guint64 jobstate)
{
        JSON_Value *value_encoder, *value_streams, *value_stream;
        JSON_Array *array_streams;
        JSON_Object *object_encoder, *object_stream;
        EncoderStreamState *stat;
        gint i;
        GstDateTime *time;
        guint64 timestamp;
        gchar *heartbeat;

        value_encoder = json_value_init_object ();
        object_encoder = json_value_get_object (value_encoder);
        json_object_set_string (object_encoder, "name", encoder_output->name);
        time = gst_date_time_new_from_unix_epoch_local_time (*(encoder_output->heartbeat)/GST_SECOND);
        heartbeat = gst_date_time_to_iso8601_string (time);
        gst_date_time_unref (time);
        json_object_set_string (object_encoder, "heartbeat", heartbeat);
        g_free (heartbeat);
        json_object_set_number (object_encoder, "count", *(encoder_output->total_count));
        json_object_set_number (object_encoder, "streamcount", encoder_output->stream_count);
        value_streams = json_value_init_array ();
        array_streams = json_value_get_array (value_streams);
        for (i = 0; i < encoder_output->stream_count; i++) {
                value_stream = json_value_init_object ();
                object_stream = json_value_get_object (value_stream);
                stat = &(encoder_output->streams[i]);
                if (jobstate == JOB_STATE_PLAYING) {
                        timestamp = stat->current_timestamp;
                        time = gst_date_time_new_from_unix_epoch_local_time (stat->last_heartbeat/GST_SECOND);
                        heartbeat = gst_date_time_to_iso8601_string (time);
                        gst_date_time_unref (time);

                } else {
                        timestamp = 0;
                        heartbeat = g_strdup ("0");
                }
                json_object_set_string (object_stream, "name", stat->name);
                json_object_set_number (object_stream, "timestamp", timestamp);
                json_object_set_string (object_stream, "heartbeat", heartbeat);
                g_free (heartbeat);
                json_array_append_value (array_streams, value_stream);
        }
        json_object_set_value (object_encoder, "streams", value_streams);

        return value_encoder;
}

static JSON_Value * encoders_stat (Job *job)
{
        JSON_Value *value_encoders, *value_encoder;
        JSON_Array *array_encoders;
        EncoderOutput *encoder_output;
        gint i;

        value_encoders = json_value_init_array ();
        array_encoders = json_value_get_array (value_encoders);
        for (i = 0; i < job->output->encoder_count; i++) {
                encoder_output = &(job->output->encoders[i]);
                value_encoder = json_value_init_object ();
                value_encoder = encoder_stat (encoder_output, *(job->output->state));
                json_array_append_value (array_encoders, value_encoder);
        }

        return value_encoders;
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
        JSON_Value *value_job, *value_source, *value_encoders, *value_result;
        JSON_Object *object_job, *object_result;
        gchar *result, *name = NULL;
        Job *job;
        GRegex *regex = NULL;
        GMatchInfo *match_info = NULL;
        struct timespec ts;


        regex = g_regex_new ("/job/(?<name>[^/]*).*", G_REGEX_OPTIMIZE, 0, NULL);
        match_info = NULL;
        g_regex_match (regex, uri, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
        }
        if (name == NULL) {
                GST_WARNING ("wrong uri");
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"wrong uri\"\n}");
        }
        job = get_job (gstreamill, name);
        if (job == NULL) {
                GST_WARNING ("uri %s not found.", uri);
                return g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"not found\",\n    \"name\": \"%s\"\n}", name);
        }
        if (clock_gettime (CLOCK_REALTIME, &ts) == -1) {
                GST_WARNING ("clock_gettime error: %s", g_strerror (errno));
                return g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"clock_gettime error\",\n    \"name\": \"%s\"\n}", name);
        }
        ts.tv_sec += 1;
        while (sem_timedwait (job->output->semaphore, &ts) == -1) {
                if (errno == EINTR) {
                        continue;
                }
                GST_WARNING ("sem_timedwait failure: %s", g_strerror (errno));
                return g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"sem_timedwait failure\",\n    \"name\": \"%s\"\n}", name);
        }

        value_job = json_value_init_object ();
        object_job = json_value_get_object (value_job);
        json_object_set_string (object_job, "name", job->name);
        json_object_set_number (object_job, "age", job->age);
        json_object_set_string (object_job, "last_start_time", job->last_start_time);
        json_object_set_string (object_job, "state", job_state_get_name (*(job->output->state)));
        json_object_set_number (object_job, "current_access", job->current_access);
        json_object_set_number (object_job, "cpu_average", job->cpu_average);
        json_object_set_number (object_job, "cpu_current", job->cpu_current);
        json_object_set_number (object_job, "memory", job->memory);
        value_source = source_stat (job);
        json_object_set_value (object_job, "source", value_source);
        json_object_set_number (object_job, "encoder_count", job->output->encoder_count);
        value_encoders = encoders_stat (job);
        json_object_set_value (object_job, "encoders", value_encoders);
        sem_post (job->output->semaphore);

        value_result = json_value_init_object ();
        object_result = json_value_get_object (value_result);
        json_object_set_string (object_result, "result", "success");
        json_object_set_value (object_result, "data", value_job);
        result = json_serialize_to_string (value_result);
        json_value_free (value_result);

        return result;
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
                GST_WARNING ("gst-inspect error, reason: %s.", error->message);
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
