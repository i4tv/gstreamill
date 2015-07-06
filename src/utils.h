
/*
 *  utils
 *
 *  Copyright (C) Zhang Ping <dqzhangp@163.com>
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <netinet/in.h>
#include <gst/gst.h>

gchar * unicode_file_name_2_shm_name (gchar *filename);
gchar * get_address (struct sockaddr in_addr);
gushort get_port (struct sockaddr in_addr);
gchar *timestamp_to_segment_dir (time_t timestamp);
gint segment_dir_to_timestamp (gchar *dir, time_t *timestamp);

#endif /* __UTILS_H__ */
