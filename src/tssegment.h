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
#define MPEGTS_DVB_ASI_PACKETSIZE 204
#define MPEGTS_ATSC_PACKETSIZE 208
#define CONTINUITY_UNSET 255
#define VERSION_NUMBER_UNSET 255
#define TABLE_ID_UNSET 0xFF
#define PACKET_SYNC_BYTE 0x47
#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE MPEGTS_ATSC_PACKETSIZE

#define MPEGTS_BIT_SET(field, offs)    ((field)[(offs) >> 3] |=  (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_IS_SET(field, offs) ((field)[(offs) >> 3] &   (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_UNSET(field, offs)  ((field)[(offs) >> 3] &= ~(1 << ((offs) & 0x7)))
#define FLAGS_CONTINUITY_COUNTER(f)    (f & 0x0f)
#define FLAGS_HAS_AFC(f)               (f & 0x20)
#define FLAGS_HAS_PAYLOAD(f)           (f & 0x10)
#define MPEGTS_AFC_PCR_FLAG            0x10

#define MAX_PCR 2576980378112

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

typedef enum
{
        PENDING_PACKET_EMPTY = 0,     /* No pending packet/buffer
                                       * Push incoming buffers to the array */
        PENDING_PACKET_HEADER,        /* PES header needs to be parsed
                                       * Push incoming buffers to the array */
        PENDING_PACKET_BUFFER,        /* Currently filling up output buffer
                                       * Push incoming buffers to the bufferlist */
        PENDING_PACKET_DISCONT        /* Discontinuity in incoming packets
                                       * Drop all incoming buffers */
} PendingPacketState;

typedef struct
{
        guint16 pid;
        guint   continuity_counter;

        /* Section data (always newly allocated) */
        guint8 *section_data;
        /* Current offset in section_data */
        guint16 section_offset;

        /* Values for pending section */
        /* table_id of the pending section_data */
        guint8  table_id;
        guint   section_length;
        guint8  version_number;
        guint16 subtable_extension;
        guint8  section_number;
        guint8  last_section_number;

        GSList *subtables;

        /* Upstream offset of the data contained in the section */
        guint64 offset;

        /* Output data */
        PendingPacketState state;
} TSPacketStream;

/* PCR/offset structure */
typedef struct _PCROffset
{
        /* PCR value (units: 1/27MHz) */
        guint64 pcr;

        /* The offset (units: bytes) */
        guint64 offset;
} PCROffset;

/* PCROffsetGroup: A group of PCR observations.
 * All values in a group have got the same reference pcr and
 * byte offset (first_pcr/first_offset).
 */
#define DEFAULT_ALLOCATED_OFFSET 16
typedef struct _PCROffsetGroup
{
        /* Flags (see PCR_GROUP_FLAG_* above) */
        guint flags;

        /* First raw PCR of this group. Units: 1/27MHz.
         * All values[].pcr are differences against first_pcr */
        guint64 first_pcr;
        /* Offset of this group in bytes.
         * All values[].offset are differences against first_offset */
        guint64 first_offset;

        /* Dynamically allocated table of PCROffset */
        PCROffset *values;
        /* number of PCROffset allocated in values */
        guint nb_allocated;
        /* number of *actual* PCROffset contained in values */
        guint last_value;

        /* Offset since the very first PCR value observed in the whole
         * stream. Units: 1/27MHz.
         * This will take into account gaps/wraparounds/resets/... and is
         * used to determine running times.
         * The value is only guaranteed to be 100% accurate if the group
         * does not have the ESTIMATED flag.
         * If the value is estimated, the pcr_offset shall be recalculated
         * (based on previous groups) whenever it is accessed.
         */
        guint64 pcr_offset;

        /* FIXME : Cache group bitrate ? */
} PCROffsetGroup;

/* Number of PCRs needed before bitrate estimation can start */
/* Note: the reason we use 10 is because PCR should normally be
 * received at least every 100ms so this gives us close to
 * a 1s moving window to calculate bitrate */
#define PCR_BITRATE_NEEDED 10

/* PCROffsetCurrent: The PCR/Offset window iterator
 * This is used to estimate/observe incoming PCR/offset values
 * Points to a group (which it is filling) */
typedef struct _PCROffsetCurrent
{
        /* The PCROffsetGroup we are filling.
         * If NULL, a group needs to be identified */
        PCROffsetGroup *group;

        /* Table of pending values we are iterating over */
        PCROffset pending[PCR_BITRATE_NEEDED];

        /* base offset/pcr from the group */
        guint64 first_pcr;
        guint64 first_offset;

        /* The previous reference PCROffset
         * This corresponds to the last entry of the group we are filling
         * and is used to calculate prev_bitrate */
        PCROffset prev;

        /* The last PCROffset in pending[] */
        PCROffset last_value;

        /* Location of first pending PCR/offset observation in pending */
        guint first;
        /* Location of last pending PCR/offset observation in pending */
        guint last;
        /* Location of next write in pending */
        guint write;

        /* bitrate is always in bytes per second */

        /* cur_bitrate is the bitrate of the pending values: d(last-first) */
        guint64 cur_bitrate;

        /* prev_bitrate is the bitrate between reference PCROffset
         * and the first pending value. Used to detect changes
         * in bitrate */
        guint64 prev_bitrate;
} PCROffsetCurrent;

#define MAX_WINDOW 512

typedef struct _MpegTSPCR
{
        guint16 pid;

        /* Following variables are only active/used when
         * calculate_skew is TRUE */
        GstClockTime base_time;
        GstClockTime base_pcrtime;
        GstClockTime prev_out_time;
        GstClockTime prev_in_time;
        GstClockTime last_pcrtime;
        gint64 window[MAX_WINDOW];
        guint window_pos;
        guint window_size;
        gboolean window_filling;
        gint64 window_min;
        gint64 skew;
        gint64 prev_send_diff;

        /* Offset to apply to PCR to handle wraparounds */
        guint64 pcroffset;

        /* Used for bitrate calculation */
        /* List of PCR/offset observations */
        GList *groups;

        /* Current PCR/offset observations (used to update pcroffsets) */
        PCROffsetCurrent *current;
} MpegTSPCR;

typedef struct {
        gint16 pid;
        guint8 payload_unit_start_indicator;
        guint8 scram_afc_cc;
        const guint8 *payload;
        const guint8 *data_start;
        const guint8 *data_end;
        const guint8 *data;
        guint8 afc_flags;
        guint64 pcr;
        guint64 offset;
} TSPacket;

typedef struct
{
        guint8 table_id;
        /* the spec says sub_table_extension is the fourth and fifth byte of a 
         * section when the section_syntax_indicator is set to a value of "1". If 
         * section_syntax_indicator is 0, sub_table_extension will be set to 0 */
        guint16  subtable_extension;
        guint8   version_number;
        guint8   last_section_number;
        /* table of bits, whether the section was seen or not.
         * Use MPEGTS_BIT_* macros to check */
        /* Size is 32, because there's a maximum of 256 (32*8) section_number */
        guint8   seen_section[32];
} TSPacketStreamSubtable;

typedef struct _TSStream
{
        guint16             pid;
        guint8              stream_type;

        /* Content of the registration descriptor (if present) */
        guint32             registration_id;

        GstMpegTsPMTStream *stream;
} TSStream;

typedef struct _TsSegment {
        GObject parent;

        GstElement element;
        GstPad *sinkpad, *srcpad;

        TSPacketStream **streams;

        /* Transport Stream segments MUST contain a single MPEG-2 Program;
         * playback of Multi-Program Transport Streams is not defined.  Each
         * Transport Stream segment SHOULD contain a PAT and a PMT at the start
         * of the segment - or have a Media Initialization Section declared in
         * the Media Playlist */
        guint program_number;
        guint16 pmt_pid;
        const GstMpegTsPMT *pmt;

        /* arrays that say whether a pid is a known psi pid or a pes pid */
        /* Use MPEGTS_BIT_* to set/unset/check the values */
        guint8 *known_psi;
        guint8 *is_pes;
        /* Whether we saw a PAT yet */
        gboolean seen_pat;
        guint16 video_pid;
        /* Reference offset */
        //guint64 refoffset;
        GPtrArray *pat;
        /* whether we saw a key frame */
        gboolean seen_key_frame;

        guint8 *data;
        /* Amount of bytes in current ->data */
        guint current_size;
        /* Size of ->data */
        guint allocated_size;
        /* Current PTS for the stream (in running time) */
        GstClockTime pts;
        GstClockTime duration;

        //gboolean push_section;

        /* current offset of the tip of the adapter */
        GstAdapter *adapter;
        guint64 offset;
        guint16 packet_size;
        const guint8 *map_data;
        gsize map_offset;
        gsize map_size;
        gboolean need_sync;

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
