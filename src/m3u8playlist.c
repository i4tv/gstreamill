/*
 * m3u8 playlist
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <gst/gst.h>

#include "m3u8playlist.h"

M3U8Playlist * m3u8playlist_new (guint version, guint window_size, gboolean allow_cache)
{
        M3U8Playlist *playlist;

        playlist = g_new0 (M3U8Playlist, 1);
        playlist->version = version;
        playlist->window_size = window_size;
        playlist->allow_cache = allow_cache;
        playlist->entries = g_queue_new ();

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
        g_return_if_fail (playlist != NULL);

        g_queue_foreach (playlist->entries, (GFunc) m3u8entry_free, NULL);
        g_queue_free (playlist->entries);
        g_free (playlist);
}

static M3U8Entry * m3u8entry_new (const gchar * url, gfloat duration)
{
        M3U8Entry *entry;

        g_return_val_if_fail (url != NULL, NULL);

        entry = g_new0 (M3U8Entry, 1);
        entry->url = g_strdup (url);
        entry->duration = duration;

        return entry;
}

gboolean m3u8playlist_add_entry (M3U8Playlist *playlist, const gchar *url, gfloat duration)
{
        M3U8Entry *entry;

        g_return_val_if_fail (playlist != NULL, FALSE);
        g_return_val_if_fail (url != NULL, FALSE);

        entry = m3u8entry_new (url, duration);

        if (playlist->window_size != -1) {
                /* Delete old entries from the playlist */
                while (playlist->entries->length >= playlist->window_size) {
                        M3U8Entry *old_entry;

                        old_entry = g_queue_pop_head (playlist->entries);
                        m3u8entry_free (old_entry);
                }
        }

        playlist->sequence_number++;;
        g_queue_push_tail (playlist->entries, entry);

        return TRUE;
}

static void render_entry (M3U8Entry * entry, M3U8Playlist * playlist)
{
        gchar *entry_str;

        g_return_val_if_fail (entry != NULL, NULL);

        entry_str = g_strdup_printf (M3U8_INF_TAG, (float) entry->duration / GST_SECOND, entry->url);
        g_string_append_printf (playlist->playlist_str, "%s", entry_str);
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

gchar * m3u8playlist_render (M3U8Playlist * playlist)
{
        gchar *pl;

        g_return_val_if_fail (playlist != NULL, NULL);

        playlist->playlist_str = g_string_new ("");

        /* #EXTM3U */
        g_string_append_printf (playlist->playlist_str, M3U8_HEADER_TAG);
        /* #EXT-X-VERSION */
        g_string_append_printf (playlist->playlist_str, M3U8_VERSION_TAG, playlist->version);
        /* #EXT-X-ALLOW_CACHE */
        g_string_append_printf (playlist->playlist_str, M3U8_ALLOW_CACHE_TAG, playlist->allow_cache ? "YES" : "NO");
        /* #EXT-X-MEDIA-SEQUENCE */
        g_string_append_printf (playlist->playlist_str, M3U8_MEDIA_SEQUENCE_TAG, playlist->sequence_number - playlist->entries->length);
        /* #EXT-X-TARGETDURATION */
        g_string_append_printf (playlist->playlist_str, M3U8_TARGETDURATION_TAG, m3u8playlist_target_duration (playlist));
        g_string_append_printf (playlist->playlist_str, "\n");

        /* Entries */
        g_queue_foreach (playlist->entries, (GFunc) render_entry, playlist);

        pl = playlist->playlist_str->str;
        g_string_free (playlist->playlist_str, FALSE);

        return pl;
}

