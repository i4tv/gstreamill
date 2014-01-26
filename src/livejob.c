/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include "jobdesc.h"
#include "livejob.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        LIVEJOB_PROP_0,
        LIVEJOB_PROP_NAME,
        LIVEJOB_PROP_JOB,
};

static void livejob_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void livejob_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void livejob_dispose (GObject *obj);
static void livejob_finalize (GObject *obj);

static void livejob_class_init (LiveJobClass *livejobclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (livejobclass);
        GParamSpec *param;

        g_object_class->set_property = livejob_set_property;
        g_object_class->get_property = livejob_get_property;
        g_object_class->dispose = livejob_dispose;
        g_object_class->finalize = livejob_finalize;

        param = g_param_spec_string (
                "name",
                "name",
                "name of livejob",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, LIVEJOB_PROP_NAME, param);

        param = g_param_spec_string (
                "job",
                "job",
                "job description of json type",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, LIVEJOB_PROP_JOB, param);
}

static void livejob_init (LiveJob *livejob)
{
        livejob->system_clock = gst_system_clock_obtain ();
        g_object_set (livejob->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
        livejob->encoder_array = g_array_new (FALSE, FALSE, sizeof (gpointer));
        livejob->m3u8push_thread_pool = NULL;
}

static void livejob_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_LIVEJOB (obj));

        switch (prop_id) {
        case LIVEJOB_PROP_NAME:
                LIVEJOB (obj)->name = (gchar *)g_value_dup_string (value);
                break;

        case LIVEJOB_PROP_JOB:
                LIVEJOB (obj)->job = (gchar *)g_value_dup_string (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void livejob_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        LiveJob *livejob = LIVEJOB (obj);

        switch (prop_id) {
        case LIVEJOB_PROP_NAME:
                g_value_set_string (value, livejob->name);
                break;

        case LIVEJOB_PROP_JOB:
                g_value_set_string (value, livejob->job);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void livejob_dispose (GObject *obj)
{
        LiveJob *livejob = LIVEJOB (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
        LiveJobOutput *output;
        gint i;
        gchar *name;

        output = livejob->output;
        for (i = 0; i < output->encoder_count; i++) {
                /* share memory release */
                name = g_strdup_printf ("%s.%d", livejob->name, i);
                if (output->encoders[i].cache_fd != -1) {
                        g_close (output->encoders[i].cache_fd, NULL);
                        if (munmap (output->encoders[i].cache_addr, SHM_SIZE) == -1) {
                                GST_ERROR ("munmap %s error: %s", name, g_strerror (errno));
                        }
                        if (shm_unlink (name) == -1) {
                                GST_ERROR ("shm_unlink %s error: %s", name, g_strerror (errno));
                        }
                }
                g_free (name);

                /* semaphore and message queue release */
                name = g_strdup_printf ("/%s.%d", livejob->name, i);
                if (sem_close (output->encoders[i].semaphore) == -1) {
                        GST_ERROR ("sem_close %s error: %s", name, g_strerror (errno));
                }
                if (sem_unlink (name) == -1) {
                        GST_ERROR ("sem_unlink %s error: %s", name, g_strerror (errno));
                }
                if (mq_close (output->encoders[i].mqdes) == -1) {
                        GST_ERROR ("mq_close %s error: %s", name, g_strerror (errno));
                }
                if (mq_unlink (name) == -1) {
                        GST_ERROR ("mq_unlink %s error: %s", name, g_strerror (errno));
                }
                g_free (name);
        }

        if (livejob->output_fd != -1) {
                g_close (livejob->output_fd, NULL);
                if (munmap (output->job_description, livejob->output_size) == -1) {
                        GST_ERROR ("munmap %s error: %s", livejob->name, g_strerror (errno));
                }
                if (shm_unlink (livejob->name) == -1) {
                        GST_ERROR ("shm_unlink %s error: %s", livejob->name, g_strerror (errno));
                }
        }
        g_free (output);

        if (livejob->name != NULL) {
                g_free (livejob->name);
                livejob->name = NULL;
        }

        if (livejob->job != NULL) {
                g_free (livejob->job);
                livejob->job = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void livejob_finalize (GObject *obj)
{
        LiveJob *livejob = LIVEJOB (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        g_array_free (livejob->encoder_array, TRUE);

        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

GType livejob_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (LiveJobClass),
                NULL, /* base class initializer */
                NULL, /* base class finalizer */
                (GClassInitFunc)livejob_class_init,
                NULL,
                NULL,
                sizeof (LiveJob),
                0,
                (GInstanceInitFunc)livejob_init,
                NULL
        };
        type = g_type_register_static (G_TYPE_OBJECT, "LiveJob", &info, 0);

        return type;
}

static gsize status_output_size (gchar *job)
{
        gsize size;
        gint i;
        gchar *pipeline;

        size = (strlen (job) / 8 + 1) * 8; /* job description, 64 bit alignment */
        size += sizeof (guint64); /* state */
        size += jobdesc_streams_count (job, "source") * sizeof (struct _SourceStreamState);
        for (i = 0; i < jobdesc_encoders_count (job); i++) {
                size += sizeof (GstClockTime); /* encoder heartbeat */
                pipeline = g_strdup_printf ("encoder.%d", i);
                size += jobdesc_streams_count (job, pipeline) * sizeof (struct _EncoderStreamState); /* encoder state */
                g_free (pipeline);
                size += sizeof (guint64); /* cache head */
                size += sizeof (guint64); /* cache tail */
                size += sizeof (guint64); /* last rap (random access point) */
                size += sizeof (guint64); /* total count */
        }

        return size;
}

static gint http_request (LiveJob *livejob, guint8 *data, gsize count)
{
	gint socketfd;
        struct hostent *hostp;
        struct sockaddr_in serveraddr;
        gint ret, len;
        gsize sent;

        socketfd = socket (AF_INET, SOCK_STREAM, 0);
        memset (&serveraddr, 0x00, sizeof (struct sockaddr_in));
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port = htons (livejob->m3u8push_port);
        serveraddr.sin_addr.s_addr = inet_addr (livejob->m3u8push_host);
        if ((serveraddr.sin_addr.s_addr = inet_addr (livejob->m3u8push_host)) == (unsigned long)INADDR_NONE) {
                hostp = gethostbyname (livejob->m3u8push_host);
                if (hostp == (struct hostent *)NULL) {
                        GST_ERROR ("put segment, host %s not found", livejob->m3u8push_host);
                        g_free (data);
                        return -1;
                }
                memcpy (&serveraddr.sin_addr, hostp->h_addr, sizeof (serveraddr.sin_addr));
        }
        ret = connect (socketfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
        if (ret < 0) {
                GST_ERROR ("put segment, connect error: %s", g_strerror (errno));
                g_free (data);
                return  -1;
        }

        sent = 0;
        while (sent < count) {
                len = INT_MAX < count - sent ? INT_MAX : count - sent;
                ret = write (socketfd, data + sent, len);
                if (ret == -1) {
                        if (errno == EAGAIN) {
                                /* block, wait 50ms */
                                g_usleep (50000);
                                continue;
                        }
                        GST_INFO ("Write error: %s", g_strerror (errno));
                        break;
                }
                sent += ret;
        }
        close (socketfd);
        g_free (data);

        return sent;
}

static void m3u8push_thread_func (gpointer data, gpointer user_data)
{
        LiveJob *livejob = (LiveJob *)user_data;
        m3u8PushRequest *m3u8_push_request = (m3u8PushRequest *)data;
        gchar *header, *request_uri;
        guint64 rap_addr;
        GstClockTime t;
        gsize segment_size, count;
        guint8 *buf, *playlist;

        /* seek gop */
        sem_wait (m3u8_push_request->encoder->semaphore);
        rap_addr = *(m3u8_push_request->encoder->head_addr);
        while (rap_addr != *(m3u8_push_request->encoder->last_rap_addr)) {
                t = encoder_output_rap_timestamp (m3u8_push_request->encoder, rap_addr);
                if (m3u8_push_request->timestamp == t) {
                        break;
                }
                rap_addr = encoder_output_rap_next (m3u8_push_request->encoder, rap_addr);
        }
        sem_post (m3u8_push_request->encoder->semaphore);

        /* header */
        segment_size = encoder_output_gop_size (m3u8_push_request->encoder, rap_addr);
        request_uri = g_strdup_printf ("%s/%s/%lu.ts", livejob->m3u8push_path, m3u8_push_request->encoder->name, m3u8_push_request->timestamp);
        header = g_strdup_printf (HTTP_PUT, request_uri, PACKAGE_NAME, PACKAGE_VERSION, livejob->m3u8push_host, segment_size);

        /* copy header to buffer */
        count = segment_size + strlen (header);
        buf = g_malloc (count);
        memcpy (buf, header, strlen (header));

        /* copy body(segment) to buffer */
        if (rap_addr + segment_size + 12 < m3u8_push_request->encoder->cache_size) {
                memcpy (buf + strlen (header), m3u8_push_request->encoder->cache_addr + rap_addr + 12, segment_size);

        } else {
                gint n;
                guint8 *p;

                n = m3u8_push_request->encoder->cache_size - rap_addr - 12;
                p = buf + strlen (header);
                memcpy (p, m3u8_push_request->encoder->cache_addr + rap_addr + 12, n);
                p += n;
                memcpy (p, m3u8_push_request->encoder->cache_addr, segment_size - n);
        }

        /* put segment */
        http_request (livejob, buf, count);

        /* put playlist */
        while ((livejob->sequence_number + 1) != m3u8_push_request->sequence_number) {
                /* waiting previous segment pushed */
                g_usleep (50000);
        }
        g_free (request_uri);
        request_uri = g_strdup_printf ("%s/%s/playlist.m3u8", livejob->m3u8push_path, m3u8_push_request->encoder->name);
        playlist = m3u8playlist_render (m3u8_push_request->encoder->m3u8_playlist);
        g_free (header);
        header = g_strdup_printf (HTTP_PUT, request_uri, PACKAGE_NAME, PACKAGE_VERSION, livejob->m3u8push_host, strlen (playlist));
        buf = g_strdup_printf ("%s%s", header, playlist);
        http_request (livejob, buf, strlen (buf));
        livejob->sequence_number++;

        /* remove segment */
        g_free (header);
        if (m3u8_push_request->rm_segment != NULL) {
                g_free (request_uri);
                request_uri = g_strdup_printf ("%s/%s/%s", livejob->m3u8push_path, m3u8_push_request->encoder->name, m3u8_push_request->rm_segment);
                header = g_strdup_printf (HTTP_DELETE, request_uri,  PACKAGE_NAME, PACKAGE_VERSION, livejob->m3u8push_host);
                http_request (livejob, header, strlen (header));
                g_free (m3u8_push_request->rm_segment);
        }

        g_free (playlist);
        g_free (request_uri);
        g_free (m3u8_push_request);
}

/**
 * livejob_initialize:
 * @livejob: (in): the livejob to be initialized.
 * @daemon: (in): is gstreamill run in background.
 *
 * Initialize the output of the livejob, the output of the livejob include the status of source and encoders and
 * the output stream.
 *
 * Returns: 0 on success.
 */
gint livejob_initialize (LiveJob *livejob, gboolean daemon)
{
        gint i, fd;
        LiveJobOutput *output;
        gchar *name, *p;

        livejob->output_size = status_output_size (livejob->job);
        if (daemon) {
                /* daemon, use share memory */
                fd = shm_open (livejob->name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                if (ftruncate (fd, livejob->output_size) == -1) {
                        GST_ERROR ("ftruncate error: %s", g_strerror (errno));
                        return 1;
                }
                p = mmap (NULL, livejob->output_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
                livejob->output_fd = fd;

        } else {
                p = g_malloc (livejob->output_size);
                livejob->output_fd = -1;
        }
        output = (LiveJobOutput *)g_malloc (sizeof (LiveJobOutput));
        output->job_description = (gchar *)p;
        g_stpcpy (output->job_description, livejob->job);
        p += (strlen (livejob->job) / 8 + 1) * 8;
        output->state = (guint64 *)p;
        p += sizeof (guint64); /* state */
        output->source.sync_error_times = 0;
        output->source.stream_count = jobdesc_streams_count (livejob->job, "source");
        output->source.streams = (struct _SourceStreamState *)p;
        for (i = 0; i < output->source.stream_count; i++) {
                output->source.streams[i].last_heartbeat = gst_clock_get_time (livejob->system_clock);
        }
        p += output->source.stream_count * sizeof (struct _SourceStreamState);
        output->encoder_count = jobdesc_encoders_count (livejob->job);
        output->encoders = (struct _EncoderOutput *)g_malloc (output->encoder_count * sizeof (struct _EncoderOutput));
        for (i = 0; i < output->encoder_count; i++) {
                name = g_strdup_printf ("encoder.%d", i);
                g_strlcpy (output->encoders[i].name, name, STREAM_NAME_LEN);
                output->encoders[i].stream_count = jobdesc_streams_count (livejob->job, name);
                g_free (name);
                name = g_strdup_printf ("/%s.%d", livejob->name, i);
                output->encoders[i].semaphore = sem_open (name, O_CREAT, 0600, 1);
                if (output->encoders[i].semaphore == SEM_FAILED) {
                        GST_ERROR ("sem_open %s error: %s", name, g_strerror (errno));
                        g_free (name);
                        return 1;
                }
                g_free (name);
                output->encoders[i].heartbeat = (GstClockTime *)p;
                *(output->encoders[i].heartbeat) = gst_clock_get_time (livejob->system_clock);
                p += sizeof (GstClockTime); /* encoder heartbeat */
                output->encoders[i].streams = (struct _EncoderStreamState *)p;
                p += output->encoders[i].stream_count * sizeof (struct _EncoderStreamState); /* encoder state */
                if (daemon) {
                        /* daemon, use share memory. */
                        name = g_strdup_printf ("%s.%d", livejob->name, i);
                        fd = shm_open (name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                        if (ftruncate (fd, SHM_SIZE) == -1) {
                                GST_ERROR ("ftruncate error: %s", g_strerror (errno));
                                return 1;
                        }
                        output->encoders[i].cache_addr = mmap (NULL, SHM_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
                        output->encoders[i].cache_fd = fd;
                        /* initialize gop size = 0. */
                        *(gint32 *)(output->encoders[i].cache_addr + 8) = 0;
                        g_free (name);

                } else {
                        output->encoders[i].cache_fd = -1;
                        output->encoders[i].cache_addr = g_malloc (SHM_SIZE);
                }
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
                output->encoders[i].total_count = (guint64 *)p;
                *(output->encoders[i].total_count) = 0;
                p += sizeof (guint64); /* total count */
                output->encoders[i].m3u8_playlist = NULL;
                output->encoders[i].last_timestamp = 0;
                output->encoders[i].mqdes = -1;
        }
        livejob->output = output;

        livejob->m3u8push_uri = jobdesc_m3u8streaming_push_server_uri (livejob->job);
        if (livejob->m3u8push_uri != NULL) {
                GError *err = NULL;

                sscanf (livejob->m3u8push_uri, "http://%[^:/]", livejob->m3u8push_host);
                livejob->m3u8push_port = 80;
                sscanf (livejob->m3u8push_uri, "http://%*[^:]:%hu", &(livejob->m3u8push_port));
                sscanf (livejob->m3u8push_uri, "http://%*[^/]%s", livejob->m3u8push_path);
                livejob->m3u8push_thread_pool = g_thread_pool_new (m3u8push_thread_func, livejob, 10, TRUE, &err);
                if (err != NULL) {
                        GST_ERROR ("Create m3u8push thread pool error %s", err->message);
                        g_error_free (err);
                        return 1;
                }
        }
 
        return 0;
}

/*
 * livejob_stat_update:
 * @livejob: (in): livejob object
 *
 * update livejob's stat
 *
 */
void livejob_stat_update (LiveJob *livejob)
{
        gchar *stat_file, *stat, **stats, **cpustats;
        guint64 utime, stime, ctime; /* process user time, process system time, total cpu time */
        gint i;

        stat_file = g_strdup_printf ("/proc/%d/stat", livejob->worker_pid);
        if (!g_file_get_contents (stat_file, &stat, NULL, NULL)) {
                GST_ERROR ("Read process %d's stat failure.", livejob->worker_pid);
                return;
        }
        stats = g_strsplit (stat, " ", 44);
        utime = g_ascii_strtoull (stats[13],  NULL, 10); /* seconds */
        stime = g_ascii_strtoull (stats[14], NULL, 10);
        /* Resident Set Size */
        livejob->memory = g_ascii_strtoull (stats[23], NULL, 10) * sysconf (_SC_PAGESIZE);
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
        livejob->cpu_average = ((utime + stime) * 100) / (ctime - livejob->start_ctime);
        livejob->cpu_current = ((utime - livejob->last_utime + stime - livejob->last_stime) * 100) / (ctime - livejob->last_ctime);
        livejob->last_ctime = ctime;
        livejob->last_utime = utime;
        livejob->last_stime = stime;
}

static void notify_function (union sigval sv)
{
        EncoderOutput *encoder;
        struct sigevent sev;
        GstClockTime last_timestamp;
        GstClockTime segment_duration;
        gsize size;
        gchar *url, buf[128];
        m3u8PushRequest *m3u8_push_request;
        GError *err = NULL;

        encoder = (EncoderOutput *)sv.sival_ptr;

        /* mq_notify first */
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = notify_function;
        sev.sigev_notify_attributes = NULL;
        sev.sigev_value.sival_ptr = sv.sival_ptr;
        if (mq_notify (encoder->mqdes, &sev) == -1) {
                GST_ERROR ("mq_notify error : %s", g_strerror (errno));
        }

        size = mq_receive (encoder->mqdes, buf, 128, NULL);
        if (size == -1) {
                GST_ERROR ("mq_receive error : %s", g_strerror (errno));
                return;
        }
        buf[size] = '\0';
        sscanf (buf, "%lu", &segment_duration);

        last_timestamp = encoder_output_rap_timestamp (encoder, *(encoder->last_rap_addr));
        url = g_strdup_printf ("%lu.ts", encoder->last_timestamp);

        g_rw_lock_writer_lock (&(encoder->m3u8_playlist_rwlock));
        /* put segment */
        if (encoder->m3u8push_thread_pool != NULL) {
                M3U8Entry *entry;

                m3u8_push_request = g_malloc (sizeof (m3u8PushRequest));
                encoder->sequence_number++;
                m3u8_push_request->sequence_number = encoder->sequence_number;
                m3u8_push_request->encoder = encoder;
                m3u8_push_request->timestamp = encoder->last_timestamp;
                m3u8_push_request->rm_segment = NULL;
                entry = m3u8playlist_tail_entry (encoder->m3u8_playlist);
                if (entry != NULL) {
                        m3u8_push_request->rm_segment = g_strdup (entry->url);
                }
                g_thread_pool_push (encoder->m3u8push_thread_pool, m3u8_push_request, &err);
                if (err != NULL) {
                        GST_FIXME ("m3u8push thread pool push error %s", err->message);
                        g_error_free (err);
                }
        }
        /* add new m3u8 playlist entry */
        m3u8playlist_add_entry (encoder->m3u8_playlist, url, segment_duration);
        g_rw_lock_writer_unlock (&(encoder->m3u8_playlist_rwlock));

        encoder->last_timestamp = last_timestamp;
        g_free (url);
}

/*
 * livejob_reset:
 * @livejob: livejob object
 *
 * reset livejobe stat
 *
 */
void livejob_reset (LiveJob *livejob)
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
        livejob->start_ctime = 0;
        for (i = 1; i < 8; i++) {
                livejob->start_ctime += g_ascii_strtoull (cpustats[i], NULL, 10);
        }
        livejob->last_ctime = 0;
        livejob->last_utime = 0;
        livejob->last_stime = 0;
        g_free (stat);
        g_strfreev (stats);
        g_strfreev (cpustats);
        start_time = gst_date_time_new_now_local_time ();
        if (livejob->last_start_time != NULL) {
                g_free (livejob->last_start_time);
        }
        livejob->last_start_time = gst_date_time_to_iso8601_string (start_time);
        gst_date_time_unref (start_time);

        version = jobdesc_m3u8streaming_version (livejob->job);
        if (version == 0) {
                version = 3;
        }
        window_size = jobdesc_m3u8streaming_window_size (livejob->job);

        for (i = 0; i < livejob->output->encoder_count; i++) {
                encoder = &(livejob->output->encoders[i]);
                name = g_strdup_printf ("/%s.%d", livejob->name, i);

                if (jobdesc_m3u8streaming (livejob->job)) {
                        /* reset m3u8 playlist */
                        if (encoder->m3u8_playlist != NULL) {
                                g_rw_lock_clear (&(encoder->m3u8_playlist_rwlock));
                                m3u8playlist_free (encoder->m3u8_playlist);
                        }
                        encoder->m3u8push_thread_pool = livejob->m3u8push_thread_pool;
                        encoder->m3u8_playlist = m3u8playlist_new (version, window_size, FALSE);
                        g_rw_lock_init (&(encoder->m3u8_playlist_rwlock));

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
                if (livejob->age > 0) {
                        if (sem_close (encoder->semaphore) == -1) {
                                GST_ERROR ("sem_close %s error: %s", name, g_strerror (errno));
                        }
                        if (sem_unlink (name) == -1) {
                                GST_ERROR ("sem_unlink %s error: %s", name, g_strerror (errno));
                        }
                        encoder->semaphore = sem_open (name, O_CREAT, 0600, 1);
                        if (encoder->semaphore == SEM_FAILED) {
                                GST_ERROR ("sem_open %s error: %s", name, g_strerror (errno));
                        }
                }

                g_free (name);
        }
}

/*
 * livejob_start:
 * @livejob: (in): livejob to be start.
 *
 * initialize source, encoders and start livejob.
 *
 * Returns: 0 on success, otherwise return 1.
 *
 */
gint livejob_start (LiveJob *livejob)
{
        Encoder *encoder;
        GstStateChangeReturn ret;
        gint i;

        livejob->source = source_initialize (livejob->job, livejob->output->source);
        if (livejob->source == NULL) {
                GST_ERROR ("Initialize livejob source error.");
                return 1;
        }

        if (encoder_initialize (livejob->encoder_array, livejob->job, livejob->output->encoders, livejob->source) != 0) {
                GST_ERROR ("Initialize livejob encoder error.");
                return 1;
        }

        /* set pipelines as PLAYING state */
        gst_element_set_state (livejob->source->pipeline, GST_STATE_PLAYING);
        livejob->source->state = GST_STATE_PLAYING;
        for (i = 0; i < livejob->encoder_array->len; i++) {
                encoder = g_array_index (livejob->encoder_array, gpointer, i);
                ret = gst_element_set_state (encoder->pipeline, GST_STATE_PLAYING);
                if (ret == GST_STATE_CHANGE_FAILURE) {
                        GST_ERROR ("Set %s to play error.", encoder->name);
                }
                if (encoder->udpstreaming != NULL) {
                        ret = gst_element_set_state (encoder->udpstreaming, GST_STATE_PLAYING);
                        if (ret == GST_STATE_CHANGE_FAILURE) {
                                GST_ERROR ("Set %s udpstreaming to play error.", encoder->name);
                        }
                }
                encoder->state = GST_STATE_PLAYING;
        }
        *(livejob->output->state) = GST_STATE_PLAYING;

        return 0;
}

/*
 * livejob_master_m3u8_playlist:
 * @livejob: (in): the livejob object
 *
 * get master m3u8 playlist of the livejob
 *
 * Returns: master m3u8 playlist
 *
 */
gchar * livejob_get_master_m3u8_playlist (LiveJob *livejob)
{
        GString *master_m3u8_playlist;
        gchar *p, *value;
        gint i;

        if (!jobdesc_m3u8streaming (livejob->job)) {
                /* m3u8streaming no enabled */
                return "not found";
        }

        master_m3u8_playlist = g_string_new ("");
        g_string_append_printf (master_m3u8_playlist, M3U8_HEADER_TAG);
        if (jobdesc_m3u8streaming_version (livejob->job) == 0) {
                g_string_append_printf (master_m3u8_playlist, M3U8_VERSION_TAG, 3);

        } else {
                g_string_append_printf (master_m3u8_playlist, M3U8_VERSION_TAG, jobdesc_m3u8streaming_version (livejob->job));
        }

        for (i = 0; i < livejob->output->encoder_count; i++) {
                p = g_strdup_printf ("encoder.%d.elements.x264enc.property.bitrate", i);
                value = jobdesc_element_property_value (livejob->job, p);
                g_string_append_printf (master_m3u8_playlist, M3U8_STREAM_INF_TAG, 1, value);
                g_string_append_printf (master_m3u8_playlist, "encoder/%d/playlist.m3u8\n", i);
                g_free (p);
                g_free (value);
        }

        p = master_m3u8_playlist->str;
        g_string_free (master_m3u8_playlist, FALSE);

        return p;
}

