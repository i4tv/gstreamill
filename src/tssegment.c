/*
 *  MPEGTS Segment Element
 *
 *  Mostly copy from /gst-plugins-bad/gst/mpegtsdemux
 */

#include <string.h>

#include "tssegment.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

G_DEFINE_TYPE (TsSegment, ts_segment, GST_TYPE_ELEMENT);

enum {
    TSSEGMENT_PROP_0,
    TSSEGMENT_PROP_BITRATE,
};

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
    GParamSpec *param;

    g_object_class->set_property = ts_segment_set_property;
    g_object_class->get_property = ts_segment_get_property;
    g_object_class->dispose = ts_segment_dispose;

    param = g_param_spec_int64 (
            "bitrate",
            "bitratef",
            "stream bitrate",
            1,
            256,
            10,
            G_PARAM_WRITABLE | G_PARAM_READABLE
            );
    g_object_class_install_property (g_object_class, TSSEGMENT_PROP_BITRATE, param);

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

    tssegment->allocated_size = 1024000; /* 1M */
    tssegment->data = g_malloc (tssegment->allocated_size);
    tssegment->current_size = 0;
    tssegment->frames_accumulate = 0;

    tssegment->adapter = gst_adapter_new ();
    tssegment->offset = 0;
    tssegment->packet_size = 0;
    tssegment->map_data = NULL;
    tssegment->map_size = 0;
    tssegment->map_offset = 0;
    tssegment->need_sync = TRUE;

    tssegment->seen_idr = FALSE;
    tssegment->pes_packet = g_malloc (4096000);
    tssegment->pes_packet_size = 0;
    tssegment->pes_packet_duration = 0;
    tssegment->h264parser = gst_h264_nal_parser_new ();
}

static void ts_segment_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    g_return_if_fail (IS_TS_SEGMENT (obj));

    switch (prop_id) {
        case TSSEGMENT_PROP_BITRATE:
            TS_SEGMENT (obj)->bitrate = g_value_get_int64 (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
}

static void ts_segment_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
    TsSegment *tssegment = TS_SEGMENT (obj);

    switch (prop_id) {
        case TSSEGMENT_PROP_BITRATE:
            g_value_set_int64 (value, tssegment->bitrate);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
            break;
    }
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

    return TRUE;
}

static gboolean try_discover_packet_size (TsSegment *tssegment)
{
    const guint8 *data;
    gsize size, i, j;

    static const guint psizes[] = {
        MPEGTS_NORMAL_PACKETSIZE,
        MPEGTS_M2TS_PACKETSIZE
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

    for (i = sync_offset; i + 2 * packet_size < size; i++) {
        if (data[i] == PACKET_SYNC_BYTE &&
                data[i + packet_size] == PACKET_SYNC_BYTE &&
                data[i + 2 * packet_size] == PACKET_SYNC_BYTE) {
            found = TRUE;
            break;
        }
    }

    tssegment->map_offset += i - sync_offset;

    if (!found) {
        flush_bytes (tssegment);
    }

    return found;
}

static gboolean parse_adaptation_field_control (TsSegment *tssegment, TSPacket *packet)
{
    guint8 length;

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
        GST_WARNING ("PID 0x%04x afc length %d overflows the buffer current %d max %d",
                packet->pid, length, (gint) (packet->data - packet->data_start),
                (gint) (packet->data_end - packet->data_start));
        return FALSE;
    }

    packet->data += length;

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

    if (FLAGS_HAS_AFC (tmp)) {
        if (!parse_adaptation_field_control (tssegment, packet)) {
            return FALSE;
        }
    }

    if (FLAGS_HAS_PAYLOAD (tmp)) {
        packet->payload = packet->data;

    } else {
        GST_DEBUG ("No payload, pid: %x", packet->pid);
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

    /* M2TS packets don't start with the sync byte, all other variants do */
    if (packet_size == MPEGTS_M2TS_PACKETSIZE) {
        sync_offset = 4;

    } else {
        sync_offset = 0;
    }

    while (1) {
        if (tssegment->need_sync) {
            if (!ts_segment_sync (tssegment)) {
                return PACKET_NEED_MORE;
            }
            tssegment->need_sync = FALSE;
        }

        if (!tssegment_map (tssegment, packet_size)) {
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
            //GST_DEBUG ("offset %" G_GUINT64_FORMAT, packet->offset);
            tssegment->offset += packet_size;
            //GST_MEMDUMP ("data_start", packet->data_start, 188);
            return parse_packet (tssegment, packet);
        }
    }
}

static GstMpegtsSection *push_section (TsSegment *tssegment, TSPacket *packet)
{
    GstMpegtsSection *section;
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

static gboolean apply_pat (TsSegment *tssegment, GstMpegtsSection * section)
{
    GPtrArray *pat;
    GstMpegtsPatProgram *patp;

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

static gboolean apply_pmt (TsSegment *tssegment, GstMpegtsSection * section)
{
    const GstMpegtsPMT *pmt;
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
    tssegment->pmt = pmt;
    for (i = 0; i < pmt->streams->len; ++i) {
        GstMpegtsPMTStream *stream = g_ptr_array_index (pmt->streams, i);
        if (stream->stream_type == GST_MPEGTS_STREAM_TYPE_VIDEO_H264) {
            tssegment->video_pid = stream->pid;
            GST_ERROR ("264: %d, stream pid: %d", stream->pid, tssegment->video_pid);
        }
    }

    return TRUE;
}

static void handle_psi (TsSegment *tssegment, TSPacket *packet)
{
    GstMpegtsSection *section;
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
            tssegment->seen_pat = TRUE;
            break;
        case GST_MPEGTS_SECTION_PMT:
            post_message = apply_pmt (tssegment, section);
            tssegment->seen_pmt = TRUE;
            break;
        default:
            break;
    }

    GST_ERROR ("Handling PSI (pid: 0x%04x , table_id: 0x%02x)", section->pid, section->table_id);

    /* Finally post message (if it wasn't corrupted) */
    if (post_message) {
        gst_element_post_message (GST_ELEMENT_CAST (tssegment),
                gst_message_new_mpegts_section (GST_OBJECT (tssegment), section));
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

H264NaluParsingResult h264_parse_slice (TsSegment *tssegment)
{
    GstH264NalUnit *nalu, *pre_nalu;
    GstH264SliceHdr *slice_hdr, *pre_slice_hdr;
    GstH264SPS *sps;
    H264NaluParsingResult ret = 0;

    nalu = &(tssegment->nalu);
    pre_nalu = &(tssegment->pre_nalu);
    slice_hdr = &(tssegment->slice_hdr);
    pre_slice_hdr = &(tssegment->pre_slice_hdr);
    sps = &(tssegment->sps);

    GST_DEBUG ("frame_num: %d:%d; field_pic_flag: %d, ref_idc: %d",
            slice_hdr->frame_num,
            pre_slice_hdr->frame_num,
            slice_hdr->field_pic_flag,
            nalu->ref_idc);
    /* frame_num differs in value. */
    if (slice_hdr->frame_num != pre_slice_hdr->frame_num) {
        GST_DEBUG ("new pic");
        ret |= H264_NALU_PIC;
        ret |= H264_NALU_FRAME;
        tssegment->frames_accumulate++;
        tssegment->pes_packet_duration += tssegment->frame_duration;

    /* pic_parameter_set_id differs in value. - not present in gstreamer */
    /* field_pic_flag differs in value. */
    } else if (slice_hdr->field_pic_flag != pre_slice_hdr->field_pic_flag) {
        GST_DEBUG ("new pic");
        ret |= H264_NALU_PIC;
        ret |= H264_NALU_FRAME;
        tssegment->frames_accumulate++;
        tssegment->pes_packet_duration += tssegment->frame_duration;

    /* bottom_field_flag is present in both and differs in value. */
    } else if (slice_hdr->field_pic_flag && pre_slice_hdr->field_pic_flag) {
        if (slice_hdr->bottom_field_flag != pre_slice_hdr->bottom_field_flag) {
            ret |= H264_NALU_PIC;
            if (!(slice_hdr->bottom_field_flag)) {
                GST_DEBUG ("new pic");
                ret |= H264_NALU_FRAME;
                tssegment->frames_accumulate++;
                tssegment->pes_packet_duration += tssegment->frame_duration;
            }
        }

    /* nal_ref_idc differs in value with one of the nal_ref_idc values being equal to 0. */
    } else if ((nalu->ref_idc != pre_nalu->ref_idc) && ((nalu->ref_idc == 0) || (pre_nalu->ref_idc == 0))) {
        GST_DEBUG ("new pic");
        ret |= H264_NALU_PIC;
        ret |= H264_NALU_FRAME;
        tssegment->frames_accumulate++;
        tssegment->pes_packet_duration += tssegment->frame_duration;

    /* pic_order_cnt_type is equal to 0 for both and either pic_order_cnt_lsb differs in value,
     *  or delta_pic_order_cnt_bottom differs in value.
     */
    } else if (sps->pic_order_cnt_type == 0) {
        if (slice_hdr->pic_order_cnt_lsb != pre_slice_hdr->pic_order_cnt_lsb) {
            GST_DEBUG ("new pic");
            ret |= H264_NALU_PIC;
            ret |= H264_NALU_FRAME;
            tssegment->frames_accumulate++;
            tssegment->pes_packet_duration += tssegment->frame_duration;
        }
        if (slice_hdr->delta_pic_order_cnt_bottom != pre_slice_hdr->delta_pic_order_cnt_bottom) {
            GST_DEBUG ("new pic");
            ret |= H264_NALU_PIC;
            ret |= H264_NALU_FRAME;
            tssegment->frames_accumulate++;
            tssegment->pes_packet_duration += tssegment->frame_duration;
        }

    /* pic_order_cnt_type is equal to 1 for both and either delta_pic_order_cnt[ 0 ] differs in value,
     * or delta_pic_order_cnt[ 1 ] differs in value.
     */
    } else if (sps->pic_order_cnt_type == 1) {
        if (slice_hdr->delta_pic_order_cnt[0] != pre_slice_hdr->delta_pic_order_cnt[0]) {
            GST_DEBUG ("new pic");
            ret |= H264_NALU_PIC;
            ret |= H264_NALU_FRAME;
            tssegment->frames_accumulate++;
            tssegment->pes_packet_duration += tssegment->frame_duration;
        }
        if (slice_hdr->delta_pic_order_cnt[1] != pre_slice_hdr->delta_pic_order_cnt[1]) {
            GST_WARNING ("new pic");
            ret |= H264_NALU_PIC;
            ret |= H264_NALU_FRAME;
            tssegment->frames_accumulate++;
            tssegment->pes_packet_duration += tssegment->frame_duration;
        }

    /* IdrPicFlag differs in value. */
    } else if (nalu->idr_pic_flag != pre_nalu->idr_pic_flag) {
        GST_DEBUG ("new pic");
        ret |= H264_NALU_PIC;
        ret |= H264_NALU_FRAME;
        tssegment->frames_accumulate++;
        tssegment->pes_packet_duration += tssegment->frame_duration;

    /* IdrPicFlag is equal to 1 for both and idr_pic_id differs in value. */
    } else if (nalu->idr_pic_flag == 1 && pre_nalu->idr_pic_flag == 1 &&
            (slice_hdr->idr_pic_id != pre_slice_hdr->idr_pic_id)) {
        GST_DEBUG ("new pic");
        ret |= H264_NALU_PIC;
        ret |= H264_NALU_FRAME;
        tssegment->frames_accumulate++;
        tssegment->pes_packet_duration += tssegment->frame_duration;
    }

    if ((GST_H264_IS_I_SLICE (slice_hdr) || GST_H264_IS_SI_SLICE (slice_hdr)) &&
            (*(nalu->data + nalu->offset + 1) & 0x80)) {
        /* means first_mb_in_slice == 0 */
        /* real frame data */
        GST_DEBUG ("I frames accumulate: %lu", tssegment->frames_accumulate);
        //GST_WARNING ("Found keyframe at: %u",nalu->sc_offset);
        ret |= H264_NALU_IDR;
    }

    return ret;
}

H264NaluParsingResult h264_parse_nalu (TsSegment *tssegment)
{
    GstH264ParserResult res = GST_H264_PARSER_OK;
    GstH264NalParser *parser = tssegment->h264parser;
    H264NaluParsingResult type = 0;
    guint8 *data;
    gsize size;
    gint offset = 0;
    GstH264NalUnit *nalu;
    GstH264SliceHdr *slice_hdr;
    GstH264SPS *sps;
    GArray *messages;
    GstH264SEIMessage sei;
    gint i;

    size = tssegment->pes_packet_size;
    data = tssegment->pes_packet;

    nalu = &(tssegment->nalu);
    slice_hdr = &(tssegment->slice_hdr);
    sps = &(tssegment->sps);

    while (1) {
        if (size - offset < 4) {
            break;
        }
        res = gst_h264_parser_identify_nalu (parser, data, offset, size, nalu);
        if (res != GST_H264_PARSER_OK && res != GST_H264_PARSER_NO_NAL_END) {
            if (res == GST_H264_PARSER_BROKEN_DATA) {
                GST_DEBUG ("GST_H264_PARSER_BROKEN_DATA");
            }else if (res == GST_H264_PARSER_BROKEN_LINK) {
                GST_DEBUG ("GST_H264_PARSER_BROKEN_LINK");
            }else if (res == GST_H264_PARSER_ERROR) {
                GST_DEBUG ("GST_H264_PARSER_ERROR");
            }else if (res == GST_H264_PARSER_NO_NAL) {
                GST_DEBUG ("GST_H264_PARSER_NO_NAL");
            }
            break;
        }

        res = gst_h264_parser_parse_nal (parser, nalu);
        if (res != GST_H264_PARSER_OK) {
            GST_WARNING ("Error identifying nal: %i", res);
            break;
        }

        switch (nalu->type) {
            case GST_H264_NAL_AU_DELIMITER:
                GST_DEBUG ("Found Delimiter");
                type |= H264_NALU_DELIMITER;
                break;
            case GST_H264_NAL_SEI:
                GST_DEBUG ("Found SEI");
                res = gst_h264_parser_parse_sei (parser, nalu, &messages);
                if (res != GST_H264_PARSER_OK) {
                    GST_WARNING ("failed to parse SEI message, return %d", res);
                }
                for (i = 0; i < messages->len; i++) {
                    sei = g_array_index (messages, GstH264SEIMessage, i);
                    if (sei.payloadType == GST_H264_SEI_PIC_TIMING) {
                        if (sei.payload.pic_timing.pic_struct_present_flag) {
                            tssegment->pic_struct = sei.payload.pic_timing.pic_struct;

                        } else {
                            tssegment->pic_struct = 0;
                        }
                    }
                }
                g_array_free (messages, TRUE);
                type |= H264_NALU_SEI;
                break;
            case GST_H264_NAL_PPS:
                GST_DEBUG ("Found PPS");
                type |= H264_NALU_PPS;
                break;
            case GST_H264_NAL_SPS:
                GST_DEBUG ("Found SPS");
                res = gst_h264_parser_parse_sps (parser, nalu, sps, TRUE);
                if (res != GST_H264_PARSER_OK) {
                    GST_WARNING ("Error parse sps");

                } else {
                }
                type |= H264_NALU_SPS;
                break;
            /* these units are considered keyframes in h264parse */
            case GST_H264_NAL_SLICE:
            case GST_H264_NAL_SLICE_DPA:
            case GST_H264_NAL_SLICE_DPB:
            case GST_H264_NAL_SLICE_DPC:
            case GST_H264_NAL_SLICE_IDR:
                GST_DEBUG ("Found slice");
                if (G_UNLIKELY (!(parser->last_sps))) {
                    break;
                }
                res = gst_h264_parser_parse_slice_hdr (parser, nalu, slice_hdr, FALSE, FALSE);
                    gst_h264_video_calculate_framerate (parser->last_sps,
                                                        slice_hdr->field_pic_flag,
                                                        tssegment->pic_struct,
                                                        &(tssegment->fps_num),
                                                        &(tssegment->fps_den));
                    tssegment->frame_duration = GST_SECOND * tssegment->fps_den / tssegment->fps_num;
                    GST_DEBUG ("field_pic_flag: %d, pic_struct: %d, fps_num: %d, fps_den: %d",
                            slice_hdr->field_pic_flag,
                            tssegment->pic_struct,
                            tssegment->fps_num,
                            tssegment->fps_den);
                type |= h264_parse_slice (tssegment);
                tssegment->pre_slice_hdr = tssegment->slice_hdr;
                tssegment->pre_nalu = tssegment->nalu;
                break;
            default:
                GST_DEBUG ("Found default %d", nalu->type);
        }

        if (offset == nalu->sc_offset + nalu->size) {
            GST_DEBUG ("Found offset == nalu->sc_offset + nalu->size");
            break;
        }

        offset = tssegment->nalu.sc_offset + tssegment->nalu.size;
    }

    return type;
}

static void pending_tspacket (TsSegment *tssegment, TSPacket *packet)
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
    memcpy (tssegment->data + tssegment->current_size, data, size);
    tssegment->current_size += size;
}

/**
 * mpegts_parse_pes_header:
 *
 * Parses the mpeg-ts PES header located in @data into the @res.
 *
 * Returns:
 * #PES_PARSING_OK if the header was fully parsed and valid,
 * #PES_PARSING_BAD if the header is invalid,
 * #PES_PARSING_NEED_MORE if more data is needed to properly parse the header.
 */
PESParsingResult mpegts_parse_pes_header (TsSegment *tssegment)
{
    PESParsingResult ret = PES_PARSING_NEED_MORE;
    guint32 val32;
    guint8 val8, flags, *data;
    gsize length;
    PESHeader *pes_header;
    guint16 header_data_length; /* The complete size of the PES header */

    pes_header = &(tssegment->pes_header);
    data = tssegment->pes_packet;
    length = tssegment->pes_packet_size;
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

    pes_header->stream_id = val32 & 0x000000ff;
    if (pes_header->stream_id != 0xE0) {
        GST_WARNING ("Not H.264 video PES packet");
        return PES_PARSING_BAD;
    }

    /*
       PES_packet_length – A 16-bit field specifying the number of bytes in the PES packet following the last byte of the
       field. A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
       PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */
    pes_header->packet_length = GST_READ_UINT16_BE (data);
    if (pes_header->packet_length) {
        pes_header->packet_length += 6;
    }
    data += 2;
    length -= 2;

    if (G_UNLIKELY (length < 3)) {
        GST_WARNING ("Not enough data to parse PES header");
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

    /* PTS_DTS_flags                    2
     * ESCR_flag                        1
     * ES_rate_flag                     1
     * DSM_trick_mode_flag              1
     * additional_copy_info_flag        1
     * PES_CRC_flag                     1
     * PES_extension_flag               1*/
    flags = *data++;

    /* PES_header_data_length 8 */
    header_data_length = *data++;
    length -= 3;
    if (G_UNLIKELY (length < header_data_length)) {
        GST_WARNING ("Not enough data to parse PES header");
        return ret;
    }

    /* PTS_DTS_flags == 0x01 is invalid */
    if (G_UNLIKELY ((flags >> 6) == 0x01)) {
        GST_WARNING ("Invalid PTS_DTS_flag (0x01 is forbidden)");
    }

    if ((flags & 0x80) == 0x80) {
        if (G_UNLIKELY (length < 5)) {
            GST_DEBUG ("Not enough data to parse PES header");
            return ret;
        }

        if ((*data & 0x01) != 0x01) {
            GST_WARNING ("bad PTS value");
            return PES_PARSING_BAD;
        }
        pes_header->PTS = ((guint64) (*data++ & 0x0E)) << 29;
        pes_header->PTS |= ((guint64) (*data++)) << 22;
        if ((*data & 0x01) != 0x01) {
            GST_WARNING ("bad PTS value");
            return PES_PARSING_BAD;
        }
        pes_header->PTS |= ((guint64) (*data++ & 0xFE)) << 14;
        pes_header->PTS |= ((guint64) (*data++)) << 7;
        if ((*data & 0x01) != 0x01) {
            GST_WARNING ("bad PTS value");
            return PES_PARSING_BAD;
        }
        pes_header->PTS |= ((guint64) (*data++ & 0xFE)) >> 1;
        //GST_WARNING ("PTS %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
          //      pes_header->PTS, GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (pes_header->PTS)));
        tssegment->current_pts = pes_header->PTS;
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
    H264NaluParsingResult h264res;
    guint8 *data;
    GstBuffer *buffer;

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

        } else if ((packet.pid == tssegment->video_pid) && FLAGS_HAS_PAYLOAD (packet.scram_afc_cc)) {
            if (G_LIKELY (packet.payload_unit_start_indicator)) {
                mpegts_parse_pes_header (tssegment);
                h264res = h264_parse_nalu (tssegment);
                /* if new frame found, push a segment downstream */
                if (h264res & H264_NALU_FRAME) {
                    data = g_malloc (tssegment->current_size);
                    memcpy (data, tssegment->data, tssegment->current_size);
                    buffer = gst_buffer_new_wrapped (data, tssegment->current_size);
                    GST_BUFFER_PTS (buffer) = tssegment->PTS;
                    GST_BUFFER_DURATION (buffer) = tssegment->pes_packet_duration;
                    if (h264res & H264_NALU_IDR) {
                        GST_MINI_OBJECT_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

                    } else {
                        GST_MINI_OBJECT_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
                    }
                    GST_DEBUG ("PTS %" GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT,
                            GST_TIME_ARGS (tssegment->PTS),
                            GST_TIME_ARGS (tssegment->pes_packet_duration));
                    if (G_LIKELY (tssegment->seen_idr)) {
                        gst_pad_push (tssegment->srcpad, buffer);

                    } else if (h264res & H264_NALU_IDR) {
                        tssegment->seen_idr = TRUE;
                    }
                    tssegment->current_size = 0;
                    tssegment->pre_pts = tssegment->current_pts;
                    tssegment->pes_packet_duration = 0;
                    tssegment->PTS = (tssegment->frames_accumulate * tssegment->fps_den * GST_SECOND ) / tssegment->fps_num;
                }

                tssegment->pes_packet_size = 0;
            }
            memcpy (tssegment->pes_packet + tssegment->pes_packet_size, packet.payload, packet.data_end - packet.payload);
            tssegment->pes_packet_size += (packet.data_end - packet.payload);
        }

        if (G_LIKELY (tssegment->seen_idr)) {
            pending_tspacket (tssegment, &packet);
        }
        clear_packet (tssegment, &packet);
    }

    return res;
}

gboolean ts_segment_plugin_init (GstPlugin * plugin)
{
    return gst_element_register (plugin, "tssegment", GST_RANK_NONE, TYPE_TS_SEGMENT);
}
