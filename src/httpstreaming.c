/*
 * streaming over http.
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#define _XOPEN_SOURCE 600
#include <unistd.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <glob.h>

#include "httpstreaming.h"
#include "utils.h"

GST_DEBUG_CATEGORY_EXTERN (ACCESS);

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

static gsize get_mpeg2ts_segment (RequestData *request_data, EncoderOutput *encoder_output, gchar **buf)
{
    GstClockTime timestamp;
    gint number;
    guint64 year, month, mday, hour, sequence, duration, rap_addr, max_age;
    GstClockTime us; /* microseconds */
    struct tm tm;
    gchar date[20], *header, *path, *file, *cache_control;
    gsize buf_size;
    GError *err = NULL;
    struct timespec ts;

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
    if (clock_gettime (CLOCK_REALTIME, &ts) == -1) {
        GST_ERROR ("get_mpeg2ts_segment clock_gettime error: %s", g_strerror (errno));
        *buf = g_strdup_printf (http_500, PACKAGE_NAME, PACKAGE_VERSION);
        request_data->response_status = 500;
        request_data->response_body_size = http_500_body_size;
        buf_size = strlen (*buf);
        return buf_size;
    }
    ts.tv_sec += 2;
    while (sem_timedwait (encoder_output->semaphore, &ts) == -1) {
        if (errno == EINTR) {
            continue;
        }
        GST_ERROR ("get_mpeg2ts_segment sem_timedwait failure: %s", g_strerror (errno));
        *buf = g_strdup_printf (http_500, PACKAGE_NAME, PACKAGE_VERSION);
        request_data->response_status = 500;
        request_data->response_body_size = http_500_body_size;
        buf_size = strlen (*buf);
        return buf_size;
    }
    /* seek gop */
    rap_addr = encoder_output_gop_seek (encoder_output, timestamp);
    if (rap_addr != G_MAXUINT64) {
        /* segment found, send it */
        gsize gop_size;

        gop_size = encoder_output_gop_size (encoder_output, rap_addr);
        cache_control = g_strdup_printf ("max-age=%lu", encoder_output->dvr_duration);
        header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "video/mpeg", gop_size, cache_control, "");
        g_free (cache_control);
        *buf = g_malloc (strlen (header) + gop_size);
        memcpy (*buf, header, strlen(header));
        if (rap_addr + gop_size + 12 < encoder_output->cache_size) {
            memcpy (*buf + strlen (header), encoder_output->cache_addr + rap_addr + 12, gop_size);

        } else {
            gint n;

            n = encoder_output->cache_size - rap_addr - 12;
            if (n > 0) {
                memcpy (*buf + strlen (header), encoder_output->cache_addr + rap_addr + 12, n);
                memcpy (*buf + strlen (header) + n, encoder_output->cache_addr, gop_size - n);

            } else {
                GST_WARNING ("nnnnn: n < 0 %d", n);
                memcpy (*buf + strlen (header), encoder_output->cache_addr - n, gop_size);
            }
        }
        buf_size = strlen (header) + gop_size;
        request_data->response_status = 200;
        request_data->response_body_size = gop_size;
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
            GST_WARNING ("read segment error: %s", err->message);
            g_error_free (err);
            *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
            request_data->response_body_size = http_404_body_size;
            request_data->response_status = 404;
            buf_size = strlen (*buf);

        } else {
            request_data->response_status = 200;
            request_data->response_body_size = buf_size;
            max_age = encoder_output->dvr_duration - (g_get_real_time () - timestamp) / 1000000;
            cache_control = g_strdup_printf ("max-age=%lu", max_age);
            header = g_strdup_printf (http_200,
                    PACKAGE_NAME,
                    PACKAGE_VERSION,
                    "video/mpeg",
                    buf_size,
                    cache_control,
                    "");
            g_free (cache_control);
            *buf = g_malloc (buf_size + strlen (header));
            memcpy (*buf, header, strlen (header));
            memcpy (*buf + strlen (header), file, buf_size);
            buf_size += strlen (header);
            g_free (header);
            g_free (file);
        }
        g_free (path);
    }

    return buf_size;
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
                g_free (format);
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

static gchar * get_str_parameter (gchar *parameters, gchar *parameter)
{
    gchar **pp1, **pp2, *format, value[256];

    pp1 = pp2 = g_strsplit (parameters, "&", 0);
    while (*pp1 != NULL) {
        if (g_str_has_prefix (*pp1, parameter)) {
            format = g_strdup_printf ("%s=%%s", parameter);
            if (sscanf (*pp1, format, value) == 1) {
                g_free (format);
                break;
            }
            g_free (format);
        }
        pp1++;
    }
    if (*pp1 == NULL) {
        value[0] = '\0';
    }
    g_strfreev (pp2);

    return g_strdup_printf ("%s", value);
}

static gboolean is_encoder_channel_url (RequestData *request_data)
{
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    gchar *e;
    gint index;

    index = -1;
    regex = g_regex_new ("^/[^/]*/encoder/(?<encoder>[0-9]+)$", G_REGEX_OPTIMIZE, 0, NULL);
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

static gboolean is_http_progress_play_request (RequestData *request_data)
{

    if (request_data->parameters[0] != '\0') {
        return FALSE;
    }

    return is_encoder_channel_url (request_data);
}

static void http_progress_play_priv_data_init (HTTPStreaming *httpstreaming,
                                               RequestData *request_data,
                                               HTTPStreamingPrivateData *priv_data)
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

static gboolean is_dvr_download_request (RequestData *request_data)
{
    gchar start_dir[11], end_dir[11], *start, *end, *segments_dir, *pattern, *format, *path;
    gint number, i;
    time_t start_time, end_time, time;
    guint64 start_min, start_sec, end_min, end_sec, start_us, end_us, us;
    glob_t pglob;
    HTTPStreamingPrivateData *priv_data;
    GStatBuf stat;

    if (!is_encoder_channel_url (request_data)) {
        return FALSE;
    }

    /* path is uri with 'encoder' removed */
    path = g_strdup (request_data->uri);
    path[strlen (path) - 9] = path[strlen (path) - 1];
    path[strlen (path) - 8] = '\0';

    /* retrieve segments */
    if (g_strrstr (request_data->parameters, "start") && g_strrstr (request_data->parameters, "end")) {
        start = get_str_parameter (request_data->parameters, "start");
        end = get_str_parameter (request_data->parameters, "end");
        if ((start != NULL) && (end != NULL)) {
            number = sscanf (start, "%10s%02lu%02lu", start_dir, &start_min, &start_sec);
            if (number != 3) {
                GST_ERROR ("bad dvr download parameters: start=%s", start);
                goto bad_request;
            }
            if (segment_dir_to_timestamp (start_dir, &start_time) != 0) {
                GST_ERROR ("segment_dir_to_timestamp error, start_dir: %s", start_dir);
                goto bad_request;
            }
            if ((start_min > 59) || (start_sec > 59)) {
                GST_ERROR ("Error start_min: %ld or start_sec: %ld", start_min, start_sec);
                goto bad_request;
            }
            number = sscanf (end, "%10s%02lu%02lu", end_dir, &end_min, &end_sec);
            if (number != 3) {
                GST_ERROR ("bad callback parameters: end=%s", end);
                goto bad_request;
            }
            if (segment_dir_to_timestamp (end_dir, &end_time) != 0) {
                GST_ERROR ("segment_dir_to_timestamp error, end_dir: %s", end_dir);
                goto bad_request;
            }
            if ((end_min > 59) || (end_sec > 59)) {
                GST_ERROR ("Error end_min: %ld or end_sec: %ld", end_min, end_sec);
                goto bad_request;
            }

            priv_data = (HTTPStreamingPrivateData *)g_malloc (sizeof (HTTPStreamingPrivateData));
            priv_data->buf = NULL;
            priv_data->job = NULL;
            priv_data->segment_list = NULL;
            priv_data->dvr_download_size = 0;
            priv_data->list_index = 0;
            priv_data->segment_position = 0;
            priv_data->segment_size = 0;
            for (time = start_time; time <= end_time; time += 3600) {
                if (time == start_time) {
                    start_us = start_min * 60000000 + start_sec * 1000000;

                } else {
                    start_us = 0;
                }

                if (time == end_time) {
                    end_us = end_min * 60000000 + end_sec * 1000000;

                } else {
                    end_us = 3600000000;
                }

                segments_dir = timestamp_to_segment_dir (time); 
                pattern = g_strdup_printf ("%s/dvr%s/%s/*_*_*.ts", MEDIA_LOCATION, path, segments_dir);
                if (glob (pattern, 0, NULL, &pglob) == GLOB_NOMATCH) {
                    g_free (segments_dir);
                    continue;

                } else {
                    g_free (pattern);
                    format = g_strdup_printf ("%s/dvr%s/%s/%%lu_", MEDIA_LOCATION, path, segments_dir);
                    g_free (segments_dir);
                    for (i = 0; i < pglob.gl_pathc; i++) {
                        if (1 != sscanf (pglob.gl_pathv[i], format, &us)) {
                            continue;
                        }
                        if ((us >= start_us) && (us <= end_us)) {
                            g_lstat (pglob.gl_pathv[i], &stat);
                            priv_data->dvr_download_size += stat.st_size;
                            priv_data->segment_list = g_slist_append (priv_data->segment_list, g_strdup (pglob.gl_pathv[i]));
                        }
                    }
                    g_free (format);
                }
                globfree (&pglob);
            }

            g_free (path);
            g_free (start);
            g_free (end);
            if (g_slist_length (priv_data->segment_list) == 0) {
                g_free (priv_data);
                return FALSE;
            }
            request_data->priv_data = priv_data;
            return TRUE;
        }

bad_request:
        if (start != NULL) {
            g_free (start);
        }
        if (end != NULL) {
            g_free (end);
        }
    }

    return FALSE;
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

    /* channel request uri should inherit parameters of master request uri */
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
        request_data->response_body_size = strlen (master_m3u8_playlist);
        buf = g_strdup_printf (http_200,
                PACKAGE_NAME,
                PACKAGE_VERSION,
                "application/vnd.apple.mpegurl",
                strlen (master_m3u8_playlist),
                CACHE_3600s,
                master_m3u8_playlist);
        g_free (master_m3u8_playlist);

    } else {
        buf = NULL;
    }

    return buf;
}

static gboolean is_channel_playlist_url_valid (RequestData *request_data)
{
    GRegex *regex;
    gboolean result;

    regex = g_regex_new ("^/[^/]*/encoder/[0-9]/playlist.m3u8$", G_REGEX_OPTIMIZE, 0, NULL);
    if (g_regex_match (regex, request_data->uri, 0, NULL)) {
        result = TRUE;

    } else {
        result = FALSE;
    }
    g_regex_unref (regex);

    return result;
}

static gchar * get_m3u8playlist (RequestData *request_data, EncoderOutput *encoder_output)
{
    gchar *m3u8playlist = NULL;
    gchar *start, *end;
    gint64 now;

    if (!is_channel_playlist_url_valid (request_data)) {
        GST_WARNING ("bad request url: %s", request_data->uri);
        return NULL;
    }

    now = g_get_real_time () / 1000000;
    /* time shift? */
    if ((g_strrstr (request_data->parameters, "timeshift") != NULL) ||
        (g_strrstr (request_data->parameters, "position") != NULL)){
        gint64 offset;
        time_t shift_position;

        if (g_strrstr (request_data->parameters, "timeshift") != NULL) {
            offset = get_gint64_parameter (request_data->parameters, "timeshift") +
                 encoder_output->playlist_window_size * encoder_output->segment_duration / GST_SECOND;
            shift_position = now - offset;

        } else {
            shift_position = get_gint64_parameter (request_data->parameters, "position") +
                 encoder_output->playlist_window_size * encoder_output->segment_duration / GST_SECOND;
        }
        if ((shift_position < now) &&
            (shift_position > now - encoder_output->dvr_duration)) {
            m3u8playlist = m3u8playlist_timeshift_get_playlist (encoder_output->record_path,
                                                            encoder_output->version,
                                                            encoder_output->playlist_window_size,
                                                            shift_position);
        }

    /* callback? */
    } else if (g_strrstr (request_data->parameters, "start") && g_strrstr (request_data->parameters, "end")) {
        start = get_str_parameter (request_data->parameters, "start");
        end = get_str_parameter (request_data->parameters, "end");
        if ((start != NULL) && (end != NULL)) {
            m3u8playlist = m3u8playlist_callback_get_playlist (encoder_output->record_path,
                                                            encoder_output->dvr_duration,
                                                            start,
                                                            end);
        }
        if (start != NULL) {
            g_free (start);
        }
        if (end != NULL) {
            g_free (end);
        }

        /* live */
    } else if (encoder_output->m3u8_playlist != NULL) {
        m3u8playlist = m3u8playlist_live_get_playlist (encoder_output->m3u8_playlist);
    }

    return m3u8playlist;
}

static const gchar *http_method_str[] = {
    "GET",
    "POST"
};

static const gchar *http_version_str[] = {
    "1.0",
    "1.1"
};

static void access_log (RequestData *request_data)
{
    gint i;
    gchar *user_agent, parameters[1024];

    user_agent = "-";
    for (i = 0; i < request_data->num_headers; i++) {
        if (g_strcmp0 (request_data->headers[i].name, "User-Agent") == 0) {
            user_agent = request_data->headers[i].value;
            break;
        }
    }

    if (g_strcmp0 (request_data->parameters, "") == 0) {
        g_sprintf (parameters, "%s", "");

    } else {
        g_sprintf (parameters, "?%s", request_data->parameters);
    }

    GST_CAT_WARNING (ACCESS, "%s - - [%%s] \"%s %s%s HTTP/%s\" %u %lu \"-\" \"%s\"\n",
            get_address (request_data->client_addr),
            http_method_str[request_data->method],
            request_data->uri,
            parameters,
            http_version_str[request_data->version],
            request_data->response_status,
            request_data->response_body_size,
            user_agent);
}

static GstClockTime http_request_process (HTTPStreaming *httpstreaming, RequestData *request_data)
{
    EncoderOutput *encoder_output;
    GstClock *system_clock = httpstreaming->httpserver->system_clock;
    HTTPStreamingPrivateData *priv_data;
    gchar *buf = NULL;
    gsize buf_size;
    gint ret;
    gboolean http_progress_play_request = FALSE, dvr_download_request = FALSE;

    encoder_output = gstreamill_get_encoder_output (httpstreaming->gstreamill, request_data->uri);
    if (encoder_output == NULL) {
        /* crossdomain request? */
        buf = request_crossdomain (request_data);

        /* master m3u8 playlist request? */
        if ((buf == NULL) && g_str_has_suffix (request_data->uri, "playlist.m3u8")) {
            buf = request_master_m3u8_playlist (httpstreaming, request_data);
            request_data->response_status = 200;
        }

        /* is dvr download request? */
        if ((buf == NULL) && is_dvr_download_request (request_data)) {
            priv_data = request_data->priv_data;
            buf = g_strdup_printf (http_200,
                    PACKAGE_NAME,
                    PACKAGE_VERSION,
                    "video/mpeg",
                    priv_data->dvr_download_size,
                    "private",
                    "");
            dvr_download_request = TRUE;
        }

        /* 404 not found */
        if (buf == NULL) {
            buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
            request_data->response_status = 404;
            request_data->response_body_size = http_404_body_size;
        }
        buf_size = strlen (buf);

    } else if (!is_encoder_output_ready (encoder_output)) {
        /* not ready */
        GST_WARNING ("%s not ready.", request_data->uri);
        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
        request_data->response_status = 404;
        request_data->response_body_size = http_404_body_size;
        buf_size = strlen (buf);

    } else if (g_str_has_suffix (request_data->uri, ".ts")) {
        /* get mpeg2 transport stream segment */
        buf_size = get_mpeg2ts_segment (request_data, encoder_output, &buf);

    } else if (g_str_has_suffix (request_data->uri, "playlist.m3u8")) {
        /* get m3u8 playlist */
        gchar *m3u8playlist, *cache_control;

        m3u8playlist = get_m3u8playlist (request_data, encoder_output);
        if (m3u8playlist == NULL) {
            buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
            request_data->response_status = 404;
            request_data->response_body_size = http_404_body_size;

        } else {
            if ((g_strrstr (request_data->parameters, "position") != NULL) &&
                (g_strrstr (request_data->parameters, "timeshift") == NULL)) {
                guint64 age;

                age = get_gint64_parameter (request_data->parameters, "position") +
                    encoder_output->dvr_duration +
                    3600 - /* remove an hour record */
                    g_get_real_time () / 1000000;
                cache_control = g_strdup_printf ("max-age=%lu", age);

            } else {
                cache_control = g_strdup_printf ("max-age=%lu", encoder_output->segment_duration / GST_SECOND);
            }
            buf = g_strdup_printf (http_200,
                    PACKAGE_NAME,
                    PACKAGE_VERSION,
                    "application/vnd.apple.mpegurl",
                    strlen (m3u8playlist),
                    cache_control,
                    m3u8playlist);
            request_data->response_status = 200;
            request_data->response_body_size = strlen (m3u8playlist);
            g_free (cache_control);
            g_free (m3u8playlist);
        }
        buf_size = strlen (buf);

    /* http progressive streaming request? */
    } else if (is_http_progress_play_request (request_data)) {
        buf = g_strdup_printf (http_chunked, PACKAGE_NAME, PACKAGE_VERSION);
        buf_size = strlen (buf);
        http_progress_play_request = TRUE;
        request_data->response_status = 200;
        request_data->response_body_size = 0;

    } else {
        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
        request_data->response_status = 404;
        request_data->response_body_size = http_404_body_size;
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
        priv_data->segment_list = NULL;
        request_data->priv_data = priv_data;
        if (http_progress_play_request) {
            http_progress_play_priv_data_init (httpstreaming, request_data, priv_data);
            priv_data->rap_addr = *(encoder_output->last_rap_addr);
        }
        return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;

    } else if (ret == -1) {
        GST_ERROR ("Write sock error: %s", g_strerror (errno));
    }

    access_log (request_data);

    /* send complete or socket error */
    g_free (buf);

    /* http progress play request and send complete? */
    if ((http_progress_play_request) && (ret == buf_size)) {
        priv_data = (HTTPStreamingPrivateData *)g_malloc (sizeof (HTTPStreamingPrivateData));
        http_progress_play_priv_data_init (httpstreaming, request_data, priv_data);
        priv_data->encoder_output = encoder_output;
        priv_data->rap_addr = *(encoder_output->last_rap_addr);
        priv_data->send_position = *(encoder_output->last_rap_addr) + 12;
        priv_data->buf = NULL;
        priv_data->segment_list = NULL;
        request_data->priv_data = priv_data;
        return gst_clock_get_time (system_clock);
    }

    /* dvr download request? */
    if ((dvr_download_request) && (ret == buf_size)) {
        return gst_clock_get_time (system_clock);
    }

    if (encoder_output != NULL) {
        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
    }

    return 0;
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

static GstClockTime send_chunk (EncoderOutput *encoder_output, RequestData *request_data)
{
    HTTPStreamingPrivateData *priv_data;
    gint64 current_gop_end_addr, tail_addr;
    gint32 ret;
    struct timespec ts;

    priv_data = request_data->priv_data;

    if (clock_gettime (CLOCK_REALTIME, &ts) == -1) {
        GST_ERROR ("send_chunk clock_gettime error: %s", g_strerror (errno));
        return 100 * GST_MSECOND + g_random_int_range (1, 1000000);
    }
    ts.tv_sec += 2;
    while (sem_timedwait (encoder_output->semaphore, &ts) == -1) {
        if (errno == EINTR) {
            continue;
        }
        GST_ERROR ("send_chunk sem_timedwait failure: %s", g_strerror (errno));
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
        request_data->response_status = 200;
        request_data->response_body_size = priv_data->chunk_size;
        access_log (request_data);
        return 10 * GST_MSECOND + g_random_int_range (1, 1000000);

    } else {
        /* not send complete, blocking, wait 200 ms. */
        return 200 * GST_MSECOND + g_random_int_range (1, 1000000);
    }
}

static GstClockTime dvr_download (RequestData *request_data, GstClock *system_clock)
{
    HTTPStreamingPrivateData *priv_data;
    gchar *path;
    GError *err = NULL;
    gint ret;

    priv_data = request_data->priv_data;

    /* first segment or current segment send complete? */
    if (priv_data->segment_position == priv_data->segment_size) {
        path = g_slist_nth_data (priv_data->segment_list, priv_data->list_index);
        if (!g_file_get_contents (path, &(priv_data->segment), &(priv_data->segment_size), &err)) {
            GST_ERROR ("read %s failure: %s", path, err->message);
            g_error_free (err);
            return 0;
        }
        priv_data->segment_position = 0;
        priv_data->list_index++;
    }

    ret = write (request_data->sock,
            priv_data->segment + priv_data->segment_position,
            priv_data->segment_size - priv_data->segment_position);
    if (ret >= 0) {
        priv_data->segment_position += ret;
        /* seng segment complete? */
        if (priv_data->segment_position == priv_data->segment_size) {
            g_free (priv_data->segment);
            /* dvr download complete? */
            if (priv_data->list_index == g_slist_length (priv_data->segment_list)) {
                return 0;

            } else {
                return gst_clock_get_time (system_clock);
            }

        } else {
            return GST_CLOCK_TIME_NONE;
        }

    } else if ((ret == -1) && (errno == EAGAIN)) {
        return GST_CLOCK_TIME_NONE;

    } else if ((ret == -1) && (errno != EAGAIN)) {
        GST_ERROR ("Write sock error: %s", g_strerror (errno));
        g_free (priv_data->segment);
        return 0;
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
            if (is_http_progress_play_request (request_data)) {
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
            access_log (request_data);
            return 0;

        } else if ((ret > 0) || ((ret == -1) && (errno == EAGAIN))) {
            /* send not completed or socket block, resend late */
            priv_data->send_position += ret > 0? ret : 0;
            return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;
        }
    }

    if (priv_data->segment_list != NULL) {
        return dvr_download (request_data, system_clock);
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
                if (priv_data->segment_list != NULL) {
                    g_slist_free (priv_data->segment_list);
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

