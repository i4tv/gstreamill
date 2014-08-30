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
#include <augeas.h>

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

static gchar * system_stat ()
{
        gchar *std_out, *p, **pp1, **pp2, model_name[128], *result;
        gint count;
        GError *err = NULL;

        /* cpu mode */
        result = g_strdup ("{\n");
        if (!g_file_get_contents ("/proc/cpuinfo", &p, NULL, &err)) {
                GST_ERROR ("read /proc/cpuinfo failure: %s", err->message);
                g_free (result);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);
                return result;
        }

        pp1 = pp2 = g_strsplit (p, "\n", 0);
        g_free (p);
        while (*pp1 != NULL) {
                if (!g_str_has_prefix (*pp1, "model name")) {
                        pp1++;
                        continue;
                }
                sscanf (*pp1, "%*[^:]: %[^\n]$", model_name);
                break;
        }
        g_strfreev (pp2);
        p = result;
        result = g_strdup_printf ("%s    \"CPU_Model\": \"%s\"", p, model_name);
        g_free (p);

        if (!g_spawn_command_line_sync ("lscpu", &std_out, NULL, NULL, &err)) {
                GST_ERROR ("invoke lscpu failure: %s", err->message);
                g_free (result);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);
                return result;
        }
        pp1 = pp2 = g_strsplit (std_out, "\n", 0);
        while (*pp1 != NULL) {
                if (g_str_has_prefix (*pp1, "CPU(s)")) {
                        sscanf (*pp1, "CPU(s):%*[ ]%d", &count);
                        p = result;
                        result = g_strdup_printf ("%s,\n    \"CPU_Count\": %d", p, count);
                        g_free (p);

                } else if (g_str_has_prefix (*pp1, "Thread(s) per core")) {
                        sscanf (*pp1, "Thread(s) per core:%*[ ]%d", &count);
                        p = result;
                        result = g_strdup_printf ("%s,\n    \"Threads_per_Core\": %d", p, count);
                        g_free (p);

                } else if (g_str_has_prefix (*pp1, "Core(s) per socket")) {
                        sscanf (*pp1, "Core(s) per socket:%*[ ]%d", &count);
                        p = result;
                        result = g_strdup_printf ("%s,\n    \"Core_per_Socket\": %d", p, count);
                        g_free (p);

                } else if (g_str_has_prefix (*pp1, "Socket(s)")) {
                        sscanf (*pp1, "Socket(s):%*[ ]%d", &count);
                        p = result;
                        result = g_strdup_printf ("%s,\n    \"Sockets_Count\": %d", p, count);
                        g_free (p);

                } else if (g_str_has_prefix (*pp1, "CPU MHz")){
                        sscanf (*pp1, "CPU MHz:%*[ ]%d", &count);
                        p = result;
                        result = g_strdup_printf ("%s,\n    \"CPU_MHz\": %d", p, count);
                        g_free (p);
                }
                pp1++;
        }
        g_strfreev (pp2);
        g_free (std_out);

        p = result;
        result = g_strdup_printf ("%s\n}", p);
        g_free (p);

        return result;
}

static gchar * request_gstreamill_stat (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *p;

        if (g_strcmp0 (request_data->uri, "/stat/gstreamill") == 0) {
                p = gstreamill_stat (httpmgmt->gstreamill);
                buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/stat/system") == 0) {
                p = system_stat ();
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

static gchar * set_network_interfaces (RequestData *request_data)
{
        gchar *interfaces, *result, *name, *path, *value;
        augeas *aug;
        JSON_Value *val;
        JSON_Array *array;
        JSON_Object *obj;
        gint if_count, i, ret;

        aug = aug_init (NULL, NULL, AUG_NONE | AUG_NO_ERR_CLOSE | AUG_NO_MODL_AUTOLOAD);
        aug_set (aug, "/augeas/load/Interfaces/lens", "Interfaces.lns");
        aug_set (aug, "/augeas/load/Interfaces/incl", "/etc/network/interfaces");
        aug_load (aug);
        interfaces = request_data->raw_request + request_data->header_size;
        val = json_parse_string (interfaces);
        array = json_value_get_array (val);
        if_count = json_array_get_count (array);
        for (i = 0; i < if_count; i++) {
                obj = json_array_get_object (array, i);
                name = (gchar *)json_object_get_string (obj, "name");
                value = (gchar *)json_object_get_string (obj, "method");
                if (value != NULL) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/method", name);
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "address");
                if (value != NULL) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/address", name);
                        ret = aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "netmask");
                if (value != NULL) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/netmask", name);
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "network");
                if (value != NULL) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/network", name);
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "broadcast");
                if (value != NULL) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/broadcast", name);
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "gateway");
                if (value != NULL) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/gateway", name);
                        aug_set (aug, path, value);
                        g_free (path);
                }
        }
        if (aug_save (aug) == -1) {
                aug_get (aug, "/augeas//error", (const gchar **)&value);
                GST_ERROR ("set /etc/network/interface failure: %s", value);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", value);

        } else {
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }
        aug_close (aug);
        json_value_free (val);

        return result;
}

static gchar * network_interfaces ()
{
        augeas *aug;
        gchar *value = NULL, **if_match, **option_match, *path, *result, *p, option[128];
        gint if_number, option_number, i, j;

        aug = aug_init (NULL, NULL, AUG_NONE | AUG_NO_ERR_CLOSE | AUG_NO_MODL_AUTOLOAD);
        aug_set (aug, "/augeas/load/Interfaces/lens", "Interfaces.lns");
        aug_set (aug, "/augeas/load/Interfaces/incl", "/etc/network/interfaces");
        aug_load (aug);
        aug_get (aug, "//files/etc/network/interfaces", (const gchar **)&value);
        if_number = aug_match (aug, "//files/etc/network/interfaces/iface[.!='lo']", &if_match);
        result = g_strdup ("[");
        for (i = 0; i < if_number; i++) {
                aug_get (aug, if_match[i], (const gchar **)&value);
                p = result;
                result = g_strdup_printf ("%s{\"name\": \"%s\",", p, value);
                g_free (p);
                p = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/*", value);
                option_number = aug_match (aug, p, &option_match);
                g_free (p);
                for (j = 0; j < option_number; j++) {
                        sscanf (option_match[j], "%*[^]]]/%[^/]", option);
                        if (g_str_has_prefix (option, "#comment")) {
                                continue;
                        }
                        aug_get (aug, option_match[j], (const gchar **)&value);
                        p = result;
                        result = g_strdup_printf ("%s\n\"%s\": \"%s\",", p, option, value);
                        g_free (p);
                        g_free (option_match[j]);
                }
                g_free (option_match);
                g_free (if_match[i]);
                result[strlen (result) - 1] = '}';
                p = result;
                result = g_strdup_printf ("%s,", p);
                g_free (p);
        }
        g_free (if_match);
        aug_close (aug);
        result[strlen (result) - 1] = ']';

        return result;
}

static gchar * network_devices ()
{
        gchar *p1, p2[128], *result, **devices, **device;
        GError *err = NULL;

        if (!g_file_get_contents ("/proc/net/dev", &p1, NULL, &err)) {
                GST_ERROR ("read /proc/net/dev failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);
        }
        devices = g_strsplit (p1, "\n", 0);
        g_free (p1);
        device = devices;
        result = g_strdup ("[");
        while (*device != NULL) {
                if (g_str_has_prefix (*device, "Inter") ||
                    g_str_has_prefix (*device, " face") ||
                    g_str_has_prefix (*device, "    lo")) {
                        device++;
                        continue;
                }
                if (sscanf (*device, "%*[ ]%[^:]:%*[.]", p2) != EOF) {
                        p1 = result;
                        result = g_strdup_printf ("%s\"%s\",", p1, p2);
                        g_free (p1);
                }
                device++;
        }
        g_strfreev (devices);
        if (strlen (result) > 1) {
                result[strlen (result) - 1] = ']';

        } else {
                g_free (result);
                result = g_strdup ("[]");
        }

        return result;
}

static gchar * list_files (gchar *pattern, gchar *format)
{
        glob_t pglob;
        gint i;
        gchar *devices, *p;

        p = g_strdup ("[");
        if (glob (pattern, 0, NULL, &pglob) == 0) {
                for (i = 0; i < pglob.gl_pathc; i++) {
                        gchar name[128];

                        if (format != NULL) {
                                sscanf (pglob.gl_pathv[i], format, name);
                                devices = g_strdup_printf ("%s\"%s\",", p, name);

                        } else {
                                devices = g_strdup_printf ("%s\"%s\",", p, pglob.gl_pathv[i]);
                        }
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

static gchar * start_job (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *job_description;

        if (request_data->method == HTTP_POST) {
                /* start a job. */
                job_description = request_data->raw_request + request_data->header_size;
                buf = gstreamill_job_start (httpmgmt->gstreamill, job_description);

        } else {
                buf = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"must be post request\"\n}");
        }

        return buf;
}

static gchar * stop_job (HTTPMgmt *httpmgmt, RequestData *request_data)
{
        gchar *buf, *p;
        GRegex *regex;
        GMatchInfo *match_info;

        if (request_data->method == HTTP_GET) {
                regex = g_regex_new ("/admin/stop/(?<name>.*)", G_REGEX_OPTIMIZE, 0, NULL);
                g_regex_match (regex, request_data->uri, 0, &match_info);
                g_regex_unref (regex);
                if (g_match_info_matches (match_info)) {
                        p = g_match_info_fetch_named (match_info, "name");
                        g_match_info_free (match_info);
                        buf = gstreamill_job_stop (httpmgmt->gstreamill, p);
                        g_free (p);
                        return buf;
                }
        }
        buf = g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"must be get request\"\n}");

        return buf;
}

static gchar * get_conf ()
{
        gchar *p, *result;
        GError *err = NULL;

        if (!g_file_get_contents ("/etc/gstreamill.conf", &p, NULL, &err)) {
                GST_ERROR ("read gstreamill conf file failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);

        } else {
                result = g_strdup_printf ("{\n    \"result\": \"success\",\n    \"data\": %s\n}", p);
                g_free (p);
        }

        return result;
}

static gchar * put_conf (RequestData *request_data)
{
        gchar *conf, *result;
        GError *err = NULL;

        conf = request_data->raw_request + request_data->header_size;
        if (!g_file_set_contents ("/etc/gstreamill.conf", conf, strlen (conf), &err)) {
                GST_ERROR ("write gstreamill.conf failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);

        } else {
                GST_INFO ("write gstreamill.conf success");
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }

        return result;
}

static gchar * get_job (gchar *uri)
{
        gchar *job_path, *job, *p;
        GError *err = NULL;

        job_path = g_strdup_printf ("/etc/gstreamill.d%s.job", &uri[13]);
        if (!g_file_get_contents (job_path, &p, NULL, &err)) {
                GST_ERROR ("read job %s failure: %s", job_path, err->message);
                job = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);

        } else {
                job = g_strdup_printf ("{\n    \"result\": \"success\",\n    \"data\": %s\n}", p);
                g_free (p);
        }
        g_free (job_path);

        
        return job;
}

static gchar * put_job (RequestData *request_data)
{
        gchar *job_path, *job, *result;
        GError *err = NULL;

        if (g_strcmp0 (&(request_data->uri[13]), "/") == 0) {
                GST_ERROR ("put job error, name is null");
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"name is null\"\n}");
        }
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

static gchar * rm_job (gchar *uri)
{
        gchar *job_path, *result;

        job_path = g_strdup_printf ("/etc/gstreamill.d%s.job", &(uri[12]));
        if (g_unlink (job_path) == -1) {
                GST_ERROR ("remove job %s failure: %s", job_path, g_strerror (errno));
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", g_strerror (errno));

        } else {
                GST_INFO ("write job %s success", job_path);
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }
        g_free (job_path);

        return result;
}

static gsize request_gstreamill_admin (HTTPMgmt *httpmgmt, RequestData *request_data, gchar **buf)
{
        gchar *path, *http_header, *p;
        gsize buf_size;
        GError *err = NULL;

        path = NULL;
        if (g_strcmp0 (request_data->uri, "/admin/") == 0) {
                path = g_strdup_printf ("%s/gstreamill/admin/index.html", DATADIR);

        } else if (g_str_has_prefix (request_data->uri, "/admin/start")) {
                p = start_job (httpmgmt, request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/admin/stop")) {
                p = stop_job (httpmgmt, request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/networkinterfaces") == 0) {
                p = network_interfaces ();
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/setnetworkinterfaces") == 0) {
                p = set_network_interfaces (request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/networkdevices") == 0) {
                p = network_devices ();
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/audiodevices") == 0) {
                p = list_files ("/dev/snd/pcmC*c", NULL);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/videodevices") == 0) {
                p = list_files ("/dev/video*", NULL);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/getconf") == 0) {
                p = get_conf ();
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if ((request_data->method == HTTP_POST) && (g_strcmp0 (request_data->uri, "/admin/putconf") == 0)) {
                p = put_conf (request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/listlivejob") == 0) {
                p = list_files ("/etc/gstreamill.d/*.job", "/etc/gstreamill.d/%[^.].job");
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

        } else if (g_str_has_prefix (request_data->uri, "/admin/rmjob/")) {
                p = rm_job (request_data->uri);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if ((request_data->method == HTTP_POST) && (g_str_has_prefix (request_data->uri, "/admin/putjob/"))) {
                p = put_job (request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/admin/jobmanage.html")) {
                if (g_str_has_prefix (request_data->parameters, "name=")) {
                        path = g_strdup_printf ("%s/gstreamill/admin/jobmanage.html", DATADIR);

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
                /* jobmanage.html? process name parameter */
                if (g_str_has_suffix (path, "jobmanage.html")) {
                        gchar name[32], *temp_buf;

                        sscanf (request_data->parameters, "name=%s", name);
                        temp_buf = g_strdup_printf (*buf, name);
                        g_free (*buf);
                        *buf = temp_buf;
                }
                /* html file? add top and bottom */
                if (g_str_has_suffix (path, ".html")) {
                        gchar *body;

                        body = *buf;
                        *buf = add_header_footer (body);
                        g_free (body);
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

                if (g_str_has_prefix (request_data->uri, "/stat/gstreamer")) {
                        buf = request_gstreamer_stat (httpmgmt, request_data);
                        buf_size = strlen (buf);

                } else if (g_str_has_prefix (request_data->uri, "/stat")) {
                        buf = request_gstreamill_stat (httpmgmt, request_data);
                        buf_size = strlen (buf);

                } else if (g_str_has_prefix (request_data->uri, "/admin")) {
                        buf_size = request_gstreamill_admin (httpmgmt, request_data, &buf);

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

