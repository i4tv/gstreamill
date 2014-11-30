/*
 * m3u8 playlist
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#ifndef __M3U8PLAYLIST_H__
#define __M3U8PLAYLIST_H__

#define M3U8_HEADER_TAG "#EXTM3U\n"
#define M3U8_VERSION_TAG "#EXT-X-VERSION:%d\n"
#define M3U8_ALLOW_CACHE_TAG "#EXT-X-ALLOW-CACHE:%s\n"
#define M3U8_TARGETDURATION_TAG "#EXT-X-TARGETDURATION:%d\n"
#define M3U8_MEDIA_SEQUENCE_TAG "#EXT-X-MEDIA-SEQUENCE:%lu\n"
#define M3U8_INF_TAG "#EXTINF:%.2f,\n%s\n"
#define M3U8_STREAM_INF_TAG "#EXT-X-STREAM-INF:PROGRAM-ID=%d,BANDWIDTH=%s000\n"
#define M3U8_X_ENDLIST_TAG "#EXT-X-ENDLIST\n"

typedef struct _M3U8Entry
{
        GstClockTime duration;
        gchar *url;
} M3U8Entry;

typedef struct _M3U8Playlist
{
        GRWLock lock;
        guint version;
        gint window_size;
        guint64 sequence_number;

        /*< Private >*/
        GQueue *adding_entries;
        GQueue *entries;
        gchar *playlist_str;
} M3U8Playlist;

M3U8Playlist * m3u8playlist_new (guint version, guint window_size, guint64 sequence);
void m3u8playlist_free (M3U8Playlist *playlist);
gint m3u8playlist_adding_entry (M3U8Playlist *playlist, const gchar *url, GstClockTime duration);
GstClockTime m3u8playlist_add_entry (M3U8Playlist *playlist);
gchar * m3u8playlist_live_get_playlist (M3U8Playlist *playlist); 
gchar * m3u8playlist_timeshift_get_playlist (gchar *path, gint64 offset); 
gchar * m3u8playlist_dvr_get_playlist (gchar *path, gint64 start, gint64 duration); 

#endif /* __M3U8PLAYLIST_H__ */
