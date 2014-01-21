/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include "jobdesc.h"
#include "livejob.h"

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

        return 0;
}

/*
 * livejob_encoder_output_rap_timestamp:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr to get its timestamp
 *
 * get the timestamp of random access point of encoder_output.
 *
 * Returns: GstClockTime type timestamp.
 *
 */
GstClockTime livejob_encoder_output_rap_timestamp (EncoderOutput *encoder_output, guint64 rap_addr)
{
        GstClockTime timestamp;

        if (rap_addr + 8 <= encoder_output->cache_size) {
                memcpy (&timestamp, encoder_output->cache_addr + rap_addr, 8);

        } else {
                gint n;

                n = encoder_output->cache_size - rap_addr;
                memcpy (&timestamp, encoder_output->cache_addr + rap_addr, n);
                memcpy (&timestamp + n, encoder_output->cache_addr, 8 - n);
        }

        return timestamp;
}

/*
 * livejob_encoder_output_gop_size:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr
 *
 * get gop size.
 *
 * Returns: size of gop.
 *
 */
guint64 livejob_encoder_output_gop_size (EncoderOutput *encoder_output, guint64 rap_addr)
{
        gint gop_size;
        guint64 gop_size_addr;

        /* gop size address */
        if (rap_addr + 8 < encoder_output->cache_size) {
        	gop_size_addr = rap_addr + 8;

        } else {
                gop_size_addr = rap_addr + 8 - encoder_output->cache_size;
        }

        /* gop size */
        if (gop_size_addr + 4 < encoder_output->cache_size) {
                memcpy (&gop_size, encoder_output->cache_addr + gop_size_addr, 4);

        } else {
                gint n;

                n = encoder_output->cache_size - gop_size_addr;
                memcpy (&gop_size, encoder_output->cache_addr + gop_size_addr, n);
                memcpy (&gop_size + n, encoder_output->cache_addr, 4 - n);
        }

        return gop_size;
}

/*
 * livejob_encoder_output_rap_next:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr
 *
 * get next random access address.
 *
 * Returns: next random access address.
 *
 */
guint64 livejob_encoder_output_rap_next (EncoderOutput *encoder_output, guint64 rap_addr)
{
        gint gop_size;
        guint64 next_rap_addr;

        /* gop size */
        gop_size = livejob_encoder_output_gop_size (encoder_output, rap_addr);

        /* next random access address */
        next_rap_addr = rap_addr + gop_size;
        if (next_rap_addr >= encoder_output->cache_size) {
                next_rap_addr -= encoder_output->cache_size;
        }

        return next_rap_addr;
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
        gchar *url;
        GstClockTime last_timestamp;
        GstClockTime segment_duration;
        gsize size;
        gchar buf[128];

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

        last_timestamp = livejob_encoder_output_rap_timestamp (encoder, *(encoder->last_rap_addr));
        url = g_strdup_printf ("%lu.ts", encoder->last_timestamp);
        g_rw_lock_writer_lock (&(encoder->m3u8_playlist_rwlock));
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

