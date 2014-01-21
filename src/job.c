/*
 *  job
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
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
        source->system_clock = gst_system_clock_obtain ();
        g_object_set (source->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
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
        encoder->system_clock = gst_system_clock_obtain ();
        g_object_set (encoder->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
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

static GstElement * element_create (gchar *job, gchar *pipeline, gchar *param)
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
        properties = jobdesc_element_properties (job, p);
        g_free (p);
        if (properties != NULL) {
                /* set propertys in element property. */
                pp = properties;
                while (*pp != NULL) {
                        p = g_strdup_printf ("%s.elements.%s.property.%s", pipeline, name, *pp);
                        value = jobdesc_element_property_value (job, p);
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

static GSList * bins_parse (gchar *job, gchar *pipeline)
{
        GstElement *element, *src;
        gchar *p, *p1, *src_name, *src_pad_name, **pp, **pp1, **bins, **binsp;
        Bin *bin;
        Link *link;
        GSList *list;

        list = NULL;
        binsp = bins = jobdesc_bins (job, pipeline);
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
                                        link->caps = jobdesc_element_caps (job, p);
                                        g_free (p);
                                        bin->links = g_slist_append (bin->links, link);
                                }
                                pp++;
                                continue;
                        }
                        element = element_create (job, pipeline, p1);
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
                                        link->caps = jobdesc_element_caps (job, p);
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
        *(output->heartbeat) = gst_clock_get_time (encoder->system_clock);
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

static gint source_extract_streams (Source *source, gchar *job)
{
        GRegex *regex;
        GMatchInfo *match_info;
        SourceStream *stream;
        gchar **bins, **p, *bin;

        p = bins = jobdesc_bins (job, "source");
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
                        g_array_append_val (source->streams, stream);

                } else if (g_strrstr (bin, "appsink") != NULL) {
                        GST_ERROR ("appsink name property must be set");
                        return 1;
                }
                p++;
        }
        g_strfreev (bins);

        return 0;
}

Source * source_initialize (gchar *job, SourceState source_stat)
{
        gint i, j;
        Source *source;
        SourceStream *stream;

        source = source_new ("name", "source", NULL);
        if (source_extract_streams (source, job) != 0) {
                return NULL;
        }

        for (i = 0; i < source->streams->len; i++) {
                stream = g_array_index (source->streams, gpointer, i);
                stream->current_position = -1;
                stream->system_clock = source->system_clock;
                stream->encoders = g_array_new (FALSE, FALSE, sizeof (gpointer));
                for (j = 0; j < SOURCE_RING_SIZE; j++) {
                        stream->ring[j] = NULL;
                }
                stream->state = &(source_stat.streams[i]);
                g_strlcpy (source_stat.streams[i].name, stream->name, STREAM_NAME_LEN);
        }

        /* parse bins and create pipeline. */
        source->bins = bins_parse (job, "source");
        if (source->bins == NULL) {
                return NULL;
        }
        source->pipeline = create_source_pipeline (source);

        return source;
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

static void udpstreaming_parse (gchar *job, Encoder *encoder)
{
        gchar *udpstreaming, **pp;
        GstElement *udpsink;

        udpstreaming = jobdesc_udpstreaming (job, encoder->name);
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

guint encoder_initialize (GArray *earray, gchar *job, EncoderOutput *encoders, Source *source)
{
        gint i, j, k;
        gchar *job_name, *pipeline;
        Encoder *encoder;
        EncoderStream *estream;
        SourceStream *sstream;
        gchar **bins;
        gsize count;

        job_name = jobdesc_get_name (job);
        count = jobdesc_encoders_count (job);
        for (i = 0; i < count; i++) {
                pipeline = g_strdup_printf ("encoder.%d", i);
                encoder = encoder_new ("name", pipeline, NULL);
                encoder->id = i;
                encoder->last_running_time = GST_CLOCK_TIME_NONE;
                encoder->output = &(encoders[i]);
                encoder->segment_duration = jobdesc_m3u8streaming_segment_duration (job);
                encoder->duration_accumulation = 0;
                encoder->last_segment_duration = 0;
                encoder->force_key_count = 0;

                bins = jobdesc_bins (job, pipeline);
                if (encoder_extract_streams (encoder, bins) != 0) {
                        g_free (job_name);
                        g_free (pipeline);
                        g_strfreev (bins);
                        return 1;
                }
                g_strfreev (bins);

                for (j = 0; j < encoder->streams->len; j++) {
                        estream = g_array_index (encoder->streams, gpointer, j);
                        estream->state = &(encoders[i].streams[j]);
                        g_strlcpy (encoders[i].streams[j].name, estream->name, STREAM_NAME_LEN);
                        estream->encoder = encoder;
                        estream->source = NULL;
                        for (k = 0; k < source->streams->len; k++) {
                                sstream = g_array_index (source->streams, gpointer, k);
                                if (g_strcmp0 (sstream->name, estream->name) == 0) {
                                        estream->source = sstream;
                                        estream->current_position = -1;
                                        estream->system_clock = encoder->system_clock;
                                        g_array_append_val (sstream->encoders, estream);
                                        break;
                                }
                        }
                        if (estream->source == NULL) {
                                g_free (job_name);
                                g_free (pipeline);
                                GST_ERROR ("cant find job %s source %s.", job_name, estream->name);
                                return 1;
                        }
                }

                /* parse bins and create pipeline. */
                encoder->bins = bins_parse (job, pipeline);
                if (encoder->bins == NULL) {
                        g_free (job_name);
                	g_free (pipeline);
                        return 1;
                }
                complete_request_element (encoder->bins);
                if (create_encoder_pipeline (encoder) != 0) {
                        g_free (job_name);
                	g_free (pipeline);
                        return 1;
                }

                /* parse udpstreaming */
                udpstreaming_parse (job, encoder);

                /* m3u8 playlist */
                if (jobdesc_m3u8streaming (job)) {
                        gchar *mq_name;

                        mq_name = g_strdup_printf ("/%s.%d", job_name, i);
                        encoder->mqdes = mq_open (mq_name, O_WRONLY);
                        if (encoder->mqdes == -1) {
                                g_free (job_name);
                                g_free (pipeline);
                                GST_ERROR ("mq_open %s error: %s", mq_name, g_strerror (errno));
                                return 1;
                        }
                        g_free (mq_name);
                }

                g_free (pipeline);
                g_array_append_val (earray, encoder);
        }
        g_free (job_name);

        return 0;
}

