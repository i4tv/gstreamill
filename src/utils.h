
/*
 *  utils
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <netinet/in.h>
#include <gst/gst.h>

gchar * unicode_file_name_2_shm_name (gchar *filename);
gchar * get_address (struct sockaddr in_addr);
gushort get_port (struct sockaddr in_addr);

#endif /* __UTILS_H__ */
