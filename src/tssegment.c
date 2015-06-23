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

        tssegment->streams = g_new0 (TSPacketStream *, 8192);
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

static inline TSPacketStreamSubtable *find_subtable (GSList * subtables, guint8 table_id, guint16 subtable_extension)
{
        GSList *tmp;

        /* FIXME: Make this an array ! */
        for (tmp = subtables; tmp; tmp = tmp->next) {
                TSPacketStreamSubtable *sub = (TSPacketStreamSubtable *) tmp->data;
                if (sub->table_id == table_id && sub->subtable_extension == subtable_extension) {
                        return sub;
                }
        }

        return NULL;
}

static gboolean seen_section_before (TSPacketStream * stream, guint8 table_id,
                guint16 subtable_extension, guint8 version_number, guint8 section_number,
                guint8 last_section_number)
{
        TSPacketStreamSubtable *subtable;

        /* Check if we've seen this table_id/subtable_extension first */
        subtable = find_subtable (stream->subtables, table_id, subtable_extension);
        if (!subtable) {
                GST_DEBUG ("Haven't seen subtable");
                return FALSE;
        }
        /* If we have, check it has the same version_number */
        if (subtable->version_number != version_number) {
                GST_DEBUG ("Different version number");
                return FALSE;
        }
        /* Did the number of sections change ? */
        if (subtable->last_section_number != last_section_number) {
                GST_DEBUG ("Different last_section_number");
                return FALSE;
        }
        /* Finally return whether we saw that section or not */
        return MPEGTS_BIT_IS_SET (subtable->seen_section, section_number);
}

static TSPacketStreamSubtable *stream_subtable_new (guint8 table_id, guint16 subtable_extension, guint8 last_section_number)
{
        TSPacketStreamSubtable *subtable;

        subtable = g_new0 (TSPacketStreamSubtable, 1);
        subtable->version_number = VERSION_NUMBER_UNSET;
        subtable->table_id = table_id;
        subtable->subtable_extension = subtable_extension;
        subtable->last_section_number = last_section_number;

        return subtable;
}

static TSPacketStream *stream_new (guint16 pid)
{
        TSPacketStream *stream;

        stream = (TSPacketStream *) g_new0 (TSPacketStream, 1);
        stream->continuity_counter = CONTINUITY_UNSET;
        stream->subtables = NULL;
        stream->table_id = TABLE_ID_UNSET;
        stream->pid = pid;
        return stream;
}

static void clear_section (TSPacketStream * stream)
{
        stream->continuity_counter = CONTINUITY_UNSET;
        stream->section_length = 0;
        stream->section_offset = 0;
        stream->table_id = TABLE_ID_UNSET;
        if (stream->section_data) {
                g_free (stream->section_data);
        }
        stream->section_data = NULL;
}

static void stream_subtable_free (TSPacketStreamSubtable *subtable)
{
        g_free (subtable);
}

static void stream_free (TSPacketStream * stream)
{
        clear_section (stream);
        if (stream->section_data) {
                g_free (stream->section_data);
        }
        g_slist_foreach (stream->subtables, (GFunc)stream_subtable_free, NULL);
        g_slist_free (stream->subtables);
        g_free (stream);
}

static void ts_segment_dispose (GObject * object)
{
        TsSegment *tssegment;

        tssegment = TS_SEGMENT (object);
        if (tssegment->streams) {
                int i;
                for (i = 0; i < 8192; i++) {
                        if (tssegment->streams[i]) {
                                stream_free (tssegment->streams[i]);
                        }
                }
                g_free (tssegment->streams);
        }
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
                MPEGTS_M2TS_PACKETSIZE,
                MPEGTS_DVB_ASI_PACKETSIZE,
                MPEGTS_ATSC_PACKETSIZE
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
                GST_ERROR ("pcr 0x%04x %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT
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

static GstMpegTsSection *parse_section_header (TsSegment *tssegment, TSPacketStream * stream)
{
        TSPacketStreamSubtable *subtable;
        GstMpegTsSection *res;

        subtable = find_subtable (stream->subtables, stream->table_id, stream->subtable_extension);
        if (subtable) {
                GST_DEBUG ("Found previous subtable_extension:0x%04x", stream->subtable_extension);
                if (G_UNLIKELY (stream->version_number != subtable->version_number)) {
                        /* If the version number changed, reset the subtable */
                        subtable->version_number = stream->version_number;
                        subtable->last_section_number = stream->last_section_number;
                        memset (subtable->seen_section, 0, 32);
                }

        } else {
                GST_DEBUG ("Appending new subtable_extension: 0x%04x", stream->subtable_extension);
                subtable = stream_subtable_new (stream->table_id, stream->subtable_extension, stream->last_section_number);
                subtable->version_number = stream->version_number;

                stream->subtables = g_slist_prepend (stream->subtables, subtable);
        }

        GST_MEMDUMP ("Full section data", stream->section_data, stream->section_length);
        /* TODO ? : Replace this by an efficient version (where we provide all
         * pre-parsed header data) */
        res = gst_mpegts_section_new (stream->pid, stream->section_data, stream->section_length);
        stream->section_data = NULL;
        clear_section (stream);

        if (res) {
                /* NOTE : Due to the new mpegts-si system, There is a insanely low probability
                 * that we might have gotten a section that was corrupted (i.e. wrong crc)
                 * and that we consider it as seen.
                 *
                 * The reason why we consider this as acceptable is because all the previous
                 * checks were already done:
                 * * transport layer checks (DVB)
                 * * 0x47 validation
                 * * continuity counter validation
                 * * subtable validation
                 * * section_number validation
                 * * section_length validation
                 *
                 * The probability of this happening vs the overhead of doing CRC checks
                 * on all sections (including those we would not use) is just not worth it.
                 * */
                MPEGTS_BIT_SET (subtable->seen_section, stream->section_number);
                res->offset = stream->offset;
        }

        return res;
}

/*
 * Ideally it should just return a section if:
 * * The section is complete
 * * The section is valid (sanity checks for length for example)
 * * The section applies now (current_next_indicator)
 * * The section is an update or was never seen
 *
 * The section should be a new GstMpegtsSection:
 * * properly initialized
 * * With pid, table_id AND section_type set (move logic from mpegtsbase)
 * * With data copied into it (yes, minor overhead)
 *
 * In all other cases it should just return NULL
 *
 * If more than one section is available, the 'remaining' field will
 * be set to the beginning of a valid GList containing other sections.
 * */
static GstMpegTsSection *push_section (TsSegment *tssegment, TSPacket *packet, GList **remaining)
{
        GstMpegTsSection *section;
        GstMpegTsSection *res = NULL;
        TSPacketStream *stream;
        gboolean long_packet;
        guint8 pointer = 0, table_id;
        guint16 subtable_extension = 0;
        gsize to_read;
        guint section_length;
        /* data points to the current read location
         * data_start points to the beginning of the data to accumulate */
        const guint8 *data, *data_start;
        guint8 packet_cc;
        GList *others = NULL;
        guint8 version_number, section_number, last_section_number;

        data = packet->data;
        packet_cc = FLAGS_CONTINUITY_COUNTER (packet->scram_afc_cc);

        /* Get our filter */
        stream = tssegment->streams[packet->pid];
        if (G_UNLIKELY (stream == NULL)) {
                if (!packet->payload_unit_start_indicator) {
                        /* Early exit (we need to start with a section start) */
                        GST_DEBUG ("PID 0x%04x  waiting for section start", packet->pid);
                        goto out;
                }
                stream = stream_new (packet->pid);
                tssegment->streams[packet->pid] = stream;
        }

        GST_MEMDUMP ("Full packet data", packet->data, packet->data_end - packet->data);

        /* This function is split into several parts:
         *
         * Pre checks (packet-wide). Determines where we go next
         * accumulate_data: store data and check if section is complete
         * section_start: handle beginning of a section, if needed loop back to
         *                accumulate_data
         *
         * The trigger that makes the loop stop and return is if:
         * 1) We do not have enough data for the current packet
         * 2) There is remaining data after a packet which is only made
         *    of stuffing bytes (0xff).
         *
         * Pre-loop checks, related to the whole incoming packet:
         *
         * If there is a CC-discont:
         *  If it is a PUSI, skip the pointer and handle section_start
         *  If not a PUSI, reset and return nothing
         * If there is not a CC-discont:
         *  If it is a PUSI
         *    If pointer, accumulate that data and check for complete section
         *    (loop)
         *  If it is not a PUSI
         *    Accumulate the expected data and check for complete section
         *    (loop)
         *    
         **/

        if (packet->payload_unit_start_indicator) {
                pointer = *data++;
                /* If the pointer is zero, we're guaranteed to be able to handle it */
                if (pointer == 0) {
                        GST_LOG ("PID 0x%04x PUSI and pointer == 0, skipping straight to section_start parsing", packet->pid);
                        goto section_start;
                }
        }

        if (stream->continuity_counter == CONTINUITY_UNSET || (stream->continuity_counter + 1) % 16 != packet_cc) {
                if (stream->continuity_counter != CONTINUITY_UNSET)
                        GST_WARNING ("PID 0x%04x section discontinuity (%d vs %d)", packet->pid,
                                        stream->continuity_counter, packet_cc);
                clear_section (stream);
                /* If not a PUSI, not much we can do */
                if (!packet->payload_unit_start_indicator) {
                        GST_LOG ("PID 0x%04x continuity discont/unset and not PUSI, bailing out", packet->pid);
                        goto out;
                }
                /* If PUSI, skip pointer data and carry on to section start */
                data += pointer;
                pointer = 0;
                GST_LOG ("discont, but PUSI, skipped %d bytes and doing section start", pointer);
                goto section_start;
        }

        GST_LOG ("Accumulating data from beginning of packet");

        data_start = data;

accumulate_data:
        /* If not the beginning of a new section, accumulate what we have */
        stream->continuity_counter = packet_cc;
        to_read = MIN (stream->section_length - stream->section_offset, packet->data_end - data_start);
        memcpy (stream->section_data + stream->section_offset, data_start, to_read);
        stream->section_offset += to_read;
        /* Point data to after the data we accumulated */
        data = data_start + to_read;
        GST_DEBUG ("Appending data (need %d, have %d)", stream->section_length, stream->section_offset);

        /* Check if we have enough */
        if (stream->section_offset < stream->section_length) {
                GST_DEBUG ("PID 0x%04x, section not complete (Got %d, need %d)",
                                stream->pid, stream->section_offset, stream->section_length);
                goto out;
        }

        /* Small sanity check. We should have collected *exactly* the right amount */
        if (G_UNLIKELY (stream->section_offset != stream->section_length)) {
                GST_WARNING ("PID 0x%04x Accumulated too much data (%d vs %d) !",
                                stream->pid, stream->section_offset, stream->section_length);
        }
        GST_DEBUG ("PID 0x%04x Section complete", stream->pid);

        if ((section = parse_section_header (tssegment, stream))) {
                if (res) {
                        others = g_list_append (others, section);

                } else {
                        res = section;
                }
        }

        /* FIXME : We need at least 8 bytes with current algorithm :(
         * We might end up losing sections that start across two packets (srsl...) */
        if (data > packet->data_end - 8 || *data == 0xff) {
                /* flush stuffing bytes and leave */
                clear_section (stream);
                goto out;
        }

        /* We have more data to process ... */
        GST_DEBUG ("PID 0x%04x, More section present in packet (remaining bytes:%"
                        G_GSIZE_FORMAT ")", stream->pid, (gsize) (packet->data_end - data));

section_start:
        GST_MEMDUMP ("section_start", data, packet->data_end - data);
        data_start = data;
        /* Beginning of a new section */
        /*
         * section_syntax_indicator means that the header is of the following format:
         * * table_id (8bit)
         * * section_syntax_indicator (1bit) == 0
         * * reserved/private fields (3bit)
         * * section_length (12bit)
         * * data (of size section_length)
         * * NO CRC !
         */
        long_packet = data[1] & 0x80;

        /* Fast path for short packets */
        if (!long_packet) {
                /* We can create the section now (function will check for size) */
                GST_DEBUG ("Short packet");
                section_length = (GST_READ_UINT16_BE (data + 1) & 0xfff) + 3;
                /* Only do fast-path if we have enough byte */
                if (section_length < packet->data_end - data) {
                        if ((section = gst_mpegts_section_new (packet->pid, g_memdup (data, section_length), section_length))) {
                                GST_DEBUG ("PID 0x%04x Short section complete !", packet->pid);
                                section->offset = packet->offset;
                                if (res) {
                                        others = g_list_append (others, section);

                                } else {
                                        res = section;
                                }
                        }
                        /* Advance reader and potentially read another section */
                        data += section_length;
                        if (data < packet->data_end && *data != 0xff) {
                                goto section_start;
                        }
                        /* If not, exit */
                        goto out;
                }
                /* We don't have enough bytes to do short section shortcut */
        }

        /* Beginning of a new section, do as much pre-parsing as possible */
        /* table_id                        : 8  bit */
        table_id = *data++;

        /* section_syntax_indicator        : 1  bit
         * other_fields (reserved)         : 3  bit
         * section_length                  : 12 bit */
        section_length = (GST_READ_UINT16_BE (data) & 0x0FFF) + 3;
        data += 2;

        if (long_packet) {
                /* subtable_extension (always present, we are in a long section) */
                /* subtable extension              : 16 bit */
                subtable_extension = GST_READ_UINT16_BE (data);
                data += 2;

                /* reserved                      : 2  bit
                 * version_number                : 5  bit
                 * current_next_indicator        : 1  bit */
                /* Bail out now if current_next_indicator == 0 */
                if (G_UNLIKELY (!(*data & 0x01))) {
                        GST_DEBUG ("PID 0x%04x table_id 0x%02x section does not apply (current_next_indicator == 0)",
                                 packet->pid, table_id);
                        goto out;
                }

                version_number = *data++ >> 1 & 0x1f;
                /* section_number                : 8  bit */
                section_number = *data++;
                /* last_section_number                : 8  bit */
                last_section_number = *data++;

        } else {
                subtable_extension = 0;
                version_number = 0;
                section_number = 0;
                last_section_number = 0;
        }
        GST_DEBUG ("PID 0x%04x length:%d table_id:0x%02x subtable_extension:0x%04x version_number:%d section_number:%d(last:%d)",
                 packet->pid, section_length, table_id, subtable_extension, version_number,
                 section_number, last_section_number);

        to_read = MIN (section_length, packet->data_end - data_start);

        /* Check as early as possible whether we already saw this section
         * i.e. that we saw a subtable with:
         * * same subtable_extension (might be zero)
         * * same version_number
         * * same last_section_number
         * * same section_number was seen
         */
        if (seen_section_before (stream, table_id, subtable_extension, version_number, section_number, last_section_number)) {
                GST_DEBUG ("PID 0x%04x Already processed table_id:0x%02x subtable_extension:0x%04x, version_number:%d, section_number:%d",
                         packet->pid, table_id, subtable_extension, version_number,
                         section_number);
                /* skip data and see if we have more sections after */
                data = data_start + to_read;
                if (data == packet->data_end || *data == 0xff) {
                        goto out;
                }
                goto section_start;
        }
        if (G_UNLIKELY (section_number > last_section_number)) {
                GST_WARNING ("PID 0x%04x corrupted packet (section_number:%d > last_section_number:%d)",
                         packet->pid, section_number, last_section_number);
                goto out;
        }

        /* Copy over already parsed values */
        stream->table_id = table_id;
        stream->section_length = section_length;
        stream->version_number = version_number;
        stream->subtable_extension = subtable_extension;
        stream->section_number = section_number;
        stream->last_section_number = last_section_number;
        stream->offset = packet->offset;

        /* Create enough room to store chunks of sections */
        stream->section_data = g_malloc (stream->section_length);
        stream->section_offset = 0;

        /* Finally, accumulate and check if we parsed enough */
        goto accumulate_data;

out:
        packet->data = data;
        *remaining = others;

        GST_DEBUG ("result: %p", res);

        return res;
}

void remove_stream (TsSegment *tssegment, gint16 pid)
{
        TSPacketStream *stream = tssegment->streams[pid];
        if (stream) {
                GST_INFO ("Removing stream for PID 0x%04x", pid);
                stream_free (stream);
                tssegment->streams[pid] = NULL;
        }
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
                /* remove the stream since we won't get another PMT otherwise */
                remove_stream (tssegment, section->pid);
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
        GList *others;
        GstMpegTsSection *section;
        gboolean post_message = TRUE;

        section = push_section (tssegment, packet, &others);
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
        if (G_UNLIKELY (others)) {
           //     for (tmp = others; tmp; tmp = tmp->next) {
             //           handle_psi (tssegment, (GstMpegTsSection *) tmp->data);
               // }
                g_list_free (others);
        }

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
                        break;
                default:
                        break;
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
        GST_BUFFER_PTS (buffer) = tssegment->pre_pts;
        GST_BUFFER_DURATION (buffer) = tssegment->duration;
        GST_ERROR ("pushing %u data timestamp: %lu", tssegment->current_size, GST_BUFFER_PTS (buffer));
        gst_pad_push (tssegment->srcpad, buffer);
        tssegment->current_size = 0;
}

static void segment_duration (TsSegment *tssegment, TSPacket *packet)
{
        if (tssegment->pre_pts < tssegment->current_pts) {
                tssegment->duration = tssegment->current_pts - tssegment->pre_pts;

        } else {
                tssegment->duration = MAX_PCR - tssegment->pre_pts + tssegment->current_pts;
        }
}

/**
 * mpegts_parse_pes_header:
 * @data: data to parse (starting from, and including, the sync code)
 * @length: size of @data in bytes
 * @res: PESHeader to fill (only valid with #PES_PARSING_OK.
 *
 * Parses the mpeg-ts PES header located in @data into the @res.
 *
 * Returns: #PES_PARSING_OK if the header was fully parsed and valid,
 * #PES_PARSING_BAD if the header is invalid, or #PES_PARSING_NEED_MORE if more data
 * is needed to properly parse the header.
 */
PESParsingResult
mpegts_parse_pes_header (const guint8 * data, gsize length, PESHeader * res)
{
        PESParsingResult ret = PES_PARSING_NEED_MORE;
        gsize origlength = length;
        guint32 val32;
        guint8 val8, flags;

        g_return_val_if_fail (res != NULL, PES_PARSING_BAD);

        /* The smallest valid PES header is 6 bytes (prefix + stream_id + length) */
        if (G_UNLIKELY (length < 6)) {
                //goto need_more_data;
                GST_DEBUG ("Not enough data to parse PES header");
                return ret;
        }

        val32 = GST_READ_UINT32_BE (data);
        data += 4;
        length -= 4;
        if (G_UNLIKELY ((val32 & 0xffffff00) != 0x00000100)) {
                //goto bad_start_code;
                GST_WARNING ("Wrong packet start code 0x%x != 0x000001xx", val32);
                return PES_PARSING_BAD;
        }

        /* Clear the header */
        memset (res, 0, sizeof (PESHeader));
        res->PTS = -1;
        res->DTS = -1;
        res->ESCR = -1;

        res->stream_id = val32 & 0x000000ff;

        res->packet_length = GST_READ_UINT16_BE (data);
        if (res->packet_length)
                res->packet_length += 6;
        data += 2;
        length -= 2;

        GST_LOG ("stream_id : 0x%08x , packet_length : %d", res->stream_id,
                        res->packet_length);

        /* Jump if we don't need to parse anything more */
        if (G_UNLIKELY (res->stream_id == 0xbc || res->stream_id == 0xbe
                                || res->stream_id == 0xbf || (res->stream_id >= 0xf0
                                        && res->stream_id <= 0xf2) || res->stream_id == 0xff
                                || res->stream_id == 0xf8)) {
                //goto done_parsing;
                GST_DEBUG ("origlength:%" G_GSIZE_FORMAT ", length:%" G_GSIZE_FORMAT,
                                origlength, length);

                res->header_size = origlength - length;
                ret = PES_PARSING_OK;

                return ret;
        }

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
                GST_WARNING ("Wrong '0x10' marker before PES_scrambling_control (0x%02x)",
                                val8);
                return PES_PARSING_BAD;
        }
        res->scrambling_control = (val8 >> 4) & 0x3;
        res->flags = val8 & 0xf;

        GST_LOG ("scrambling_control 0x%0x", res->scrambling_control);
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
        GST_ERROR ("flags_2: %s%s%s%s%s%s%s%s%s",
                        flags & 0x80 ? "PTS " : "",
                        flags & 0x40 ? "DTS " : "",
                        flags & 0x20 ? "ESCR" : "",
                        flags & 0x10 ? "ES_rate " : "",
                        flags & 0x08 ? "DSM_trick_mode " : "",
                        flags & 0x04 ? "additional_copy_info " : "",
                        flags & 0x02 ? "CRC " : "",
                        flags & 0x01 ? "extension " : "", flags ? "" : "<none>");

        /* PES_header_data_length           8 */
        res->header_size = *data++;
        length -= 3;
        if (G_UNLIKELY (length < res->header_size)) {
                GST_DEBUG ("Not enough data to parse PES header");
                return ret;
        }

        res->header_size += 9;        /* We add 9 since that's the offset
                                       * of the field in the header*/
        GST_DEBUG ("header_size : %d", res->header_size);

        /* PTS/DTS */

        /* PTS_DTS_flags == 0x01 is invalid */
        if (G_UNLIKELY ((flags >> 6) == 0x01)) {
                GST_WARNING ("Invalid PTS_DTS_flag (0x01 is forbidden)");
        }

        if ((flags & 0x80) == 0x80) {
                /* PTS */
                if (G_UNLIKELY (length < 5)) {
                        GST_DEBUG ("Not enough data to parse PES header");
                        return ret;
                }

                READ_TS (data, res->PTS, bad_PTS_value);
                length -= 5;
                GST_DEBUG ("PTS %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
                                res->PTS, GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (res->PTS)));

        }

        return ret;

bad_PTS_value:
        GST_WARNING ("bad PTS value");
        return PES_PARSING_BAD;
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

///GST_ERROR ("----*******************---");
        while (res == GST_FLOW_OK) {
                ret = next_ts_packet (tssegment, &packet);

                if (G_UNLIKELY (ret == PACKET_NEED_MORE)) {
                        break;
                }

///GST_ERROR ("------- end: %p start: %p", packet.data_end, packet.payload);
                if (G_UNLIKELY (ret == PACKET_BAD)) {
                        /* bad header, skip the packet */
                        GST_WARNING ("bad packet, skipping");
                        goto next;
                }
///GST_ERROR (">>>>>>>>>>>>>>>>>>>pcr %lu", packet.pcr);
                //if (packet.pcr != GST_CLOCK_TIME_NONE) {
                  //      tssegment->pts = packet.pcr;
///GST_ERROR (">>>>>>>>>>>>>>>>>>pcr %lu", tssegment->pts);
                //}

                if (packet.payload && MPEGTS_BIT_IS_SET (tssegment->known_psi, packet.pid)) {
                        handle_psi (tssegment, &packet);
                        /* base PSI data */
//                        GList *others, *tmp;
//                        GstMpegTsSection *section;

#if 0
                        section = push_section (tssegment, &packet, &others);
                        if (section) {
                                handle_psi (tssegment, section);

                        }
                        if (G_UNLIKELY (others)) {
                                for (tmp = others; tmp; tmp = tmp->next) {
                                        handle_psi (tssegment, (GstMpegTsSection *) tmp->data);
                                }
                                g_list_free (others);
                        }
                        if (tssegment->seen_key_frame) {
                                pending_packet (tssegment, &packet);
                        }
#endif
                } else if ((packet.pid == tssegment->video_pid) &&
                           G_LIKELY (packet.payload_unit_start_indicator) &&
                           FLAGS_HAS_PAYLOAD (packet.scram_afc_cc)) {
                        if (is_key_frame (tssegment, &packet)) {
                                PESHeader res;
                                mpegts_parse_pes_header (packet.payload, 184, &res);
                                GST_ERROR ("PTS %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT, res.PTS, GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (res.PTS)));
                                tssegment->current_pts = MPEGTIME_TO_GSTTIME (res.PTS);
                                /* push a segment downstream */
                                if (tssegment->seen_key_frame) {
                                        segment_duration (tssegment, &packet);
                                        pushing_data (tssegment);
                                        tssegment->pre_pts = MPEGTIME_TO_GSTTIME (res.PTS);

                                } else {
                                        tssegment->seen_key_frame = TRUE;
                                        tssegment->pre_pts = MPEGTIME_TO_GSTTIME (res.PTS);
                                }

                        }
                        /* push packet to segment */
                        pending_packet (tssegment, &packet);

               } else if (tssegment->seen_key_frame) {
                        /* push packet to segment if have seen key frame */
                        pending_packet (tssegment, &packet);

                } else {
                        /* discard packet before seen key frame */
                        
                }

        next:
                clear_packet (tssegment, &packet);
        }

        return res;
}

gboolean ts_segment_plugin_init (GstPlugin * plugin)
{
        return gst_element_register (plugin, "tssegment", GST_RANK_NONE, TYPE_TS_SEGMENT);
}
