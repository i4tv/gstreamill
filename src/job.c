/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

#include "job.h"
#include "jobdesc.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        LIVEJOB_PROP_0,
        LIVEJOB_PROP_NAME,
        LIVEJOB_PROP_JOB,
};

enum {
        SOURCE_PROP_0,
        SOURCE_PROP_NAME,
        SOURCE_PROP_STATE
};

enum {
        ENCODER_PROP_0,
        ENCODER_PROP_NAME,
        ENCODER_PROP_STATE
};

static void source_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void source_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void source_dispose (GObject *obj);
static void source_finalize (GObject *obj);
static void encoder_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void encoder_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void encoder_dispose (GObject *obj);
static void encoder_finalize (GObject *obj);
static void livejob_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void livejob_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void livejob_dispose (GObject *obj);
static void livejob_finalize (GObject *obj);

static void source_class_init (SourceClass *sourceclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (sourceclass);
        GParamSpec *param;

        g_object_class->set_property = source_set_property;
        g_object_class->get_property = source_get_property;
        g_object_class->dispose = source_dispose;
        g_object_class->finalize = source_finalize;

        param = g_param_spec_string (
                "name",
                "name",
                "name of source",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, SOURCE_PROP_NAME, param);

        param = g_param_spec_int (
                "state",
                "statef",
                "state",
                GST_STATE_VOID_PENDING,
                GST_STATE_PLAYING,
                GST_STATE_VOID_PENDING,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, SOURCE_PROP_STATE, param);
}

static void source_init (Source *source)
{
        source->streams = g_array_new (FALSE, FALSE, sizeof (gpointer));
}

GType source_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (SourceClass), /* class size */
                NULL, /* base class initializer */
                NULL, /* base class finalizer */
                (GClassInitFunc)source_class_init, /* class init */
                NULL, /* class finalize */
                NULL, /* class data */
                sizeof (Source), /*instance size */
                0, /* n_preallocs */
                (GInstanceInitFunc)source_init, /* instance_init */
                NULL /* value_table */
        };
        type = g_type_register_static (G_TYPE_OBJECT, "Source", &info, 0);

        return type;
}

static void source_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_SOURCE (obj));

        switch (prop_id) {
        case SOURCE_PROP_NAME:
                SOURCE (obj)->name = (gchar *)g_value_dup_string (value);
                break;

        case SOURCE_PROP_STATE:
                SOURCE (obj)->state= g_value_get_int (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void source_dispose (GObject *obj)
{
        Source *source = SOURCE (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        if (source->name != NULL) {
                g_free (source->name);
                source->name = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void source_finalize (GObject *obj)
{
        Source *source = SOURCE (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
        gint i;
        SourceStream *stream;

        for (i = source->streams->len - 1; i >= 0; i--) {
                stream = g_array_index (source->streams, gpointer, i);
                g_array_free (stream->encoders, FALSE);
                g_free (stream);
                g_array_remove_index (source->streams, i);
        }
        g_array_free (source->streams, FALSE);

        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void source_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        Source *source = SOURCE (obj);

        switch (prop_id) {
        case SOURCE_PROP_NAME:
                g_value_set_string (value, source->name);
                break;

        case SOURCE_PROP_STATE:
                g_value_set_int (value, source->state);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void encoder_class_init (EncoderClass *encoderclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (encoderclass);
        GParamSpec *param;

        g_object_class->set_property = encoder_set_property;
        g_object_class->get_property = encoder_get_property;
        g_object_class->dispose = encoder_dispose;
        g_object_class->finalize = encoder_finalize;

        param = g_param_spec_string (
                "name",
                "name",
                "name of encoder",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, ENCODER_PROP_NAME, param);

        param = g_param_spec_int (
                "state",
                "statef",
                "state",
                GST_STATE_VOID_PENDING,
                GST_STATE_PLAYING,
                GST_STATE_VOID_PENDING,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, ENCODER_PROP_STATE, param);
}

static void encoder_init (Encoder *encoder)
{
        encoder->streams = g_array_new (FALSE, FALSE, sizeof (gpointer));
}

GType encoder_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (EncoderClass),
                NULL, /* base class initializer */
                NULL, /* base class finalizer */
                (GClassInitFunc)encoder_class_init,
                NULL,
                NULL,
                sizeof (Encoder),
                0,
                (GInstanceInitFunc)encoder_init,
                NULL
        };
        type = g_type_register_static (G_TYPE_OBJECT, "Encoder", &info, 0);

        return type;
}

static void encoder_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_ENCODER (obj));

        switch (prop_id) {
        case ENCODER_PROP_NAME:
                ENCODER (obj)->name = (gchar *)g_value_dup_string (value);
                break;

        case ENCODER_PROP_STATE:
                ENCODER (obj)->state= g_value_get_int (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void encoder_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        Encoder *encoder = ENCODER (obj);

        switch (prop_id) {
        case ENCODER_PROP_NAME:
                g_value_set_string (value, encoder->name);
                break;

        case ENCODER_PROP_STATE:
                g_value_set_int (value, encoder->state);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void encoder_dispose (GObject *obj)
{
        Encoder *encoder = ENCODER (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        if (encoder->name != NULL) {
                mq_close (encoder->mqdes);
                g_free (encoder->name);
                encoder->name = NULL;
        }

        G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void encoder_finalize (GObject *obj)
{
        Encoder *encoder = ENCODER (obj);
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
        gint i;
        EncoderStream *estream;

        for (i = encoder->streams->len - 1; i >= 0; i--) {
                estream = g_array_index (encoder->streams, gpointer, i);
                g_free (estream);
                g_array_remove_index (encoder->streams, i);
        }
        g_array_free (encoder->streams, FALSE);

        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

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

static void print_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
        int i, num;

        num = gst_tag_list_get_tag_size (list, tag);

        for (i = 0; i < num; ++i) {
                const GValue *val;

                val = gst_tag_list_get_value_index (list, tag, i);
                if (G_VALUE_HOLDS_STRING (val)) {
                        GST_INFO ("%20s : %s", tag, g_value_get_string (val));

                } else if (G_VALUE_HOLDS_UINT (val)) {
                        GST_INFO ("%20s : %u", tag, g_value_get_uint (val));

                } else if (G_VALUE_HOLDS_DOUBLE (val)) {
                        GST_INFO ("%20s : %g", tag, g_value_get_double (val));

                } else if (G_VALUE_HOLDS_BOOLEAN (val)) {
                        GST_INFO ("%20s : %s", tag, (g_value_get_boolean (val)) ? "true" : "false");

                } else if (GST_VALUE_HOLDS_BUFFER (val)) {
                        GST_INFO ("%20s : buffer of size %lu", tag, gst_buffer_get_size (gst_value_get_buffer (val)));

                } else {
                        GST_INFO ("%20s : tag of type '%s'", tag, G_VALUE_TYPE_NAME (val));
                }
        }
}

static gboolean bus_callback (GstBus *bus, GstMessage *msg, gpointer user_data)
{
        gchar *debug;
        GError *error;
        GstState old, new, pending;
        GstStreamStatusType type;
        GstClock *clock;
        GstTagList *tags;
        GObject *object = user_data;
        GValue state = { 0, }, name = { 0, };

        g_value_init (&name, G_TYPE_STRING);
        g_object_get_property (object, "name", &name);

        switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
                GST_INFO ("End of stream\n");
                break;

        case GST_MESSAGE_TAG:
                GST_INFO ("TAG");
                gst_message_parse_tag (msg, &tags);
                gst_tag_list_foreach (tags, print_one_tag, NULL);
                break;

        case GST_MESSAGE_ERROR: 
                gst_message_parse_error (msg, &error, &debug);
                g_free (debug);
                GST_WARNING ("%s error: %s", g_value_get_string (&name), error->message);
                g_error_free (error);
                break;

        case GST_MESSAGE_STATE_CHANGED:
                g_value_init (&state, G_TYPE_INT);
                gst_message_parse_state_changed (msg, &old, &new, &pending);
                GST_INFO ("pipeline %s element %s state from %s to %s",
                          g_value_get_string (&name),
                          GST_MESSAGE_SRC_NAME (msg),
                          gst_element_state_get_name (old),
                          gst_element_state_get_name (new));
                if (g_strcmp0 (g_value_get_string (&name), GST_MESSAGE_SRC_NAME (msg)) == 0) {
                        GST_INFO ("pipeline %s state change to %s", g_value_get_string (&name), gst_element_state_get_name (new));
                        g_value_set_int (&state, new);
                        g_object_set_property (object, "state", &state);
                        g_value_unset (&state);
                }
                break;

        case GST_MESSAGE_STREAM_STATUS:
                gst_message_parse_stream_status (msg, &type, NULL);
                GST_INFO ("stream status %d", type);
                break;

        case GST_MESSAGE_NEW_CLOCK:
                gst_message_parse_new_clock (msg, &clock);
                GST_INFO ("New source clock %s", GST_OBJECT_NAME (clock));
                break;
        case GST_MESSAGE_ASYNC_DONE:
                GST_INFO ("source %s message: %s", g_value_get_string (&name), GST_MESSAGE_TYPE_NAME (msg));
                break;

        default:
                GST_INFO ("%s message: %s", g_value_get_string (&name), GST_MESSAGE_TYPE_NAME (msg));
        }
        g_value_unset (&name);

        return TRUE;
}

static gchar** get_property_names (gchar *param)
{
        gchar *p1, *p2, **pp, **pp1;
        GRegex *regex;

        /* strip space at begin */
        regex = g_regex_new ("[^ ]* *(.*)", 0, 0, NULL);
        p1 = g_regex_replace (regex, param, -1, 0, "\\1", 0, NULL);
        g_regex_unref (regex);
        /* strip space beside = */
        regex = g_regex_new ("( *= *)", 0, 0, NULL);
        p2 = g_regex_replace (regex, p1, -1, 0, "=", 0, NULL);
        g_free (p1);
        g_regex_unref (regex);
        /* strip redundant space */
        regex = g_regex_new ("( +)", 0, 0, NULL);
        p1 = g_regex_replace (regex, p2, -1, 0, " ", 0, NULL);
        g_free (p2);
        g_regex_unref (regex);
        pp = g_strsplit (p1, " ", 0);
        g_free (p1);

        pp1 = pp;
        while (*pp1 != NULL) {
                if (g_strrstr (*pp1, "=") == NULL) {
                        GST_ERROR ("Configure error: %s", *pp1);
                        g_strfreev (pp);
                        return NULL;
                }
                regex = g_regex_new ("([^=]*)=.*", 0, 0, NULL);
                p1 = g_regex_replace (regex, *pp1, -1, 0, "\\1", 0, NULL);
                g_free (*pp1);
                g_regex_unref (regex);
                *pp1 = p1;
                pp1++;
        }

        return pp;
}

static gchar* get_property_value (gchar *param, gchar *property)
{
        gchar *r, *v;
        GRegex *regex;

        r = g_strdup_printf (".* *%s *= *([^ ]*).*", property);
        regex = g_regex_new (r, 0, 0, NULL);
        v = g_regex_replace (regex, param, -1, 0, "\\1", 0, NULL);
        g_regex_unref (regex);
        g_free (r);
        if (g_strcmp0 (param, v) == 0) {
                g_free (v);
                v = NULL;
        }

        return v;
}

static gboolean set_element_property (GstElement *element, gchar* name, gchar* value)
{
        GParamSpec *param_spec;
        GValue v = { 0, };

        param_spec = g_object_class_find_property (G_OBJECT_GET_CLASS (element), name);
        if (param_spec == NULL) {
                GST_ERROR ("Can't find property name: %s", name);
                return FALSE;
        }

        g_value_init (&v, param_spec->value_type);
        gst_value_deserialize (&v, value);
        g_object_set_property (G_OBJECT (element), param_spec->name, &v);

        return TRUE;
}

static GstElement * element_create (LiveJob *livejob, gchar *pipeline, gchar *param)
{
        GstElement *element;
        gchar *name, *p, **pp, **pp1, **properties, *value;
        GRegex *regex;

        /* create element. */
        regex = g_regex_new (" .*", 0, 0, NULL);
        name = g_regex_replace (regex, param, -1, 0, "", 0, NULL);
        g_regex_unref (regex);
        element = gst_element_factory_make (name, NULL);
        if (element == NULL) {
                GST_ERROR ("make element %s error.", name);
                g_free (name);
                return NULL;
        }

        p = g_strdup_printf ("%s.elements.%s.property", pipeline, name);
        properties = jobdesc_element_properties (livejob->job, p);
        g_free (p);
        if (properties != NULL) {
                /* set propertys in element property. */
                pp = properties;
                while (*pp != NULL) {
                        p = g_strdup_printf ("%s.elements.%s.property.%s", pipeline, name, *pp);
                        value = jobdesc_element_property_value (livejob->job, p);
                        g_free (p);
                        if (!set_element_property (element, *pp, value)) {
                                GST_ERROR ("Set property error %s=%s", *pp, value);
                                return NULL;
                        }
                        GST_INFO ("Set property: %s = %s.", *pp, value);
                        pp++;
                }
        }

        /* set element propertys in bin. */
        pp = get_property_names (param);
        if (pp == NULL) {
                gst_object_unref (element);
                g_strfreev (pp);
                g_free (name);
                return NULL;
        }
        pp1 = pp;
        while (*pp1 != NULL) {
                p = get_property_value (param, *pp1);
                if (p == NULL) {
                        GST_ERROR ("Create element %s failure, Configure error: %s=%s", name, *pp1, p);
                        gst_object_unref (element);
                        g_strfreev (pp);
                        g_free (name);
                        return NULL;
                }
                if (!set_element_property (element, *pp1, p)) {
                        GST_ERROR ("Create element %s failure, Set property error: %s=%s", name, *pp1, p);
                        g_free (name);
                        return NULL;
                }
                GST_INFO ("Set property: %s=%s", *pp1, p);
                g_free (p);
                pp1++;
        }
        g_strfreev (pp);
        GST_INFO ("Create element %s success.", name);
        g_free (name);

        return element;
}

static void pad_added_callback (GstElement *src, GstPad *pad, gpointer data)
{
        gchar *src_pad_name;
        GSList *bins = data;
        Bin *bin;
        GstCaps *caps;
        GSList *elements, *links;
        GstElement *element, *pipeline;
        Link *link;

        src_pad_name = gst_pad_get_name (pad);
        bin = NULL;
        while (bins != NULL) {
                bin = bins->data;
                if (g_str_has_prefix (src_pad_name, bin->name)) {
                        break;

                } else {
                        bin = NULL;
                }
                bins = bins->next;
        }
        if (bin == NULL) {
                GST_WARNING ("skip sometimes pad: %s", src_pad_name);
                return;
        }
        GST_INFO ("sometimes pad: %s found", src_pad_name);

        pipeline = (GstElement *)gst_element_get_parent (src);
        elements = bin->elements;
        while (elements != NULL) {
                element = elements->data;
                gst_element_set_state (element, GST_STATE_PLAYING);
                gst_bin_add (GST_BIN (pipeline), element);
                elements = elements->next;
        }

        links = bin->links;
        while (links != NULL) {
                link = links->data;
                GST_INFO ("Link %s -> %s", link->src_name, link->sink_name);
                if (link->caps != NULL) {
                        caps = gst_caps_from_string (link->caps);
                        gst_element_link_filtered (link->src, link->sink, caps);
                        gst_caps_unref (caps);

                } else {
                        gst_element_link (link->src, link->sink);
                }
                links = links->next;
        }

        if (gst_element_link (src, bin->previous->sink)) {
                GST_INFO ("new added pad name: %s, delayed src pad name %s. ok!", src_pad_name, bin->previous->src_pad_name);
        }

        g_free (src_pad_name);
}

static void free_bin (Bin *bin)
{
}

static gboolean is_pad (gchar *element)
{
        gchar *p;

        p = element;
        while (*p != '\0') {
                if (*p == '.') {
                        return TRUE;
                }
                if (*p == ' ') {
                        return FALSE;
                }
                p++;
        }

        return FALSE;
}

static void delay_sometimes_pad_link (Source *source, gchar *name)
{
        GSList *elements, *bins;
        Bin *bin;
        GstElement *element;

        bins = source->bins;
        while (bins != NULL) {
                bin = bins->data;
                elements = bin->elements;
                while (elements != NULL) {
                        element = elements->data;
                        if (g_strcmp0 (gst_element_get_name (element), name) != 0) {
                                elements = elements->next;
                                continue;
                        }
                        if (bin->signal_id != 0) {
                                /* have connected pad_added signal */
                                break;
                        }
                        bin->signal_id = g_signal_connect_data (element,
                                                                "pad-added",
                                                                G_CALLBACK (pad_added_callback),
                                                                source->bins,
                                                                (GClosureNotify)free_bin,
                                                                (GConnectFlags) 0);
                        GST_INFO ("delay sometimes pad linkage %s", bin->name);
                        elements = elements->next;
                }
                bins = bins->next;
        }
}

static gchar * get_bin_name (gchar *bin)
{
        GRegex *regex;
        GMatchInfo *match_info;
        gchar *name;

        /* bin->name, same as appsrc or appsink. */
        regex = g_regex_new ("appsrc *name=(?<name>[^ ]*)", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, bin, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
                g_match_info_free (match_info);
                return name;
        }

        /* demuxer.video ! queue ! mpeg2dec ! queue ! appsink name = video */
        regex = g_regex_new ("! *appsink *name *= *(?<name>[^ ]*)[^!]*$", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, bin, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
                g_match_info_free (match_info);
                return name;
        }
        
        /* udpsrc ! queue ! mpegtsdemux name=demuxer */
        regex = g_regex_new ("name *= *(?<name>[^ ]*)$", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, bin, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
                g_match_info_free (match_info);
                return name;
        }
        
        /* mpegtsmux name=muxer ! queue ! appsink sync=FALSE */
        regex = g_regex_new ("[^!]*name *= *(?<name>[^ ]*)", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, bin, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                name = g_match_info_fetch_named (match_info, "name");
                g_match_info_free (match_info);
                return name;
        }

        return NULL;
}

static GSList * bins_parse (LiveJob *livejob, gchar *pipeline)
{
        GstElement *element, *src;
        gchar *p, *p1, *src_name, *src_pad_name, **pp, **pp1, **bins, **binsp;
        Bin *bin;
        Link *link;
        GSList *list;

        list = NULL;
        binsp = bins = jobdesc_bins (livejob->job, pipeline);
        while (*binsp != NULL) {
                bin = g_slice_new (Bin);
                bin->links = NULL;
                bin->elements = NULL;
                bin->previous = NULL;
                bin->signal_id = 0;
                pp = pp1 = g_strsplit (*binsp, "!", 0);
                src = NULL;
                src_name = NULL;
                src_pad_name = NULL;
                bin->name = get_bin_name (*binsp);

                while (*pp != NULL) {
                        p1 = g_strdup (*pp);
                        p1 = g_strstrip (p1);
                        if (is_pad (p1)) {
                                /* request pad or sometimes pad */
                                if (src == NULL) {
                                        /* should be a sometimes pad */
                                        src_name = g_strndup (p1, g_strrstr (p1, ".") - p1);
                                        src_pad_name = g_strndup (g_strrstr (p1, ".") + 1, strlen (p1) - strlen (src_name) -1);

                                } else {
                                        /* should be a request pad */
                                        link = g_slice_new (Link);
                                        link->src = src;
                                        link->src_name = src_name;
                                        link->src_pad_name = src_pad_name;
                                        link->sink = NULL;
                                        link->sink_name = g_strndup (p1, g_strrstr (p1, ".") - p1);
                                        link->sink_pad_name = g_strdup (link->sink_name);
                                        p = g_strdup_printf ("%s.elements.%s.caps", pipeline, src_name);
                                        link->caps = jobdesc_element_caps (livejob->job, p);
                                        g_free (p);
                                        bin->links = g_slist_append (bin->links, link);
                                }
                                pp++;
                                continue;
                        }
                        element = element_create (livejob, pipeline, p1);
                        if (element != NULL) {
                                if (src_name != NULL) {
                                        link = g_slice_new (Link);
                                        link->src = src;
                                        link->src_name = src_name;
                                        link->src_pad_name = src_pad_name;
                                        link->sink = element;
                                        link->sink_name = p1;
                                        link->sink_pad_name = NULL;
                                        p = g_strdup_printf ("%s.elements.%s.caps", pipeline, src_name);
                                        link->caps = jobdesc_element_caps (livejob->job, p);
                                        g_free (p);
                                        if (src_pad_name == NULL) {
                                                bin->links = g_slist_append (bin->links, link);

                                        } else {
                                                bin->previous = link;
                                        }

                                } else {
                                        bin->first = element;
                                }
                                bin->elements = g_slist_append (bin->elements, element);
                                src = element;
                                src_name = p1;
                                src_pad_name = NULL;

                        } else {
                                /* create element failure */
                                g_free (bins);
                                g_strfreev (pp1);
                                return NULL;
                        }
                        pp++;
                }
                bin->last = element;
                list = g_slist_append (list, bin);
                g_strfreev (pp1);
                binsp++;
        }
        g_strfreev (bins);

        return list;
}

static SourceStream * source_get_stream (Source *source, gchar *name)
{
        SourceStream *stream;
        gint i;

        stream = NULL;
        for (i = 0; i < source->streams->len; i++) {
                stream = g_array_index (source->streams, gpointer, i);
                if (g_strcmp0 (stream->name, name) == 0) {
                        break;
                }
        }

        return stream;
}

static GstFlowReturn source_appsink_callback (GstAppSink *elt, gpointer user_data)
{
        GstSample *sample;
        GstBuffer *buffer;
        SourceStream *stream = (SourceStream *)user_data;
        EncoderStream *encoder;
        gint i;

        sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
        buffer = gst_sample_get_buffer (sample);
        stream->state->last_heartbeat = gst_clock_get_time (stream->system_clock);
        stream->current_position = (stream->current_position + 1) % SOURCE_RING_SIZE;

        /* output running status */
        GST_DEBUG ("%s current position %d, buffer duration: %ld", stream->name, stream->current_position, GST_BUFFER_DURATION (buffer));
        for (i = 0; i < stream->encoders->len; i++) {
                encoder = g_array_index (stream->encoders, gpointer, i);
                if (stream->current_position == encoder->current_position) {
                        GST_WARNING ("encoder stream %s can not catch up source output.", encoder->name);
                }
        }

        /* out a buffer */
        if (stream->ring[stream->current_position] != NULL) {
                gst_sample_unref (stream->ring[stream->current_position]);
        }
        stream->ring[stream->current_position] = sample;
        stream->state->current_timestamp = GST_BUFFER_PTS (buffer);

        return GST_FLOW_OK;
}

static GstElement * create_source_pipeline (Source *source)
{
        GstElement *pipeline, *element;
        Bin *bin;
        Link *link;
        GSList *bins, *links, *elements;
        GstAppSinkCallbacks appsink_callbacks = {
                NULL,
                NULL,
                source_appsink_callback
        };
        GstElementFactory *element_factory;
        GType type;
        SourceStream *stream;
        GstCaps *caps;
        GstBus *bus;

        pipeline = gst_pipeline_new (NULL);

        bins = source->bins;
        while (bins != NULL) {
                bin = bins->data;
                if (bin->previous == NULL) {
                        /* add element to pipeline */
                        elements = bin->elements;
                        while (elements != NULL) {
                                element = elements->data;
                                gst_bin_add (GST_BIN (pipeline), element);
                                elements = g_slist_next (elements);
                        }

                        /* links element */
                        links = bin->links;
                        while (links != NULL) {
                                link = links->data;
                                GST_INFO ("link %s -> %s", link->src_name, link->sink_name);
                                if (link->caps != NULL) {
                                        caps = gst_caps_from_string (link->caps);
                                        gst_element_link_filtered (link->src, link->sink, caps);
                                        gst_caps_unref (caps);

                                } else {
                                        gst_element_link (link->src, link->sink);
                                }
                                links = g_slist_next (links);
                        }

                } else {
                        /* delay sometimes pad link */
                        delay_sometimes_pad_link (source, bin->previous->src_name);
                }
                        
                /* new stream, set appsink output callback. */
                element = bin->last;
                element_factory = gst_element_get_factory (element);
                type = gst_element_factory_get_element_type (element_factory);
                if (g_strcmp0 ("GstAppSink", g_type_name (type)) == 0) {
                        stream = source_get_stream (source, bin->name);
                        gst_app_sink_set_callbacks (GST_APP_SINK (element), &appsink_callbacks, stream, NULL);
                        GST_INFO ("Set callbacks for bin %s", bin->name);
                }

                bins = g_slist_next (bins);
        }

        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
        gst_bus_add_watch (bus, bus_callback, source);
        g_object_unref (bus);

        return pipeline;
}

static void complete_request_element (GSList *bins)
{
        GSList *l1, *l2, *l3, *l4;
        Bin *bin, *bin2;
        Link *link;
        GstElement *element;

        l1 = bins;
        while (l1 != NULL) {
                bin = l1->data;
                l2 = bin->links;
                while (l2 != NULL) {
                        link = l2->data;
                        if (link->sink != NULL) {
                                l2 = g_slist_next (l2);
                                continue;
                        }
                        GST_INFO ("Request element link: %s -> %s", link->src_name, link->sink_name);
                        l3 = bins;
                        while (l3 != NULL) {
                                bin2 = l3->data;
                                l4 = bin2->elements;
                                while (l4 != NULL) {
                                        element = l4->data;
                                        l4 = g_slist_next (l4);
                                        if (g_strcmp0 (link->sink_name, gst_element_get_name (element)) == 0) {
                                                /* request sink element found, e.g mpeg2mux */
                                                link->sink = element;
                                        }
                                }
                                l3 = g_slist_next (l3);
                        }
                        l2 = g_slist_next (l2);
                }
                l1 = g_slist_next (l1);
        }
}

static EncoderStream * encoder_get_stream (Encoder *encoder, gchar *name)
{
        EncoderStream *stream;
        gint i;

        stream = NULL;
        for (i = 0; i < encoder->streams->len; i++) {
                stream = g_array_index (encoder->streams, gpointer, i);
                if (g_strcmp0 (stream->name, name) == 0) {
                        break;
                }
        }

        return stream;
}

/*
 * free memory size between tail and head.
 */
static gint cache_free (Encoder *encoder)
{
        if (*(encoder->output->head_addr) > *(encoder->output->tail_addr)) {
                return *(encoder->output->head_addr) - *(encoder->output->tail_addr);

        } else {
                return *(encoder->output->head_addr) + encoder->output->cache_size - *(encoder->output->tail_addr);
        }
}

/*
 * move head_addr to next gop.
 */
static void move_head (Encoder *encoder)
{
        gint gop_size;

        gop_size = livejob_encoder_output_gop_size (encoder->output, *(encoder->output->head_addr));
        /* move head. */
        if (*(encoder->output->head_addr) + gop_size < encoder->output->cache_size) {
                *(encoder->output->head_addr) += gop_size;

        } else {
                *(encoder->output->head_addr) = *(encoder->output->head_addr) + gop_size - encoder->output->cache_size;
        }
}

/*
 * move last random access point address.
 */
static void move_last_rap (Encoder *encoder, GstBuffer *buffer)
{
        gchar buf[12];
        gint32 size, n;

        /* calculate and write gop size. */
        if (*(encoder->output->tail_addr) >= *(encoder->output->last_rap_addr)) {
                size = *(encoder->output->tail_addr) - *(encoder->output->last_rap_addr);

        } else {
                size = encoder->output->cache_size - *(encoder->output->last_rap_addr) + *(encoder->output->tail_addr);
        }

        if (*(encoder->output->last_rap_addr) + 12 <= encoder->output->cache_size) {
                memcpy (encoder->output->cache_addr + *(encoder->output->last_rap_addr) + 8, &size, 4);

        } else {
                n = encoder->output->cache_size - *(encoder->output->last_rap_addr) - 8;
                memcpy (encoder->output->cache_addr + *(encoder->output->last_rap_addr), &size, n);
                memcpy (encoder->output->cache_addr, &size + n, 4 - n);
        }

        /* new gop timestamp, 4bytes reservation for gop size. */
        *(encoder->output->last_rap_addr) = *(encoder->output->tail_addr);
        memcpy (buf, &(GST_BUFFER_PTS (buffer)), 8);
        size = 0;
        memcpy (buf + 8, &size, 4);
        if (*(encoder->output->tail_addr) + 12 < encoder->output->cache_size) {
                memcpy (encoder->output->cache_addr + *(encoder->output->tail_addr), buf, 12);
                *(encoder->output->tail_addr) += 12;

        } else {
                n = encoder->output->cache_size - *(encoder->output->tail_addr);
                memcpy (encoder->output->cache_addr + *(encoder->output->tail_addr), buf, n);
                memcpy (encoder->output->cache_addr, buf + n, 12 - n);
                *(encoder->output->tail_addr) = 12 - n;
        }
}

static void copy_buffer (Encoder *encoder, GstBuffer *buffer)
{
        gint size;
        GstMapInfo info;

        gst_buffer_map (buffer, &info, GST_MAP_READ);
        if (*(encoder->output->tail_addr) + gst_buffer_get_size (buffer) < encoder->output->cache_size) {
                memcpy (encoder->output->cache_addr + *(encoder->output->tail_addr), info.data, gst_buffer_get_size (buffer));
                *(encoder->output->tail_addr) = *(encoder->output->tail_addr) + gst_buffer_get_size (buffer);

        } else {
                size = encoder->output->cache_size - *(encoder->output->tail_addr);
                memcpy (encoder->output->cache_addr + *(encoder->output->tail_addr), info.data, size);
                memcpy (encoder->output->cache_addr, info.data + size, gst_buffer_get_size (buffer) - size);
                *(encoder->output->tail_addr) = gst_buffer_get_size (buffer) - size;
        }
        gst_buffer_unmap (buffer, &info);
}

static void udp_streaming (Encoder *encoder, GstBuffer *buffer)
{
        gsize buffer_size;
        gssize offset;
        GstFlowReturn ret;

        offset = 0;
        buffer_size = gst_buffer_get_size (buffer);
        while (buffer_size != 0) {
                if ((encoder->cache_size == 0) && (buffer_size < 1316)) {
                        encoder->cache_7x188 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_MEMORY, offset, buffer_size);
                        encoder->cache_size = buffer_size;
                        break;

                } else if (encoder->cache_size == 0) {
                        /* buffer_size >= 1316 */
                        encoder->cache_7x188 = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_MEMORY, offset, 1316);
                        offset += 1316;
                        buffer_size -= 1316;

                } else if (encoder->cache_size + buffer_size >= 1316) {
                        gsize size;
                        gst_buffer_ref (buffer);
                        size = 1316 - encoder->cache_size;
                        encoder->cache_7x188 = gst_buffer_append_region (encoder->cache_7x188, buffer, offset, size);
                        offset += 1316 - encoder->cache_size;
                        buffer_size -= 1316 - encoder->cache_size;
                        encoder->cache_size = 0;

                } else {
                        /* encoder->cache_size + buffer_size < 1316 */
                        gst_buffer_ref (buffer);
                        encoder->cache_7x188 = gst_buffer_append_region (encoder->cache_7x188, buffer, offset, buffer_size);
                        encoder->cache_size += buffer_size;
                        break;
                }
                ret = gst_app_src_push_buffer ((GstAppSrc *)encoder->appsrc, encoder->cache_7x188);
                if (ret != GST_FLOW_OK) {
                        GST_ERROR ("appsrc push buffer failure, return %s.", gst_flow_get_name (ret));
                        gst_buffer_unref (encoder->cache_7x188);
                }
                encoder->cache_size = 0;
        }
}

static GstFlowReturn encoder_appsink_callback (GstAppSink * sink, gpointer user_data)
{
        GstBuffer *buffer;
        GstSample *sample;
        Encoder *encoder = (Encoder *)user_data;
        EncoderOutput *output;

        output = encoder->output;
        *(output->heartbeat) = gst_clock_get_time (encoder->livejob->system_clock);
        sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
        buffer = gst_sample_get_buffer (sample);
        sem_wait (encoder->output->semaphore);
        (*(output->total_count)) += gst_buffer_get_size (buffer);

        /* update head_addr, free enough memory for current buffer. */
        while (cache_free (encoder) < gst_buffer_get_size (buffer) + 12) { /* timestamp + gop size = 12 */
                move_head (encoder);
        }

        if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
                if (GST_BUFFER_PTS (buffer) == encoder->last_running_time) {
                        /* 
                         * random access point found.
                         * write previous gop size to 4 bytes reservation,
                         * write current gop timestamp,
                         * reserve 4 bytes for size of current gop,
                         */
                        gchar *msg;

                        move_last_rap (encoder, buffer);
                        msg = g_strdup_printf ("%lu", encoder->last_segment_duration);
                        if (mq_send (encoder->mqdes, msg, strlen (msg), 1) == -1) {
                                GST_ERROR ("mq_send error: %s", g_strerror (errno));
                        }
                        g_free (msg);
                        encoder->last_running_time = GST_CLOCK_TIME_NONE;
                }
        }

        /* udpstreaming? */
        if (encoder->udpstreaming) {
                udp_streaming (encoder, buffer);
        }

        /*
         * copy buffer to cache.
         * update tail_addr and last_rap_addr
         */
        copy_buffer (encoder, buffer);
        sem_post (output->semaphore);

        gst_sample_unref (sample);

        return GST_FLOW_OK;
}

static void encoder_appsrc_need_data_callback (GstAppSrc *src, guint length, gpointer user_data)
{
        EncoderStream *stream = (EncoderStream *)user_data;
        gint current_position;
        GstBuffer *buffer;
        GstPad *pad;
        GstEvent *event;

        current_position = (stream->current_position + 1) % SOURCE_RING_SIZE;
        for (;;) {
                stream->state->last_heartbeat = gst_clock_get_time (stream->system_clock);
                /* insure next buffer isn't current buffer */
                if (current_position == stream->source->current_position ||
                        stream->source->current_position == -1) {
                        GST_DEBUG ("waiting %s source ready", stream->name);
                        g_usleep (50000); /* wiating 50ms */
                        continue;
                }

                /* first buffer, set caps. */
                if (stream->current_position == -1) {
                        GstCaps *caps;
                        caps = gst_sample_get_caps (stream->source->ring[current_position]);
                        gst_app_src_set_caps (src, caps);
                        if (!g_str_has_prefix (gst_caps_to_string (caps), "video")) {
                                /* only for video stream, force key unit */
                                stream->encoder = NULL;
                        }
                        GST_INFO ("set stream %s caps: %s", stream->name, gst_caps_to_string (caps));
                }

                buffer = gst_sample_get_buffer (stream->source->ring[current_position]);
                GST_DEBUG ("%s encoder position %d; timestamp %" GST_TIME_FORMAT " source position %d",
                        stream->name,   
                        stream->current_position,
                        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
                        stream->source->current_position);

                /* force key unit? */
                if ((stream->encoder != NULL) && (stream->encoder->segment_duration != 0)) {
                        if (stream->encoder->duration_accumulation >= stream->encoder->segment_duration) {
                                GstClockTime running_time;

                                stream->encoder->last_segment_duration = stream->encoder->duration_accumulation;
                                running_time = GST_BUFFER_PTS (buffer);
                                pad = gst_element_get_static_pad ((GstElement *)src, "src");
                                event = gst_video_event_new_downstream_force_key_unit (running_time,
                                                                                       running_time,
                                                                                       running_time,
                                                                                       TRUE,
                                                                                       stream->encoder->force_key_count);
                                gst_pad_push_event (pad, event);
                                stream->encoder->force_key_count++;
                                stream->encoder->duration_accumulation = 0;
                        }
                        stream->encoder->duration_accumulation += GST_BUFFER_DURATION (buffer);
                }

                /* push buffer */
                if (gst_app_src_push_buffer (src, gst_buffer_ref (buffer)) != GST_FLOW_OK) {
                        GST_ERROR ("%s, gst_app_src_push_buffer failure.", stream->name);
                }

                stream->state->current_timestamp = GST_BUFFER_PTS (buffer);

                break;
        }
        stream->current_position = current_position;
}

static GstPadProbeReturn encoder_appsink_event_probe (GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
        GstEvent *event = gst_pad_probe_info_get_event (info);
        Encoder *encoder = data;
        GstClockTime timestamp, running_time, stream_time;
        gboolean all_headers;
        guint count;

        if (!gst_video_event_is_force_key_unit (event)) {
                return GST_PAD_PROBE_OK;
        }
        /* force key unit event */
        gst_video_event_parse_downstream_force_key_unit (event, &timestamp, &stream_time, &running_time, &all_headers, &count);
        if (encoder->last_segment_duration != 0) {
                encoder->last_running_time = timestamp;
        }

        return GST_PAD_PROBE_OK;
}

static gint create_encoder_pipeline (Encoder *encoder)
{
        GstElement *pipeline, *element;
        Bin *bin;
        Link *link;
        GSList *bins, *links, *elements;
        GstElementFactory *element_factory;
        GType type;
        EncoderStream *stream;
        GstAppSrcCallbacks callbacks = {
                encoder_appsrc_need_data_callback,
                NULL,
                NULL
        };
        GstAppSinkCallbacks encoder_appsink_callbacks = {
                NULL,
                NULL,
                encoder_appsink_callback
        };
        GstCaps *caps;
        GstBus *bus;
 
        pipeline = gst_pipeline_new (NULL);

        /* add element to pipeline first. */
        bins = encoder->bins;
        while (bins != NULL) {
                bin = bins->data;
                elements = bin->elements;
                while (elements != NULL) {
                        element = elements->data;
                        if (!gst_bin_add (GST_BIN (pipeline), element)) {
                                GST_ERROR ("add element %s to bin %s error.", gst_element_get_name (element), bin->name);
                                return 1;
                        }
                        elements = g_slist_next (elements);
                }
                bins = g_slist_next (bins);
        }

        /* then links element. */
        bins = encoder->bins;
        while (bins != NULL) {
                bin = bins->data;
                element = bin->first;
                element_factory = gst_element_get_factory (element);
                type = gst_element_factory_get_element_type (element_factory);
                stream = NULL;
                if (g_strcmp0 ("GstAppSrc", g_type_name (type)) == 0) {
                        GST_INFO ("Encoder appsrc found.");
                        stream = encoder_get_stream (encoder, bin->name);
                        gst_app_src_set_callbacks (GST_APP_SRC (element), &callbacks, stream, NULL);
                }
                element = bin->last;
                element_factory = gst_element_get_factory (element);
                type = gst_element_factory_get_element_type (element_factory);
                if (g_strcmp0 ("GstAppSink", g_type_name (type)) == 0) {
                        GstPad *pad;

                        GST_INFO ("Encoder appsink found.");
                        gst_app_sink_set_callbacks (GST_APP_SINK (element), &encoder_appsink_callbacks, encoder, NULL);
                        pad = gst_element_get_static_pad (element, "sink");
                        gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, encoder_appsink_event_probe, encoder, NULL);
                }
                links = bin->links;
                while (links != NULL) {
                        link = links->data;
                        GST_INFO ("link element: %s -> %s", link->src_name, link->sink_name);
                        if (link->caps != NULL) {
                                caps = gst_caps_from_string (link->caps);
                                gst_element_link_filtered (link->src, link->sink, caps);
                                gst_caps_unref (caps);

                        } else {
                                gst_element_link (link->src, link->sink);
                        }
                        links = g_slist_next (links);
                }
                bins = g_slist_next (bins);
        }
        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
        gst_bus_add_watch (bus, bus_callback, encoder);
        g_object_unref (bus);
        encoder->pipeline = pipeline;

        return 0;
}

static gsize status_output_size (LiveJob *livejob)
{
        gsize size;
        gint i;
        gchar *pipeline;

        size = (strlen (livejob->job) / 8 + 1) * 8; /* job description, 64 bit alignment */
        size += sizeof (guint64); /* state */
        size += jobdesc_streams_count (livejob->job, "source") * sizeof (struct _SourceStreamState);
        for (i = 0; i < jobdesc_encoders_count (livejob->job); i++) {
                size += sizeof (GstClockTime); /* encoder heartbeat */
                pipeline = g_strdup_printf ("encoder.%d", i);
                size += jobdesc_streams_count (livejob->job, pipeline) * sizeof (struct _EncoderStreamState); /* encoder state */
                g_free (pipeline);
                size += sizeof (guint64); /* cache head */
                size += sizeof (guint64); /* cache tail */
                size += sizeof (guint64); /* last rap (random access point) */
                size += sizeof (guint64); /* total count */
        }

        return size;
}

static gint source_extract_streams (LiveJob *livejob)
{
        GRegex *regex;
        GMatchInfo *match_info;
        SourceStream *stream;
        gchar **bins, **p, *bin;

        p = bins = jobdesc_bins (livejob->job, "source");
        while (*p != NULL) {
                bin = *p;
                regex = g_regex_new ("! *appsink *name *= *(?<name>[^ ]*)[^!]*$", G_REGEX_OPTIMIZE, 0, NULL);
                g_regex_match (regex, bin, 0, &match_info);
                g_regex_unref (regex);
                if (g_match_info_matches (match_info)) {
                        stream = (SourceStream *)g_malloc (sizeof (SourceStream));
                        stream->name = g_match_info_fetch_named (match_info, "name");
                        GST_INFO ("source stream %s found %s", stream->name, bin);
                        g_match_info_free (match_info);
                        g_array_append_val (livejob->source->streams, stream);

                } else if (g_strrstr (bin, "appsink") != NULL) {
                        GST_ERROR ("appsink name property must be set");
                        return 1;
                }
                p++;
        }
        g_strfreev (bins);

        return 0;
}

static guint source_initialize (LiveJob *livejob)
{
        gint i, j;
        SourceStream *stream;

        livejob->source = source_new ("name", "source", NULL);
        livejob->source->livejob = livejob;
        if (source_extract_streams (livejob) != 0) {
                return 1;
        }

        for (i = 0; i < livejob->source->streams->len; i++) {
                stream = g_array_index (livejob->source->streams, gpointer, i);
                stream->current_position = -1;
                stream->system_clock = livejob->system_clock;
                stream->encoders = g_array_new (FALSE, FALSE, sizeof (gpointer));
                for (j = 0; j < SOURCE_RING_SIZE; j++) {
                        stream->ring[j] = NULL;
                }
                stream->state = &(livejob->output->source.streams[i]);
                g_strlcpy (livejob->output->source.streams[i].name, stream->name, STREAM_NAME_LEN);
        }

        /* parse bins and create pipeline. */
        livejob->source->bins = bins_parse (livejob, "source");
        if (livejob->source->bins == NULL) {
                return 1;
        }
        livejob->source->pipeline = create_source_pipeline (livejob->source);

        return 0;
}

static gint encoder_extract_streams (Encoder *encoder, gchar **bins)
{
        GRegex *regex;
        GMatchInfo *match_info;
        EncoderStream *stream;
        gchar *bin, **p;

        p = bins;
        while (*p != NULL) {
                bin = *p;
                regex = g_regex_new ("appsrc *name=(?<name>[^ ]*)", G_REGEX_OPTIMIZE, 0, NULL);
                g_regex_match (regex, bin, 0, &match_info);
                g_regex_unref (regex);
                if (g_match_info_matches (match_info)) {
                        stream = (EncoderStream *)g_malloc (sizeof (EncoderStream));
                        stream->name = g_match_info_fetch_named (match_info, "name");
                        g_match_info_free (match_info);
                        g_array_append_val (encoder->streams, stream);
                        GST_INFO ("encoder stream %s found %s", stream->name, bin);

                } else if (g_str_has_prefix (bin, "appsrc")) {
                        GST_ERROR ("appsrc name property must be set");
                        return 1;
                }
                p++;
        }

        return 0;
}

static void udpstreaming_parse (LiveJob *livejob, Encoder *encoder)
{
        gchar *udpstreaming, **pp;
        GstElement *udpsink;

        udpstreaming = jobdesc_udpstreaming (livejob->job, encoder->name);
        if (udpstreaming == NULL) {
                encoder->udpstreaming = NULL;
                encoder->appsrc = NULL;
                return;
        }
        encoder->udpstreaming = gst_pipeline_new ("udpstreaming");
        encoder->appsrc = gst_element_factory_make ("appsrc", "source");
        udpsink = gst_element_factory_make ("udpsink", "sink");
        pp = g_strsplit (udpstreaming, ":", 0);
        g_object_set (G_OBJECT (udpsink), "host", pp[0], "port", atoi (pp[1]), NULL);
        g_strfreev (pp);
        gst_bin_add_many (GST_BIN (encoder->udpstreaming), encoder->appsrc, udpsink, NULL);
        gst_element_link_many (encoder->appsrc, udpsink, NULL);
        encoder->cache_size = 0;
}

static guint encoder_initialize (LiveJob *livejob)
{
        gint i, j, k;
        gchar *pipeline;
        Encoder *encoder;
        EncoderStream *stream;
        SourceStream *source;
        gchar **bins;
        gsize count;

        count = jobdesc_encoders_count (livejob->job);
        for (i = 0; i < count; i++) {
                pipeline = g_strdup_printf ("encoder.%d", i);
                encoder = encoder_new ("name", pipeline, NULL);
                encoder->livejob = livejob;
                encoder->id = i;
                encoder->last_running_time = GST_CLOCK_TIME_NONE;
                encoder->output = &(livejob->output->encoders[i]);
                encoder->segment_duration = jobdesc_m3u8streaming_segment_duration (encoder->livejob->job);
                encoder->duration_accumulation = 0;
                encoder->last_segment_duration = 0;
                encoder->force_key_count = 0;

                bins = jobdesc_bins (livejob->job, pipeline);
                if (encoder_extract_streams (encoder, bins) != 0) {
                        g_strfreev (bins);
                        return 1;
                }
                g_strfreev (bins);

                for (j = 0; j < encoder->streams->len; j++) {
                        stream = g_array_index (encoder->streams, gpointer, j);
                        stream->state = &(livejob->output->encoders[i].streams[j]);
                        g_strlcpy (livejob->output->encoders[i].streams[j].name, stream->name, STREAM_NAME_LEN);
                        stream->encoder = encoder;
                        stream->source = NULL;
                        for (k = 0; k < livejob->source->streams->len; k++) {
                                source = g_array_index (livejob->source->streams, gpointer, k);
                                if (g_strcmp0 (source->name, stream->name) == 0) {
                                        stream->source = source;
                                        stream->current_position = -1;
                                        stream->system_clock = encoder->livejob->system_clock;
                                        g_array_append_val (source->encoders, stream);
                                        break;
                                }
                        }
                        if (stream->source == NULL) {
                                GST_ERROR ("cant find livejob %s source %s.", livejob->name, stream->name);
                                return 1;
                        }
                }

                /* parse bins and create pipeline. */
                encoder->bins = bins_parse (livejob, pipeline);
                if (encoder->bins == NULL) {
                	g_free (pipeline);
                        return 1;
                }
                complete_request_element (encoder->bins);
                if (create_encoder_pipeline (encoder) != 0) {
                	g_free (pipeline);
                        return 1;
                }

                /* parse udpstreaming */
                udpstreaming_parse (livejob, encoder);

                /* m3u8 playlist */
                if (jobdesc_m3u8streaming (livejob->job)) {
                        gchar *name;

                        name = g_strdup_printf ("/%s.%d", livejob->name, i);
                        encoder->mqdes = mq_open (name, O_WRONLY);
                        if (encoder->mqdes == -1) {
                                GST_ERROR ("mq_open %s error: %s", name, g_strerror (errno));
                                return 1;
                        }
                        g_free (name);
                }

                g_free (pipeline);
                g_array_append_val (livejob->encoder_array, encoder);
        }

        return 0;
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

        livejob->output_size = status_output_size (livejob);
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

        if (source_initialize (livejob) != 0) {
                GST_ERROR ("Initialize livejob source error.");
                return 1;
        }

        if (encoder_initialize (livejob) != 0) {
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

