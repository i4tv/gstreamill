/*
 *  MPEGTS Segment Element
 *
 *  Copyright (C) Zhang Ping <dqzhangp@163.com>
 */

#ifndef __TSSEGMENT_H__
#define __TSSEGMENT_H__

#include <gst/gst.h>

typedef struct _TsSegment {
        GstElement element;
        GstPad *sinkpad, *srcpad;
} TsSegment;

typedef struct _TsSegmentClass {
        GstElementClass parent_class;
} TsSegmentClass;

#define TYPE_TS_SEGMENT            (ts_segment_get_type())
#define TS_SEGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),TYPE_TS_SEGMENT,TsSegment))
#define TS_SEGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),TYPE_TS_SEGMENT,TsSegmentClass))
#define IS_TS_SEGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),TYPE_TS_SEGMENT))
#define IS_TS_SEGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),TYPE_TS_SEGMENT))

GType ts_segment_get_type (void);

gboolean ts_segment_plugin_init (GstPlugin * plugin);

#endif /* __TSSEGMENT_H__ */
