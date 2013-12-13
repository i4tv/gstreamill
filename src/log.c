/*
 * log.
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <time.h>
#include <string.h>
#include <libgen.h>

#include "log.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        LOG_PROP_0,
        LOG_PROP_PATH,
};

static GObject *log_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties);
static void log_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void log_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void log_dispose (GObject *obj);
static void log_func (GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function, gint line, GObject *object, GstDebugMessage *message, gpointer user_data);

static void log_class_init (LogClass *logclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (logclass);
        GParamSpec *param;

        g_object_class->constructor = log_constructor;
        g_object_class->set_property = log_set_property;
        g_object_class->get_property = log_get_property;
        g_object_class->dispose = log_dispose;

        param = g_param_spec_string (
                "log_path",
                "log_path",
                "path to loging",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, LOG_PROP_PATH, param);

}

static void log_init (Log *log)
{
}

GType log_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (LogClass),
                NULL, /* base class initializer */
                NULL, /* base class finalizer */
                (GClassInitFunc)log_class_init,
                NULL,
                NULL,
                sizeof (Log),
                0,
                (GInstanceInitFunc)log_init,
                NULL
        };
        type = g_type_register_static (G_TYPE_OBJECT, "Log", &info, 0);

        return type;
}

static GObject * log_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
        GObject *obj;
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        obj = parent_class->constructor (type, n_construct_properties, construct_properties);

        return obj;
}

static void log_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_LOG (obj));

        switch (prop_id) {
        case LOG_PROP_PATH:
                LOG (obj)->log_path = (gchar *)g_value_dup_string (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void log_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        Log *log = LOG (obj);

        switch (prop_id) {
        case LOG_PROP_PATH:
                g_value_set_string (value, log->log_path);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void log_dispose (GObject *obj)
{
        Log *log = LOG (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        if (log->log_path != NULL) {
                g_free (log->log_path);
                log->log_path = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

#define CAT_FMT "%s %s:%d: "
static void log_func (GstDebugCategory *category,
                      GstDebugLevel level,
                      const gchar *file,
                      const gchar *function,
                      gint line,
                      GObject *object,
                      GstDebugMessage *message,
                      gpointer user_data)
{
        FILE *log_hd = *(FILE **)user_data;
        time_t t;
        struct tm *tm;
        gchar date[26];

        if (level > gst_debug_category_get_threshold (category)) {
                return;
        }

        time (&t);
        tm = localtime (&t);
        asctime_r (tm, date);
        date[strlen (date)-1] = '\0';
        fprintf (log_hd, "%s: %s" CAT_FMT "%s\n",
                 date,
                 gst_debug_level_get_name (level),
                 gst_debug_category_get_name (category), file, line,
                 gst_debug_message_get (message));
}

gint log_set_log_handler (Log *log)
{
        gchar *dir;

        dir = g_strdup_printf ("%s", log->log_path);
        if (g_mkdir_with_parents (dirname (dir), 0755) != 0) {
                GST_ERROR ("Can't open or create log directory: %s.", dirname (dir));
                return 1;
        }
        log->func = log_func;
        log->log_hd = fopen (log->log_path, "ae");
        setvbuf(log->log_hd, NULL, _IOLBF, 0);
        if (log->log_hd == NULL) {
                GST_ERROR ("Error open log file %s, %s.", log->log_path, g_strerror (errno));
                return -1;
        } else {
                gst_debug_add_log_function (log_func, &(log->log_hd), NULL);
                return 0;
        }

        g_free (dir);
}

gint log_reopen (Log *log)
{
        log->log_hd = freopen (log->log_path, "w", log->log_hd);

        return 0;
}
