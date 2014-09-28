/*
 *  job
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <glob.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include "utils.h"
#include "jobdesc.h"
#include "job.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        JOB_PROP_0,
        JOB_PROP_NAME,
        JOB_PROP_DESCRIPTION,
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
}

static void job_init (Job *job)
{
        job->system_clock = gst_system_clock_obtain ();
        g_object_set (job->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
        job->encoder_array = g_array_new (FALSE, FALSE, sizeof (gpointer));
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
        gchar *name, *name_hexstr;

        if (job->output == NULL) {
                return;
        }
        output = job->output;
        for (i = 0; i < output->encoder_count; i++) {
                /* message queue release */
                name = g_strdup_printf ("/%s.%d", job->name, i);
                if ((output->encoders[i].mqdes != -1) && (mq_close (output->encoders[i].mqdes) == -1)) {
                        GST_ERROR ("mq_close %s error: %s", name, g_strerror (errno));
                }
                if ((output->encoders[i].mqdes != -1) && (mq_unlink (name) == -1)) {
                        GST_ERROR ("mq_unlink %s error: %s", name, g_strerror (errno));
                }
                if (output->encoders[i].record_path != NULL) {
                        g_free (output->encoders[i].record_path);
                }
                g_free (name);
        }
        /* share memory release */
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

        if (job->name != NULL) {
                g_free (job->name);
                job->name = NULL;
        }

        if (job->description != NULL) {
                g_free (job->description);
                job->description = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void job_finalize (GObject *obj)
{
        Job *job = JOB (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

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
                size += sizeof (sem_t); /* encoder output semaphore */
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
                if (value != NULL) {
                        GST_INFO ("job %s with m3u8 output, append end tag", job->name);
                        g_string_append_printf (master_m3u8_playlist, M3U8_STREAM_INF_TAG, 1, value);
                        g_string_append_printf (master_m3u8_playlist, "encoder/%d/playlist.m3u8\n", i);
                        g_free (value);
                }
                g_free (p);
        }

        p = master_m3u8_playlist->str;
        g_string_free (master_m3u8_playlist, FALSE);

        return p;
}

static guint64 get_dvr_sequence (JobOutput *joboutput, gint index)
{
        glob_t pglob;
        gchar *pattern, *format;
        guint64 sequence;

        pattern = g_strdup_printf ("%s/*", joboutput->encoders[index].record_path);
        if (glob (pattern, 0, NULL, &pglob) == GLOB_NOMATCH) {
                sequence = 0;

        } else {
                format = g_strdup_printf ("%s/%%*[^_]_%%lu_%%*[^_]$", joboutput->encoders[index].record_path);
                sscanf (pglob.gl_pathv[pglob.gl_pathc - 1], format, &sequence);
                sequence += 1;
                g_free (format);
        }
        globfree (&pglob);
        g_free (pattern);

        return sequence;
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
        gchar *name, *p, *name_hexstr;

        job->output_size = status_output_size (job->description);
        if (daemon) {
                /* daemon, use share memory */
                name_hexstr = unicode_file_name_2_shm_name (job->name);
                fd = shm_open (name_hexstr, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                if (fd == -1) {
                        GST_ERROR ("shm_open %s failure: %s", name_hexstr, g_strerror (errno));
                        job->output = NULL;
                        g_free (name_hexstr);
                        return 1;
                }
                g_free (name_hexstr);
                if (ftruncate (fd, job->output_size) == -1) {
                        GST_ERROR ("ftruncate error: %s", g_strerror (errno));
                        job->output = NULL;
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
        g_stpcpy (output->job_description, job->description);
        p += (strlen (job->description) / 8 + 1) * 8;
        output->state = (guint64 *)p;
        *(output->state) = GST_STATE_READY;
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
        output->encoders = (struct _EncoderOutput *)g_malloc (output->encoder_count * sizeof (struct _EncoderOutput));
        for (i = 0; i < output->encoder_count; i++) {
                name = g_strdup_printf ("%s.encoder.%d", job->name, i);
                g_strlcpy (output->encoders[i].name, name, STREAM_NAME_LEN);
                g_free (name);
                name = g_strdup_printf ("encoder.%d", i);
                output->encoders[i].stream_count = jobdesc_streams_count (job->description, name);
                g_free (name);
                output->encoders[i].semaphore = (sem_t *)p;
                sem_init (output->encoders[i].semaphore, 1, 1);
                p += sizeof (sem_t);
                output->encoders[i].heartbeat = (GstClockTime *)p;
                *(output->encoders[i].heartbeat) = gst_clock_get_time (job->system_clock);
                p += sizeof (GstClockTime); /* encoder heartbeat */
                output->encoders[i].eos = (gboolean *)p;
                *(output->encoders[i].eos) = FALSE;
                p += sizeof (gboolean);
                output->encoders[i].streams = (struct _EncoderStreamState *)p;
                p += output->encoders[i].stream_count * sizeof (struct _EncoderStreamState); /* encoder state */
                output->encoders[i].total_count = (guint64 *)p;
                *(output->encoders[i].total_count) = 0;
                p += sizeof (guint64); /* total count size */
                output->encoders[i].mqdes = -1;

                /* non live job has no output */
                if (!job->is_live) {
                        continue;
                }

                output->encoders[i].cache_addr = p;
                p += SHM_SIZE;
                /* initialize gop size = 0. */
                *(gint32 *)(output->encoders[i].cache_addr + 8) = 0;
                /* first gop timestamp is 0 */
                *(GstClockTime *)(output->encoders[i].cache_addr) = 0;
                output->encoders[i].cache_size = SHM_SIZE;
                output->encoders[i].head_addr = (guint64 *)p;
                *(output->encoders[i].head_addr) = 0;
                p += sizeof (guint64); /* cache head */
                output->encoders[i].tail_addr = (guint64 *)p;
                /* timestamp + gop size = 12 */
                *(output->encoders[i].tail_addr) = 12;
                p += sizeof (guint64); /* cache tail */
                output->encoders[i].last_rap_addr = (guint64 *)p;
                *(output->encoders[i].last_rap_addr) = 0;
                p += sizeof (guint64); /* last rap addr */
                output->encoders[i].m3u8_playlist = NULL;

                /* timeshift and dvr */
                output->encoders[i].record_path = NULL;
                output->encoders[i].dvr_duration = jobdesc_dvr_duration (job->description);
                if (output->encoders[i].dvr_duration == 0) {
                        continue;
                }
                output->encoders[i].record_path = g_strdup_printf ("/var/gstreamill/dvr/%s/%d", job->name, i);
                if (!g_file_test (output->encoders[i].record_path, G_FILE_TEST_EXISTS) &&
                    (g_mkdir_with_parents (output->encoders[i].record_path, 0755) != 0)) {
                        GST_ERROR ("Can't open or create %s directory", output->encoders[i].record_path);
                }
                output->encoders[i].sequence = get_dvr_sequence (output, i);
        }
        job->output = output;

        /* m3u8 master playlist */
        if (jobdesc_m3u8streaming (job->description)) {
                job->output->master_m3u8_playlist = render_master_m3u8_playlist (job);
                if (job->output->master_m3u8_playlist == NULL) {
                        GST_ERROR ("render master m3u8 playlist failure.");
                        return 1;
                }

        } else {
                job->output->master_m3u8_playlist = NULL;
        }

        return 0;
}

/*
 * job_stat_update:
 * @job: (in): job object
 *
 * update job's stat
 *
 */
void job_stat_update (Job *job)
{
        gchar *stat_file, *stat, **stats, **cpustats;
        guint64 utime, stime, ctime; /* process user time, process system time, total cpu time */
        gint i;

        stat_file = g_strdup_printf ("/proc/%d/stat", job->worker_pid);
        if (!g_file_get_contents (stat_file, &stat, NULL, NULL)) {
                GST_ERROR ("Read process %d's stat failure.", job->worker_pid);
                return;
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
                return;
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
}

static void dvr_record_segment (EncoderOutput *encoder_output, GstClockTime duration)
{
        gchar *path;
        gint64 realtime;
        guint64 rap_addr;
        gsize segment_size;
        gchar *buf;
        GError *err = NULL;

        /* seek gop it's timestamp is m3u8_push_request->timestamp */
        sem_wait (encoder_output->semaphore);
        rap_addr = encoder_output_gop_seek (encoder_output, encoder_output->last_timestamp);
        sem_post (encoder_output->semaphore);

        /* gop not found? */
        if (rap_addr == G_MAXUINT64) {
                GST_ERROR ("Segment not found!");
                return;
        }

        segment_size = encoder_output_gop_size (encoder_output, rap_addr);
        buf = g_malloc (segment_size);

        /* copy segment to buf */
        if (rap_addr + segment_size + 12 < encoder_output->cache_size) {
                memcpy (buf, encoder_output->cache_addr + rap_addr + 12, segment_size);

        } else {
                gint n;
                guint8 *p;

                n = encoder_output->cache_size - rap_addr - 12;
                p = buf;
                memcpy (p, encoder_output->cache_addr + rap_addr + 12, n);
                p += n;
                memcpy (p, encoder_output->cache_addr, segment_size - n);
        }

        realtime = GST_TIME_AS_MSECONDS (g_get_real_time ());
        path = g_strdup_printf ("/%s/%ld_%lu_%.2f.ts", encoder_output->record_path, realtime, encoder_output->sequence, (double)GST_TIME_AS_SECONDS (duration));
        encoder_output->sequence += 1;

        if (!g_file_set_contents (path, buf, segment_size, &err)) {
                GST_ERROR ("write segment %s failure: %s", path, err->message);
                g_error_free (err);

        } else {
                GST_INFO ("write segment %s success", path);
        }

        g_free (path);
        g_free (buf);
}

static void notify_function (union sigval sv)
{
        EncoderOutput *encoder_output;
        struct sigevent sev;
        GstClockTime last_timestamp;
        GstClockTime segment_duration;
        gsize size;
        gchar *url, buf[128];
        GError *err = NULL;

        encoder_output = (EncoderOutput *)sv.sival_ptr;

        /* mq_notify first */
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = notify_function;
        sev.sigev_notify_attributes = NULL;
        sev.sigev_value.sival_ptr = sv.sival_ptr;
        if (mq_notify (encoder_output->mqdes, &sev) == -1) {
                GST_ERROR ("mq_notify error : %s", g_strerror (errno));
        }

        size = mq_receive (encoder_output->mqdes, buf, 128, NULL);
        if (size == -1) {
                GST_ERROR ("mq_receive error : %s", g_strerror (errno));
                return;
        }
        buf[size] = '\0';
        sscanf (buf, "%lu", &segment_duration);
        last_timestamp = encoder_output_rap_timestamp (encoder_output, *(encoder_output->last_rap_addr));
        url = g_strdup_printf ("%lu.ts", encoder_output->last_timestamp);
        m3u8playlist_add_entry (encoder_output->m3u8_playlist, url, segment_duration);
        if (encoder_output->dvr_duration != 0) {
                dvr_record_segment (encoder_output, segment_duration);
        }
        encoder_output->last_timestamp = last_timestamp;
        g_free (url);
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
        struct sigevent sev;
        struct mq_attr attr;
        gchar *name;

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

        /* is live job? */
        if (!(job->is_live)) {
                return;
        }

        version = jobdesc_m3u8streaming_version (job->description);
        if (version == 0) {
                version = 3;
        }
        window_size = jobdesc_m3u8streaming_window_size (job->description);

        for (i = 0; i < job->output->encoder_count; i++) {
                encoder = &(job->output->encoders[i]);
                name = g_strdup_printf ("/%s.%d", job->name, i);

                if (jobdesc_m3u8streaming (job->description)) {
                        /* reset m3u8 playlist */
                        if (encoder->m3u8_playlist != NULL) {
                                m3u8playlist_free (encoder->m3u8_playlist);
                        }
                        encoder->m3u8_playlist = m3u8playlist_new (version, window_size);

                        /* reset message queue */
                        if (encoder->mqdes != -1) {
                                if (mq_close (encoder->mqdes) == -1) {
                                        GST_ERROR ("mq_close %s error: %s", name, g_strerror (errno));
                                }
                                if (mq_unlink (name) == -1) {
                                        GST_ERROR ("mq_unlink %s error: %s", name, g_strerror (errno));
                                }
                        }
                        attr.mq_flags = 0;
                        attr.mq_maxmsg = 32;
                        attr.mq_msgsize = 128;
                        attr.mq_curmsgs = 0;
                        if ((encoder->mqdes = mq_open (name, O_RDONLY | O_CREAT | O_NONBLOCK, 0666, &attr)) == -1) {
                                GST_ERROR ("mq_open error : %s", g_strerror (errno));
                        }
                        sev.sigev_notify = SIGEV_THREAD;
                        sev.sigev_notify_function = notify_function;
                        sev.sigev_notify_attributes = NULL;
                        sev.sigev_value.sival_ptr = encoder;
                        if (mq_notify (encoder->mqdes, &sev) == -1) {
                                GST_ERROR ("mq_notify error : %s", g_strerror (errno));
                        }
                }

                /* reset semaphore */
                if (job->age > 0) {
                        sem_init (encoder->semaphore, 1, 1);
                }

                g_free (name);
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
                GST_ERROR ("Initialize job source error.");
                return 1;
        }

        if (encoder_initialize (job->encoder_array, job->description, job->output->encoders, job->source) != 0) {
                GST_ERROR ("Initialize job encoder error.");
                return 1;
        }

        /* set pipelines as PLAYING state */
        gst_element_set_state (job->source->pipeline, GST_STATE_PLAYING);
        gst_element_get_state (job->source->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        *(job->output->source.duration) = 0;
        if (!job->is_live && gst_element_query_duration (job->source->pipeline, GST_FORMAT_TIME, &duration)) {
                *(job->output->source.duration) = duration;
        }
        job->source->state = GST_STATE_PLAYING;
        for (i = 0; i < job->encoder_array->len; i++) {
                encoder = g_array_index (job->encoder_array, gpointer, i);
                ret = gst_element_set_state (encoder->pipeline, GST_STATE_PLAYING);
                if (ret == GST_STATE_CHANGE_FAILURE) { //FIXME
                        GST_ERROR ("Set %s to play error.", encoder->name);
                }
                if (encoder->udpstreaming != NULL) { //FIXME
                        ret = gst_element_set_state (encoder->udpstreaming, GST_STATE_PLAYING);
                        if (ret == GST_STATE_CHANGE_FAILURE) {
                                GST_ERROR ("Set %s udpstreaming to play error.", encoder->name);
                        }
                }
                encoder->state = GST_STATE_PLAYING;
        }
        *(job->output->state) = GST_STATE_PLAYING;

        return 0;
}

