/*
 *  livejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __LIVEJOB_H__
#define __LIVEJOB_H__

#include "job.h"

gint livejob_initialize (LiveJob *livejob, gboolean daemon);
GstClockTime livejob_encoder_output_rap_timestamp (EncoderOutput *encoder_output, guint64 rap_addr);
guint64 livejob_encoder_output_gop_size (EncoderOutput *encoder_output, guint64 rap_addr);
guint64 livejob_encoder_output_rap_next (EncoderOutput *encoder_output, guint64 rap_addr);
gchar * livejob_get_master_m3u8_playlist (LiveJob *livejob);

#endif /* __LIVEJOB_H__ */
