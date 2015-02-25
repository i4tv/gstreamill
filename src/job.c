/*
 *  job
 *
 *  Copyright (C) Zhang Ping <dqzhangp@163.com>
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <glob.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <glib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <glib/gstdio.h>

#include "utils.h"
#include "jobdesc.h"
#include "job.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        JOB_PROP_0,
        JOB_PROP_NAME,
        JOB_PROP_DESCRIPTION,
        JOB_PROP_EXEPATH,
};

static void job_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void job_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void job_dispose (GObject *obj);
static void job_finalize (GObject *obj);

static void job_class_init (JobClass *jobclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (jobclass);
        GParamSpec *param;

        g_object_class->set_property = job_set_property;
        g_object_class->get_property = job_get_property;
        g_object_class->dispose = job_dispose;
        g_object_class->finalize = job_finalize;

        param = g_param_spec_string (
                "name",
                "name",
                "name of job",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, JOB_PROP_NAME, param);

        param = g_param_spec_string (
                "job",
                "job",
                "job description of json type",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, JOB_PROP_DESCRIPTION, param);

        param = g_param_spec_string (
                "exe_path",
                "exe_path",
                "exe path",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, JOB_PROP_EXEPATH, param);
}

static void job_init (Job *job)
{
        job->system_clock = gst_system_clock_obtain ();
        g_object_set (job->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
        job->encoder_array = g_array_new (FALSE, FALSE, sizeof (gpointer));
        g_mutex_init (&(job->access_mutex));
}

static void job_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_JOB (obj));

        switch (prop_id) {
        case JOB_PROP_NAME:
                JOB (obj)->name = (gchar *)g_value_dup_string (value);
                break;

        case JOB_PROP_DESCRIPTION:
                JOB (obj)->description = (gchar *)g_value_dup_string (value);
                break;

        case JOB_PROP_EXEPATH:
                JOB (obj)->exe_path = (gchar *)g_value_dup_string (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void job_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        Job *job = JOB (obj);

        switch (prop_id) {
        case JOB_PROP_NAME:
                g_value_set_string (value, job->name);
                break;

        case JOB_PROP_DESCRIPTION:
                g_value_set_string (value, job->description);
                break;

        case JOB_PROP_EXEPATH:
                g_value_set_string (value, job->exe_path);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void job_dispose (GObject *obj)
{
        Job *job = JOB (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
        JobOutput *output;
        gint i;
        gchar *name_hexstr;

        if (job->output == NULL) {
                return;
        }
        output = job->output;

        /* free semaphore */
        if (output->semaphore != NULL) {
                if (sem_close (output->semaphore) == -1) {
                        GST_ERROR ("sem_close failure: %s", g_strerror (errno));
                }
                if (sem_unlink (output->semaphore_name) == -1) {
                        GST_ERROR ("sem_unlink %s error: %s", job->name, g_strerror (errno));
                }
                g_free (output->semaphore_name);
        }

        /* free encoders output */
        if (job->is_live) {
                if (output->master_m3u8_playlist != NULL) {
                        g_free (output->master_m3u8_playlist);
                }
                for (i = 0; i < output->encoder_count; i++) {
                        if (output->encoders[i].record_path != NULL) {
                                g_free (output->encoders[i].record_path);
                        }
                        m3u8playlist_free (output->encoders[i].m3u8_playlist);
                }
        }
        g_free (output->encoders);

        /* free share memory */
        if (job->output_fd != -1) {
                g_close (job->output_fd, NULL);
                if (munmap (output->job_description, job->output_size) == -1) {
                        GST_ERROR ("munmap %s error: %s", job->name, g_strerror (errno));
                }
                name_hexstr = unicode_file_name_2_shm_name (job->name);
                if (shm_unlink (name_hexstr) == -1) {
                        GST_ERROR ("shm_unlink %s error: %s", job->name, g_strerror (errno));
                }
                g_free (name_hexstr);
        }
        g_free (output);

        if (job->description != NULL) {
                g_free (job->description);
                job->description = NULL;
        }

        if (job->exe_path != NULL) {
                g_free (job->exe_path);
                job->exe_path = NULL;
        }

        if (job->name != NULL) {
                g_free (job->name);
                job->name = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void job_finalize (GObject *obj)
{
        Job *job = JOB (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
        Encoder *encoder;
        gint i;

        /* free source  */
        g_object_unref (job->source);

        /* free encoders */
        for (i = 0; i < job->encoder_array->len; i++) {
                encoder = g_array_index (job->encoder_array, gpointer, i);
                g_object_unref (encoder);
        }
        g_array_free (job->encoder_array, TRUE);

        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

GType job_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (JobClass),
                NULL, /* base class initializer */
                NULL, /* base class finalizer */
                (GClassInitFunc)job_class_init,
                NULL,
                NULL,
                sizeof (Job),
                0,
                (GInstanceInitFunc)job_init,
                NULL
        };
        type = g_type_register_static (G_TYPE_OBJECT, "Job", &info, 0);

        return type;
}

static gsize status_output_size (gchar *job)
{
        gsize size;
        gint i;
        gchar *pipeline;

        size = (strlen (job) / 8 + 1) * 8; /* job description, 64 bit alignment */
        size += sizeof (guint64); /* state */
        size += sizeof (gint64); /* duration for transcode */
        size += jobdesc_streams_count (job, "source") * sizeof (struct _SourceStreamState);
        for (i = 0; i < jobdesc_encoders_count (job); i++) {
                size += sizeof (GstClockTime); /* encoder output heartbeat */
                size += sizeof (gboolean); /* end of stream */
                pipeline = g_strdup_printf ("encoder.%d", i);
                size += jobdesc_streams_count (job, pipeline) * sizeof (struct _EncoderStreamState); /* encoder state */
                g_free (pipeline);
                size += sizeof (guint64); /* cache head */
                size += sizeof (guint64); /* cache tail */
                size += sizeof (guint64); /* last rap (random access point) */
                size += sizeof (guint64); /* total count */
                /* nonlive job has no output */
                if (!jobdesc_is_live (job)) {
                        continue;
                }
                /* output share memory */
                size += SHM_SIZE;
        }

        return size;
}

gchar * job_state_get_name (guint64 state)
{
        switch (state) {
        case JOB_STATE_VOID_PENDING:
                return ("JOB_STATE_VOID_PENDING");
        case JOB_STATE_READY:
                return ("JOB_STATE_READY");
        case JOB_STATE_PLAYING:
                return ("JOB_STATE_PLAYING");
        case JOB_STATE_START_FAILURE:
                return ("JOB_STATE_START_FAILURE");
        case JOB_STATE_STOPING:
                return ("JOB_STATE_STOPING");
        case JOB_STATE_STOPED:
                return ("JOB_STATE_STOPED");
        }

        return NULL;
}

/**
 * job_initialize:
 * @job: (in): the job to be initialized.
 * @daemon: (in): is gstreamill run in background.
 *
 * Initialize the output of the job, the output of the job include the status of source and encoders and
 * the output stream.
 *
 * Returns: 0 on success.
 */
gint job_initialize (Job *job, gboolean daemon)
{
        gint i, fd;
        JobOutput *output;
        gchar *name, *p, *name_hexstr, *semaphore_name;
        struct timespec ts;
        sem_t *semaphore;

        job->output_size = status_output_size (job->description);
        name_hexstr = unicode_file_name_2_shm_name (job->name);
        semaphore_name = g_strdup_printf ("/%s", name_hexstr);
        semaphore = sem_open (semaphore_name, O_CREAT, 0644, 1);
        if (semaphore == SEM_FAILED) {
                GST_ERROR ("open semaphore failed: %s", g_strerror (errno));
                g_free (semaphore_name);
                return 1;
        }
        if (clock_gettime (CLOCK_REALTIME, &ts) == -1) {
                GST_ERROR ("clock_gettime error: %s", g_strerror (errno));
                g_free (semaphore_name);
                return 1;
        }
        ts.tv_sec += 2;
        while (sem_timedwait (semaphore, &ts) == -1) {
                if (errno == EINTR) {
                        continue;
                }
                GST_ERROR ("sem_timedwait failure: %s", g_strerror (errno));
                g_free (semaphore_name);
                return 1;
        }
        if (daemon) {
                /* daemon, use share memory */
                fd = shm_open (name_hexstr, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                if (fd == -1) {
                        GST_ERROR ("shm_open %s failure: %s", name_hexstr, g_strerror (errno));
                        job->output = NULL;
                        g_free (name_hexstr);
                        sem_post (semaphore);
                        return 1;
                }
                g_free (name_hexstr);
                if (ftruncate (fd, job->output_size) == -1) {
                        GST_ERROR ("ftruncate error: %s", g_strerror (errno));
                        job->output = NULL;
                        sem_post (semaphore);
                        return 1;
                }
                p = mmap (NULL, job->output_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
                job->output_fd = fd;

        } else {
                p = g_malloc (job->output_size);
                job->output_fd = -1;
        }
        output = (JobOutput *)g_malloc (sizeof (JobOutput));
        output->job_description = (gchar *)p;
        output->semaphore = semaphore;
        output->semaphore_name = semaphore_name;
        g_stpcpy (output->job_description, job->description);
        p += (strlen (job->description) / 8 + 1) * 8;
        output->state = (guint64 *)p;
        p += sizeof (guint64); /* state */
        output->source.duration = (gint64 *)p;
        p += sizeof (gint64); /* duration for transcode */
        output->source.sync_error_times = 0;
        output->source.stream_count = jobdesc_streams_count (job->description, "source");
        output->source.streams = (struct _SourceStreamState *)p;
        for (i = 0; i < output->source.stream_count; i++) {
                output->source.streams[i].last_heartbeat = gst_clock_get_time (job->system_clock);
        }
        p += output->source.stream_count * sizeof (struct _SourceStreamState);
        output->encoder_count = jobdesc_encoders_count (job->description);
        if (output->encoder_count == 0) {
                GST_ERROR ("Invalid job without encoders, initialize job failure");
                sem_post (semaphore);
                return 1;
        }
        output->encoders = (struct _EncoderOutput *)g_malloc (output->encoder_count * sizeof (struct _EncoderOutput));
        for (i = 0; i < output->encoder_count; i++) {
                name = g_strdup_printf ("%s.encoder.%d", job->name, i);
                g_strlcpy (output->encoders[i].name, name, STREAM_NAME_LEN);
                g_free (name);
                name = g_strdup_printf ("encoder.%d", i);
                output->encoders[i].stream_count = jobdesc_streams_count (job->description, name);
                g_free (name);
                output->encoders[i].semaphore = output->semaphore;
                output->encoders[i].heartbeat = (GstClockTime *)p;
                p += sizeof (GstClockTime); /* encoder heartbeat */
                output->encoders[i].eos = (gboolean *)p;
                p += sizeof (gboolean);
                output->encoders[i].streams = (struct _EncoderStreamState *)p;
                p += output->encoders[i].stream_count * sizeof (struct _EncoderStreamState); /* encoder state */
                output->encoders[i].total_count = (guint64 *)p;
                p += sizeof (guint64); /* total count size */

                /* non live job has no output */
                if (!job->is_live) {
                        continue;
                }

                output->encoders[i].cache_addr = p;
                p += SHM_SIZE;
                output->encoders[i].cache_size = SHM_SIZE;
                output->encoders[i].head_addr = (guint64 *)p;
                p += sizeof (guint64); /* cache head */
                output->encoders[i].tail_addr = (guint64 *)p;
                p += sizeof (guint64); /* cache tail */
                output->encoders[i].last_rap_addr = (guint64 *)p;
                p += sizeof (guint64); /* last rap addr */
        }
        job->output = output;
        sem_post (semaphore);

        return 0;
}

static gchar * render_master_m3u8_playlist (Job *job)
{
        GString *master_m3u8_playlist;
        gchar *p, *value;
        gint i;

        master_m3u8_playlist = g_string_new ("");
        g_string_append_printf (master_m3u8_playlist, M3U8_HEADER_TAG);
        if (jobdesc_m3u8streaming_version (job->description) == 0) {
                g_string_append_printf (master_m3u8_playlist, M3U8_VERSION_TAG, 3);

        } else {
                g_string_append_printf (master_m3u8_playlist, M3U8_VERSION_TAG, jobdesc_m3u8streaming_version (job->description));
        }

        for (i = 0; i < job->output->encoder_count; i++) {
                p = g_strdup_printf ("encoder.%d.elements.x264enc.property.bitrate", i);
                value = jobdesc_element_property_value (job->description, p);
                /* value == NULL, audio only? */
                if (value != NULL) {
                        g_string_append_printf (master_m3u8_playlist, M3U8_STREAM_INF_TAG, 1, value);
                        g_free (value);

                } else {
                        /* audio only, use 64kbit bitrate */
                        g_string_append_printf (master_m3u8_playlist, M3U8_STREAM_INF_TAG, 1, "64");
                }
                g_string_append_printf (master_m3u8_playlist, "encoder/%d/playlist.m3u8<%%parameters%%>\n", i);
                g_free (p);
        }

        p = master_m3u8_playlist->str;
        g_string_free (master_m3u8_playlist, FALSE);

        return p;
}

static guint64 get_dvr_sequence (JobOutput *joboutput)
{
        glob_t pglob;
        gchar *pattern, *format;
        guint64 encoder_sequence, sequence;
        gint i;

        sequence = 0;
        for (i = 0; i < joboutput->encoder_count; i++) {
                encoder_sequence = 0;
                pattern = g_strdup_printf ("%s/*", joboutput->encoders[i].record_path);
                if (glob (pattern, 0, NULL, &pglob) != GLOB_NOMATCH) {
                        format = g_strdup_printf ("%s/%%*[^_]_%%lu_%%*[^_]$", joboutput->encoders[i].record_path);
                        sscanf (pglob.gl_pathv[pglob.gl_pathc - 1], format, &encoder_sequence);
                        encoder_sequence += 1;
                        g_free (format);
                }
                globfree (&pglob);
                g_free (pattern);
                if (encoder_sequence > sequence) {
                        sequence = encoder_sequence;
                }
        }

        return sequence;
}

/*
 * job_output_initialize:
 * @job: (in): job object
 *
 * job output, for client access.
 *
 */
gint job_output_initialize (Job *job)
{
        gint i;
        JobOutput *output;

        output = job->output;
        /* m3u8 streaming? */
        if (jobdesc_m3u8streaming (job->description)) {
                /* m3u8 master playlist */
                output->master_m3u8_playlist = render_master_m3u8_playlist (job);
                if (output->master_m3u8_playlist == NULL) {
                        GST_ERROR ("job %s render master m3u8 playlist failure", job->name);
                        return 1;
                }

        } else {
                output->master_m3u8_playlist = NULL;
                return 0;
        }

        /* initialize m3u8 and dvr parameters */
        for (i = 0; i < output->encoder_count; i++) {
                output->encoders[i].m3u8_playlist = NULL;
                output->encoders[i].system_clock = job->system_clock;
                /* timeshift and dvr */
                output->encoders[i].record_path = NULL;
                output->encoders[i].clock_time = GST_CLOCK_TIME_NONE;
                output->encoders[i].dvr_duration = jobdesc_dvr_duration (job->description);
                if (output->encoders[i].dvr_duration == 0) {
                        continue;
                }
                output->encoders[i].record_path = g_strdup_printf ("%s/dvr/%s/%d", MEDIA_LOCATION, job->name, i);
                if (!g_file_test (output->encoders[i].record_path, G_FILE_TEST_EXISTS) &&
                    (g_mkdir_with_parents (output->encoders[i].record_path, 0755) != 0)) {
                        GST_ERROR ("Can't open or create %s directory", output->encoders[i].record_path);
                }
        }
        output->sequence = get_dvr_sequence (output);

        return 0;
}

/*
 * job_encoders_output_initialize:
 * @job: (in): job object
 *
 * subprocess encoders output.
 *
 * Returns: 0 on success.
 */
gint job_encoders_output_initialize (Job *job)
{
        gint i;
        struct timespec ts;

        if (clock_gettime (CLOCK_REALTIME, &ts) == -1) {
                GST_ERROR ("job_encoders_output_initialize clock_gettime error: %s", g_strerror (errno));
                return 1;
        }
        ts.tv_sec += 2;
        while (sem_timedwait (job->output->semaphore, &ts) == -1) {
                if (errno == EINTR) {
                        continue;
                }
                GST_ERROR ("job_encoders_output_initialize sem_timedwait failure: %s", g_strerror (errno));
                return 1;
        }
        *(job->output->state) = JOB_STATE_READY;
        for (i = 0; i < job->output->encoder_count; i++) {
                *(job->output->encoders[i].heartbeat) = gst_clock_get_time (job->system_clock);
                *(job->output->encoders[i].eos) = FALSE;
                *(job->output->encoders[i].total_count) = 0;

                /* non live job has no output */
                if (!job->is_live) {
                        continue;
                }

                /* initialize gop size = 0. */
                *(gint32 *)(job->output->encoders[i].cache_addr + 8) = 0;
                /* first gop timestamp is 0 */
                *(GstClockTime *)(job->output->encoders[i].cache_addr) = 0;
                *(job->output->encoders[i].head_addr) = 0;
                /* timestamp + gop size = 12 */
                *(job->output->encoders[i].tail_addr) = 12;
                *(job->output->encoders[i].last_rap_addr) = 0;
        }
        sem_post (job->output->semaphore);

        return 0;
}

/*
 * job_stat_update:
 * @job: (in): job object
 *
 * update job's stat
 *
 * Returns: 0 on success.
 */
gint job_stat_update (Job *job)
{
        gchar *stat_file, *stat, **stats, **cpustats;
        guint64 utime, stime, ctime; /* process user time, process system time, total cpu time */
        gint i;

        stat_file = g_strdup_printf ("/proc/%d/stat", job->worker_pid);
        if (!g_file_get_contents (stat_file, &stat, NULL, NULL)) {
                GST_ERROR ("Read job %s's stat failure.", job->name);
                return 1;
        }
        stats = g_strsplit (stat, " ", 44);
        utime = g_ascii_strtoull (stats[13],  NULL, 10); /* seconds */
        stime = g_ascii_strtoull (stats[14], NULL, 10);
        /* Resident Set Size */
        job->memory = g_ascii_strtoull (stats[23], NULL, 10) * sysconf (_SC_PAGESIZE);
        g_free (stat_file);
        g_free (stat);
        g_strfreev (stats);
        if (!g_file_get_contents ("/proc/stat", &stat, NULL, NULL)) {
                GST_ERROR ("Read /proc/stat failure.");
                return 1;
        }
        stats = g_strsplit (stat, "\n", 10);
        cpustats = g_strsplit (stats[0], " ", 10);
        ctime = 0;
        for (i = 1; i < 8; i++) {
                ctime += g_ascii_strtoull (cpustats[i], NULL, 10);
        }
        g_free (stat);
        g_strfreev (stats);
        g_strfreev (cpustats);
        job->cpu_average = ((utime + stime) * 100) / (ctime - job->start_ctime);
        job->cpu_current = ((utime - job->last_utime + stime - job->last_stime) * 100) / (ctime - job->last_ctime);
        job->last_ctime = ctime;
        job->last_utime = utime;
        job->last_stime = stime;

        return 0;
}

/*
 * job_reset:
 * @job: job object
 *
 * reset job stat
 *
 */
void job_reset (Job *job)
{
        gchar *stat, **stats, **cpustats;
        GstDateTime *start_time;
        gint i;
        EncoderOutput *encoder;
        guint version, window_size;

        *(job->output->state) = JOB_STATE_VOID_PENDING;
        g_file_get_contents ("/proc/stat", &stat, NULL, NULL);
        stats = g_strsplit (stat, "\n", 10);
        cpustats = g_strsplit (stats[0], " ", 10);
        job->start_ctime = 0;
        for (i = 1; i < 8; i++) {
                job->start_ctime += g_ascii_strtoull (cpustats[i], NULL, 10);
        }
        job->last_ctime = 0;
        job->last_utime = 0;
        job->last_stime = 0;
        g_free (stat);
        g_strfreev (stats);
        g_strfreev (cpustats);
        start_time = gst_date_time_new_now_local_time ();
        if (job->last_start_time != NULL) {
                g_free (job->last_start_time);
        }
        job->last_start_time = gst_date_time_to_iso8601_string (start_time);
        gst_date_time_unref (start_time);

        /* is live job with m3u8streaming? */
        if (!(job->is_live) || !(jobdesc_m3u8streaming (job->description))) {
                return;
        }

        version = jobdesc_m3u8streaming_version (job->description);
        if (version == 0) {
                version = 3;
        }
        window_size = jobdesc_m3u8streaming_window_size (job->description);

        for (i = 0; i < job->output->encoder_count; i++) {
                encoder = &(job->output->encoders[i]);

                /* encoder dvr sequence */
                encoder->sequence = job->output->sequence;

                /* reset m3u8 playlist */
                if (encoder->m3u8_playlist != NULL) {
                        m3u8playlist_free (encoder->m3u8_playlist);
                }
                encoder->m3u8_playlist = m3u8playlist_new (version, window_size, 0);
        }
}

/*
 * job_start:
 * @job: (in): job to be start.
 *
 * initialize source, encoders and start job.
 *
 * Returns: 0 on success, otherwise return 1.
 *
 */
gint job_start (Job *job)
{
        Encoder *encoder;
        GstStateChangeReturn ret;
        gint i;
        gint64 duration;

        job->source = source_initialize (job->description, &(job->output->source));
        if (job->source == NULL) {
                GST_WARNING ("Initialize job source error.");
                *(job->output->state) = JOB_STATE_START_FAILURE;
                return 1;
        }

        if (encoder_initialize (job->encoder_array, job->description, job->output->encoders, job->source) != 0) {
                GST_WARNING ("Initialize job encoder error.");
                *(job->output->state) = JOB_STATE_START_FAILURE;
                return 2;
        }

        /* set pipelines as PLAYING state */
        gst_element_set_state (job->source->pipeline, GST_STATE_PLAYING);
        ret = gst_element_get_state (job->source->pipeline, NULL, NULL, 5 * GST_SECOND);
        if (ret == GST_STATE_CHANGE_FAILURE) {
                GST_WARNING ("Set %s source pipeline to play error.", job->name);
                *(job->output->state) = JOB_STATE_START_FAILURE;
                return 3;

        } else if (ret == GST_STATE_CHANGE_ASYNC) {
                GST_WARNING ("Set %s source pipeline to play timeout.", job->name);
                *(job->output->state) = JOB_STATE_START_FAILURE;
                return 4;
        }
        GST_INFO ("Set source pipeline to play state ok");
        *(job->output->source.duration) = 0;
        if (!job->is_live && gst_element_query_duration (job->source->pipeline, GST_FORMAT_TIME, &duration)) {
                *(job->output->source.duration) = duration;
        }
        for (i = 0; i < job->encoder_array->len; i++) {
                encoder = g_array_index (job->encoder_array, gpointer, i);
                ret = gst_element_set_state (encoder->pipeline, GST_STATE_PLAYING);
                if (ret == GST_STATE_CHANGE_FAILURE) {
                        GST_WARNING ("Set %s to play error.", encoder->name);
                        *(job->output->state) = JOB_STATE_START_FAILURE;
                        return 5;

                }
                if (encoder->udpstreaming != NULL) {
                        ret = gst_element_set_state (encoder->udpstreaming, GST_STATE_PLAYING);
                        if (ret == GST_STATE_CHANGE_FAILURE) {
                                GST_WARNING ("Set %s udpstreaming to play error.", encoder->name);
                                *(job->output->state) = JOB_STATE_START_FAILURE;
                                return 6;

                        }
                }
                GST_INFO ("Set encoder %s to play state ok", encoder->name);
        }
        *(job->output->state) = JOB_STATE_PLAYING;
        GST_INFO ("Set job %s to play state ok", job->name);

        return 0;
}

/*
 * job_stop:
 * @job: (in): job to be start.
 * @sig: (in): signal, SIGTERM or SIGUSR2
 *
 * initialize source, encoders and start job.
 *
 * Returns: 0 on success, otherwise return 1.
 *
 */
gint job_stop (Job *job, gint sig)
{
        if (sig == SIGTERM) {
                /* normally stop */
                if (*(job->output->state) != JOB_STATE_STOPED) {
                        *(job->output->state) = JOB_STATE_STOPING;
                }
                GST_WARNING ("Stop job %s, pid %d.", job->name, job->worker_pid);

        } else {
                /* unexpect stop, restart job */
                GST_WARNING ("Restart job %s, pid %d.", job->name, job->worker_pid);
        }

        if (job->worker_pid != 0) {
                kill (job->worker_pid, sig);

        } else {
                GST_WARNING ("job %s's state is not playing", job->name); 
        }

        return 0;
}


