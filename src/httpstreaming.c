/*
 * streaming over http.
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#define _XOPEN_SOURCE
#include <unistd.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "httpstreaming.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        HTTPSTREAMING_PROP_0,
        HTTPSTREAMING_PROP_ADDRESS,
        HTTPSTREAMING_PROP_GSTREAMILL,
};

static void httpstreaming_class_init (HTTPStreamingClass *httpstreamingclass);
static void httpstreaming_init (HTTPStreaming *httpstreaming);
static GObject *httpstreaming_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties);
static void httpstreaming_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void httpstreaming_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);

static void httpstreaming_class_init (HTTPStreamingClass *httpstreamingclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (httpstreamingclass);
        GParamSpec *param;

        g_object_class->constructor = httpstreaming_constructor;
        g_object_class->set_property = httpstreaming_set_property;
        g_object_class->get_property = httpstreaming_get_property;

        param = g_param_spec_string (
                "address",
                "address",
                "address of httpstreaming",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPSTREAMING_PROP_ADDRESS, param);

        param = g_param_spec_pointer (
                "gstreamill",
                "gstreamill",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPSTREAMING_PROP_GSTREAMILL, param);
}

static void httpstreaming_init (HTTPStreaming *httpstreaming)
{
}

static GObject * httpstreaming_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
        GObject *obj;
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        obj = parent_class->constructor (type, n_construct_properties, construct_properties);

        return obj;
}

static void httpstreaming_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_HTTPSTREAMING (obj));

        switch (prop_id) {
        case HTTPSTREAMING_PROP_ADDRESS:
                HTTPSTREAMING (obj)->address = (gchar *)g_value_dup_string (value);
                break;

        case HTTPSTREAMING_PROP_GSTREAMILL:
                HTTPSTREAMING (obj)->gstreamill = (Gstreamill *)g_value_get_pointer (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void httpstreaming_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        HTTPStreaming  *httpstreaming = HTTPSTREAMING (obj);

        switch (prop_id) {
        case HTTPSTREAMING_PROP_ADDRESS:
                g_value_set_string (value, httpstreaming->address);
                break;

        case HTTPSTREAMING_PROP_GSTREAMILL:
                g_value_set_pointer (value, httpstreaming->gstreamill);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

GType httpstreaming_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (HTTPStreamingClass), /* class size */
                NULL, /* base initializer */
                NULL, /* base finalizer */
                (GClassInitFunc) httpstreaming_class_init, /* class init */
                NULL, /* class finalize */
                NULL, /* class data */
                sizeof (HTTPStreaming),
                0, /* instance size */
                (GInstanceInitFunc) httpstreaming_init, /* instance init */
                NULL /* value table */
        };
        type = g_type_register_static (G_TYPE_OBJECT, "HTTPStreaming", &info, 0);

        return type;
}

static gint send_data (EncoderOutput *encoder_output, RequestData *request_data)
{
        HTTPStreamingPrivateData *priv_data;
        struct iovec iov[3];
        gint ret;

        priv_data = request_data->priv_data;
        iov[0].iov_base = NULL;
        iov[0].iov_len = 0;
        iov[1].iov_base = NULL;
        iov[1].iov_len = 0;
        iov[2].iov_base = "\r\n";
        iov[2].iov_len = 2;
        if (priv_data->send_count < priv_data->chunk_size_str_len) {
                iov[0].iov_base = priv_data->chunk_size_str + priv_data->send_count;
                iov[0].iov_len = priv_data->chunk_size_str_len - priv_data->send_count;
                iov[1].iov_base = encoder_output->cache_addr + priv_data->send_position;
                iov[1].iov_len = priv_data->chunk_size;

        } else if (priv_data->send_count < (priv_data->chunk_size_str_len + priv_data->chunk_size)) {
                iov[1].iov_base = encoder_output->cache_addr + priv_data->send_position + (priv_data->send_count - priv_data->chunk_size_str_len);
                iov[1].iov_len = priv_data->chunk_size - (priv_data->send_count - priv_data->chunk_size_str_len);

        } else if (priv_data->send_count > (priv_data->chunk_size_str_len + priv_data->chunk_size)) {
                iov[2].iov_base = "\n";
                iov[2].iov_len = 1;
        }
        ret = writev (request_data->sock, iov, 3);
        if (ret == -1) {
                GST_DEBUG ("write error %s sock %d", g_strerror (errno), request_data->sock);
        }

        return ret;
}

/*
 * return -1 means the gop is current output gop.
 */
static gint64 get_current_gop_end (EncoderOutput *encoder_output, HTTPStreamingPrivateData *priv_data)
{
        gint32 current_gop_size, n;
        gint64 current_gop_end_addr;

        /* read gop size. */
        n = encoder_output->cache_size - priv_data->rap_addr;
        if (n >= 12) {
                memcpy (&current_gop_size, encoder_output->cache_addr + priv_data->rap_addr + 8, 4);

        } else if (n > 8) {
                memcpy (&current_gop_size, encoder_output->cache_addr + priv_data->rap_addr + 8, n - 8);
                memcpy (&current_gop_size + n - 8, encoder_output->cache_addr, 12 - n);

        } else {
                memcpy (&current_gop_size, encoder_output->cache_addr + 8 - n, 4);
        }

        if (current_gop_size == 0) {
                /* current output gop. */
                return -1;
        }
        current_gop_end_addr = priv_data->rap_addr + current_gop_size + 12;
        if (current_gop_end_addr > encoder_output->cache_size) {
                current_gop_end_addr -= encoder_output->cache_size;
        }

        return current_gop_end_addr;
}

static GstClockTime send_chunk (EncoderOutput *encoder_output, RequestData *request_data)
{
        HTTPStreamingPrivateData *priv_data;
        gint64 current_gop_end_addr, tail_addr;
        gint32 ret;

        priv_data = request_data->priv_data;

        if (sem_wait (encoder_output->semaphore) == -1) {
                GST_WARNING ("send_chunk sem_wait failure: %s", g_strerror (errno));
                /* sem_wait failure, wait a while. */
                return 100 * GST_MSECOND + g_random_int_range (1, 1000000);
        }
        tail_addr = *(encoder_output->tail_addr);
        current_gop_end_addr = get_current_gop_end (encoder_output, priv_data);

        if (priv_data->send_count == priv_data->chunk_size + priv_data->chunk_size_str_len + 2) {
                /* completly send a chunk, prepare next. */
                priv_data->send_position += priv_data->send_count - priv_data->chunk_size_str_len - 2;
                if (priv_data->send_position == encoder_output->cache_size) {
                        priv_data->send_position = 0;
                }
                g_free (priv_data->chunk_size_str);
                priv_data->chunk_size_str_len = 0;
                priv_data->chunk_size = 0;
        }

        if (priv_data->send_position == current_gop_end_addr) {
                /* next gop. */
                priv_data->rap_addr = current_gop_end_addr;
                if (priv_data->send_position + 12 < encoder_output->cache_size) {
                        priv_data->send_position += 12;

                } else {
                        priv_data->send_position = priv_data->send_position + 12 - encoder_output->cache_size;
                }
                current_gop_end_addr = get_current_gop_end (encoder_output, priv_data);
        }
        sem_post (encoder_output->semaphore);

        if (priv_data->chunk_size == 0) {
                if (current_gop_end_addr == -1) {
                        /* current output gop. */
                        if ((tail_addr - priv_data->send_position) > 16384) {
                                priv_data->chunk_size = 16384;

                        } else if (tail_addr > priv_data->send_position) {
                                /* send to tail. */
                                priv_data->chunk_size = tail_addr - priv_data->send_position;

                        } else if (tail_addr == priv_data->send_position) {
                                /* no data available, wait a while. */
                                return 100 * GST_MSECOND + g_random_int_range (1, 1000000);

                        } else if ((encoder_output->cache_size - priv_data->send_position) > 16384) {
                                priv_data->chunk_size = 16384;

                        } else {
                                priv_data->chunk_size = encoder_output->cache_size - priv_data->send_position;
                        }

                } else {
                        /* completely output gop. */
                        if ((current_gop_end_addr - priv_data->send_position) > 16384) {
                                priv_data->chunk_size = 16384;

                        } else if (current_gop_end_addr > priv_data->send_position) {
                                /* send to gop end. */
                                priv_data->chunk_size = current_gop_end_addr - priv_data->send_position;

                        } else if (current_gop_end_addr == priv_data->send_position) {
                                /* no data available, wait a while. */
                                return 100 * GST_MSECOND + g_random_int_range (1, 1000000); //FIXME FIXME

                        } else {
                                /* send to cache end. */
                                priv_data->chunk_size = encoder_output->cache_size - priv_data->send_position;
                        }
                }
                priv_data->chunk_size_str = g_strdup_printf ("%x\r\n", priv_data->chunk_size);
                priv_data->chunk_size_str_len = strlen (priv_data->chunk_size_str);
                priv_data->send_count = 0;
        }

        /* send data. */
        ret = send_data (encoder_output, request_data);
        if (ret == -1) {
                return GST_CLOCK_TIME_NONE;

        } else {
                priv_data->send_count += ret;
                request_data->bytes_send += ret;
        }
        if (priv_data->send_count == priv_data->chunk_size + priv_data->chunk_size_str_len + 2) {
                /* send complete, wait 10 ms. */
                return 10 * GST_MSECOND + g_random_int_range (1, 1000000);

        } else {
                /* not send complete, blocking, wait 200 ms. */
                return 200 * GST_MSECOND + g_random_int_range (1, 1000000);
        }
}

static gsize get_mpeg2ts_segment (RequestData *request_data, EncoderOutput *encoder_output, gchar **buf)
{
        GstClockTime timestamp;
        gint number;
        guint64 year, month, mday, hour, sequence, duration, rap_addr;
        GstClockTime us; /* microseconds */
        struct tm tm;
        gchar date[20], *header, *path, *file;
        gsize buf_size;
        GError *err = NULL;

        number = sscanf (request_data->uri, "/%*[^/]/encoder/%*[^/]/%04lu%02lu%02lu%02lu/%lu_%lu_%lu.ts$",
                         &year, &month, &mday, &hour, &us, &sequence, &duration);
        if (number != 7) {
                GST_WARNING ("uri not found: %s", request_data->uri);
                *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (*buf);
                return buf_size;
        }
        sprintf (date, "%04lu-%02lu-%02lu %02lu:00:00", year, month, mday, hour);
        memset (&tm, 0, sizeof (struct tm));
        strptime (date, "%Y-%m-%d %H:%M:%S", &tm);
        tm.tm_isdst = daylight;
        timestamp = mktime (&tm) * 1000000 + us;

        /* read from memory */
        if (sem_wait (encoder_output->semaphore) == -1) {
                GST_WARNING ("get_mpeg2ts_segment sem_wait failure: %s", g_strerror (errno));
                *buf = g_strdup_printf (http_500, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (*buf);
                return buf_size;
        }
        /* seek gop */
        rap_addr = encoder_output_gop_seek (encoder_output, timestamp);
        if (rap_addr != G_MAXUINT64) {
                /* segment found, send it */
                gsize gop_size;

                gop_size = encoder_output_gop_size (encoder_output, rap_addr);
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "video/mpeg", gop_size, CACHE_60s, "");
                *buf = g_malloc (strlen (header) + gop_size);
                memcpy (*buf, header, strlen(header));
                if (rap_addr + gop_size + 12 < encoder_output->cache_size) {
                        memcpy (*buf + strlen (header), encoder_output->cache_addr + rap_addr + 12, gop_size);

                } else {
                        gint n;

                        n = encoder_output->cache_size - rap_addr - 12;
                        memcpy (*buf + strlen (header), encoder_output->cache_addr + rap_addr + 12, n);
                        memcpy (*buf + strlen (header) + n, encoder_output->cache_addr, gop_size - n);
                }
                buf_size = strlen (header) + gop_size;
                g_free (header);

        } else {
                buf_size = 0;
        }
        sem_post (encoder_output->semaphore);

        /* buf_size == 0? segment not found in memory, read frome dvr directory */
        if (buf_size == 0) {
                path = g_strdup_printf ("%s/%04lu%02lu%02lu%02lu/%010lu_%lu_%lu.ts",
                                         encoder_output->record_path,
                                         year, month, mday, hour, us, sequence, duration);
                if (!g_file_get_contents (path, &file, &buf_size, &err)) {
                        g_error_free (err);
                        GST_WARNING ("segment not found: %s", request_data->uri);
                        *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                        buf_size = strlen (*buf);

                } else {
                        header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "video/mpeg", buf_size, CACHE_60s, "");
                        *buf = g_malloc (buf_size + strlen (header));
                        memcpy (*buf, header, strlen (header));
                        memcpy (*buf + strlen (header), file, buf_size);
                        g_free (header);
                        g_free (file);
                        buf_size += strlen (header);
                }
                g_free (path);
        }

        return buf_size;
}

static gboolean is_http_progress_play_url (RequestData *request_data)
{
        GRegex *regex = NULL;
        GMatchInfo *match_info = NULL;
        gchar *e;
        gint index;

        if (request_data->parameters[0] != '\0') {
                GST_WARNING ("parameters is needless : %s?%s", request_data->uri, request_data->parameters);
                return FALSE;
        }

        index = -1;
        regex = g_regex_new ("^/.*/encoder/(?<encoder>[0-9]+)$", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, request_data->uri, 0, &match_info);
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
                GST_DEBUG ("not http progress play uri: %s", request_data->uri);
                return FALSE;
        }

        return TRUE;
}

static void http_progress_play_priv_data_init (HTTPStreaming *httpstreaming, RequestData *request_data, HTTPStreamingPrivateData *priv_data)
{
        priv_data->job = gstreamill_get_job (httpstreaming->gstreamill, request_data->uri);
        priv_data->livejob_age = priv_data->job->age;
        priv_data->chunk_size = 0;
        priv_data->send_count = 2;
        priv_data->chunk_size_str = g_strdup ("");
        priv_data->chunk_size_str_len = 0;
        request_data->priv_data = priv_data;
        request_data->bytes_send = 0;
}

static gchar * request_crossdomain (RequestData *request_data)
{
        gchar *buf;
        gchar *crossdomain = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<cross-domain-policy>\n"
                             "    <allow-access-from domain=\"*\"/>\n"
                             "</cross-domain-policy>\n";

        if (g_str_has_suffix (request_data->uri, "crossdomain.xml")) {
                buf = g_strdup_printf (http_200,
                               PACKAGE_NAME,
                               PACKAGE_VERSION,
                               "text/xml",
                               strlen (crossdomain),
                               NO_CACHE,
                               crossdomain);
        } else {
                buf = NULL;
        }

        return buf;
}

static gchar * request_master_m3u8_playlist (HTTPStreaming *httpstreaming, RequestData *request_data)
{
        gchar *master_m3u8_playlist, *buf, *replace;
        GRegex *regex;

        /* master m3u8 request? */
        buf = gstreamill_get_master_m3u8playlist (httpstreaming->gstreamill, request_data->uri);
        if (buf == NULL) {
                return buf;
        }
        regex = g_regex_new ("(<%parameters%>)", 0, 0, NULL);
        if (g_strcmp0 (request_data->parameters, "") == 0) {
                master_m3u8_playlist = g_regex_replace (regex, buf, -1, 0, "", 0, NULL);

        } else {
                replace = g_strdup_printf ("?%s", request_data->parameters);
                master_m3u8_playlist = g_regex_replace (regex, buf, -1, 0, replace, 0, NULL);
                g_free (replace);
        }
        g_free (buf);
        g_regex_unref (regex);
        if (master_m3u8_playlist != NULL) {
                buf = g_strdup_printf (http_200,
                               PACKAGE_NAME,
                               PACKAGE_VERSION,
                               "application/vnd.apple.mpegurl",
                               strlen (master_m3u8_playlist),
                               CACHE_60s,
                               master_m3u8_playlist);
                g_free (master_m3u8_playlist);

        } else {
                buf = NULL;
        }

        return buf;
}

static guint64 get_gint64_parameter (gchar *parameters, gchar *parameter)
{
        gchar **pp1, **pp2, *format;
        gint64 value;

        pp1 = pp2 = g_strsplit (parameters, "&", 0);
        while (*pp1 != NULL) {
                if (g_str_has_prefix (*pp1, parameter)) {
                        format = g_strdup_printf ("%s=%%ld", parameter);
                        if (sscanf (*pp1, format, &value) == 1) {
                                break;
                        }
                        g_free (format);
                }
                pp1++;
        }
        if (*pp1 == NULL) {
                value = 0;
        }
        g_strfreev (pp2);

        return value;
}

static gchar * get_m3u8playlist (RequestData *request_data, EncoderOutput *encoder_output)
{
        gchar *m3u8playlist = NULL;

        /* time shift */
        if (g_strrstr (request_data->parameters, "offset") != NULL) {
                gint64 offset;

                offset = get_gint64_parameter (request_data->parameters, "offset");
                m3u8playlist = m3u8playlist_timeshift_get_playlist (encoder_output->record_path, offset);

        /* callback */
        } else if (g_strrstr (request_data->parameters, "start") && 
                   g_strrstr (request_data->parameters, "end")) {
                m3u8playlist = m3u8playlist_callback_get_playlist (encoder_output->record_path, request_data->parameters);

        /* live */
        } else if (encoder_output->m3u8_playlist != NULL) {
                m3u8playlist = m3u8playlist_live_get_playlist (encoder_output->m3u8_playlist);
        }

        return m3u8playlist;
}

static GstClockTime http_request_process (HTTPStreaming *httpstreaming, RequestData *request_data)
{
        EncoderOutput *encoder_output;
        GstClock *system_clock = httpstreaming->httpserver->system_clock;
        HTTPStreamingPrivateData *priv_data;
        gchar *buf = NULL;
        gsize buf_size;
        gint ret;
        gboolean is_http_progress_play_request = FALSE;

        encoder_output = gstreamill_get_encoder_output (httpstreaming->gstreamill, request_data->uri);
        if (encoder_output == NULL) {
                buf = request_crossdomain (request_data);
                /* not crossdomain request if buf == NULL */
                if ((buf == NULL) && g_str_has_suffix (request_data->uri, "playlist.m3u8")) {
                        buf = request_master_m3u8_playlist (httpstreaming, request_data);
                }
                /* not master m3u8 playlist request if buf == NULL */
                if (buf == NULL) {
                        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                }
                buf_size = strlen (buf);

        } else if (!is_encoder_output_ready (encoder_output)) {
                /* not ready */
                GST_DEBUG ("%s not ready.", request_data->uri);
                buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (buf);

        } else if (g_str_has_suffix (request_data->uri, ".ts")) {
                /* get mpeg2 transport stream segment */
                buf_size = get_mpeg2ts_segment (request_data, encoder_output, &buf);

        } else if (g_str_has_suffix (request_data->uri, "playlist.m3u8")) {
                /* get m3u8 playlist */
                gchar *m3u8playlist;

                m3u8playlist = get_m3u8playlist (request_data, encoder_output);
                if (m3u8playlist == NULL) {
                        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);

                } else {
                        buf = g_strdup_printf (http_200,
                                       PACKAGE_NAME,
                                       PACKAGE_VERSION,
                                       "application/vnd.apple.mpegurl",
                                       strlen (m3u8playlist),
                                       CACHE_60s,
                                       m3u8playlist);
                        g_free (m3u8playlist);
                }
                buf_size = strlen (buf);

        } else if (is_http_progress_play_url (request_data)) {
                /* http progressive streaming request */
                GST_INFO ("Play %s.", request_data->uri);
                buf = g_strdup_printf (http_chunked, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (buf);
                is_http_progress_play_request = TRUE;

        } else {
                buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (buf);
        }

        /* write out buf */
        ret = write (request_data->sock, buf, buf_size);
        if (((ret > 0) && (ret != buf_size)) || ((ret == -1) && (errno == EAGAIN))) {
                /* send not completed or socket block, resend late */
                priv_data = (HTTPStreamingPrivateData *)g_malloc (sizeof (HTTPStreamingPrivateData));
                priv_data->buf = buf;
                priv_data->buf_size = buf_size;
                priv_data->job = NULL;
                priv_data->send_position = ret > 0? ret : 0;
                priv_data->encoder_output = encoder_output;
                request_data->priv_data = priv_data;
                if (is_http_progress_play_request) {
                        http_progress_play_priv_data_init (httpstreaming, request_data, priv_data);
                        priv_data->rap_addr = *(encoder_output->last_rap_addr);
                }
                return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;

        } else if (ret == -1) {
                GST_ERROR ("Write sock error: %s", g_strerror (errno));
        }

        /* send complete or socket error */
        g_free (buf);
        if ((is_http_progress_play_request) && (ret == buf_size)) {
                /* http progress play request and send complete */
                priv_data = (HTTPStreamingPrivateData *)g_malloc (sizeof (HTTPStreamingPrivateData));
                http_progress_play_priv_data_init (httpstreaming, request_data, priv_data);
                priv_data->encoder_output = encoder_output;
                priv_data->rap_addr = *(encoder_output->last_rap_addr);
                priv_data->send_position = *(encoder_output->last_rap_addr) + 12;
                priv_data->buf = NULL;
                request_data->priv_data = priv_data;
                return gst_clock_get_time (system_clock);
        }
        if (encoder_output != NULL) {
                gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
        }
        return 0;
}

static GstClockTime http_continue_process (HTTPStreaming *httpstreaming, RequestData *request_data)
{
        HTTPStreamingPrivateData *priv_data;
        EncoderOutput *encoder_output;
        GstClock *system_clock = httpstreaming->httpserver->system_clock;
        gint ret;

        priv_data = request_data->priv_data;
        encoder_output = priv_data->encoder_output;
        if (priv_data->buf != NULL) {
                ret = write (request_data->sock,
                             priv_data->buf + priv_data->send_position,
                             priv_data->buf_size - priv_data->send_position);
                /* send complete or send error */
                if ((ret + priv_data->send_position == priv_data->buf_size) ||
                    ((ret == -1) && (errno != EAGAIN))) {
                        if ((ret == -1) && (errno != EAGAIN)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (priv_data->buf);
                        priv_data->buf = NULL;
                        /* progressive play? continue */
                        if (is_http_progress_play_url (request_data)) {
                                priv_data->send_position = *(encoder_output->last_rap_addr) + 12;
                                priv_data->buf = NULL;
                                return gst_clock_get_time (system_clock);
                        }
                        if (encoder_output != NULL) {
                                gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        }
                        if (priv_data->job != NULL) {
                                g_object_unref (priv_data->job);
                        }
                        g_free (priv_data);
                        request_data->priv_data = NULL;
                        return 0;

                } else if ((ret > 0) || ((ret == -1) && (errno == EAGAIN))) {
                        /* send not completed or socket block, resend late */
                        priv_data->send_position += ret > 0? ret : 0;
                        return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;
                }
        }
        if ((priv_data->livejob_age != priv_data->job->age) ||
            (*(priv_data->job->output->state) != JOB_STATE_PLAYING)) {
                if (priv_data->encoder_output != NULL) {
                        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                }
                if (priv_data->job != NULL) {
                        g_object_unref (priv_data->job);
                }
                g_free (request_data->priv_data);
                request_data->priv_data = NULL;
                return 0;
        }
        if (priv_data->send_position == *(encoder_output->tail_addr)) {
                /* no more stream, wait 10ms */
                GST_DEBUG ("current:%lu == tail:%lu", priv_data->send_position, *(encoder_output->tail_addr));
                return gst_clock_get_time (system_clock) + 500 * GST_MSECOND + g_random_int_range (1, 1000000);
        }
        return send_chunk (encoder_output, request_data) + gst_clock_get_time (system_clock);
}

static GstClockTime httpstreaming_dispatcher (gpointer data, gpointer user_data)
{
        RequestData *request_data = data;
        HTTPStreaming *httpstreaming = (HTTPStreaming *)user_data;
        HTTPStreamingPrivateData *priv_data;

        switch (request_data->status) {
        case HTTP_REQUEST:
                GST_INFO ("new request arrived, socket is %d, uri is %s", request_data->sock, request_data->uri);
                return http_request_process (httpstreaming, request_data);

        case HTTP_CONTINUE:
                return http_continue_process (httpstreaming, request_data);

        case HTTP_FINISH:
                if (request_data->priv_data != NULL) {
                        priv_data = request_data->priv_data;
                        if (priv_data->encoder_output != NULL) {
                                gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        }
                        if (priv_data->job != NULL) {
                                g_object_unref (priv_data->job);
                        }
                        if (priv_data->buf != NULL) {
                                g_free (priv_data->buf);
                        }
                        g_free (request_data->priv_data);
                        request_data->priv_data = NULL;
                }
                return 0;

        default:
                GST_ERROR ("Unknown status %d", request_data->status);
                return 0;
        }
}

gint httpstreaming_start (HTTPStreaming *httpstreaming, gint maxthreads)
{
        gchar node[128], service[32];

        /* get streaming listen port */
        if (sscanf (httpstreaming->address, "%[^:]:%s", node, service) == EOF) {
                GST_WARNING ("http streaming address error: %s", httpstreaming->address);
                return 1;
        }

        /* start http streaming */
        httpstreaming->httpserver = httpserver_new ("maxthreads", maxthreads, "node", node, "service", service, NULL);
        if (httpserver_start (httpstreaming->httpserver, httpstreaming_dispatcher, httpstreaming) != 0) {
                GST_ERROR ("Start streaming httpserver error!");
                return 1;
        }
        
        return 0;
}

