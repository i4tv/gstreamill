/*
 * streaming over http.
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <unistd.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
        PrivateData *priv_data;
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
static gint64 get_current_gop_end (EncoderOutput *encoder_output, PrivateData *priv_data)
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
        current_gop_end_addr = priv_data->rap_addr + current_gop_size;
        if (current_gop_end_addr > encoder_output->cache_size) {
                current_gop_end_addr -= encoder_output->cache_size;
        }

        return current_gop_end_addr;
}

static GstClockTime send_chunk (EncoderOutput *encoder_output, RequestData *request_data)
{
        PrivateData *priv_data;
        gint64 current_gop_end_addr, tail_addr;
        gint32 ret;

        priv_data = request_data->priv_data;

        sem_wait (encoder_output->semaphore);
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

static void get_mpeg2ts_segment (RequestData *request_data, EncoderOutput *encoder_output)
{
        GstClockTime timestamp;
        gchar *buf;
        guint64 rap_addr;

        sscanf (request_data->uri, "/live/%*[^/]/encoder/%*[^/]/%lu.ts", &timestamp);
        sem_wait (encoder_output->semaphore);

        /* seek gop */
        rap_addr = encoder_output_gop_seek (encoder_output, timestamp);

        /* segment found, send it */
        if (rap_addr != G_MAXUINT64) {
                gsize gop_size;

                gop_size = encoder_output_gop_size (encoder_output, rap_addr);
                sem_post (encoder_output->semaphore);
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "video/mpeg", gop_size, ""); 
                if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                        GST_ERROR ("Write segment http head error: %s", g_strerror (errno));
                }
                g_free (buf);
                if (rap_addr + gop_size + 12 < encoder_output->cache_size) {
                        if (httpserver_write (request_data->sock, encoder_output->cache_addr + rap_addr + 12, gop_size) != gop_size) {
                                GST_ERROR ("Write segment error: %s", g_strerror (errno));
                        }

                } else {
                        gint n;

                        n = encoder_output->cache_size - rap_addr - 12;
                        if (httpserver_write (request_data->sock, encoder_output->cache_addr + rap_addr + 12, n) != n) {
                                GST_ERROR ("Write segment error: %s", g_strerror (errno));
                        }
                        if (httpserver_write (request_data->sock, encoder_output->cache_addr, gop_size - n) != gop_size - n) {
                                GST_ERROR ("Write segment error: %s", g_strerror (errno));
                        }
                }

        } else {
                /* segment not found */
                sem_post (encoder_output->semaphore);
                GST_ERROR ("Segment not found!");
                buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                        GST_ERROR ("Write sock error: %s", g_strerror (errno));
                }
                g_free (buf);
        }
}

static is_http_progress_play_url (RequestData *request_data)
{
        GRegex *regex = NULL;
        GMatchInfo *match_info = NULL;
        gchar *e;
        gint index;

        if (request_data->parameters[0] != '\0') {
                GST_ERROR ("parameters is needless : %s?%s", request_data->uri, request_data->parameters);
                return FALSE;
        }

        index = -1;
        regex = g_regex_new ("^/live/.*/encoder/(?<encoder>[0-9]+)$", G_REGEX_OPTIMIZE, 0, NULL);
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
                GST_ERROR ("invalid uri: %s", request_data->uri);
                return FALSE;
        }

        return TRUE;
}

static GstClockTime httpstreaming_dispatcher (gpointer data, gpointer user_data)
{
        RequestData *request_data = data;
        HTTPStreaming *httpstreaming = (HTTPStreaming *)user_data;
        gchar *buf;
        EncoderOutput *encoder_output;
        PrivateData *priv_data;
        GstClock *system_clock = httpstreaming->httpserver->system_clock;

        switch (request_data->status) {
        case HTTP_REQUEST:
                GST_INFO ("new request arrived, socket is %d, uri is %s", request_data->sock, request_data->uri);
                encoder_output = gstreamill_get_encoder_output (httpstreaming->gstreamill, request_data->uri);
                if (encoder_output == NULL) {
                        /* no such encoder */
                        gchar *master_m3u8_playlist;

                        master_m3u8_playlist = gstreamill_get_master_m3u8playlist (httpstreaming->gstreamill, request_data->uri);
                        if (master_m3u8_playlist != NULL) {
                                buf = g_strdup_printf (http_200,
                                                       PACKAGE_NAME,
                                                       PACKAGE_VERSION,
                                                       "application/vnd.apple.mpegurl",
                                                       strlen (master_m3u8_playlist),
                                                       master_m3u8_playlist);
                                g_free (master_m3u8_playlist);

                        } else {
                                buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                        }
                        if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (buf);
                        return 0;

                } else if (*(encoder_output->head_addr) == *(encoder_output->tail_addr)) {
                        /* not ready */
                        GST_DEBUG ("%s not ready.", request_data->uri);
                        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                        if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (buf);
                        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        return 0;

                } else if (g_str_has_suffix (request_data->uri, ".ts")) {
                        /* get mpeg2 transport stream segment */
                        get_mpeg2ts_segment (request_data, encoder_output);
                        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        return 0;

                } else if (g_str_has_suffix (request_data->uri, "playlist.m3u8")) {
                        /* get m3u8 playlist */
                        gchar *m3u8playlist;

                        m3u8playlist = gstreamill_get_m3u8playlist (httpstreaming->gstreamill, encoder_output);
                        buf = g_strdup_printf (http_200,
                                               PACKAGE_NAME,
                                               PACKAGE_VERSION,
                                               "application/vnd.apple.mpegurl",
                                               strlen (m3u8playlist),
                                               m3u8playlist); 
                        if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (m3u8playlist);
                        g_free (buf);
                        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        return 0;

                } else if (is_http_progress_play_url (request_data)) {
                        /* http progressive streaming request */
                        GST_INFO ("Play %s.", request_data->uri);
                        priv_data = (PrivateData *)g_malloc (sizeof (PrivateData));
                        priv_data->job = gstreamill_get_job (httpstreaming->gstreamill, request_data->uri);
                        priv_data->livejob_age = priv_data->job->age;
                        priv_data->chunk_size = 0;
                        priv_data->send_count = 2;
                        priv_data->chunk_size_str = g_strdup ("");
                        priv_data->chunk_size_str_len = 0;
                        priv_data->encoder_output = encoder_output;
                        priv_data->rap_addr = *(encoder_output->last_rap_addr);
                        priv_data->send_position = *(encoder_output->last_rap_addr) + 12;
                        request_data->priv_data = priv_data;
                        request_data->bytes_send = 0;
                        buf = g_strdup_printf (http_chunked, PACKAGE_NAME, PACKAGE_VERSION);
                        if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (buf);
                        return gst_clock_get_time (system_clock);

                } else {
                        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                        if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (buf);
                        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        return 0;
                }
                break;

        case HTTP_CONTINUE:
                priv_data = request_data->priv_data;
                if ((priv_data->livejob_age != priv_data->job->age) ||
                    (*(priv_data->job->output->state) != GST_STATE_PLAYING)) {
                        g_free (request_data->priv_data);
                        request_data->priv_data = NULL;
                        gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                        return 0;
                }
                encoder_output = priv_data->encoder_output;
                if (priv_data->send_position == *(encoder_output->tail_addr)) {
                        /* no more stream, wait 10ms */
                        GST_DEBUG ("current:%lu == tail:%lu", priv_data->send_position, *(encoder_output->tail_addr));
                        return gst_clock_get_time (system_clock) + 500 * GST_MSECOND + g_random_int_range (1, 1000000);
                }
                return send_chunk (encoder_output, request_data) + gst_clock_get_time (system_clock);

        case HTTP_FINISH:
                g_free (request_data->priv_data);
                request_data->priv_data = NULL;
                gstreamill_unaccess (httpstreaming->gstreamill, request_data->uri);
                return 0;

        default:
                GST_ERROR ("Unknown status %d", request_data->status);
                buf = g_strdup_printf (http_400, PACKAGE_NAME, PACKAGE_VERSION);
                if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                        GST_ERROR ("Write sock error: %s", g_strerror (errno));
                }
                g_free (buf);
                return 0;
        }
}

gint httpstreaming_start (HTTPStreaming *httpstreaming, gint maxthreads)
{
        gchar node[128], service[32];

        /* get streaming listen port */
        if (sscanf (httpstreaming->address, "%[^:]:%s", node, service) == EOF) {
                GST_ERROR ("http streaming address error: %s", httpstreaming->address);
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

