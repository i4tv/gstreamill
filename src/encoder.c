/*
 *  encoder
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include "source.h"
#include "encoder.h"
#include "jobdesc.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        ENCODER_PROP_0,
        ENCODER_PROP_NAME
};

static void encoder_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void encoder_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void encoder_dispose (GObject *obj);
static void encoder_finalize (GObject *obj);

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
        if (*(encoder->output->head_addr) >= *(encoder->output->tail_addr)) {
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

        gop_size = encoder_output_gop_size (encoder->output, *(encoder->output->head_addr));
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

        if (*(encoder->output->last_rap_addr) + 12 < encoder->output->cache_size) {
                memcpy (encoder->output->cache_addr + *(encoder->output->last_rap_addr) + 8, &size, 4);

        } else if (*(encoder->output->last_rap_addr) + 8 < encoder->output->cache_size) {
                n = encoder->output->cache_size - *(encoder->output->last_rap_addr) - 8;
                memcpy (encoder->output->cache_addr + *(encoder->output->last_rap_addr) + 8, &size, n);
                memcpy (encoder->output->cache_addr, &size + n, 4 - n);

        } else {
                n = *(encoder->output->last_rap_addr) + 8 - encoder->output->cache_size;
                memcpy (encoder->output->cache_addr + n, &size, 4);
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

static GstFlowReturn new_sample_callback (GstAppSink * sink, gpointer user_data)
{
        GstBuffer *buffer;
        GstSample *sample;
        Encoder *encoder = (Encoder *)user_data;

        *(encoder->output->heartbeat) = gst_clock_get_time (encoder->system_clock);
        sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
        buffer = gst_sample_get_buffer (sample);
        if (sem_wait (encoder->output->semaphore) == -1) {
                GST_ERROR ("new_sample_callback sem_wait failure: %s", g_strerror (errno));
                gst_sample_unref (sample);
                return GST_FLOW_OK;
        }

        (*(encoder->output->total_count)) += gst_buffer_get_size (buffer);

        /* update head_addr, free enough memory for current buffer. */
        while (cache_free (encoder) <= gst_buffer_get_size (buffer) + 12) { /* timestamp + gop size = 12 */
                move_head (encoder);
        }

        if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
                /* 
                 * random access point found.
                 * write previous gop size to 4 bytes reservation,
                 * write current gop timestamp,
                 * reserve 4 bytes for size of current gop,
                 */
                if (encoder->mqdes == -1) {
                        /* no m3u8 output */
                        move_last_rap (encoder, buffer);

                } else if (GST_BUFFER_PTS (buffer) == encoder->last_running_time) {
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
         * update tail_addr
         */
        copy_buffer (encoder, buffer);

        sem_post (encoder->output->semaphore);
        gst_sample_unref (sample);

        return GST_FLOW_OK;
}

static void need_data_callback (GstAppSrc *src, guint length, gpointer user_data)
{
        EncoderStream *stream = (EncoderStream *)user_data;
        gint current_position;
        GstBuffer *buffer;
        GstPad *pad;
        GstEvent *event;

        current_position = (stream->current_position + 1) % SOURCE_RING_SIZE;
        for (;;) {
                if (stream->state != NULL) {
                        stream->state->last_heartbeat = gst_clock_get_time (stream->system_clock);
                }
                /* insure next buffer isn't current buffer */
                if ((current_position == stream->source->current_position) || stream->source->current_position == -1) {
                        if ((current_position == stream->source->current_position) && stream->source->eos) {
                                GstFlowReturn ret;

                                ret = gst_app_src_end_of_stream (src);
                                GST_INFO ("EOS of source %s, tell encoder %s, return %s", stream->source->name, stream->name, gst_flow_get_name (ret));
                                break;
                        }
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

                if (stream->state != NULL) {
                        stream->state->current_timestamp = GST_BUFFER_PTS (buffer);
                }

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

        if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
                GST_ERROR ("End of Stream of encoder %s", encoder->name);
                *(encoder->output->eos) = TRUE;
                return GST_PAD_PROBE_OK;
        }

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
                need_data_callback,
                NULL,
                NULL
        };
        GstAppSinkCallbacks encoder_appsink_callbacks = {
                NULL,
                NULL,
                new_sample_callback
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
                if ((g_strcmp0 ("GstAppSink", g_type_name (type)) == 0) ||
                    (g_strcmp0 ("GstHlsSink", g_type_name (type)) == 0) ||
                    (g_strcmp0 ("GstFileSink", g_type_name (type)) == 0)) {
                        GstPad *pad;

                        if (g_strcmp0 ("GstAppSink", g_type_name (type)) == 0) {
                                GST_INFO ("Encoder appsink found.");
                                gst_app_sink_set_callbacks (GST_APP_SINK (element), &encoder_appsink_callbacks, encoder, NULL);
                        }
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
                        GST_ERROR ("extract encoder %s streams failure", encoder->name);
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
                                GST_ERROR ("cant find job %s source %s.", job_name, estream->name);
                                g_free (job_name);
                                g_free (pipeline);
                                return 1;
                        }
                }

                /* mkdir for transcode job. */
                if (!jobdesc_is_live (job)) {
                        gchar *locations[] = {"%s.elements.filesink.property.location", "%s.elements.hlssink.property.location", NULL};
                        gchar *p, *value, **location;

                        location = locations;
                        while (*location != NULL) {
                                p = g_strdup_printf (*location, pipeline);
                                value = jobdesc_element_property_value (job, p);
                                g_free (p);
                                if (value != NULL) {
                                        break;
                                }
                                location += 1;
                        }
                        if (*location == NULL) {
                                GST_ERROR ("No location found for transcode");
                                return 1;
                        }
                        p = g_path_get_dirname (value);
                        g_free (value);
                        if (g_mkdir_with_parents (p, 0755) != 0) {
                                GST_ERROR ("Can't open or create directory: %s.", p);
                                g_free (p);
                                return 1;
                        }
                        g_free (p);
                }

                /* parse bins and create pipeline. */
                encoder->bins = bins_parse (job, pipeline);
                if (encoder->bins == NULL) {
                        GST_ERROR ("parse job %s bins error", job_name);
                        g_free (job_name);
                        g_free (pipeline);
                        return 1;
                }
                complete_request_element (encoder->bins);
                if (create_encoder_pipeline (encoder) != 0) {
                        GST_ERROR ("create encoder %s pipeline failure", encoder->name);
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

                } else {
                        encoder->mqdes = -1;
                }

                g_free (pipeline);
                g_array_append_val (earray, encoder);
        }
        g_free (job_name);

        return 0;
}

gboolean is_encoder_output_ready (EncoderOutput *encoder_output)
{
        gboolean ready;

        if (sem_wait (encoder_output->semaphore) == -1) {
                GST_ERROR ("is_encoder_output_ready sem_wait failure: %s", g_strerror (errno));
                return FALSE;
        }
        if (*(encoder_output->head_addr) == *(encoder_output->tail_addr)) {
                ready = FALSE;

        } else {
                ready = TRUE;
        }
        sem_post (encoder_output->semaphore);

        return ready;
}

/*
 * encoder_output_rap_timestamp:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr to get its timestamp
 *
 * get the timestamp of random access point of encoder_output.
 *
 * Returns: GstClockTime type timestamp.
 *
 */
GstClockTime encoder_output_rap_timestamp (EncoderOutput *encoder_output, guint64 rap_addr)
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

static guint64 encoder_output_rap_next (EncoderOutput *encoder_output, guint64 rap_addr)
{
        gint gop_size;
        guint64 next_rap_addr;

        /* gop size */
        gop_size = encoder_output_gop_size (encoder_output, rap_addr);

        /* next random access address */
        next_rap_addr = rap_addr + gop_size;
        if (next_rap_addr >= encoder_output->cache_size) {
                next_rap_addr -= encoder_output->cache_size;
        }

        return next_rap_addr;
}

/*
 * encoder_output_gop_seek:
 * @encoder_output: (in): the encoder output.
 * @timestamp: (in): time stamp of the top to seek
 *
 * return the address of the gop with time stamp of timestamp.
 *
 * Returns: gop addr if found, otherwise G_MAXUINT64 or 18446744073709551615(0xffffffffffffffff).
 *
 */
guint64 encoder_output_gop_seek (EncoderOutput *encoder_output, GstClockTime timestamp)
{
        guint64 rap_addr;
        GstClockTime t;

        rap_addr = *(encoder_output->head_addr);
        if (rap_addr > encoder_output->cache_size) {
                GST_ERROR ("FATAL: %s rap_addr value is %lu", encoder_output->name, rap_addr);
                return G_MAXUINT64;
        }
        while (rap_addr != *(encoder_output->last_rap_addr)) {
                t = encoder_output_rap_timestamp (encoder_output, rap_addr);
                if (timestamp == t) {
                        break;
                }
                rap_addr = encoder_output_rap_next (encoder_output, rap_addr);
                if (rap_addr > encoder_output->cache_size) {
                        GST_ERROR ("FATAL: %s rap_addr value is %lu", encoder_output->name, rap_addr);
                        return G_MAXUINT64;
                }
        }

        if (rap_addr != *(encoder_output->last_rap_addr)) {
                return rap_addr;

        } else {
                return G_MAXUINT64;
        }
}

/*
 * encoder_output_gop_size:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr
 *
 * get gop size.
 *
 * Returns: size of gop.
 *
 */
guint64 encoder_output_gop_size (EncoderOutput *encoder_output, guint64 rap_addr)
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

