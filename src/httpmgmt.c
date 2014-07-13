/*
 * managment api over http
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <unistd.h>
#include <glob.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>

#include "parson.h"
#include "httpmgmt.h"
#include "gstreamill.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        HTTPMGMT_PROP_0,
        HTTPMGMT_PROP_ADDRESS,
        HTTPMGMT_PROP_GSTREAMILL,
};

static void httpmgmt_class_init (HTTPMgmtClass *httpmgmtclass);
static void httpmgmt_init (HTTPMgmt *httpmgmt);
static GObject *httpmgmt_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties);
static void httpmgmt_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void httpmgmt_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);

static void httpmgmt_class_init (HTTPMgmtClass *httpmgmtclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (httpmgmtclass);
        GParamSpec *param;

        g_object_class->constructor = httpmgmt_constructor;
        g_object_class->set_property = httpmgmt_set_property;
        g_object_class->get_property = httpmgmt_get_property;

        param = g_param_spec_string (
                "address",
                "address",
                "address of httpmgmt",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPMGMT_PROP_ADDRESS, param);

        param = g_param_spec_pointer (
                "gstreamill",
                "gstreamill",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPMGMT_PROP_GSTREAMILL, param);
}

static void httpmgmt_init (HTTPMgmt *httpmgmt)
{
        httpmgmt->system_clock = gst_system_clock_obtain ();
        g_object_set (httpmgmt->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
}

static GObject * httpmgmt_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
        GObject *obj;
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        obj = parent_class->constructor (type, n_construct_properties, construct_properties);

        return obj;
}

static void httpmgmt_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_HTTPMGMT (obj));

        switch(prop_id) {
        case HTTPMGMT_PROP_ADDRESS:
                HTTPMGMT (obj)->address = (gchar *)g_value_dup_string (value);
                break;

        case HTTPMGMT_PROP_GSTREAMILL:
                HTTPMGMT (obj)->gstreamill = (Gstreamill *)g_value_get_pointer (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void httpmgmt_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        HTTPMgmt  *httpmgmt = HTTPMGMT (obj);

        switch (prop_id) {
        case HTTPMGMT_PROP_ADDRESS:
                g_value_set_string (value, httpmgmt->address);
                break;

        case HTTPMGMT_PROP_GSTREAMILL:
                g_value_set_pointer (value, httpmgmt->gstreamill);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

GType httpmgmt_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (HTTPMgmtClass), /* class size */
                NULL, /* base initializer */
                NULL, /* base finalizer */
                (GClassInitFunc) httpmgmt_class_init, /* class init */
                NULL, /* class finalize */
                NULL, /* class data */
                sizeof (HTTPMgmt),
                0, /* instance size */
                (GInstanceInitFunc) httpmgmt_init, /* instance init */
                NULL /* value table */
        };
        type = g_type_register_static (G_TYPE_OBJECT, "HTTPMgmt", &info, 0);

        return type;
}

static gchar * start_job (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *p, *var;

        if (request_data->method == HTTP_POST) {
                /* start a job. */
                var = request_data->raw_request + request_data->header_size;
                p = gstreamill_job_start (httpmgmt->gstreamill, var);
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else {
                buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
        }

        return buf;
}

static gchar * stop_job (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *p;
        GRegex *regex;
        GMatchInfo *match_info;

        if (request_data->method == HTTP_GET) {
                regex = g_regex_new ("/stop/(?<name>.*)", G_REGEX_OPTIMIZE, 0, NULL);
                g_regex_match (regex, request_data->uri, 0, &match_info);
                g_regex_unref (regex);
                if (g_match_info_matches (match_info)) {
                        buf = g_match_info_fetch_named (match_info, "name");
                        g_match_info_free (match_info);
                        p = gstreamill_job_stop (httpmgmt->gstreamill, buf);
                        g_free (buf);
                        buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                        g_free (p);
                        return buf;
                }
        }
        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);

        return buf;
}

static gchar * request_gstreamill_stat (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *p;

        if (g_strcmp0 (request_data->uri, "/stat/gstreamill") == 0) {
                p = gstreamill_stat (httpmgmt->gstreamill);
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill/job/number")) {
                p = g_strdup_printf ("%d", gstreamill_job_number (httpmgmt->gstreamill));
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill/job/")) {
                p = gstreamill_job_stat (httpmgmt->gstreamill, request_data->uri);
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill/starttime")) {
                p = g_strdup_printf ("%s", gstreamill_get_start_time (httpmgmt->gstreamill));
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill/version")) {
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (VERSION), NO_CACHE, VERSION);

        } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill/builddate")) {
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (__DATE__), NO_CACHE, __DATE__);

        } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill/buildtime")) {
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (__TIME__), NO_CACHE, __TIME__);

        } else {
                buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
        }

        return buf;
}

static gchar * request_gstreamer_stat (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *p;

        p = gstreamill_gstreamer_stat (httpmgmt->gstreamill, request_data->uri);
        buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (p), NO_CACHE, p);
        g_free (p);

        return buf;
}

static gchar * gen_http_header (gchar *path, gsize body_size)
{
        gchar *header;

        if (g_str_has_suffix (path, ".html")) {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/html", body_size, NO_CACHE, "");

        } else if (g_str_has_suffix (path, ".css")) {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/css", body_size, NO_CACHE, "");

        } else if (g_str_has_suffix (path, ".js")) {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/x-javascript", body_size, NO_CACHE, "");

        } else if (g_str_has_suffix (path, ".ttf")) {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/x-font-ttf", body_size, NO_CACHE, "");

        } else if (g_str_has_suffix (path, ".svg")) {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "image/svg+xml", body_size, NO_CACHE, "");

        } else if (g_str_has_suffix (path, ".ico")) {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "image/x-icon", body_size, NO_CACHE, "");

        } else {
                header = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/octet-stream", body_size, NO_CACHE, "");
        }

        return header;
}
 
static gchar * add_top_bottom (gchar *middle)
{
        gchar *buf, *path, *top, *bottom;

        path = g_strdup_printf ("%s/gstreamill/admin/header.html", DATADIR);
        if (!g_file_get_contents (path, &top, NULL, NULL)) {
                g_free (path);
                return g_strdup ("Internal error, file header.html not found.");
        }
        g_free (path);
        path = g_strdup_printf ("%s/gstreamill/admin/footer.html", DATADIR);
        if (!g_file_get_contents (path, &bottom, NULL, NULL)) {
                g_free (path);
                return g_strdup ("Internal error, file footer.html not found.");
        }
        g_free (path);
        buf = g_strdup_printf ("%s%s%s", top, middle, bottom);
        g_free (top);
        g_free (bottom);

        return buf;
}

static gchar * new_live_job (gchar *newjob)
{
        JSON_Value *val;
        JSON_Object *obj;
        gchar *name, *template, *p1, *p2;

        val = json_parse_string_with_comments(newjob);
        if (val == NULL) {
                GST_ERROR ("invalid json type of new job description");
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid json type of new job description\"\n}\n");
        }

        obj = json_value_get_object (val);
        name = (gchar *)json_object_get_string (obj, "name");
        if (name == NULL) {
                GST_ERROR ("invalid new job without name");
                json_value_free (val);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without name\"\n}\n");
        }
        p1 = g_strdup_printf ("/etc/gstreamill.d/%s", name);
        if (g_file_test (p1, G_FILE_TEST_EXISTS)) {
                GST_ERROR ("new job %s, already exist", name);
                json_value_free (val);
                g_free (p1);
                return  g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"already exist\"\n}\n");
        }
        g_free (p1);
        if (!g_file_test ("/etc/gstreamill.d", G_FILE_TEST_EXISTS & G_FILE_TEST_IS_DIR)) {
                g_mkdir ("/etc/gstreamill.d", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        }
        p1 = (gchar *)json_object_get_string (obj, "source");
        if (p1 == NULL) {
                GST_ERROR ("invalid new job without source");
                json_value_free (val);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without source\"\n}");
        }
        template = g_strdup_printf ("source_%s", p1);
        p1 = (gchar *)json_object_get_string (obj, "multibitrate");
        if (p1 == NULL) {
                GST_ERROR ("invalid new job without multibitrate");
                json_value_free (val);
                g_free (template);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without multirate\"\n}");
        }
        p2 = template;
        template = g_strdup_printf ("%s.multibitrate_%s", p2, p1);
        g_free (p2);
        p1 = (gchar *)json_object_get_string (obj, "udp");
        if (p1 == NULL) {
                GST_ERROR ("invalid new job without udp");
                json_value_free (val);
                g_free (template);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without udp\"\n}");
        }
        p2 = template;
        template = g_strdup_printf ("%s.udp_%s", p2, p1);
        g_free (p2);
        p1 = (gchar *)json_object_get_string (obj, "m3u8");
        if (p1 == NULL) {
                GST_ERROR ("invalid new job without m3u8");
                json_value_free (val);
                g_free (template);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without m3u8\"\n}");
        }
        p2 = template;
        template = g_strdup_printf ("%s/gstreamill/admin/jobtemplates/%s.m3u8_%s", DATADIR, p2, p1);
        g_free (p2);
        if (!g_file_get_contents (template, &p1, NULL, NULL)) {
                GST_ERROR ("no template %s found", template);
                g_free (template);
                json_value_free (val);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"no suited template found\"\n}");
        }
        p2 = g_strdup_printf (p1, name);
        g_free (p1);
        p1 = g_strdup_printf ("/etc/gstreamill.d/%s.job", name);

        g_file_set_contents (p1, p2, strlen(p2), NULL);
        g_free (template);
        g_free (p1);
        g_free (p2);
        json_value_free (val);

        return g_strdup ("{\n    \"result\": \"success\"\n}");
}

static gsize request_gstreamer_admin (HTTPMgmt *httpmgmt, RequestData *request_data, gchar **buf)
{
        gchar *path, *http_header, *p;
        gsize buf_size;

        /* prepare file path */
        if (g_strcmp0 (request_data->uri, "/admin/") == 0) {
                path = g_strdup_printf ("%s/gstreamill/admin/index.html", DATADIR);

        } else if ((request_data->method == HTTP_POST) && (g_strcmp0 (request_data->uri, "/admin/newlivejob") == 0)) {
                p = request_data->raw_request + request_data->header_size;
                *buf = new_live_job (p);
                path = NULL;

        } else if (g_strcmp0 (request_data->uri, "/admin/capturedevices") == 0) {
                glob_t pglob;
                gint i;

                p = g_strdup_printf ("{\n    \"audio\": [");
                /* alsa audio cature devices */
                if (glob ("/dev/snd/pcmC*c", 0, NULL, &pglob) == 0) {
                        for (i = 0; i < pglob.gl_pathc; i++) {
                                *buf = g_strdup_printf ("%s\"%s\",", p, pglob.gl_pathv[i]);
                                g_free (p);
                                p = *buf;
                        }
                        globfree (&pglob);
                }
                if (p[strlen (p) - 1] == ',') {
                        p[strlen (p) - 1] = ']';
                        *buf = g_strdup_printf ("%s,\n    \"video\": [", p);

                } else {
                        *buf = g_strdup_printf ("%s,\n    \"video\": [", p);
                }
                g_free (p);
                p = *buf;
                /* v4l2 video capture devices */
                if (glob ("/dev/video*", 0, NULL, &pglob) == 0) {
                        for (i = 0; i < pglob.gl_pathc; i++) {
                                *buf = g_strdup_printf ("%s\"%s\",", p, pglob.gl_pathv[i]);
                                g_free (p);
                                p = *buf;
                        }
                        globfree (&pglob);
                }
                if (p[strlen (p) - 1] == ',') {
                        p[strlen (p) - 1] = ']';
                        *buf = g_strdup_printf ("%s\n}", p);

                } else {
                        *buf = g_strdup_printf ("%s]\n}\n", p);
                }
                g_free (p);
                p = *buf;
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);
                path = NULL;

        } else {
                path = g_strdup_printf ("%s/gstreamill%s", DATADIR, request_data->uri);
        }

        if (path == NULL) {
                /* not static content */
                buf_size = strlen (*buf);

        } else if (!g_file_get_contents (path, buf, &buf_size, NULL)) {
                GST_ERROR ("read file %s failure", p);
                *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (*buf);

        } else {
                /* read file success, html file? add top and bottom */
                if (g_str_has_suffix (path, ".html")) {
                        gchar *middle;

                        middle = *buf;
                        *buf = add_top_bottom (middle);
                        g_free (middle);
                        buf_size = strlen (*buf);
                }
                http_header = gen_http_header (path, buf_size);
                p = g_malloc (buf_size + strlen (http_header));
                memcpy (p, http_header, strlen (http_header));
                memcpy (p + strlen (http_header), *buf, buf_size);
                g_free (*buf);
                *buf = p;
                buf_size += strlen (http_header);
                g_free (http_header);
        }

        if (path != NULL) {
                g_free (path);
        }

        return buf_size;
}

static GstClockTime httpmgmt_dispatcher (gpointer data, gpointer user_data)
{
        RequestData *request_data = data;
        HTTPMgmt *httpmgmt = user_data;
        HTTPMgmtPrivateData *priv_data;
        gchar *buf;
        gsize buf_size;
        gint ret;

        switch (request_data->status) {
        case HTTP_REQUEST:
                GST_INFO ("new request arrived, socket is %d, uri is %s", request_data->sock, request_data->uri);
                if (g_str_has_prefix (request_data->uri, "/start")) {
                        buf = start_job (httpmgmt, request_data);
                        buf_size = strlen (buf);

                } else if (g_str_has_prefix (request_data->uri, "/stop")) {
                        buf = stop_job (httpmgmt, request_data);
                        buf_size = strlen (buf);

                } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamill")) {
                        buf = request_gstreamill_stat (httpmgmt, request_data);
                        buf_size = strlen (buf);

                } else if (g_str_has_prefix (request_data->uri, "/stat/gstreamer")) {
                        buf = request_gstreamer_stat (httpmgmt, request_data);
                        buf_size = strlen (buf);

                } else if (g_str_has_prefix (request_data->uri, "/admin")) {
                        buf_size = request_gstreamer_admin (httpmgmt, request_data, &buf);

                } else {
                        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                        buf_size = strlen (buf);
                }

                ret = write (request_data->sock, buf, buf_size);
                if (((ret > 0) && (ret != buf_size)) || ((ret == -1) && (errno == EAGAIN))) {
                        /* send not completed or socket block, resend late */
                        priv_data = (HTTPMgmtPrivateData *)g_malloc (sizeof (HTTPMgmtPrivateData));
                        priv_data->buf = buf;
                        priv_data->buf_size = buf_size;
                        priv_data->send_position = ret > 0? ret : 0;
                        request_data->priv_data = priv_data;
                        return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;

                } else if (ret == -1) {
                        GST_ERROR ("Write sock error: %s", g_strerror (errno));
                }
                /* send complete or socket error */
                g_free (buf);
                return 0;

        case HTTP_CONTINUE:
                priv_data = request_data->priv_data;
                ret = write (request_data->sock, priv_data->buf + priv_data->send_position, priv_data->buf_size - priv_data->send_position);
                if ((ret + priv_data->send_position == priv_data->buf_size) || ((ret == -1) && (errno != EAGAIN))) {
                        /* send complete or send error, finish the request */
                        if ((ret == -1) && (errno != EAGAIN)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        g_free (priv_data->buf);
                        g_free (priv_data);
                        request_data->priv_data = NULL;
                        return 0;

                } else if ((ret > 0) || ((ret == -1) && (errno == EAGAIN))) {
                        /* send not completed or socket block, resend late */
                        priv_data->send_position += ret > 0? ret : 0;
                        return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;
                }

        case HTTP_FINISH:
                g_free (request_data->priv_data);
                request_data->priv_data = NULL;
                return 0;

        default:
                GST_ERROR ("Unknown status %d", request_data->status);
        }

        return 0;
}

gint httpmgmt_start (HTTPMgmt *httpmgmt)
{
        gchar node[128], service[32];

        /* httpmgmt port */
        if (sscanf (httpmgmt->address, "%[^:]:%s", node, service) == EOF) {
                GST_ERROR ("http managment address error: %s", httpmgmt->address);
                return 1;
        }

        /* start httpmgmt */
        httpmgmt->httpserver = httpserver_new ("maxthreads", 1, "node", node, "service", service, NULL);
        if (httpserver_start (httpmgmt->httpserver, httpmgmt_dispatcher, httpmgmt) != 0) {
                GST_ERROR ("Start mgmt httpserver error!");
                return 1;
        }

        return 0;
}

