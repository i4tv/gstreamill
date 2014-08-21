/*
 *  source
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

#include "source.h"
#include "encoder.h"
#include "jobdesc.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        SOURCE_PROP_0,
        SOURCE_PROP_NAME,
        SOURCE_PROP_STATE
};

static void source_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void source_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void source_dispose (GObject *obj);
static void source_finalize (GObject *obj);

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

gboolean bus_callback (GstBus *bus, GstMessage *msg, gpointer user_data)
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
                GST_INFO ("%s end of stream\n", g_value_get_string (&name));
                break;

        case GST_MESSAGE_TAG:
                GST_INFO ("TAG");
                gst_message_parse_tag (msg, &tags);
                gst_tag_list_foreach (tags, print_one_tag, NULL);
                break;

        /*
        an error occurred. When the application receives an error message it should
        stop playback of the pipeline and not assume that more data will be played.
        */
        case GST_MESSAGE_ERROR: 
                gst_message_parse_error (msg, &error, &debug);
                g_free (debug);
                GST_ERROR ("%s error found: %s, exit", g_value_get_string (&name), error->message);
                g_error_free (error);
                exit (4); /* exit 4 for pipeline error */
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
                        if (value == NULL) {
                                GST_ERROR ("property %s not found", p);

                        } else {
                                if (!set_element_property (element, *pp, value)) {
                                        GST_ERROR ("Set property error %s=%s", *pp, value);
                                        return NULL;
                                }
                                GST_INFO ("Set property: %s = %s.", *pp, value);
                                g_free (value);
                        }
                        g_free (p);
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

GSList * bins_parse (gchar *job, gchar *pipeline)
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
                                GST_ERROR ("create element failure: %s", p1);
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

static void eos_callback (GstAppSink *appsink, gpointer user_data)
{
        SourceStream *stream = (SourceStream *)user_data;

        GST_INFO ("EOS of %s", stream->name);
        stream->eos = TRUE;
}

static GstFlowReturn new_sample_callback (GstAppSink *elt, gpointer user_data)
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
                if (stream->is_live) {
                        /* a live job, warning encoder too slow */
                        if (stream->current_position == encoder->current_position) {
                                GST_WARNING ("encoder stream %s can not catch up source output.", encoder->name);
                        }

                } else {
                        /* not a live job, avoid decoder too fast */
                        while (stream->current_position == encoder->current_position) {
                                GST_DEBUG ("waiting %s encoder", stream->name);
                                g_usleep (50000); /* wiating 50ms */
                                continue;
                        }
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
                eos_callback,
                NULL,
                new_sample_callback
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

Source * source_initialize (gchar *job, SourceState *source_stat)
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
                stream->eos = FALSE;
                stream->current_position = -1;
                if (jobdesc_is_live (job)) {
                        stream->is_live = TRUE;
                } else {

                        stream->is_live = FALSE;
                }
                stream->system_clock = source->system_clock;
                stream->encoders = g_array_new (FALSE, FALSE, sizeof (gpointer));
                for (j = 0; j < SOURCE_RING_SIZE; j++) {
                        stream->ring[j] = NULL;
                }
                stream->state = &(source_stat->streams[i]);
                g_strlcpy (source_stat->streams[i].name, stream->name, STREAM_NAME_LEN);
        }

        /* parse bins and create pipeline. */
        source->bins = bins_parse (job, "source");
        if (source->bins == NULL) {
                return NULL;
        }
        source->pipeline = create_source_pipeline (source);

        return source;
}

