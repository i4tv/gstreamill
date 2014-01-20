/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <string.h>

#include "jobdesc.h"
#include "livejob.h"

/*
 * livejob_encoder_output_rap_timestamp:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr to get its timestamp
 *
 * get the timestamp of random access point of encoder_output.
 *
 * Returns: GstClockTime type timestamp.
 *
 */
GstClockTime livejob_encoder_output_rap_timestamp (EncoderOutput *encoder_output, guint64 rap_addr)
{
        GstClockTime timestamp;

        if (rap_addr + 8 <= encoder_output->cache_size) {
                memcpy (&timestamp, encoder_output->cache_addr + rap_addr, 8);

        } else {
                gint n;

                n = encoder_output->cache_size - rap_addr;
                memcpy (&timestamp, encoder_output->cache_addr + rap_addr, n);
                memcpy (&timestamp + n, encoder_output->cache_addr, 8 - n);
        }

        return timestamp;
}

/*
 * livejob_encoder_output_gop_size:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr
 *
 * get gop size.
 *
 * Returns: size of gop.
 *
 */
guint64 livejob_encoder_output_gop_size (EncoderOutput *encoder_output, guint64 rap_addr)
{
        gint gop_size;
        guint64 gop_size_addr;

        /* gop size address */
        if (rap_addr + 8 < encoder_output->cache_size) {
        	gop_size_addr = rap_addr + 8;

        } else {
                gop_size_addr = rap_addr + 8 - encoder_output->cache_size;
        }

        /* gop size */
        if (gop_size_addr + 4 < encoder_output->cache_size) {
                memcpy (&gop_size, encoder_output->cache_addr + gop_size_addr, 4);

        } else {
                gint n;

                n = encoder_output->cache_size - gop_size_addr;
                memcpy (&gop_size, encoder_output->cache_addr + gop_size_addr, n);
                memcpy (&gop_size + n, encoder_output->cache_addr, 4 - n);
        }

        return gop_size;
}

/*
 * livejob_encoder_output_rap_next:
 * @encoder_output: (in): the encoder output.
 * @rap_addr: (in): the rap addr
 *
 * get next random access address.
 *
 * Returns: next random access address.
 *
 */
guint64 livejob_encoder_output_rap_next (EncoderOutput *encoder_output, guint64 rap_addr)
{
        gint gop_size;
        guint64 next_rap_addr;

        /* gop size */
        gop_size = livejob_encoder_output_gop_size (encoder_output, rap_addr);

        /* next random access address */
        next_rap_addr = rap_addr + gop_size;
        if (next_rap_addr >= encoder_output->cache_size) {
                next_rap_addr -= encoder_output->cache_size;
        }

        return next_rap_addr;
}

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

