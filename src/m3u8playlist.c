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

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

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
    guint64 sequence;
    gint number;

    g_rw_lock_writer_lock (&(playlist->lock));

    /* Delete old entries from the playlist */
    while ((playlist->window_size != 0) && (playlist->entries->length >= playlist->window_size)) {
        entry = g_queue_pop_head (playlist->entries);
        m3u8entry_free (entry);
    }

    /* add entry */
    entry = m3u8entry_new (url, duration);
    number = sscanf (url, "%*[^/]/%lu.ts$", &sequence);
    if (number == 1) {
        playlist->sequence_number = sequence;
    }
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

gchar * m3u8playlist_timeshift_get_playlist (gchar *path, guint64 duration, guint version, guint window_size, time_t shift_position)
{
    M3U8Playlist *m3u8playlist = NULL;
    gint i;
    gchar *playlist, *segment_dir, *p;
    time_t time;
    guint64 sequence;

    sequence = shift_position / (duration / GST_SECOND);
    m3u8playlist = m3u8playlist_new (version, window_size, sequence);
    for (i = 0; i < window_size; i++) {
        time = shift_position + i * (duration / GST_SECOND);
        segment_dir = timestamp_to_segment_dir (time);
        sequence = time / (duration / GST_SECOND);
        p = g_strdup_printf ("%s/%lu.ts", segment_dir, sequence);
        m3u8playlist_add_entry (m3u8playlist, p, duration);
        g_free (segment_dir);
        g_free (p);
    }

    playlist = g_strdup (m3u8playlist->playlist_str);
    m3u8playlist_free (m3u8playlist);

    return playlist;
}

gchar * m3u8playlist_callback_get_playlist (gchar *path, guint64 duration, guint64 dvr_duration, gchar *start, gchar *end)
{
    gchar start_dir[11], end_dir[11];
    gint number;
    time_t start_time, end_time, time;
    guint64 start_min, start_sec, end_min, end_sec, sequence;
    gchar *segment_dir, *p;
    GString *gstring;

    time = g_get_real_time () / 1000000;
    number = sscanf (start, "%10s%02lu%02lu", start_dir, &start_min, &start_sec);
    if (number != 3) {
        GST_ERROR ("bad callback parameters: start=%s", start);
        return NULL;
    }
    if (segment_dir_to_timestamp (start_dir, &start_time) != 0) {
        GST_ERROR ("segment_dir_to_timestamp error, start_dir: %s", start_dir);
        return NULL;
    }
    if ((start_min > 59) || (start_sec > 59)) {
        GST_ERROR ("Error start_min: %ld or start_sec: %ld", start_min, start_sec);
        return NULL;
    }
    if (((time - dvr_duration) > start_time) || (start_time + start_min * 60 + start_sec > time)) {
        GST_ERROR ("callback request exceed dvr duration, start_time: %ld", start_time);
        return NULL;
    }
    start_time += start_min * 60 + start_sec;

    number = sscanf (end, "%10s%02lu%02lu", end_dir, &end_min, &end_sec);
    if (number != 3) {
        GST_ERROR ("bad callback parameters: end=%s", end);
        return NULL;
    }
    if (segment_dir_to_timestamp (end_dir, &end_time) != 0) {
        GST_ERROR ("segment_dir_to_timestamp error, end_dir: %s", end_dir);
        return NULL;
    }
    if ((end_min > 59) || (end_sec > 59)) {
        GST_ERROR ("Error end_min: %ld or end_sec: %ld", end_min, end_sec);
        return NULL;
    }
    if (((time - dvr_duration) > end_time) || (end_time + end_min * 60 + end_sec > time)) {
        GST_ERROR ("callback request exceed dvr duration, end_time: %ld", end_time);
        return NULL;
    }
    end_time += end_min * 60 + end_sec;

    gstring = g_string_new ("");
    for (time = start_time; time <= end_time; time += duration / GST_SECOND) {
        segment_dir = timestamp_to_segment_dir (time); 
        sequence = time / (duration / GST_SECOND);
        p = g_strdup_printf ("%s/%lu.ts", segment_dir, sequence);
        g_string_append_printf (gstring, M3U8_INF_TAG, (float)duration / GST_SECOND, p);
        g_free (p);
        g_free (segment_dir);
    }

    p = gstring->str;
    g_string_free (gstring, FALSE);
    gstring = g_string_new ("");
    g_string_append_printf (gstring, M3U8_HEADER_TAG);
    g_string_append_printf (gstring, M3U8_VERSION_TAG, 3);
    g_string_append_printf (gstring, M3U8_ALLOW_CACHE_TAG, "YES");
    g_string_append_printf (gstring, M3U8_TARGETDURATION_TAG, duration / GST_SECOND);
    g_string_append_printf (gstring, "\n");
    g_string_append_printf (gstring, "%s", p);
    g_free (p);
    g_string_append_printf (gstring, M3U8_X_ENDLIST_TAG);
    p = gstring->str;
    g_string_free (gstring, FALSE);

    return p;
}
