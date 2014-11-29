/*
 * m3u8 playlist
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <glob.h>
#include <string.h>
#include <stdio.h>
#include <gst/gst.h>

#include "m3u8playlist.h"

M3U8Playlist * m3u8playlist_new (guint version, guint window_size, guint64 sequence)
{
        M3U8Playlist *playlist;

        if (version != 3) {
                GST_WARNING ("version must be 3");
                return NULL;
        }
        if (window_size < 1) {
                GST_WARNING ("windows size must greater than 0");
                return NULL;
        }
        playlist = g_new0 (M3U8Playlist, 1);
        g_rw_lock_init (&(playlist->lock));
        playlist->version = version;
        playlist->window_size = window_size;
        playlist->entries = g_queue_new ();
        playlist->playlist_str = NULL;
        playlist->sequence_number = sequence;

        return playlist;
}

static void m3u8entry_free (M3U8Entry * entry)
{
        g_return_if_fail (entry != NULL);

        g_free (entry->url);
        g_free (entry);
}

void m3u8playlist_free (M3U8Playlist * playlist)
{
        g_queue_foreach (playlist->entries, (GFunc) m3u8entry_free, NULL);
        g_queue_free (playlist->entries);
        g_rw_lock_clear ((&playlist->lock));
        if (playlist->playlist_str != NULL) {
                g_free (playlist->playlist_str);
        }
        g_free (playlist);
}

static M3U8Entry * m3u8entry_new (const gchar * url, gfloat duration)
{
        M3U8Entry *entry;

        entry = g_new0 (M3U8Entry, 1);
        entry->url = g_strdup (url);
        entry->duration = duration;

        return entry;
}

static void render_entry (M3U8Entry * entry, GString * gstring)
{
        gchar *entry_str;

        g_return_val_if_fail (entry != NULL, NULL);

        entry_str = g_strdup_printf (M3U8_INF_TAG, (float) entry->duration / GST_SECOND, entry->url);
        g_string_append_printf (gstring, "%s", entry_str);
        g_free (entry_str);
}

static guint m3u8playlist_target_duration (M3U8Playlist * playlist)
{
        gint i;
        M3U8Entry *entry;
        guint64 target_duration = 0;

        for (i = 0; i < playlist->entries->length; i++) {
                entry = (M3U8Entry *) g_queue_peek_nth (playlist->entries, i);
                if (entry->duration > target_duration) {
                        target_duration = entry->duration;
                }
        }

        return (guint) ((target_duration + 500 * GST_MSECOND) / GST_SECOND);
}

static gchar * m3u8playlist_render (M3U8Playlist * playlist)
{
        GString *gstring;
        gchar *p;

        gstring = g_string_new ("");
        /* #EXTM3U */
        g_string_append_printf (gstring, M3U8_HEADER_TAG);
        /* #EXT-X-VERSION */
        g_string_append_printf (gstring, M3U8_VERSION_TAG, playlist->version);
        /* #EXT-X-ALLOW_CACHE */
        g_string_append_printf (gstring, M3U8_ALLOW_CACHE_TAG, "NO");
        /* #EXT-X-MEDIA-SEQUENCE */
        if (playlist->window_size != 0) {
                g_string_append_printf (gstring, M3U8_MEDIA_SEQUENCE_TAG, playlist->sequence_number - playlist->entries->length);
        }
        /* #EXT-X-TARGETDURATION */
        g_string_append_printf (gstring, M3U8_TARGETDURATION_TAG, m3u8playlist_target_duration (playlist));
        g_string_append_printf (gstring, "\n");
        /* Entries */
        g_queue_foreach (playlist->entries, (GFunc) render_entry, gstring);
        /* #EXT-X-ENDLIST */
        if (playlist->window_size == 0) {
                g_string_append_printf (gstring, M3U8_X_ENDLIST_TAG);
        }
        p = gstring->str;
        g_string_free (gstring, FALSE);

        return p;
}

gint m3u8playlist_add_entry (M3U8Playlist *playlist, const gchar *url, gfloat duration)
{
        M3U8Entry *entry;

        g_rw_lock_writer_lock (&(playlist->lock));

        entry = m3u8entry_new (url, duration);
        /* Delete old entries from the playlist */
        while ((playlist->window_size != 0) && (playlist->entries->length >= playlist->window_size)) {
                M3U8Entry *old_entry;

                old_entry = g_queue_pop_head (playlist->entries);
                m3u8entry_free (old_entry);
        }
        playlist->sequence_number++;;
        g_queue_push_tail (playlist->entries, entry);

        /* genertae playlist */
        if (playlist->playlist_str != NULL) {
                g_free (playlist->playlist_str);
        }
        playlist->playlist_str = m3u8playlist_render (playlist);

        g_rw_lock_writer_unlock (&(playlist->lock));

        return 0;
}

gchar * m3u8playlist_live_get_playlist (M3U8Playlist *playlist)
{
        gchar *p;

        g_rw_lock_reader_lock (&(playlist->lock));
        p = g_strdup (playlist->playlist_str);
        g_rw_lock_reader_unlock (&(playlist->lock));

        return p;
}

gchar * m3u8playlist_timeshift_get_playlist (gchar *path, gint64 offset)
{
        M3U8Playlist *m3u8playlist = NULL;
        gint i, j;
        gchar **pp, *p;
        glob_t pglob;
        gchar *pattern, *playlist;
        guint64 time;

        time = g_get_real_time () + 1000000 * offset;
        /* loop seek time shift position, step: 10s */
        for (i = 0; i < 10; i++) {
                pattern = g_strdup_printf ("%s/%lu*.ts", path, time / 10000000 - i);
                if (glob (pattern, 0, NULL, &pglob) == 0) {
                        for (j = pglob.gl_pathc - 1; j >= 0; j--) {
                                p = &(pglob.gl_pathv[j][strlen (path) + 1]);
                                pp = g_strsplit (p, "_", 0);
                                if (g_ascii_strtoull (pp[0], NULL, 10) <= time) {
                                        /* sequence: g_ascii_strtoull (pp[1], NULL, 10) */
                                        m3u8playlist = m3u8playlist_new (3, 3, g_ascii_strtoull (pp[1], NULL, 10));
                                        /* remove .ts */
                                        pp[2][strlen (pp[2]) - 3] = '\0';
                                        m3u8playlist_add_entry (m3u8playlist, p, g_strtod ((pp[2]), NULL));
                                        g_strfreev (pp);
                                        break;
                                }
                                g_strfreev (pp);
                        }
                }
                g_free (pattern);
                globfree (&pglob);
                if (m3u8playlist != NULL) {
                        break;
                }
        }

        if (m3u8playlist == NULL) {
                return NULL;
        }

        /* add entry */
        for (i = 0; i < 10; i++) {
                pattern = g_strdup_printf ("%s/%lu*.ts", path, time / 10000000 + i);
                if (glob (pattern, 0, NULL, &pglob) == 0) {
                        for (j = 0; j < pglob.gl_pathc; j++) {
                                p = &(pglob.gl_pathv[j][strlen (path) + 1]);
                                pp = g_strsplit (p, "_", 0);
                                if (g_ascii_strtoull (pp[0], NULL, 10) > time) {
                                        /* remove .ts */
                                        pp[2][strlen (pp[2]) - 3] = '\0';
                                        m3u8playlist_add_entry (m3u8playlist, p, g_strtod ((pp[2]), NULL));
                                        if (m3u8playlist->entries->length == 3) {
                                                g_strfreev (pp);
                                                break;
                                        }
                                }
                                g_strfreev (pp);
                        }
                }
                g_free (pattern);
                globfree (&pglob);
                if (m3u8playlist->entries->length == 3) {
                        break;
                }
        }
        playlist = g_strdup (m3u8playlist->playlist_str);
        m3u8playlist_free (m3u8playlist);

        return playlist;
}

gchar * m3u8playlist_dvr_get_playlist (gchar *path, gint64 start, gint64 duration)
{
        M3U8Playlist *m3u8playlist;
        gchar time[15], *from, *end, *pattern, *format, *playlist;
        glob_t pglob;
        gint i;

        from = g_strdup_printf ("%lu", start);
        end = g_strdup_printf ("%lu", start + duration);
        for (i = 0; i < strlen (from); i++) {
                if (from[i] == end[i]) {
                        time[i] = from[i];

                } else {
                        time[i] = '\0';
                        break;
                }
        }
        g_free (from);
        g_free (end);
        pattern = g_strdup_printf ("%s/%s*_*_*.ts", path, time);
        if (glob (pattern, 0, NULL, &pglob) == GLOB_NOMATCH) {
                playlist = NULL;

        } else {
                guint64 t;
                gchar *p, **pp;

                m3u8playlist = m3u8playlist_new (3, 0, 0);
                format = g_strdup_printf ("%s/%%lu_", path);
                for (i = 0; i < pglob.gl_pathc; i++) {
                        sscanf (pglob.gl_pathv[i], format, &t);
                        t /= GST_MSECOND;
                        if ((t >= start) && (t <= (start + duration))) {
                                p = &(pglob.gl_pathv[i][strlen (path) + 1]);
                                pp = g_strsplit (p, "_", 0);
                                /* remove .ts */
                                pp[2][strlen (pp[2]) - 3] = '\0';
                                m3u8playlist_add_entry (m3u8playlist, p, g_strtod ((pp[2]), NULL));
                        }
                }
                playlist = g_strdup (m3u8playlist->playlist_str);
                m3u8playlist_free (m3u8playlist);
                g_free (format);
        }
        g_free (pattern);

        return playlist;
}
