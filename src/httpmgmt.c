/*
 * managment api over http
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glob.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>
#include <augeas.h>

#define __USE_GNU
#include <sys/mman.h>

#include "parson.h"
#include "jobdesc.h"
#include "httpmgmt.h"
#include "gstreamill.h"
#include "mediaman.h"

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
        JSON_Value *value;
        JSON_Object *object;
        gchar *std_out, *p, **pp1, **pp2, model_name[128], *result;
        gint count;
        GError *err = NULL;

        value = json_value_init_object ();
        object = json_value_get_object (value);

        /* cpu mode */
        if (!g_file_get_contents ("/proc/cpuinfo", &p, NULL, &err)) {
                GST_ERROR ("read /proc/cpuinfo failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);
                json_value_free (value);
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
        json_object_set_string (object, "CPU_Model", model_name);

        if (!g_spawn_command_line_sync ("lscpu", &std_out, NULL, NULL, &err)) {
                GST_ERROR ("invoke lscpu failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);
                json_value_free (value);
                return result;
        }
        pp1 = pp2 = g_strsplit (std_out, "\n", 0);
        while (*pp1 != NULL) {
                if (g_str_has_prefix (*pp1, "CPU(s)")) {
                        sscanf (*pp1, "CPU(s):%*[ ]%d", &count);
                        json_object_set_number (object, "CPU_Count", count);

                } else if (g_str_has_prefix (*pp1, "Thread(s) per core")) {
                        sscanf (*pp1, "Thread(s) per core:%*[ ]%d", &count);
                        json_object_set_number (object, "Threads_per_Core", count);

                } else if (g_str_has_prefix (*pp1, "Core(s) per socket")) {
                        sscanf (*pp1, "Core(s) per socket:%*[ ]%d", &count);
                        json_object_set_number (object, "Core_per_Socket", count);

                } else if (g_str_has_prefix (*pp1, "Socket(s)")) {
                        sscanf (*pp1, "Socket(s):%*[ ]%d", &count);
                        json_object_set_number (object, "Sockets_Count", count);

                } else if (g_str_has_prefix (*pp1, "CPU MHz")){
                        sscanf (*pp1, "CPU MHz:%*[ ]%d", &count);
                        json_object_set_number (object, "CPU_MHz", count);
                }
                pp1++;
        }
        g_strfreev (pp2);
        g_free (std_out);
        result = json_serialize_to_string (value);
        json_value_free (value);

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

        } else if (g_str_has_suffix (path, ".js") || g_str_has_suffix (path, ".json")) {
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

        path = g_strdup_printf ("%s/admin/header.html", ADMIN_LOCATION);
        if (!g_file_get_contents (path, &top, NULL, &err)) {
                GST_ERROR ("read %s failure: %s", path, err->message);
                g_free (path);
                g_error_free (err);
                return g_strdup ("Internal error, file header.html not found.");
        }
        g_free (path);
        path = g_strdup_printf ("%s/admin/footer.html", ADMIN_LOCATION);
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

static augeas * aug_init_debian ()
{
        augeas *aug;

        aug = aug_init (NULL, NULL, AUG_NONE | AUG_NO_ERR_CLOSE | AUG_NO_MODL_AUTOLOAD);
        aug_set (aug, "/augeas/load/Interfaces/lens", "Interfaces.lns");
        aug_set (aug, "/augeas/load/Interfaces/incl", "/etc/network/interfaces");
        aug_load (aug);

        return aug;
}

static augeas * aug_init_redhat ()
{
        augeas *aug;

        aug = aug_init (NULL, NULL, AUG_NONE | AUG_NO_ERR_CLOSE | AUG_NO_MODL_AUTOLOAD);
        aug_set (aug, "/augeas/load/Shellvars/lens", "Shellvars.lns");
        aug_set (aug, "/augeas/load/Shellvars/incl[6]", "/etc/sysconfig/network-scripts/ifcfg-*");
        aug_load (aug);

        return aug;
}

static gchar * set_network_interfaces_debian (RequestData *request_data)
{
        gchar *interfaces, *result, *name, *path, *value;
        augeas *aug;
        JSON_Value *val;
        JSON_Array *array;
        JSON_Object *obj;
        gint if_count, i;

        aug = aug_init_debian ();
        interfaces = request_data->raw_request + request_data->header_size;
        val = json_parse_string (interfaces);
        if (val == NULL) {
                GST_ERROR ("parse json type interfaces failure");
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid data\"\n}");
                aug_close (aug);
                return result;
        }
        array = json_value_get_array (val);
        if (array == NULL) {
                GST_ERROR ("get interfaces array failure");
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"not array type interfaces\"\n}");
                aug_close (aug);
                json_value_free (val);
                return result;
        }
        if_count = json_array_get_count (array);
        for (i = 0; i < if_count; i++) {
                obj = json_array_get_object (array, i);
                name = (gchar *)json_object_get_string (obj, "name");
                value = (gchar *)json_object_get_string (obj, "method");
                if (value != NULL && strlen(value) != 0) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/method", name);
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/auto", name);
                                gchar* key = "method";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "address");
                if (value != NULL && strlen(value) != 0) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/address", name);
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/auto", name);
                                gchar* key = "address";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "netmask");
                if (value != NULL && strlen(value) != 0) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/netmask", name);
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/auto", name);
                                gchar* key = "netmask";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "network");
                if (value != NULL && strlen(value) != 0) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/network", name);
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/auto", name);
                                gchar* key = "network";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "broadcast");
                if (value != NULL && strlen(value) != 0) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/broadcast", name);
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/auto", name);
                                gchar* key = "broadcast";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                        g_free (path);
                }
                value = (gchar *)json_object_get_string (obj, "gateway");
                if (value != NULL && strlen(value) != 0) {
                        path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/gateway", name);
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/auto", name);
                                gchar* key = "gateway";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
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
        json_value_free (val);
        aug_close (aug);

        return result;
}

static gchar * set_network_interfaces_redhat (RequestData *request_data)
{
        gchar *interfaces, *result, *name, *path, *value;
        augeas *aug;
        JSON_Value *val;
        JSON_Array *array;
        JSON_Object *obj;
        gint if_count, i;

        aug = aug_init_redhat ();
        interfaces = request_data->raw_request + request_data->header_size;
        val = json_parse_string (interfaces);
        if (val == NULL) {
                GST_ERROR ("parse json type interfaces failure");
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid data\"\n}");
                aug_close (aug);
                return result;
        }
        array = json_value_get_array (val);
        if (array == NULL) {
                GST_ERROR ("get interfaces array failure");
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"not array type interfaces\"\n}");
                aug_close (aug);
                json_value_free (val);
                return result;
        }
        if_count = json_array_get_count (array);
        for (i = 0; i < if_count; i++) {
                obj = json_array_get_object (array, i);
                name = (gchar *)json_object_get_string (obj, "name");
                value = (gchar *)json_object_get_string (obj, "address");
                path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/IPADDR0", name);
                if (value != NULL && strlen(value) != 0) {
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "NAME");
                                gchar* key = "IPADDR0";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                }
                else {
                    if (0 != aug_get (aug, path, (const gchar **)&result)) {
                        gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "IPADDR0");
                        aug_rm(aug, key_path);
                        g_free(key_path);                   
                    }
                }
                g_free (path);

                value = (gchar *)json_object_get_string (obj, "netmask");
                path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/PREFIX0", name);
                if (value != NULL && strlen(value) != 0) {
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "NAME");
                                gchar* key = "PREFIX0";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                }
                else {
                    if (0 != aug_get (aug, path, (const gchar **)&result)) {
                        gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "PREFIX0");
                        aug_rm(aug, key_path);
                        g_free (key_path);
                    }
                }
                g_free (path);

                value = (gchar *)json_object_get_string (obj, "gateway");
                path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/GATEWAY0", name);
                if (value != NULL && strlen(value) != 0) {
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "NAME");
                                gchar* key = "GATEWAY0";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                }
                else {
                    if (0 != aug_get (aug, path, (const gchar **)&result)) {
                        gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "GATEWAY0");
                        aug_rm(aug, key_path);
                        g_free (key_path);
                    }
                }
                g_free (path);

                value = (gchar *)json_object_get_string (obj, "nameserver1");
                path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/DNS1", name);
                if (value != NULL && strlen(value) != 0) {
                        if (0 == aug_get (aug, path, (const gchar **)&result)) {
                                gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "NAME");
                                gchar* key = "DNS1";
                                aug_insert(aug, key_path, key, 0);
                                g_free (key_path);
                        }
                        aug_set (aug, path, value);
                }
                else {
                    if (0 != aug_get (aug, path, (const gchar **)&result)) {
                        gchar* key_path = g_strdup_printf ("//files/etc/sysconfig/network-scripts/ifcfg-%s/%s", name, "DNS1");
                        aug_rm(aug, key_path);
                        g_free (key_path);
                    }
                }
                g_free (path);
        }

        if (aug_save (aug) == -1) {
                aug_get (aug, "/augeas//error", (const gchar **)&value);
                GST_ERROR ("set /etc/network/interface failure: %s", value);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", value);

        } else {
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }
        json_value_free (val);
        aug_close (aug);

        return result;
}

static gchar * set_network_interfaces (RequestData *request_data)
{
        if (g_file_test ("/etc/network/interfaces", G_FILE_TEST_IS_REGULAR)) {
                return set_network_interfaces_debian (request_data);

        } else {
                return set_network_interfaces_redhat (request_data);
        }
}

static gchar * get_network_interfaces_debian ()
{
        JSON_Value *value_obj, *value_array;
        JSON_Object *object;
        JSON_Array *array;
        augeas *aug;
        gchar *value = NULL, **if_match, **option_match, *result, *p, option[128];
        gint if_number, option_number, i, j;

        aug = aug_init_debian ();
        value_array = json_value_init_array ();
        array = json_value_get_array (value_array);
        if_number = aug_match (aug, "//files/etc/network/interfaces/iface[.!='lo']", &if_match);
        for (i = 0; i < if_number; i++) {
                aug_get (aug, if_match[i], (const gchar **)&value);
                value_obj = json_value_init_object ();
                object = json_value_get_object (value_obj);
                json_object_set_string (object, "name", value);
                p = g_strdup_printf ("//files/etc/network/interfaces/iface[.='%s']/*", value);
                option_number = aug_match (aug, p, &option_match);
                g_free (p);
                for (j = 0; j < option_number; j++) {
                        sscanf (option_match[j], "%*[^]]]/%[^/]", option);
                        if (g_str_has_prefix (option, "#comment")) {
                                continue;
                        }
                        aug_get (aug, option_match[j], (const gchar **)&value);
                        json_object_set_string (object, option, value);
                        g_free (option_match[j]);
                }
                g_free (option_match);
                g_free (if_match[i]);
                json_array_append_value (array, value_obj);
        }
        g_free (if_match);
        aug_close (aug);
        result = json_serialize_to_string (value_array);
        json_value_free (value_array);

        return result;
}

static gchar * get_network_interfaces_redhat ()
{
        JSON_Value *value_obj, *value_array;
        JSON_Object *object;
        JSON_Array *array;
        augeas *aug;
        gchar *value = NULL, **if_match, **option_match, *result, *p, option[128];
        gint if_number, option_number, i, j;

        aug = aug_init_redhat ();
        value_array = json_value_init_array ();
        array = json_value_get_array (value_array);
        if_number = aug_match (aug, "//files/etc/sysconfig/network-scripts/*", &if_match);
        for (i = 0; i < if_number; i++) {
                if (g_str_has_suffix (if_match[i], "ifcfg-lo") || g_str_has_prefix (if_match[i], "/augeas")) {
                        continue;
                }
                p = g_strdup_printf ("%s/*", if_match[i]);
                option_number = aug_match (aug, p, &option_match);
                g_free (p);
                value_obj = json_value_init_object ();
                object = json_value_get_object (value_obj);
                for (j = 0; j < option_number; j++) {
                        p = g_strdup_printf ("%s/%%[^$]", if_match[i]);
                        sscanf (option_match[j], p, option);
                        g_free (p);
                        if (g_str_has_prefix (option, "#comment")) {
                                continue;
                        }
                        if (g_strcmp0 (option, "NAME") == 0) {
                                aug_get (aug, option_match[j], (const gchar **)&value);
                                json_object_set_string (object, "name", value);
                        }
                        if (g_str_has_prefix (option, "IPADDR")) {
                                aug_get (aug, option_match[j], (const gchar **)&value);
                                json_object_set_string (object, "address", value);
                        }
                        if (g_str_has_prefix (option, "GATEWAY")) {
                                aug_get (aug, option_match[j], (const gchar **)&value);
                                json_object_set_string (object, "gateway", value);
                        }
                        if (g_str_has_prefix (option, "PREFIX")) {
                                aug_get (aug, option_match[j], (const gchar **)&value);
                                json_object_set_string (object, "netmask", value);
                        }
                        g_free (option_match[j]);
                }
                g_free (option_match);
                g_free (if_match[i]);
                json_array_append_value (array, value_obj);
        }
        g_free (if_match);
        aug_close (aug);
        result = json_serialize_to_string (value_array);
        json_value_free (value_array);

        return result;
}

static gchar * get_network_interfaces ()
{
        if (g_file_test ("/etc/network/interfaces", G_FILE_TEST_IS_REGULAR)) {
                return get_network_interfaces_debian ();

        } else {
                return get_network_interfaces_redhat ();
        }
}

static gchar * get_network_devices ()
{
        JSON_Value *value_obj, *value_array;
        JSON_Object *object;
        JSON_Array *array;
        gchar *p1, p2[128], *result, **devices, **device;
        GError *err = NULL;


        if (!g_file_get_contents ("/proc/net/dev", &p1, NULL, &err)) {
                GST_ERROR ("read /proc/net/dev failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);
                return result;
        }
        devices = g_strsplit (p1, "\n", 0);
        g_free (p1);
        device = devices;
        value_array = json_value_init_array ();
        array = json_value_get_array (value_array);
        while (*device != NULL) {
                if (g_str_has_prefix (*device, "Inter") ||
                    g_str_has_prefix (*device, " face") ||
                    g_str_has_prefix (*device, "    lo")) {
                        device++;
                        continue;
                }
                if (sscanf (*device, "%*[ ]%[^:]:%*[.]", p2) != EOF) {
                        json_array_append_string (array, p2);
                }
                device++;
        }
        g_strfreev (devices);
        value_obj = json_value_init_object ();
        object = json_value_get_object (value_obj);
        json_object_set_string (object, "result", "success");
        json_object_set_value (object, "data", value_array);
        result = json_serialize_to_string (value_obj);
        json_value_free (value_obj);

        return result;
}

static gchar * list_devices (gchar *pattern, gchar *format)
{
        glob_t pglob;
        gint i;
        gchar *devices = NULL, *p = NULL;

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
                        GST_WARNING ("stop job request, job name is %s", p);
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

        if (!g_file_get_contents (CONF_FILE, &p, NULL, &err)) {
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
        if (!g_file_set_contents (CONF_FILE, conf, strlen (conf), &err)) {
                GST_ERROR ("write gstreamill.conf failure: %s", err->message);
                result = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", err->message);
                g_error_free (err);

        } else {
                GST_INFO ("write gstreamill.conf success");
                result = g_strdup ("{\n    \"result\": \"success\"\n}");
        }

        return result;
}

static gchar * get_job_description (gchar *uri)
{
        gchar *job_path, *job, *p;
        GError *err = NULL;

        job_path = g_strdup_printf (JOBS_DIR "%s.job", &uri[13]);
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

static gchar * put_job (RequestData *request_data, gboolean create)
{
        gchar *job_path, *job, *result;
        GError *err = NULL;

        if (!g_file_test (JOBS_DIR, G_FILE_TEST_EXISTS) && (g_mkdir_with_parents (JOBS_DIR, 0755) != 0)) {
                GST_ERROR ("Can't open or create " JOBS_DIR " directory");
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"can't create " JOBS_DIR"\"\n}");
        }
        if (g_strcmp0 (&(request_data->uri[13]), "/") == 0) {
                GST_ERROR ("put job error, name is null");
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"name is null\"\n}");
        }
        job_path = g_strdup_printf (JOBS_DIR "%s.job", &(request_data->uri[14]));
        if (create && g_file_test (job_path, G_FILE_TEST_IS_REGULAR)) {
                GST_ERROR ("Job exist: %s", job_path);
                g_free (job_path);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"Job Exist\"\n}");
        }
        job = request_data->raw_request + request_data->header_size;
        if (!jobdesc_is_valid (job)) {
                GST_ERROR ("Invalid job: %s", job);
                g_free (job_path);
                return g_strdup ("{\n    \"result\": \"failure\",\n    \"reason\": \"invalid job\"\n}");
        }
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

        job_path = g_strdup_printf (JOBS_DIR "%s.job", &(uri[13]));
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
        gchar *path, *http_header, *p = NULL;
        gsize buf_size;
        GError *err = NULL;

        path = NULL;
        if (g_strcmp0 (request_data->uri, "/admin/") == 0) {
                path = g_strdup_printf ("%s/admin/index.html", ADMIN_LOCATION);

        } else if (g_str_has_prefix (request_data->uri, "/admin/start")) {
                p = start_job (httpmgmt, request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/admin/stop")) {
                p = stop_job (httpmgmt, request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/getnetworkinterfaces") == 0) {
                p = get_network_interfaces ();
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/setnetworkinterfaces") == 0) {
                p = set_network_interfaces (request_data);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/getnetworkdevices") == 0) {
                p = get_network_devices ();
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/audiodevices") == 0) {
                p = list_devices ("/dev/snd/pcmC*c", NULL);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/videodevices") == 0) {
                p = list_devices ("/dev/video*", NULL);
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
                p = list_devices (JOBS_DIR "/*.job", JOBS_DIR "/%[^.].job");
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_strcmp0 (request_data->uri, "/admin/listnonlivejob") == 0) {
                p = gstreamill_list_nonlive_job (httpmgmt->gstreamill);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if (g_str_has_prefix (request_data->uri, "/admin/getjob/")) {
                p = get_job_description (request_data->uri);
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
                p = put_job (request_data, TRUE);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if ((request_data->method == HTTP_POST) && (g_str_has_prefix (request_data->uri, "/admin/setjob/"))) {
                p = put_job (request_data, FALSE);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else {
                /* static content, prepare file path */
                path = g_strdup_printf ("%s%s", ADMIN_LOCATION, request_data->uri);
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

static gsize get_chunknumber (gchar *parameters)
{
        gchar **pp1, **pp2, *p;
        gsize size;

        pp1 = pp2 = g_strsplit (parameters, "&", 0);
        p = NULL;
        while (*pp1 != NULL) {
                if (g_str_has_prefix (*pp1, "resumableChunkNumber")) {
                        p = g_strdup (&((*pp1)[21]));
                        break;
                }
                pp1++;
        }
        g_strfreev (pp2);
        size = atoi (p);
        g_free (p);

        return size;
}

static gsize get_chunksize (gchar *parameters)
{
        gchar **pp1, **pp2, *p;
        gsize size;

        pp1 = pp2 = g_strsplit (parameters, "&", 0);
        p = NULL;
        while (*pp1 != NULL) {
                if (g_str_has_prefix (*pp1, "resumableChunkSize")) {
                        p = g_strdup (&((*pp1)[19]));
                        break;
                }
                pp1++;
        }
        g_strfreev (pp2);
        size = atoi (p);
        g_free (p);

        return size;
}

static gchar * get_filename (gchar *parameters)
{
        gchar **pp1, **pp2, *p;

        pp1 = pp2 = g_strsplit (parameters, "&", 0);
        p = NULL;
        while (*pp1 != NULL) {
                if (g_str_has_prefix (*pp1, "resumableFilename")) {
                        p = g_strdup (&((*pp1)[18]));
                        break;
                }
                pp1++;
        }
        g_strfreev (pp2);

        return p;
}

static gsize media_download (HTTPMgmt *httpmgmt, RequestData *request_data, gchar **buf)
{
        gint fd;
        struct stat st;
        gsize buf_size;
        gchar *p;

        p = g_strdup_printf ("%s/%s", MEDIA_LOCATION, request_data->uri + 16);
        GST_WARNING ("download %s", p);
        fd = open (p, O_RDONLY);
        g_free (p);
        if (fd == -1) {
                GST_ERROR ("open %s error: %s", p, g_strerror (errno));
                p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", g_strerror (errno));
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);
                return 0;
        }

        if (fstat (fd, &st) == -1) {
                GST_ERROR ("fstat error: %s", g_strerror (errno));
                p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"%s\"\n}", g_strerror (errno));
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                close (fd);
                g_free (p);
                return 0;

        } else {
                gchar *p1, *p2;
                HTTPMgmtPrivateData *priv_data;

                p1 = mmap (NULL, st.st_size + sysconf (_SC_PAGE_SIZE), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (p1 == MAP_FAILED) {
                        GST_ERROR ("mmap anonymous error: %s", g_strerror (errno));
                        p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"mmap anonymous %s\"\n}", g_strerror (errno));
                        *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                        close (fd);
                        g_free (p);
                        return 0;
                }
                p2 = mmap (p1 + sysconf (_SC_PAGE_SIZE), st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                if (p2 == MAP_FAILED) {
                        GST_ERROR ("mmap file error: %s", g_strerror (errno));
                        p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"mmap file %s\"\n}", g_strerror (errno));
                        *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                        close (fd);
                        g_free (p);
                        munmap (p1, st.st_size + sysconf (_SC_PAGE_SIZE));
                        return 0;
                }
                p = mremap (p2, st.st_size, st.st_size, MREMAP_MAYMOVE | MREMAP_FIXED, p1 + sysconf (_SC_PAGE_SIZE));
                if (p == MAP_FAILED) {
                        GST_ERROR ("mremap file error: %s", g_strerror (errno));
                        p = g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reason\": \"mremap file %s\"\n}", g_strerror (errno));
                        *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                        close (fd);
                        g_free (p);
                        munmap (p1, st.st_size + sysconf (_SC_PAGE_SIZE));
                        munmap (p2, st.st_size);
                        return 0;
                }
                p = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/octet-stream", st.st_size, NO_CACHE, "");
                memcpy (p1 + sysconf (_SC_PAGE_SIZE) - strlen (p), p, strlen (p));
                *buf = p1 + sysconf (_SC_PAGE_SIZE) - strlen (p);
                buf_size = strlen (p) + st.st_size;
                g_free (p);
                priv_data = (HTTPMgmtPrivateData *)g_malloc (sizeof (HTTPMgmtPrivateData));
                priv_data->buf = *buf;
                priv_data->buf_size = buf_size;
                priv_data->send_position = 0;
                priv_data->fd = fd;
                priv_data->p = p1;
                request_data->priv_data = priv_data;
                return buf_size;
        }
}

static gsize request_gstreamill_media (HTTPMgmt *httpmgmt, RequestData *request_data, gchar **buf)
{
        gchar *path, *content, *p;
        gsize buf_size, content_size;

        if ((request_data->method == HTTP_POST) && (g_str_has_prefix (request_data->uri, "/media/upload"))) {
                p = get_filename (request_data->parameters);
                path = g_strdup_printf ("%s/transcode/in/%s", MEDIA_LOCATION, p);
                g_free (p);
                content = request_data->raw_request + request_data->header_size;
                content_size = request_data->request_length - request_data->header_size;
                if (media_append (path, content, content_size)) {
                        p = g_strdup_printf ("%ld", content_size);
                        *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (p), NO_CACHE, p);
                        g_free (p);

                } else {
                        *buf = g_strdup_printf (http_500, PACKAGE_NAME, PACKAGE_VERSION);
                }
                g_free (path);

        } else if ((request_data->method == HTTP_GET) && (g_str_has_prefix (request_data->uri, "/media/upload"))) {
                p = get_filename (request_data->parameters);
                path = g_strdup_printf ("%s/transcode/in/%s", MEDIA_LOCATION, p);
                g_free (p);
                if (get_chunksize (request_data->parameters) * get_chunknumber (request_data->parameters) <= media_size (path)) {
                        p = g_strdup ("complete");
                        *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "text/plain", strlen (p), NO_CACHE, p);
                        g_free (p);

                } else {
                        *buf = g_strdup_printf (http_204, PACKAGE_NAME, PACKAGE_VERSION);
                }
                g_free (path);

        } else if ((request_data->method == HTTP_GET) && (g_str_has_prefix (request_data->uri, "/media/download"))) {
                buf_size = media_download (httpmgmt, request_data, buf);
                /* success? */
                if (buf_size != 0) {
                        return buf_size;
                }

        } else if ((request_data->method == HTTP_GET) && (g_strcmp0 (request_data->uri, "/media/transcodeinlist") == 0)) {
                path = g_strdup_printf ("%s/transcode/in", MEDIA_LOCATION);
                p = media_transcode_in_list (path);
                g_free (path);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if ((request_data->method == HTTP_GET) && (g_str_has_prefix (request_data->uri, "/media/rm/transcode/"))) {
                p = request_data->uri + 20;
                path = g_strdup_printf ("%s/transcode/%s", MEDIA_LOCATION, p);
                p = media_transcode_rm (path);
                g_free (path);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);

        } else if ((request_data->method == HTTP_GET) && (g_strcmp0 (request_data->uri, "/media/transcodeoutlist") == 0)) {
                path = g_strdup_printf ("%s/transcode/out", MEDIA_LOCATION);
                p = media_transcode_out_list (path);
                g_free (path);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else if ((request_data->method == HTTP_GET) && (g_strcmp0 (request_data->uri, "/media/getmediadir") == 0)) {
                p = g_strdup_printf ("{\n    \"media_dir\": \"%s\"\n}", MEDIA_LOCATION);
                *buf = g_strdup_printf (http_200, PACKAGE_NAME, PACKAGE_VERSION, "application/json", strlen (p), NO_CACHE, p);
                g_free (p);

        } else {
                *buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
        }
        buf_size = strlen (*buf);

        return buf_size;
}

static void free_priv_data (HTTPMgmtPrivateData *priv_data)
{
        /* fd != -1, media download? */
        if (priv_data->fd == -1) {
                g_free (priv_data->buf);
                g_free (priv_data);

        } else {
                close (priv_data->fd);
                munmap (priv_data->p, priv_data->buf_size);
                munmap (priv_data->p + sysconf (_SC_PAGE_SIZE), priv_data->buf_size - sysconf (_SC_PAGE_SIZE));
                g_free (priv_data);
        }
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

                } else if (g_str_has_prefix (request_data->uri, "/media")) {
                        buf_size = request_gstreamill_media (httpmgmt, request_data, &buf);

                } else {
                        buf = g_strdup_printf (http_404, PACKAGE_NAME, PACKAGE_VERSION);
                        buf_size = strlen (buf);
                }

                ret = write (request_data->sock, buf, buf_size);
                /* send not completed or socket block? */
                if (((ret > 0) && (ret != buf_size)) || ((ret == -1) && (errno == EAGAIN))) {
                        /* media download? */
                        if (request_data->priv_data != NULL) {
                                priv_data = request_data->priv_data;
                                priv_data->send_position += ret;
                                return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;
                        }
                        priv_data = (HTTPMgmtPrivateData *)g_malloc (sizeof (HTTPMgmtPrivateData));
                        priv_data->fd = -1;
                        priv_data->buf = buf;
                        priv_data->buf_size = buf_size;
                        priv_data->send_position = ret > 0? ret : 0;
                        request_data->priv_data = priv_data;
                        return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;

                } else if (ret == -1) {
                        GST_ERROR ("Write sock error: %s", g_strerror (errno));
                }
                /* send complete or socket error */
                if (request_data->priv_data != NULL) {
                        free_priv_data (request_data->priv_data);
                        request_data->priv_data = NULL;

                } else {
                        g_free (buf);
                }
                return 0;

        case HTTP_CONTINUE:
                priv_data = request_data->priv_data;
                ret = write (request_data->sock, priv_data->buf + priv_data->send_position, priv_data->buf_size - priv_data->send_position);
                if ((ret + priv_data->send_position == priv_data->buf_size) || ((ret == -1) && (errno != EAGAIN))) {
                        /* send complete or send error, finish the request */
                        if ((ret == -1) && (errno != EAGAIN)) {
                                GST_ERROR ("Write sock error: %s", g_strerror (errno));
                        }
                        free_priv_data (priv_data);
                        request_data->priv_data = NULL;
                        return 0;

                } else if ((ret > 0) || ((ret == -1) && (errno == EAGAIN))) {
                        /* send not completed or socket block, resend late */
                        priv_data->send_position += ret > 0? ret : 0;
                        return ret > 0? 10 * GST_MSECOND + g_random_int_range (1, 1000000) : GST_CLOCK_TIME_NONE;
                }

        case HTTP_FINISH:
                if (request_data->priv_data != NULL) {
                        free_priv_data (request_data->priv_data);
                        request_data->priv_data = NULL;
                }
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

