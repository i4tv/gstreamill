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
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
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
 
static gchar * add_header_footer (gchar *middle)
{
        gchar *buf, *path, *top, *bottom;
        GError *err = NULL;

        path = g_strdup_printf ("%s/gstreamill/admin/header.html", DATADIR);
        if (!g_file_get_contents (path, &top, NULL, &err)) {
                g_free (path);
                GST_ERROR ("read %s failure: %s", path, err->message);
                g_error_free (err);
                return g_strdup ("Internal error, file header.html not found.");
        }
        g_free (path);
        path = g_strdup_printf ("%s/gstreamill/admin/footer.html", DATADIR);
        if (!g_file_get_contents (path, &bottom, NULL, &err)) {
                GST_ERROR ("read %s failure: %s", path, err->message);
                g_error_free (err);
                g_free (path);
                return g_strdup ("Internal error, file footer.html not found.");
        }
        g_free (path);
        buf = g_strdup_printf ("%s%s%s", top, middle, bottom);
        g_free (top);
        g_free (bottom);

        return buf;
}

static gboolean generate_configurable_para (JSON_Object *obj, gchar *name, gchar **result)
{
        gchar *template, *p1, *p2, *p3, *configurable_para;
        gdouble multibitrate;
        gint i;
        GError *err = NULL;

        /* source */
        p1 = (gchar *)json_object_get_string (obj, "source");
        template = g_strdup_printf ("/%s/gstreamill/admin/jobtemplates/%s.conf", DATADIR, p1);
        if (!g_file_get_contents (template, &p1, NULL, &err)) {
                GST_ERROR ("no template %s found: %s", template, err->message);
                g_error_free (err);
                g_free (template);
                *result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"no %s template found\"\n}", template);
                return FALSE;
        }
        configurable_para = p1;
        g_free (template);

        /* multibitrate and udp */
        multibitrate = json_object_get_number (obj, "multibitrate");
        template = g_strdup_printf ("%s/gstreamill/admin/jobtemplates/encoder.conf", DATADIR);
        if (!g_file_get_contents (template, &p1, NULL, &err)) {
                GST_ERROR ("no template %s found: %s", template, err->message);
                g_error_free (err);
                g_free (template);
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"no encoder template found\"\n}");
                return FALSE;
        }
        g_free (template);
        p2 = (gchar *)json_object_get_string (obj, "udp");
        for (i = 0; i < multibitrate; i++) {
                p3 = configurable_para;
                if (g_strcmp0 (p2, "yes") == 0) {
                        configurable_para = g_strdup_printf ("%s%s", p3, p1);
                        configurable_para[strlen (configurable_para) - 1] = ',';
                        g_free (p3);
                        p3 = configurable_para;
                        configurable_para = g_strdup_printf ("%s\n            \"udpstreaming\" : \"127.0.0.1:22345\"\n        },\n", p3);
        
                } else {
                        configurable_para = g_strdup_printf ("%s%s        },\n", p3, p1);
                }
                g_free (p3);
        }
        configurable_para[strlen (configurable_para) - 2] = '\0';
        g_free (p1);

        /* m3u8 */
        p2 = (gchar *)json_object_get_string (obj, "m3u8");
        if (g_strcmp0 (p2, "yes") == 0) {
                template = g_strdup_printf ("%s/gstreamill/admin/jobtemplates/m3u8.conf", DATADIR);
                if (!g_file_get_contents (template, &p1, NULL, &err)) {
                        GST_ERROR ("no template %s found: %s", template, err->message);
                        g_error_free (err);
                        g_free (template);
                        g_free (configurable_para);
                        *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"no suited template found\"\n}");
                        return FALSE;
                }
                p3 = configurable_para;
                configurable_para = g_strdup_printf ("%s\n    ],\n%s", p3, p1);
                g_free (p1);
                g_free (p3);

        } else {
                p3 = configurable_para;
                configurable_para = g_strdup_printf ("%s\n    ]\n}", p3);
                g_free (p3);
        }

        /* save new created live job */
        p1 = g_strdup_printf (configurable_para, name);
        g_free (configurable_para);
        configurable_para = p1;
        p1 = g_strdup_printf ("/etc/gstreamill.d/conf/%s.conf", name);
        if (!g_file_set_contents (p1, configurable_para, strlen (configurable_para), &err)) {
                GST_ERROR ("save job conf %s failure: %s", p1, err->message);
                g_error_free (err);
                g_free (p1);
                g_free (configurable_para);
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"save job conf failure\"\n}");
                return FALSE;
        }
        g_free (p1);
        g_free (configurable_para);

        *result = NULL;
        return TRUE;
}

static gboolean generate_job (JSON_Object *obj, gchar *name, gchar **result)
{
        gchar *template, *p1, *p2, *p3, *job_desc;
        gdouble multibitrate;
        gint i;
        GError *err = NULL;

        /* source */
        p1 = (gchar *)json_object_get_string (obj, "source");
        if (p1 == NULL) {
                GST_ERROR ("invalid new job without source");
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without source\"\n}");
                return FALSE;
        }
        template = g_strdup_printf ("%s/gstreamill/admin/jobtemplates/%s", DATADIR, p1);
        if (!g_file_get_contents (template, &p1, NULL, &err)) {
                GST_ERROR ("no template %s found: %s", template, err->message);
                g_free (template);
                g_error_free (err);
                *result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"no %s template found\"\n}", template);
                return FALSE;
        }
        job_desc = p1;
        g_free (template);

        /* multibitrate and udp */
        multibitrate = json_object_get_number (obj, "multibitrate");
        if (multibitrate == 0) {
                GST_ERROR ("invalid new job without multibitrate");
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without multibitrate\"\n}");
                return FALSE;
        }
        template = g_strdup_printf ("%s/gstreamill/admin/jobtemplates/encoder", DATADIR);
        if (!g_file_get_contents (template, &p1, NULL, &err)) {
                GST_ERROR ("no template %s found: %s", template, err->message);
                g_free (template);
                g_error_free (err);
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"no encoder template found\"\n}");
                return FALSE;
        }
        g_free (template);
        p2 = (gchar *)json_object_get_string (obj, "udp");
        if (p2 == NULL) {
                GST_ERROR ("invalid new job without udp");
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without udp\"\n}");
                return FALSE;
        }
        for (i = 0; i < multibitrate; i++) {
                p3 = job_desc;
                if (g_strcmp0 (p2, "yes") == 0) {
                        job_desc = g_strdup_printf ("%s%s", p3, p1);
                        job_desc[strlen (job_desc) - 1] = ',';
                        g_free (p3);
                        p3 = job_desc;
                        job_desc = g_strdup_printf ("%s\n            \"udpstreaming\" : \"127.0.0.1:22345\"\n        },\n", p3);
        
                } else {
                        job_desc = g_strdup_printf ("%s%s        },\n", p3, p1);
                }
                g_free (p3);
        }
        job_desc[strlen (job_desc) - 2] = '\0';
        g_free (p1);

        /* m3u8 */
        p2 = (gchar *)json_object_get_string (obj, "m3u8");
        if (p2 == NULL) {
                GST_ERROR ("invalid new job without m3u8");
                g_free (template);
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without m3u8\"\n}");
                return FALSE;
        }
        if (g_strcmp0 (p2, "yes") == 0) {
                template = g_strdup_printf ("%s/gstreamill/admin/jobtemplates/m3u8", DATADIR);
                if (!g_file_get_contents (template, &p1, NULL, &err)) {
                        GST_ERROR ("no template %s found: %s", template, err->message);
                        g_free (template);
                        g_error_free (err);
                        g_free (job_desc);
                        *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"no suited template found\"\n}");
                        return FALSE;
                }
                p3 = job_desc;
                job_desc = g_strdup_printf ("%s\n    ],\n%s", p3, p1);
                g_free (p1);
                g_free (p3);

        } else {
                p3 = job_desc;
                job_desc = g_strdup_printf ("%s\n    ]\n}", p3);
                g_free (p3);
        }

        /* save new created live job */
        p1 = g_strdup_printf (job_desc, name);
        g_free (job_desc);
        job_desc = p1;
        p1 = g_strdup_printf ("/etc/gstreamill.d/%s.job", name);
        if (!g_file_set_contents (p1, job_desc, strlen(job_desc), &err)) {
                GST_ERROR ("save job %s failure: %s", p1, err->message);
                g_error_free (err);
                g_free (p1);
                g_free (job_desc);
                *result = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"save job failure\"\n}");
                return FALSE;
        }
        g_free (p1);
        g_free (job_desc);

        *result = NULL;
        return TRUE;
}

static gchar * new_live_job (gchar *newjob)
{
        JSON_Value *val;
        JSON_Object *obj;
        gchar *name, *result, *p1;//, *p2, *p3, *job_desc;

        val = json_parse_string_with_comments(newjob);
        if (val == NULL) {
                GST_ERROR ("invalid job description");
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid job description\"\n}\n");
        }
        obj = json_value_get_object (val);
        if (!g_file_test ("/etc/gstreamill.d", G_FILE_TEST_EXISTS & G_FILE_TEST_IS_DIR)) {
                g_mkdir ("/etc/gstreamill.d", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        }
        if (!g_file_test ("/etc/gstreamill.d/conf", G_FILE_TEST_EXISTS & G_FILE_TEST_IS_DIR)) {
                g_mkdir ("/etc/gstreamill.d/conf", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        }

        /* name */
        name = (gchar *)json_object_get_string (obj, "name");
        if (name == NULL) {
                GST_ERROR ("invalid new job without name");
                json_value_free (val);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid new job without name\"\n}\n");
        }
        p1 = g_strdup_printf ("/etc/gstreamill.d/%s.job", name);
        if (g_file_test (p1, G_FILE_TEST_EXISTS)) {
                GST_ERROR ("job %s, already exist", name);
                json_value_free (val);
                g_free (p1);
                return  g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"already exist\"\n}\n");
        }
        g_free (p1);

        /* generate job and configurable para success? */
        if (generate_job (obj, name, &result) && generate_configurable_para (obj, name, &result)) {
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }
        json_value_free (val);

        return result;
}

static gchar * capture_devices (gchar *pattern)
{
        glob_t pglob;
        gint i;
        gchar *devices, *p;

        p = g_strdup ("[");
        if (glob (pattern, 0, NULL, &pglob) == 0) {
                for (i = 0; i < pglob.gl_pathc; i++) {
                        devices = g_strdup_printf ("%s\"%s\",", p, pglob.gl_pathv[i]);
                        g_free (p);
                        p = devices;
                }
                globfree (&pglob);
                devices[strlen (devices) - 1] = ']';

        } else {
                devices = g_strdup_printf ("%s]", p);
                g_free (p);
        }

        return devices;
}

static gchar * get_job (gchar *uri)
{
        gchar *job_path, *job;
        GError *err = NULL;

        job_path = g_strdup_printf ("/etc/gstreamill.d%s.job", &uri[13]);
        if (!g_file_get_contents (job_path, &job, NULL, &err)) {
                GST_ERROR ("read job %s failure: %s", job_path, err->message);
                job = NULL;
                g_error_free (err);
        }
        g_free (job_path);

        return job;
}

static gchar * put_job (RequestData *request_data)
{
        gchar *job_path, *job, *result;
        GError *err = NULL;

        job_path = g_strdup_printf ("/etc/gstreamill.d%s.job", &(request_data->uri[13]));
        job = request_data->raw_request + request_data->header_size;
        if (!g_file_set_contents (job_path, job, strlen (job), &err)) {
                GST_ERROR ("write job %s failure: %s", job_path, err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);

        } else {
                GST_INFO ("write job %s success", job_path);
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }
        g_free (job_path);

        return result;
}

static gsize request_gstreamer_admin (HTTPMgmt *httpmgmt, RequestData *request_data, gchar **buf)
{
        gchar *path, *http_header, *p;
        gsize buf_size;
        GError *err = NULL;

        path = NULL;
        if (g_strcmp0 (request_data->uri, "/admin/") == 0) {
                path = g_strdup_printf ("%s/gstreamill/admin/index.html", DATADIR);

        } else if ((request_data->method == HTTP_POST) && (g_strcmp0 (request_data->uri, "/admin/newlivejob") == 0)) {
                p = request_data->raw_request + request_data->header_size;
                *buf = new_live_job (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/audiodevices") == 0) {
                p = capture_devices ("/dev/snd/pcmC*c");
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/videodevices") == 0) {
                p = capture_devices ("/dev/video*");
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/admin/getjob/")) {
                p = get_job (request_data->uri);
                if (p != NULL) {
                        *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                        g_free (p);

                } else {
                        *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                }

        } else if ((request_data->method == HTTP_POST) && (g_str_has_prefix (request_data->uri, "/admin/putjob/"))) {
                p = put_job (request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/admin/setjob.html")) {
                if (g_str_has_prefix (request_data->parameters, "name=")) {
                        path = g_strdup_printf ("%s/gstreamill/admin/setjob.html", DATADIR);

                } else {
                        *buf = g_strdup_printf (http_400, PACKAGE_NAME, PACKAGE_VERSION);
                }

        } else {
                /* static content, prepare file path */
                path = g_strdup_printf ("%s/gstreamill%s", DATADIR, request_data->uri);
        }

        if (path == NULL) {
                /* not static content */
                buf_size = strlen (*buf);

        } else if (!g_file_get_contents (path, buf, &buf_size, &err)) {
                GST_ERROR ("read file %s failure: %s", p, err->message);
                *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                buf_size = strlen (*buf);
                g_error_free (err);

        } else {
                /* setjob.html? process name parameter */
                if (g_str_has_suffix (path, "setjob.html")) {
                        gchar name[32], *temp_buf;

                        sscanf (request_data->parameters, "name=%s", name);
                        temp_buf = g_strdup_printf (*buf, name);
                        g_free (*buf);
                        *buf = temp_buf;
                }
                /* html file? add top and bottom */
                if (g_str_has_suffix (path, ".html")) {
                        gchar *middle;

                        middle = *buf;
                        *buf = add_header_footer (middle);
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

