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
#include <gst/codecparsers/gsth265parser.h>

#define MPEGTS_NORMAL_PACKETSIZE 188
#define MPEGTS_M2TS_PACKETSIZE 192
#define CONTINUITY_UNSET 255
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF
#define PACKET_SYNC_BYTE 0x47
#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE 208

#define MPEGTS_BIT_SET(field, offs) ((field)[(offs) >> 3] |=  (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_IS_SET(field, offs) ((field)[(offs) >> 3] &   (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_UNSET(field, offs) ((field)[(offs) >> 3] &= ~(1 << ((offs) & 0x7)))
#define FLAGS_CONTINUITY_COUNTER(f) (f & 0x0f)
#define FLAGS_HAS_AFC(f) (f & 0x20)
#define FLAGS_HAS_PAYLOAD(f) (f & 0x10)
#define MPEGTS_AFC_PCR_FLAG 0x10
#define MAX_MPEGTIME 8589934592

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
    NALU_DELIMITER = 2,
    NALU_SEI = 4,
    NALU_VPS = 8,
    NALU_SPS = 16,
    NALU_PPS = 32,
    NALU_PIC = 64,
    NALU_FRAME = 128,
    NALU_IDR = 256
} NaluParsingResult;

typedef struct {
    guint8 stream_id; /* See ID_* above */
    guint32 packet_length; /* The size of the PES header and PES data (if 0 => unbounded packet) */
    guint64 PTS; /* PTS (-1 if not present or invalid) */
    guint64 DTS; /* DTS (-1 if not present or invalid) */
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
    guint program_number;
    guint16 pmt_pid;
    const GstMpegtsPMT *pmt;
    /* arrays that say whether a pid is a known psi pid or a pes pid */
    /* Use MPEGTS_BIT_* to set/unset/check the values */
    guint8 *known_psi;
    /* Whether we saw a PAT yet */
    gboolean seen_pat;
    gboolean seen_pmt;
    guint16 video_pid;
    guint8 video_cc;
    guint8 video_stream_type;
    /* Reference offset */
    GPtrArray *pat;
    guint8 *data;
    /* Amount of bytes in current ->data */
    guint current_size;
    /* Size of ->data */
    guint allocated_size;

    /* current offset of the tip of the adapter */
    GstAdapter *adapter;
    guint64 offset;
    guint16 packet_size;
    const guint8 *map_data;
    gsize map_offset;
    gsize map_size;
    gboolean need_sync;

    PESHeader pes_header;
    GstClockTime PTS;
    /* Current PTS for the stream (in running time) */
    GstClockTime pre_pts;
    //GstClockTime current_pts;
    GstClockTime duration;

    GstH264NalParser *h264parser;
    GstH264SPS sps;
    GstH264SEIMessage sei;
    GstH264SliceHdr slice_hdr, pre_slice_hdr;
    GstH264NalUnit h264_nalu, h264_pre_nalu;
    guint field_pic_flag;
    guint pic_struct;

    GstH265Parser *h265parser;
    GstH265NalUnit h265_nalu, h265_pre_nalu;

    gint fps_num;
    gint fps_den;
    GstClockTime frame_duration;
    GstClockTime frames_accumulate;
    /* whether we saw a idr */
    gboolean seen_idr;
    gsize pes_packet_size;
    guint8 *pes_packet;
    GstClockTime pes_packet_duration;
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
