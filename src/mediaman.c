
/*
 *  mediaman
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <gio/gio.h>
#include <gst/gst.h>

gboolean media_append (gchar *path, gchar *buf, gssize size)
{
        GFile *gfile;
        GOutputStream *out;
        GError *err = NULL;
        gssize written;

        gfile = g_file_new_for_path (path);
        out = (GOutputStream *)g_file_append_to (gfile, G_FILE_CREATE_NONE, NULL, &err);
        if (out == NULL) {
                GST_ERROR ("Error opening file: %s\n", err->message);
                g_error_free (err);
                g_object_unref (gfile);
                return FALSE;
        }
        written = g_output_stream_write (out, buf, size, NULL, &err);
	if (written == -1) {
                GST_ERROR ("Error writing to stream: %s", err->message);
                g_error_free (err);
        }
        g_object_unref (out);
        g_object_unref (gfile);
}
