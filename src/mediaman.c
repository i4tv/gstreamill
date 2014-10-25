
/*
 *  mediaman
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <string.h>
#include <glob.h>
#include <dirent.h>
#include <glib.h>
#include <glib/gstdio.h>
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

        return TRUE;
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
        gchar *pattern, *list, *p, *file;
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
                file = g_path_get_basename (pglob.gl_pathv[i]);
                list = g_strdup_printf ("%s\"%s\",", p, file);
                g_free (file);
                g_free (p);
        }
        globfree (&pglob);
        list[strlen (list) - 1] = ']';

        return list;
}

static gchar * transcode_out_list (gchar *path)
{
        gchar *pattern, *list, *p, *file;
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
                if (g_str_has_suffix (pglob.gl_pathv[i], "gstreamill.log")) {
                        continue;
                }
                p = list;
                file = g_path_get_basename (pglob.gl_pathv[i]);
                list = g_strdup_printf ("%s\"%s\",", p, file);
                g_free (file);
                g_free (p);
        }
        globfree (&pglob);
        if (strlen (list) == 1) {
                p = list;
                list = g_strdup_printf ("%s]", p);
                g_free (p);

        } else {
                list[strlen (list) - 1] = ']';
        }

        return list;
}

gchar * media_transcode_out_list (gchar *path)
{
        struct dirent **outdirlist;
        gchar *list, *p1, *p2;
        gint n;

        n = scandir (path, &outdirlist, NULL, alphasort);
        if (n < 0) {
                GST_ERROR ("scandir error: %s", g_strerror (errno));
                return g_strdup ("[]");

        }
        list = g_strdup ("{\n");
        while (n--) {
                if ((g_strcmp0 (outdirlist[n]->d_name, ".") != 0) && (g_strcmp0 (outdirlist[n]->d_name, "..") != 0)) {
                        p1 = g_strdup_printf ("%s/%s", path, outdirlist[n]->d_name);
                        p2 = transcode_out_list (p1);
                        g_free (p1);
                        p1 = list;
                        list = g_strdup_printf ("%s    \"%s\":%s,\n", p1, outdirlist[n]->d_name, p2);
                        g_free (p2);
                        g_free (p1);
                }
                g_free (outdirlist[n]);
        }
        g_free (outdirlist);
        list[strlen (list) - 2] = '\n';
        list[strlen (list) - 1] = '}';

        return list;
}

gchar * media_transcode_rm (gchar *media)
{
        if (g_remove (media) == 0) {
                return g_strdup ("{\n    \"result\": \"success\"\n}");

        } else {
                GST_ERROR ("rm %s failure: %s", media, g_strerror (errno));
                return g_strdup_printf ("{\n    \"result\": \"failure\",\n    \"reseon\": \"%s\"}", g_strerror (errno));
        }
}

