/*
 *  MPEGTS Segment Element
 *
 *  Copyright (C) Zhang Ping <dqzhangp@163.com>
 */

#include "tssegment.h"

G_DEFINE_TYPE (TsSegment, ts_segment, GST_TYPE_ELEMENT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
                                                                     GST_PAD_SINK,
                                                                     GST_PAD_ALWAYS,
                                                                     GST_STATIC_CAPS ("video/mpegts"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
                                                                    GST_PAD_SRC,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS ("video/mpegts"));

static void ts_segment_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void ts_segment_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static GstFlowReturn ts_segment_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

static void ts_segment_class_init (TsSegmentClass * klass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (klass);
        GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

        g_object_class->set_property = ts_segment_set_property;
        g_object_class->get_property = ts_segment_get_property;

        gst_element_class_set_static_metadata (element_class,
                                               "MPEGTS Segment plugin",
                                               "TSSegment/TSSegment",
                                               "MPEGTS Segment plugin",
                                               "Zhang Ping <zhangping@163.com>");

        gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_template));
        gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_template));
}

static void ts_segment_init (TsSegment *tssegment)
{
        tssegment->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
        gst_pad_set_chain_function (tssegment->sinkpad, GST_DEBUG_FUNCPTR(ts_segment_chain));
        gst_element_add_pad (GST_ELEMENT (tssegment), tssegment->sinkpad);

        tssegment->srcpad = gst_pad_new_from_static_template (&src_template, "src");
        gst_element_add_pad (GST_ELEMENT (tssegment), tssegment->srcpad);
}

static void ts_segment_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

static void ts_segment_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
}

static GstFlowReturn ts_segment_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
        TsSegment *tssegment;

        tssegment = TS_SEGMENT (parent);

        return gst_pad_push (tssegment->srcpad, buf);
}

gboolean ts_segment_plugin_init (GstPlugin * plugin)
{
        return gst_element_register (plugin, "tssegment", GST_RANK_NONE, TYPE_TS_SEGMENT);
}
