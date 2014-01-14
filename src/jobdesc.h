/*
 * json type of job description parser
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#ifndef __JOBDESC_H__
#define __JOBDESC_H__

#include <gst/gst.h>

typedef enum {
        JT_ERROR = 0,
        JT_LIVE,
        JT_TRANSCODE,
        JT_RECORDE,
        JT_UNKNOWN
} JobType;

gboolean livejobdesc_is_valid (gchar *job);
gchar * livejobdesc_get_name (gchar *job);
gint livejobdesc_encoders_count (gchar *job);
gint livejobdesc_streams_count (gchar *job, gchar *pipeline);
JobType livejobdesc_get_type (gchar *job);
gchar * livejobdesc_get_debug (gchar *job);
gchar ** livejobdesc_bins (gchar *job, gchar *pipeline);
gchar * livejobdesc_udpstreaming (gchar *job, gchar *pipeline);
gchar ** livejobdesc_element_properties (gchar *job, gchar *element);
gchar * livejobdesc_element_property_value (gchar *job, gchar *property);
gchar * livejobdesc_element_caps (gchar *job, gchar *element);
gboolean livejobdesc_m3u8streaming (gchar *job);
guint livejobdesc_m3u8streaming_version (gchar *job);
guint livejobdesc_m3u8streaming_window_size (gchar *job);
GstClockTime livejobdesc_m3u8streaming_segment_duration (gchar *job);

#endif /* __JOBDESC_H__ */
