/*
 * m3u8 playlist
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#define _XOPEN_SOURCE
#include <glob.h>
#include <string.h>
#include <stdio.h>
#include <gst/gst.h>
#include <time.h>

#include "utils.h"
#include "m3u8playlist.h"

M3U8Playlist * m3u8playlist_new (guint version, guint window_size, guint64 sequence)
{
        M3U8Playlist *playlist;

        if (version != 3) {
                GST_WARNING ("version must be 3");
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

static M3U8Entry * m3u8entry_new (const gchar * url, GstClockTime duration)
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

        g_return_if_fail (entry != NULL);

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
        g_string_append_printf (gstring, M3U8_HEADER_TAG);
        g_string_append_printf (gstring, M3U8_VERSION_TAG, playlist->version);
        g_string_append_printf (gstring, M3U8_ALLOW_CACHE_TAG, "NO");
        if (playlist->window_size != 0) {
                g_string_append_printf (gstring, M3U8_MEDIA_SEQUENCE_TAG, playlist->sequence_number - playlist->entries->length);
        }
        g_string_append_printf (gstring, M3U8_TARGETDURATION_TAG, m3u8playlist_target_duration (playlist));
        g_string_append_printf (gstring, "\n");
        g_queue_foreach (playlist->entries, (GFunc) render_entry, gstring);
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

        /* Delete old entries from the playlist */
        while ((playlist->window_size != 0) && (playlist->entries->length >= playlist->window_size)) {
                entry = g_queue_pop_head (playlist->entries);
                m3u8entry_free (entry);
        }

        /* add entry */
        entry = m3u8entry_new (url, duration);
        playlist->sequence_number++;
        g_queue_push_tail (playlist->entries, entry);

        /* genertae playlist */
        if (playlist->playlist_str != NULL) {
                g_free (playlist->playlist_str);
        }
        playlist->playlist_str = m3u8playlist_render (playlist);

        g_rw_lock_writer_unlock (&(playlist->lock));

        return duration;
}

gchar * m3u8playlist_live_get_playlist (M3U8Playlist *playlist)
{
        gchar *p;

        g_rw_lock_reader_lock (&(playlist->lock));
        if (playlist->playlist_str == NULL) {
                GST_WARNING ("live playlist is null");
                p = NULL;

        } else {
                p = g_strdup (playlist->playlist_str);
        }
        g_rw_lock_reader_unlock (&(playlist->lock));

        return p;
}

gchar * m3u8playlist_timeshift_get_playlist (gchar *path, gint64 offset)
{
        M3U8Playlist *m3u8playlist = NULL;
        gint i, j;
        gchar **pp, *p, *segment_dir;
        glob_t pglob;
        gchar *pattern, *playlist;
        time_t time, shift_position;
        guint64 sequence;

        shift_position = g_get_real_time () / 1000000 - offset;
        /* loop seek time shift position, step: 10s */
        for (i = 0; i < 10; i++) {
                time = shift_position - i * 10;
                segment_dir = timestamp_to_segment_dir (time);
                pattern = g_strdup_printf ("%s/%s/%03lu*.ts", path, segment_dir, (time % 3600) / 10);
                g_free (segment_dir);
                if (glob (pattern, 0, NULL, &pglob) == 0) {
                        for (j = pglob.gl_pathc - 1; j >= 0; j--) {
                                p = &(pglob.gl_pathv[j][strlen (path) + 12]);
                                pp = g_strsplit (p, "_", 0);
                                if ((g_ascii_strtoull (pp[0], NULL, 10) / 1000000) <= (shift_position % 3600)) {
                                        /* sequence: g_ascii_strtoull (pp[1], NULL, 10) */
                                        sequence = g_ascii_strtoull (pp[1], NULL, 10);
                                        m3u8playlist = m3u8playlist_new (3, 3, sequence);
                                        /* remove .ts */
                                        pp[2][strlen (pp[2]) - 3] = '\0';
                                        p -= 11;
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
                time = shift_position + i * 10;
                segment_dir = timestamp_to_segment_dir (time);
                pattern = g_strdup_printf ("%s/%s/%03lu*.ts", path, segment_dir, (time % 3600) / 10);
                g_free (segment_dir);
                if (glob (pattern, 0, NULL, &pglob) == 0) {
                        for (j = 0; j < pglob.gl_pathc; j++) {
                                p = &(pglob.gl_pathv[j][strlen (path) + 12]);
                                pp = g_strsplit (p, "_", 0);
                                /* next segment? */
                                if (g_ascii_strtoull (pp[1], NULL, 10) != (sequence + 1)) {
                                        g_strfreev (pp);
                                        continue;
                                }
                                sequence += 1;
                                /* remove .ts */
                                pp[2][strlen (pp[2]) - 3] = '\0';
                                p -= 11;
                                m3u8playlist_add_entry (m3u8playlist, p, g_strtod ((pp[2]), NULL));
                                if (m3u8playlist->entries->length == 3) {
                                        g_strfreev (pp);
                                        break;
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

gchar * m3u8playlist_callback_get_playlist (gchar *path, gchar *parameters)
{
        gchar start[15], end[15], start_dir[11], end_dir[11];
        gint number, i;
        time_t start_time, end_time, time;
        guint64 start_min, start_sec, end_min, end_sec, start_us, end_us, us;
        gchar *segments_dir, *pattern, *format, *p, **pp;
        glob_t pglob;
        M3U8Playlist *m3u8playlist;

        number = sscanf (parameters, "start=%14s&end=%14s", start, end);
        if ((number != 2) || (strlen (start) != 14) || (strlen (end) != 14)) {
                GST_ERROR ("bad callback parameters: %s", parameters);
                return NULL;
        }

        number = sscanf (start, "%10s%02lu%02lu", start_dir, &start_min, &start_sec);
        if (number != 3) {
                GST_ERROR ("bad callback parameters: start=%s", start);
                return NULL;
        }
        if (segment_dir_to_timestamp (start_dir, &start_time) != 0) {
                GST_ERROR ("segment_dir_to_timestamp error, start_dir: %s", start_dir);
                return NULL;
        }

        number = sscanf (end, "%10s%02lu%02lu", end_dir, &end_min, &end_sec);
        if (number != 3) {
                GST_ERROR ("bad callback parameters: end=%s", end);
                return NULL;
        }
        if (segment_dir_to_timestamp (end_dir, &end_time) != 0) {
                GST_ERROR ("segment_dir_to_timestamp error, end_dir: %s", end_dir);
                return NULL;
        }

        m3u8playlist = m3u8playlist_new (3, 0, 0);
        for (time = start_time; time <= end_time; time += 3600) {
                if (time == start_time) {
                        start_us = start_min * 60000000 + start_sec * 1000000;

                } else {
                        start_us = 0;
                }

                if (time == end_time) {
                        end_us = end_min * 60000000 + end_sec * 1000000;

                } else {
                        end_us = 3600000000;
                }

                segments_dir = timestamp_to_segment_dir (time); 
                pattern = g_strdup_printf ("%s/%s/*", path, segments_dir);
                if (glob (pattern, 0, NULL, &pglob) == GLOB_NOMATCH) {
                        g_free (segments_dir);
                        continue;

                } else {
                        g_free (pattern);
                        format = g_strdup_printf ("%s/%s/%%lu_", path, segments_dir);
                        g_free (segments_dir);
                        for (i = 0; i < pglob.gl_pathc; i++) {
                                sscanf (pglob.gl_pathv[i], format, &us);
                                if ((us >= start_us) && (us <= end_us)) {
                                        p = &(pglob.gl_pathv[i][strlen (path) + 1]);
                                        pp = g_strsplit (p, "_", 0);
                                        /* remove .ts */
                                        pp[2][strlen (pp[2]) - 3] = '\0';
                                        m3u8playlist_add_entry (m3u8playlist, p, g_strtod ((pp[2]), NULL));
                                }
                        }
                        g_free (format);
                }
                globfree (&pglob);
        }
        p = g_strdup (m3u8playlist->playlist_str);
        m3u8playlist_free (m3u8playlist);

        return p;
}
