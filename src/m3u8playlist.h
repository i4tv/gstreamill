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

typedef struct _M3U8Entry
{
        gfloat duration;
        gchar *url;
} M3U8Entry;

typedef struct _M3U8Playlist
{
        guint version;
        gboolean allow_cache;
        gint window_size;
        guint64 sequence_number;

        /*< Private >*/
        GQueue *entries;
        GString *playlist_str;
} M3U8Playlist;

M3U8Playlist * m3u8playlist_new (guint version, guint window_size, gboolean allow_cache);
void m3u8playlist_free (M3U8Playlist *playlist);
gchar * m3u8playlist_add_entry (M3U8Playlist *playlist, const gchar *url, gfloat duration);
gchar * m3u8playlist_render (M3U8Playlist *playlist); 

#endif /* __M3U8PLAYLIST_H__ */
