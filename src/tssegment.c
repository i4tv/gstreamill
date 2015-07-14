/*
 *  MPEGTS Segment Element
 *
 *  Mostly copy from /gst-plugins-bad/gst/mpegtsdemux
 */

#include <string.h>

#include "tssegment.h"

G_DEFINE_TYPE (TsSegment, ts_segment, GST_TYPE_ELEMENT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
                                                                     GST_PAD_SINK,
                                                                     GST_PAD_ALWAYS,
                                                                     GST_STATIC_CAPS ("video/mpegts"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
                                                                    GST_PAD_SRC,
                                                                    GST_PAD_ALWAYS,
                                                                    GST_STATIC_CAPS ("video/mpegts"));

static void ts_segment_dispose (GObject * object);
static void ts_segment_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void ts_segment_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static GstFlowReturn ts_segment_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

static void ts_segment_class_init (TsSegmentClass * klass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (klass);
        GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

        g_object_class->set_property = ts_segment_set_property;
        g_object_class->get_property = ts_segment_get_property;
        g_object_class->dispose = ts_segment_dispose;

        gst_element_class_set_static_metadata (element_class,
                                               "MPEGTS Segment plugin",
                                               "TSSegment/TSSegment",
                                               "MPEGTS Segment plugin",
                                               "Zhang Ping <zhangping@163.com>");

        gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_template));
        gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_template));
}

static void ts_segment_init (TsSegment *tssegment)
{
        tssegment->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
        gst_pad_set_chain_function (tssegment->sinkpad, GST_DEBUG_FUNCPTR(ts_segment_chain));
        gst_element_add_pad (GST_ELEMENT (tssegment), tssegment->sinkpad);

        tssegment->srcpad = gst_pad_new_from_static_template (&src_template, "src");
        gst_element_add_pad (GST_ELEMENT (tssegment), tssegment->srcpad);

        tssegment->is_pes = g_new0 (guint8, 1024);
        tssegment->known_psi = g_new0 (guint8, 1024);
        memset (tssegment->known_psi, 0, 1024);
        /* Known PIDs : PAT, TSDT, IPMP CIT */
        MPEGTS_BIT_SET (tssegment->known_psi, 0);
        MPEGTS_BIT_SET (tssegment->known_psi, 2);
        MPEGTS_BIT_SET (tssegment->known_psi, 3);
        /* TDT, TOT, ST */
        MPEGTS_BIT_SET (tssegment->known_psi, 0x14);
        /* network synchronization */
        MPEGTS_BIT_SET (tssegment->known_psi, 0x15);
        tssegment->seen_pat = FALSE;
        tssegment->seen_pmt = FALSE;

        tssegment->video_pid = 0;
        tssegment->seen_key_frame = FALSE;

        tssegment->allocated_size = 1024000; /* 1M */
        tssegment->data = g_malloc (tssegment->allocated_size);
        tssegment->current_size = 0;
        //tssegment->push_section = TRUE;

        tssegment->adapter = gst_adapter_new ();
        tssegment->offset = 0;
        tssegment->packet_size = 0;
        tssegment->map_data = NULL;
        tssegment->map_size = 0;
        tssegment->map_offset = 0;
        tssegment->need_sync = TRUE;

        tssegment->h264parser = gst_h264_nal_parser_new ();
}

static void ts_segment_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
}

static void ts_segment_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
}

static void ts_segment_dispose (GObject * object)
{
        TsSegment *tssegment;

        tssegment = TS_SEGMENT (object);
        g_free (tssegment->known_psi);
        g_object_unref (tssegment);
}

typedef enum {
        PACKET_BAD = 0,
        PACKET_OK = 1,
        PACKET_NEED_MORE
} TSPacketReturn;

static void flush_bytes (TsSegment *tssegment)
{
        gsize size;

        size = tssegment->map_offset;
        if (size > 0) {
                //GST_ERROR ("flushing %" G_GSIZE_FORMAT " bytes from adapter", size);
                gst_adapter_flush (tssegment->adapter, size);
        }

        tssegment->map_data = NULL;
        tssegment->map_size = 0;
        tssegment->map_offset = 0;
        tssegment->need_sync = TRUE;
}

static gboolean tssegment_map (TsSegment *tssegment, gsize size)
{
        gsize available;

        if (tssegment->map_size - tssegment->map_offset >= size) {
                return TRUE;
        }

        flush_bytes (tssegment);

        available = gst_adapter_available (tssegment->adapter);
        if (available < size) {
                return FALSE;
        }

        tssegment->map_data = (guint8 *) gst_adapter_map (tssegment->adapter, available);
        if (!tssegment->map_data) {
                return FALSE;
        }

        tssegment->map_size = available;
        tssegment->map_offset = 0;

        //GST_ERROR ("mapped %" G_GSIZE_FORMAT " bytes from adapter", available);

        return TRUE;
}

static gboolean try_discover_packet_size (TsSegment *tssegment)
{
        const guint8 *data;
        gsize size, i, j;

        static const guint psizes[] = {
                MPEGTS_NORMAL_PACKETSIZE,
                MPEGTS_M2TS_PACKETSIZE
                //MPEGTS_DVB_ASI_PACKETSIZE,
                //MPEGTS_ATSC_PACKETSIZE
        };

        tssegment->map_data = gst_adapter_map (tssegment->adapter, 4 * MPEGTS_MAX_PACKETSIZE);
        if (!tssegment->map_data) {
                return FALSE;
        }

        tssegment->map_size = gst_adapter_available (tssegment->adapter);
        if (tssegment->map_size < 4 * MPEGTS_MAX_PACKETSIZE) {
                return FALSE;
        }

        size = tssegment->map_size - tssegment->map_offset;
        data = tssegment->map_data + tssegment->map_offset;

        for (i = 0; i + 3 * MPEGTS_MAX_PACKETSIZE < size; i++) {
                /* find a sync byte */
                if (data[i] != PACKET_SYNC_BYTE) {
                        continue;
                }

                /* check for 4 consecutive sync bytes with each possible packet size */
                for (j = 0; j < G_N_ELEMENTS (psizes); j++) {
                        guint packet_size = psizes[j];

                        if (data[i + packet_size] == PACKET_SYNC_BYTE &&
                            data[i + 2 * packet_size] == PACKET_SYNC_BYTE &&
                            data[i + 3 * packet_size] == PACKET_SYNC_BYTE) {
                                tssegment->packet_size = packet_size;
                                goto out;
                        }
                }
        }

out:
        tssegment->map_offset += i;

        if (tssegment->packet_size == 0) {
                GST_ERROR ("Could not determine packet size in %" G_GSIZE_FORMAT
                           " bytes buffer, flush %" G_GSIZE_FORMAT " bytes", size, i);
                flush_bytes (tssegment);
                return FALSE;
        }

        GST_ERROR ("have packetsize detected: %u bytes", tssegment->packet_size);

        if (tssegment->packet_size == MPEGTS_M2TS_PACKETSIZE && tssegment->map_offset >= 4) {
                tssegment->map_offset -= 4;
        }

        return TRUE;
}

static gboolean ts_segment_sync (TsSegment *tssegment)
{
        gboolean found = FALSE;
        const guint8 *data;
        guint packet_size;
        gsize size, sync_offset, i;

        packet_size = tssegment->packet_size;

        if (!tssegment_map (tssegment, 3 * packet_size)) {
                return FALSE;
        }

        size = tssegment->map_size - tssegment->map_offset;
        data = tssegment->map_data + tssegment->map_offset;

        if (packet_size == MPEGTS_M2TS_PACKETSIZE) {
                sync_offset = 4;

        } else {
                sync_offset = 0;
        }
///GST_ERROR ("====%u size %ld, map_size: %ld, map_offset: %ld", packet_size, size, tssegment->map_size, tssegment->map_offset);
        for (i = sync_offset; i + 2 * packet_size < size; i++) {
///GST_ERROR ("====%u:%02x", packet_size, data[i]);
                if (data[i] == PACKET_SYNC_BYTE &&
                    data[i + packet_size] == PACKET_SYNC_BYTE &&
                    data[i + 2 * packet_size] == PACKET_SYNC_BYTE) {
                        found = TRUE;
                        break;
                }
        }

        tssegment->map_offset += i - sync_offset;

        if (!found) {
///GST_ERROR ("====%u", packet_size);
                flush_bytes (tssegment);
        }

        return found;
}

static inline guint64 compute_pcr (const guint8 * data)
{
        guint32 pcr1;
        guint16 pcr2;
        guint64 pcr, pcr_ext;

        pcr1 = GST_READ_UINT32_BE (data);
        pcr2 = GST_READ_UINT16_BE (data + 4);
        pcr = ((guint64) pcr1) << 1;
        pcr |= (pcr2 & 0x8000) >> 15;
        pcr_ext = (pcr2 & 0x01ff);
        return pcr * 300 + pcr_ext % 300;
}

static gboolean parse_adaptation_field_control (TsSegment *tssegment, TSPacket *packet)
{
        guint8 length, afcflags;
        const guint8 *data;

        length = *packet->data++;

        /* an adaptation field with length 0 is valid and
         * can be used to insert a single stuffing byte */
        if (!length) {
                packet->afc_flags = 0;
                return TRUE;
        }

        if ((packet->scram_afc_cc & 0x30) == 0x20) {
                /* no payload, adaptation field of 183 bytes */
                if (length > 183) {
                        GST_WARNING ("PID 0x%04x afc == 0x%02x and length %d > 183",
                                        packet->pid, packet->scram_afc_cc & 0x30, length);
                        return FALSE;
                }
                if (length != 183) {
                        GST_WARNING ("PID 0x%04x afc == 0x%02x and length %d != 183",
                                      packet->pid, packet->scram_afc_cc & 0x30, length);
                        GST_MEMDUMP ("Unknown payload", packet->data + length, packet->data_end - packet->data - length);
                }

        } else if (length > 182) {
                GST_WARNING ("PID 0x%04x afc == 0x%02x and length %d > 182", packet->pid, packet->scram_afc_cc & 0x30, length);
                return FALSE;
        }

        if (packet->data + length > packet->data_end) {
                GST_DEBUG ("PID 0x%04x afc length %d overflows the buffer current %d max %d",
                            packet->pid, length, (gint) (packet->data - packet->data_start),
                            (gint) (packet->data_end - packet->data_start));
                return FALSE;
        }

        data = packet->data;
        packet->data += length;

        afcflags = packet->afc_flags = *data++;

        GST_DEBUG ("flags: %s%s%s%s%s%s%s%s%s",
                    afcflags & 0x80 ? "discontinuity " : "",
                    afcflags & 0x40 ? "random_access " : "",
                    afcflags & 0x20 ? "elementary_stream_priority " : "",
                    afcflags & 0x10 ? "PCR " : "",
                    afcflags & 0x08 ? "OPCR " : "",
                    afcflags & 0x04 ? "splicing_point " : "",
                    afcflags & 0x02 ? "transport_private_data " : "",
                    afcflags & 0x01 ? "extension " : "", afcflags == 0x00 ? "<none>" : "");

        /* PCR */
        if (afcflags & MPEGTS_AFC_PCR_FLAG) {
                //MpegTSPCR *pcrtable = NULL;
                packet->pcr = compute_pcr (data);
///GST_ERROR (">>>>>>>>>>>>>>>>>>>>>>>>>pcr %lu", packet->pcr);
                data += 6;
                GST_DEBUG ("pcr 0x%04x %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT
                           ") offset:%" G_GUINT64_FORMAT, packet->pid, packet->pcr,
                            GST_TIME_ARGS (PCRTIME_TO_GSTTIME (packet->pcr)), packet->offset);
#if 0
                PACKETIZER_GROUP_LOCK (packetizer);
                if (packetizer->calculate_skew && GST_CLOCK_TIME_IS_VALID (packetizer->last_in_time)) {
                        pcrtable = get_pcr_table (packetizer, packet->pid);
                        calculate_skew (packetizer, pcrtable, packet->pcr, packetizer->last_in_time);
                }
                if (packetizer->calculate_offset) {
                        if (!pcrtable)
                                pcrtable = get_pcr_table (packetizer, packet->pid);
                        record_pcr (packetizer, pcrtable, packet->pcr, packet->offset);
                }
                PACKETIZER_GROUP_UNLOCK (packetizer);
#endif

        } else {
                packet->pcr = GST_CLOCK_TIME_NONE;
        }

        return TRUE;
}

static TSPacketReturn parse_packet (TsSegment *tssegment, TSPacket *packet)
{
        const guint8 *data;
        guint8 tmp;

        data = packet->data_start;
        data += 1;
        tmp = *data;

        /* transport_error_indicator 1 */
        if (G_UNLIKELY (tmp & 0x80)) {
                return PACKET_BAD;
        }

        /* payload_unit_start_indicator 1 */
        packet->payload_unit_start_indicator = tmp & 0x40;

        /* transport_priority 1 */
        /* PID 13 */
        packet->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
        data += 2;

        packet->scram_afc_cc = tmp = *data++;
        /* transport_scrambling_control 2 */
        if (G_UNLIKELY (tmp & 0xc0)) {
                return PACKET_BAD;
        }

        packet->data = data;

        packet->afc_flags = 0;
        packet->pcr = G_MAXUINT64;

        if (FLAGS_HAS_AFC (tmp)) {
                if (!parse_adaptation_field_control (tssegment, packet)) {
                        return FALSE;
                }
        }

        if (FLAGS_HAS_PAYLOAD (tmp)) {
                packet->payload = packet->data;

        } else {
                packet->payload = NULL;
        }

        return PACKET_OK;
}

static TSPacketReturn next_ts_packet (TsSegment *tssegment, TSPacket *packet)
{
        const guint8 *packet_data;
        guint packet_size;
        gsize sync_offset;

        packet_size = tssegment->packet_size;
        if (G_UNLIKELY (!packet_size)) {
                if (!try_discover_packet_size (tssegment)) {
                        return PACKET_NEED_MORE;
                }
                packet_size = tssegment->packet_size;
        }

////GST_ERROR ("====%u", packet_size);
        /* M2TS packets don't start with the sync byte, all other variants do */
        if (packet_size == MPEGTS_M2TS_PACKETSIZE) {
                sync_offset = 4;

        } else {
                sync_offset = 0;
        }

        while (1) {
                if (tssegment->need_sync) {
                        if (!ts_segment_sync (tssegment)) {
////GST_ERROR ("====%u", packet_size);
                                return PACKET_NEED_MORE;
                        }
                        tssegment->need_sync = FALSE;
                }

                if (!tssegment_map (tssegment, packet_size)) {
GST_ERROR ("====%u", packet_size);
                        return PACKET_NEED_MORE;
                }

                packet_data = &tssegment->map_data[tssegment->map_offset + sync_offset];

                /* Check sync byte */
                if (G_UNLIKELY (*packet_data != PACKET_SYNC_BYTE)) {
                        GST_ERROR ("lost sync");
                        tssegment->need_sync = TRUE;

                } else {
                        /* ALL mpeg-ts variants contain 188 bytes of data. Those with bigger
                         * packet sizes contain either extra data (timesync, FEC, ..) either
                         * before or after the data */
                        packet->data_start = packet_data;
                        packet->data_end = packet->data_start + 188;
                        packet->offset = tssegment->offset;
                        GST_DEBUG ("offset %" G_GUINT64_FORMAT, packet->offset);
                        tssegment->offset += packet_size;
                        GST_MEMDUMP ("data_start", packet->data_start, 16);
                        return parse_packet (tssegment, packet);
                }
        }
}

static GstMpegTsSection *push_section (TsSegment *tssegment, TSPacket *packet)
{
        GstMpegTsSection *section;
        const guint8 *data;
        guint8 pointer = 0;
        guint section_length;

        data = packet->data;
        pointer = *data++;
        data += pointer;
        section_length = (GST_READ_UINT16_BE (data + 1) & 0xfff) + 3;
        section = gst_mpegts_section_new (packet->pid, g_memdup (data, section_length), section_length);

        return section;
}

typedef struct
{
        gboolean res;
        guint16 pid;
} PIDLookup;

static gboolean apply_pat (TsSegment *tssegment, GstMpegTsSection * section)
{
        GPtrArray *pat;
        GstMpegTsPatProgram *patp;
        //gint i;

        pat = gst_mpegts_section_get_pat (section);
        if (G_UNLIKELY (pat == NULL)) {
                return FALSE;
        }
        tssegment->pat = pat;
        patp = g_ptr_array_index (pat, 0);

        tssegment->program_number = patp->program_number;
        tssegment->pmt_pid = patp->network_or_program_map_PID;
        MPEGTS_BIT_SET (tssegment->known_psi, patp->network_or_program_map_PID);

        return TRUE;
}

static gboolean apply_pmt (TsSegment *tssegment, GstMpegTsSection * section)
{
        const GstMpegTsPMT *pmt;
        gint i;

        pmt = gst_mpegts_section_get_pmt (section);
        if (G_UNLIKELY (pmt == NULL)) {
                GST_ERROR ("Could not get PMT (corrupted ?)");
                return FALSE;
        }

        /* FIXME : not so sure this is valid anymore */
        if (G_UNLIKELY (tssegment->seen_pat == FALSE)) {
                GST_WARNING ("Got pmt without pat first. Returning");
                return TRUE;
        }


        /* activate program */
        /* Ownership of pmt_info is given to the program */
        //activate_program (tssegment, program, section->pid, section, pmt, initial_program);
        tssegment->pmt = pmt;
        for (i = 0; i < pmt->streams->len; ++i) {
                GstMpegTsPMTStream *stream = g_ptr_array_index (pmt->streams, i);
                if (stream->stream_type == GST_MPEG_TS_STREAM_TYPE_VIDEO_H264) {
                        MPEGTS_BIT_SET (tssegment->is_pes, stream->pid);
                        tssegment->video_pid = stream->pid;
                        GST_ERROR ("264: %d, stream pid: %d", stream->pid, tssegment->video_pid);
                }
        }

        return TRUE;
}

static void handle_psi (TsSegment *tssegment, TSPacket *packet)
{
        GstMpegTsSection *section;
        gboolean post_message = TRUE;

        section = push_section (tssegment, packet);
        if (section == NULL) {
                return;
        }

        if ((section->section_type == GST_MPEGTS_SECTION_PAT) && tssegment->seen_pat){
                gst_mpegts_section_unref (section);
                return;
        }

        if ((section->section_type == GST_MPEGTS_SECTION_PMT) && tssegment->seen_pmt){
                gst_mpegts_section_unref (section);
                return;
        }

        GST_ERROR ("Handling PSI (pid: 0x%04x , table_id: 0x%02x)", section->pid, section->table_id);

        switch (section->section_type) {
                case GST_MPEGTS_SECTION_PAT:
                        post_message = apply_pat (tssegment, section);
                        memcpy (tssegment->pat_packet, packet->data_start, 188);
                        tssegment->seen_pat = TRUE;
                        //GST_ERROR ("First PAT offset: %" G_GUINT64_FORMAT, section->offset);
                        break;
                case GST_MPEGTS_SECTION_PMT:
                        post_message = apply_pmt (tssegment, section);
                        memcpy (tssegment->pmt_packet, packet->data_start, 188);
                        tssegment->seen_pmt = TRUE;
                        break;
                default:
                        break;
        }

        GST_ERROR ("Handling PSI (pid: 0x%04x , table_id: 0x%02x)", section->pid, section->table_id);

        /* Finally post message (if it wasn't corrupted) */
        if (post_message) {
                gst_element_post_message (GST_ELEMENT_CAST (tssegment), gst_message_new_mpegts_section (GST_OBJECT (tssegment), section));
        }

        gst_mpegts_section_unref (section);
}

static void clear_packet (TsSegment *tssegment, TSPacket *packet)
{
        guint8 packet_size = tssegment->packet_size;

        if (tssegment->map_data) {
                tssegment->map_offset += packet_size;
                if (tssegment->map_size - tssegment->map_offset < packet_size) {
                        flush_bytes (tssegment);
                }
        }
}

static gboolean is_key_frame (TsSegment *tssegment, TSPacket *packet)
{
        GstH264ParserResult res = GST_H264_PARSER_OK;
        GstH264NalParser *parser = tssegment->h264parser;
        const guint8 * data;
        gsize size;
        gint offset = 0;
        GstH264NalUnit unit;
        GstH264SliceHdr slice;
        gboolean is_key = FALSE;

        GST_DEBUG ("pid 0x%04x pusi:%d, afc:%d, cont:%d, payload:%p", packet->pid,
                  packet->payload_unit_start_indicator, packet->scram_afc_cc & 0x30,
                  FLAGS_CONTINUITY_COUNTER (packet->scram_afc_cc), packet->payload);

        size = packet->data_end - packet->payload;
        data = packet->payload;

        while (res == GST_H264_PARSER_OK) {
                res = gst_h264_parser_identify_nalu (parser, data, offset, size, &unit);

                if (res != GST_H264_PARSER_OK && res != GST_H264_PARSER_NO_NAL_END) {
                        GST_INFO ("Error identifying nalu: %i", res);
                        break;
                }

                res = gst_h264_parser_parse_nal (parser, &unit);
                if (res != GST_H264_PARSER_OK) {
                        GST_WARNING ("Error identifying nal: %i", res);
                        break;
                }

                switch (unit.type) {
                case GST_H264_NAL_SEI:
                        GST_DEBUG ("Found SEI");
                        break;
                case GST_H264_NAL_PPS:
                        GST_DEBUG ("Found PPS");
                        break;
                case GST_H264_NAL_SPS:
                        GST_DEBUG ("Found SPS");
                        break;
                /* these units are considered keyframes in h264parse */
                case GST_H264_NAL_SLICE:
                case GST_H264_NAL_SLICE_DPA:
                case GST_H264_NAL_SLICE_DPB:
                case GST_H264_NAL_SLICE_DPC:
                case GST_H264_NAL_SLICE_IDR:
                        res = gst_h264_parser_parse_slice_hdr (parser, &unit, &slice, FALSE, FALSE);
                        if ((GST_H264_IS_I_SLICE (&slice) || GST_H264_IS_SI_SLICE (&slice)) &&
                            (*(unit.data + unit.offset + 1) & 0x80)) {
                                /* means first_mb_in_slice == 0 */
                                /* real frame data */
                                GST_DEBUG ("Found keyframe at: %u",unit.sc_offset);
                                is_key = TRUE;
                        }
                }

                if (offset == unit.sc_offset + unit.size) {
                        break;
                }
                offset = unit.sc_offset + unit.size;
        }

        return is_key;
}

static void pending_packet (TsSegment *tssegment, TSPacket *packet)
{
        const guint8 *data;
        guint size;

        size = packet->data_end - packet->data_start;
        data = packet->data_start;

        if (G_UNLIKELY (tssegment->current_size + size > tssegment->allocated_size)) {
                GST_DEBUG ("resizing buffer");
                do {
                        tssegment->allocated_size *= 2;
                } while (tssegment->current_size + size > tssegment->allocated_size);
                tssegment->data = g_realloc (tssegment->data, tssegment->allocated_size);
        }
////GST_ERROR ("size: %u current size: %u, allocated size : %u, data: %p, datastart: %p", size, tssegment->current_size, tssegment->allocated_size, tssegment->data, data);
        memcpy (tssegment->data + tssegment->current_size, data, size);
        tssegment->current_size += size;
}

static void tspacket_cc_inc (guint8 *tspacket)
{
        guint8 *buf = tspacket + 3;
        guint8 cc = (*buf + 1) & 0x0f;

        *buf &= 0xf0;
        *buf |= cc;
}

static void pushing_data (TsSegment *tssegment)
{
        guint8 *data;
        GstBuffer *buffer;

        if (tssegment->current_size == 0) {
                return;
        }

        data = g_malloc (tssegment->current_size + 188*2);
        tspacket_cc_inc (tssegment->pat_packet);
        memcpy (data, tssegment->pat_packet, 188);
        tspacket_cc_inc (tssegment->pmt_packet);
        memcpy (data + 188, tssegment->pmt_packet, 188);
        memcpy (data + 188*2, tssegment->data, tssegment->current_size);
        buffer = gst_buffer_new_wrapped (data, tssegment->current_size + 188*2);
        GST_BUFFER_PTS (buffer) = MPEGTIME_TO_GSTTIME (tssegment->pre_pts);
        GST_BUFFER_DURATION (buffer) = MPEGTIME_TO_GSTTIME (tssegment->duration);
        GST_ERROR ("pushing %u data timestamp: %lu, duration: %lu", tssegment->current_size, GST_BUFFER_PTS (buffer), MPEGTIME_TO_GSTTIME (tssegment->duration));
        gst_pad_push (tssegment->srcpad, buffer);
        tssegment->current_size = 0;
}

static void segment_duration (TsSegment *tssegment, TSPacket *packet)
{
        if (tssegment->pre_pts < tssegment->current_pts) {
                tssegment->duration = tssegment->current_pts - tssegment->pre_pts;

        } else {
                tssegment->duration = MAX_MPEGTIME - tssegment->pre_pts + tssegment->current_pts;
        }
}

/**
 * mpegts_parse_pes_header:
 * @data: data to parse (starting from, and including, the sync code)
 * @length: size of @data in bytes
 *
 * Parses the mpeg-ts PES header located in @data into the @res.
 *
 * Returns: #PES_PARSING_OK if the header was fully parsed and valid,
 * #PES_PARSING_BAD if the header is invalid, or #PES_PARSING_NEED_MORE if more data
 * is needed to properly parse the header.
 */
PESParsingResult mpegts_parse_pes_header (TsSegment *tssegment, const guint8 * data, gsize length)
{
        PESParsingResult ret = PES_PARSING_NEED_MORE;
        guint32 val32, packet_length;
        guint8 val8, flags, stream_id;

        /* The smallest valid PES header is 6 bytes (prefix + stream_id + length) */
        if (G_UNLIKELY (length < 6)) {
                GST_DEBUG ("Not enough data to parse PES header");
                return ret;
        }

        val32 = GST_READ_UINT32_BE (data);
        data += 4;
        length -= 4;
        if (G_UNLIKELY ((val32 & 0xffffff00) != 0x00000100)) {
                GST_WARNING ("Wrong packet start code 0x%x != 0x000001xx", val32);
                return PES_PARSING_BAD;
        }

        stream_id = val32 & 0x000000ff;
        if (stream_id != 0xE0) {
                GST_WARNING ("Not H.264 video PES packet");
                return PES_PARSING_BAD;
        }

        packet_length = GST_READ_UINT16_BE (data);
        if (packet_length) {
                packet_length += 6;
        }
        data += 2;
        length -= 2;

        GST_LOG ("stream_id : 0x%08x , packet_length : %d", stream_id, packet_length);

        if (G_UNLIKELY (length < 3)) {
                GST_DEBUG ("Not enough data to parse PES header");
                return ret;
        }

        /* '10'                             2
         * PES_scrambling_control           2
         * PES_priority                     1
         * data_alignment_indicator         1
         * copyright                        1
         * original_or_copy                 1 */
        val8 = *data++;
        if (G_UNLIKELY ((val8 & 0xc0) != 0x80)) {
                GST_WARNING ("Wrong '0x10' marker before PES_scrambling_control (0x%02x)", val8);
                return PES_PARSING_BAD;
        }

        GST_LOG ("scrambling_control 0x%0x", (val8 >> 4) & 0x3);
        GST_LOG ("flags_1: %s%s%s%s%s",
                        val8 & 0x08 ? "priority " : "",
                        val8 & 0x04 ? "data_alignment " : "",
                        val8 & 0x02 ? "copyright " : "",
                        val8 & 0x01 ? "original_or_copy " : "", val8 & 0x0f ? "" : "<none>");

        /* PTS_DTS_flags                    2
         * ESCR_flag                        1
         * ES_rate_flag                     1
         * DSM_trick_mode_flag              1
         * additional_copy_info_flag        1
         * PES_CRC_flag                     1
         * PES_extension_flag               1*/
        flags = *data++;
        GST_DEBUG ("flags_2: %s%s%s%s%s%s%s%s%s",
                        flags & 0x80 ? "PTS " : "",
                        flags & 0x40 ? "DTS " : "",
                        flags & 0x20 ? "ESCR" : "",
                        flags & 0x10 ? "ES_rate " : "",
                        flags & 0x08 ? "DSM_trick_mode " : "",
                        flags & 0x04 ? "additional_copy_info " : "",
                        flags & 0x02 ? "CRC " : "",
                        flags & 0x01 ? "extension " : "", flags ? "" : "<none>");

        /* PES_header_data_length           8 */
        length -= 3;
        if (G_UNLIKELY (length < *data++)) {
                GST_DEBUG ("Not enough data to parse PES header");
                return ret;
        }

        /* PTS_DTS_flags == 0x01 is invalid */
        if (G_UNLIKELY ((flags >> 6) == 0x01)) {
                GST_WARNING ("Invalid PTS_DTS_flag (0x01 is forbidden)");
        }

        if ((flags & 0x80) == 0x80) {
                /* PTS */
                guint64 PTS;

                if (G_UNLIKELY (length < 5)) {
                        GST_DEBUG ("Not enough data to parse PES header");
                        return ret;
                }

                if ((*data & 0x01) != 0x01) {
                        GST_WARNING ("bad PTS value");
                        return PES_PARSING_BAD;
                }
                PTS = ((guint64) (*data++ & 0x0E)) << 29;
                PTS |= ((guint64) (*data++)) << 22;
                if ((*data & 0x01) != 0x01) {
                        GST_WARNING ("bad PTS value");
                        return PES_PARSING_BAD;
                }
                PTS |= ((guint64) (*data++ & 0xFE)) << 14;
                PTS |= ((guint64) (*data++)) << 7;
                if ((*data & 0x01) != 0x01) {
                        GST_WARNING ("bad PTS value");
                        return PES_PARSING_BAD;
                }
                PTS |= ((guint64) (*data++ & 0xFE)) >> 1;
                GST_DEBUG ("PTS %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
                            PTS, GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (PTS)));
                tssegment->current_pts = PTS;
        }

        return ret;
}

static GstFlowReturn ts_segment_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
        TsSegment *tssegment;
        GstAdapter *adapter;
        GstFlowReturn res = GST_FLOW_OK;
        TSPacket packet;
        TSPacketReturn ret;

        tssegment = TS_SEGMENT (parent);
        adapter = tssegment->adapter;
        gst_adapter_push (adapter, buf);

        while (res == GST_FLOW_OK) {
                ret = next_ts_packet (tssegment, &packet);

                if (G_UNLIKELY (ret == PACKET_NEED_MORE)) {
                        break;
                }

                if (G_UNLIKELY (ret == PACKET_BAD)) {
                        /* bad header, skip the packet */
                        GST_WARNING ("bad packet, skipping");
                        clear_packet (tssegment, &packet);
                        continue;
                }

                if (packet.payload && MPEGTS_BIT_IS_SET (tssegment->known_psi, packet.pid)) {
                        handle_psi (tssegment, &packet);

                } else if ((packet.pid == tssegment->video_pid) &&
                           G_LIKELY (packet.payload_unit_start_indicator) &&
                           FLAGS_HAS_PAYLOAD (packet.scram_afc_cc)) {
                        if (is_key_frame (tssegment, &packet)) {
                                mpegts_parse_pes_header (tssegment, packet.payload, 184);
                                /* push a segment downstream */
                                if (tssegment->seen_key_frame) {
                                        segment_duration (tssegment, &packet);
                                        pushing_data (tssegment);
                                        tssegment->pre_pts = tssegment->current_pts;

                                } else {
                                        tssegment->seen_key_frame = TRUE;
                                        tssegment->pre_pts = tssegment->current_pts;
                                }
                        }
                        /* push packet to segment */
                        pending_packet (tssegment, &packet);

                } else if (tssegment->seen_key_frame) {
                        /* push packet to segment if have seen key frame */
                        pending_packet (tssegment, &packet);
                }
                clear_packet (tssegment, &packet);
        }

        return res;
}

gboolean ts_segment_plugin_init (GstPlugin * plugin)
{
        return gst_element_register (plugin, "tssegment", GST_RANK_NONE, TYPE_TS_SEGMENT);
}
