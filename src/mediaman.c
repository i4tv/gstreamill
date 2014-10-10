
/*
 *  mediaman
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <string.h>
#include <glob.h>
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

gssize media_size (gchar *path)
{
        GFile *gfile;
        GFileInfo *ginfo;
        GError *err = NULL;
        goffset size;

        gfile = g_file_new_for_path (path);
        ginfo = g_file_query_info (gfile, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &err);
        size = g_file_info_get_size (ginfo);
        g_object_unref (gfile);

        return size;
}

gchar * media_transcode_in_list (gchar *path)
{
        gchar *pattern, *list, *p;
        glob_t pglob;
        gint i;

        pattern = g_strdup_printf ("%s/*", path);
        glob (pattern, 0, NULL, &pglob);
        g_free (pattern);
        if (pglob.gl_pathc == 0) {
                globfree (&pglob);
                return g_strdup ("[]");
        }
        list = g_strdup ("[");
        for (i = 0; i < pglob.gl_pathc; i++) {
                p = list;
                list = g_strdup_printf ("%s\"%s\",", p, pglob.gl_pathv[i]);
                g_free (p);
        }
        globfree (&pglob);
        list[strlen (list) - 1] = ']';

        return list;
}
