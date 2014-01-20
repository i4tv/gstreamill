/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include "jobdesc.h"
#include "livejob.h"

/*
 * livejob_master_m3u8_playlist:
 * @livejob: (in): the livejob object
 *
 * get master m3u8 playlist of the livejob
 *
 * Returns: master m3u8 playlist
 *
 */
gchar * livejob_get_master_m3u8_playlist (LiveJob *livejob)
{
        GString *master_m3u8_playlist;
        gchar *p, *value;
        gint i;

        if (!jobdesc_m3u8streaming (livejob->job)) {
                /* m3u8streaming no enabled */
                return "not found";
        }

        master_m3u8_playlist = g_string_new ("");
        g_string_append_printf (master_m3u8_playlist, M3U8_HEADER_TAG);
        if (jobdesc_m3u8streaming_version (livejob->job) == 0) {
                g_string_append_printf (master_m3u8_playlist, M3U8_VERSION_TAG, 3);

        } else {
                g_string_append_printf (master_m3u8_playlist, M3U8_VERSION_TAG, jobdesc_m3u8streaming_version (livejob->job));
        }

        for (i = 0; i < livejob->output->encoder_count; i++) {
                p = g_strdup_printf ("encoder.%d.elements.x264enc.property.bitrate", i);
                value = jobdesc_element_property_value (livejob->job, p);
                g_string_append_printf (master_m3u8_playlist, M3U8_STREAM_INF_TAG, 1, value);
                g_string_append_printf (master_m3u8_playlist, "encoder/%d/playlist.m3u8\n", i);
                g_free (p);
                g_free (value);
        }

        p = master_m3u8_playlist->str;
        g_string_free (master_m3u8_playlist, FALSE);

        return p;
}

