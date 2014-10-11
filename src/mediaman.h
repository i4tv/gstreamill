
/*
 *  mediaman
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __MEDIAMAN_H__
#define __MEDIAMAN_H__

#include <gst/gst.h>

gboolean media_append (gchar *path, gchar *buf, gssize size);
gssize media_size (gchar *path);
gssize media_md5sum (gchar *path);
gchar * media_transcode_in_list (gchar *path);
gchar * media_transcode_out_list (gchar *path);
gchar * media_transcode_in_rm (gchar *media);
gchar * media_transcode_out_rm (gchar *media);

#endif /* __MEDIAMAN_H__ */
