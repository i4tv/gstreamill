/*
 *  MPEGTS Segment Element
 *
 *  Mostly copy from /gst-plugins-bad/gst/mpegtsdemux
 */

#ifndef __TSSEGMENT_H__
#define __TSSEGMENT_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/mpegts/mpegts.h>
#include <gst/codecparsers/gsth264parser.h>

#define MPEGTS_NORMAL_PACKETSIZE 188
#define MPEGTS_M2TS_PACKETSIZE 192
#define CONTINUITY_UNSET 255
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF
#define PACKET_SYNC_BYTE 0x47
#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE 208

#define MPEGTS_BIT_SET(field, offs)    ((field)[(offs) >> 3] |=  (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_IS_SET(field, offs) ((field)[(offs) >> 3] &   (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_UNSET(field, offs)  ((field)[(offs) >> 3] &= ~(1 << ((offs) & 0x7)))
#define FLAGS_CONTINUITY_COUNTER(f)    (f & 0x0f)
#define FLAGS_HAS_AFC(f)               (f & 0x20)
#define FLAGS_HAS_PAYLOAD(f)           (f & 0x10)
#define MPEGTS_AFC_PCR_FLAG            0x10

#define MAX_PCR 2576980378112
#define MAX_MPEGTIME 8589934592

/* PCR_TO_GST calculation requires at least 10 extra bits.
 * Since maximum PCR value is coded with 42 bits, we are
 * safe to use direct calculation (10+42 < 63)*/
#define PCRTIME_TO_GSTTIME(t) (((t) * (guint64)1000) / 27)

#define SAFE_FOURCC_FORMAT "02x%02x%02x%02x (%c%c%c%c)"
#define SAFE_CHAR(a) (g_ascii_isalnum((gchar) (a)) ? ((gchar)(a)) : '.')
#define SAFE_FOURCC_ARGS(a)                             \
  ((guint8) ((a)>>24)),                                 \
    ((guint8) ((a) >> 16 & 0xff)),                      \
    ((guint8) a >> 8 & 0xff),                           \
    ((guint8) a & 0xff),                                \
    SAFE_CHAR((a)>>24),                                 \
    SAFE_CHAR((a) >> 16 & 0xff),                        \
    SAFE_CHAR((a) >> 8 & 0xff),                         \
    SAFE_CHAR(a & 0xff)

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15

typedef struct {
    gint16 pid;
    guint8 payload_unit_start_indicator;
    guint8 scram_afc_cc;
    const guint8 *payload;
    const guint8 *data_start;
    const guint8 *data_end;
    const guint8 *data;
    guint8 afc_flags;
    guint64 offset;
} TSPacket;

typedef enum {
    PES_FIELD_ID_TOP_ONLY  = 0x00, /* Display from top field only */
    PES_FIELD_ID_BOTTOM_ONLY = 0x01, /* Display from bottom field only */
    PES_FIELD_ID_COMPLETE_FRAME = 0x10, /* Display complete frame */
    PES_FIELD_ID_INVALID  = 0x11 /* Reserved/Invalid */
} PESFieldID;

typedef enum {
    PES_FLAG_PRIORITY  = 1 << 3, /* PES_priority (present: high-priority) */
    PES_FLAG_DATA_ALIGNMENT = 1 << 2, /* data_alignment_indicator */
    PES_FLAG_COPYRIGHT  = 1 << 1, /* copyright */
    PES_FLAG_ORIGINAL_OR_COPY = 1 << 0 /* original_or_copy */
} PESHeaderFlags;

typedef enum {
    PES_PARSING_OK        = 0,    /* Header fully parsed and valid */
    PES_PARSING_BAD       = 1,    /* Header invalid (CRC error for ex) */
    PES_PARSING_NEED_MORE = 2     /* Not enough data to parse header */
} PESParsingResult;

typedef enum {
    H264_NALU_DELIMITER = 2,
    H264_NALU_SEI = 4,
    H264_NALU_SPS = 8,
    H264_NALU_PPS = 16,
    H264_NALU_PIC = 32,
    H264_NALU_FRAME = 64,
    H264_NALU_IDR = 128
} H264NaluParsingResult;

typedef struct {
    guint8 stream_id; /* See ID_* above */
    guint32 packet_length; /* The size of the PES header and PES data
                             * (if 0 => unbounded packet) */
    guint16 header_size; /* The complete size of the PES header */

    /* All remaining entries in this structure are optional */
    guint8 scrambling_control; /* 0x00  : Not scrambled/unspecified,
                                 * The following are according to ETSI TS 101 154
                                 * 0x01  : reserved for future DVB use
                                 * 0x10  : PES packet scrambled with Even key
                                 * 0x11  : PES packet scrambled with Odd key
                                 */
    PESHeaderFlags flags;

    guint64 PTS;  /* PTS (-1 if not present or invalid) */
    guint64 DTS;  /* DTS (-1 if not present or invalid) */
    guint64 ESCR;  /* ESCR (-1 if not present or invalid) */

    guint32 ES_rate; /* in bytes/seconds (0 if not present or invalid) */

    /* Only valid for _FAST_FORWARD, _FAST_REVERSE and _FREEZE_FRAME */
    PESFieldID field_id;
    /* Only valid for _FAST_FORWARD and _FAST_REVERSE */
    gboolean intra_slice_refresh;
    guint8 frequency_truncation;
    /* Only valid for _SLOW_FORWARD and _SLOW_REVERSE */
    guint8 rep_cntrl;

    guint8 additional_copy_info; /* Private data */
    guint16 previous_PES_packet_CRC;

    /* Extension fields */
    const guint8* private_data;   /* PES_private_data, 16 bytes long */
    guint8 pack_header_size;  /* Size of pack_header in bytes */
    const guint8* pack_header;
    gint8 program_packet_sequence_counter; /* -1 if not present or invalid */
    gboolean MPEG1_MPEG2_identifier;
    guint8 original_stuff_length;

    guint32 P_STD_buffer_size; /* P-STD buffer size in bytes (0 if invalid
                                * or not present */

    guint8 stream_id_extension; /* Public range (0x00 - 0x3f) only valid if stream_id == ID_EXTENDED_STREAM_ID
                                  * Private range (0x40 - 0xff) can be present in any stream type */

    gsize extension_field_length;   /* Length of remaining extension field data */
    const guint8* stream_id_extension_data; /* Valid if extension_field_length != 0 */
} PESHeader;

/* MPEG_TO_GST calculation requires at least 17 extra bits (100000)
 * Since maximum PTS/DTS value is coded with 33bits, we are
 * safe to use direct calculation (17+33 < 63) */
#define MPEGTIME_TO_GSTTIME(t) ((t) * (guint64)100000 / 9)

typedef struct _TsSegment {
    GObject parent;

    GstElement element;
    GstPad *sinkpad, *srcpad;

    gint64 bitrate;

    /* Transport Stream segments MUST contain a single MPEG-2 Program;
     * playback of Multi-Program Transport Streams is not defined.  Each
     * Transport Stream segment SHOULD contain a PAT and a PMT at the start
     * of the segment - or have a Media Initialization Section declared in
     * the Media Playlist */
    guint program_number;
    guint16 pmt_pid;
    const GstMpegtsPMT *pmt;

    /* arrays that say whether a pid is a known psi pid or a pes pid */
    /* Use MPEGTS_BIT_* to set/unset/check the values */
    guint8 *known_psi;
    guint8 *is_pes;
    /* Whether we saw a PAT yet */
    gboolean seen_pat;
    gboolean seen_pmt;
    guint16 video_pid;
    /* Reference offset */
    //guint64 refoffset;
    GPtrArray *pat;
    PESHeader pes_header;
    guint8 *data;
    /* Amount of bytes in current ->data */
    guint current_size;
    /* Size of ->data */
    guint allocated_size;

    GstClockTime PTS;
    /* Current PTS for the stream (in running time) */
    GstClockTime pre_pts;
    GstClockTime current_pts;
    GstClockTime duration;
    GstH264SPS sps;
    GstH264SEIMessage sei;
    GstH264SliceHdr slice_hdr, pre_slice_hdr;
    GstH264NalUnit nalu, pre_nalu;
    guint field_pic_flag;
    guint pic_struct;
    gint fps_num;
    gint fps_den;
    GstClockTime frame_duration;
    GstClockTime frames_accumulate;

    /* current offset of the tip of the adapter */
    GstAdapter *adapter;
    guint64 offset;
    guint16 packet_size;
    const guint8 *map_data;
    gsize map_offset;
    gsize map_size;
    gboolean need_sync;

    /* whether we saw a idr */
    gboolean seen_idr;
    gsize pes_packet_size;
    guint8 *pes_packet;
    GstClockTime pes_packet_duration;
    GstH264NalParser *h264parser;
} TsSegment;

typedef struct _TsSegmentClass {
    GstElementClass parent_class;
} TsSegmentClass;

#define TYPE_TS_SEGMENT            (ts_segment_get_type())
#define TS_SEGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),TYPE_TS_SEGMENT,TsSegment))
#define TS_SEGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),TYPE_TS_SEGMENT,TsSegmentClass))
#define IS_TS_SEGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),TYPE_TS_SEGMENT))
#define IS_TS_SEGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),TYPE_TS_SEGMENT))

GType ts_segment_get_type (void);

gboolean ts_segment_plugin_init (GstPlugin * plugin);

#endif /* __TSSEGMENT_H__ */
