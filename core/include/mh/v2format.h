/* mh/v2format.h: the on-card / in-image layout of a "PSXRAW" V2 library, with explicit little
 * endian encode/decode helpers. No dependency except base (crc/sha, common.h).
 *
 * Everything below was recovered from v2_header_valid, v2_validate_image, v2_load_source,
 * recover_v2_header_from_body and v2_write_handle_direct of the original (docs/map/v2.md).
 * All offsets are bytes from the start of the card or image; LBA means 512-byte block.
 * All multi-byte fields are little endian.
 *
 *   0x000000  header A   (0xA00 slot: 0x930 header bytes + zero padding up to 0xA00)
 *   0x000A00  header B   (identical copy of A: the backup written FIRST on commit)
 *   0x001400  Case table 256 x 0x50                                         (0x5000)
 *   0x006400  Disc table 256 x 0x100                                       (0x10000)
 *   0x016400  unused, zero (0x400)
 *   0x016800  art region: 256 case-art slots of 0x2800 bytes, slot i = Case i (0x280000)
 *   0x296800  menu: 1..93 raw sectors of 2352 bytes, each in a 0xA00 slot     (LBA 0x14B4)
 *   0x2D0A00  global menu config "PSTCFG2" (0xA00 slot, 0x930 bytes used)      (LBA 0x1685)
 *   0x2D1400  menu music (raw bytes, up to 0x22EC00)                           (LBA 0x168A)
 *   0x500000  end of the fixed zone; disc packages start at LBA >= 0x2800
 *
 * The bytes 0x1400..0x500000 are rewritten as one block on commit; the headers are written
 * last (B, then A) so a card whose header A is invalid is "uncommitted". Header A at byte 0
 * doubles as the MBR sector (see MH_V2_HF_MBR).
 *
 * A disc package starts at LBA `base` (>= 0x2800, multiple of 256). The Disc record holds
 * every LBA, so readers never compute them, but the writer always produces:
 *   base+0x00 (0x0A00)  track map "PSTTRK1" (0x930 bytes + zero)
 *   base+0x05 (0x2800)  cheat blob (MH_CHEAT_BLOB_MAX), cheat_size bytes used, rest zero
 *   base+0x19 (0x0A00)  reserved/"extra" slot, zero (record fields extra_lba/extra_size)
 *   base+0x1E (0x2800)  Disc art (exactly 10240 bytes, an .art payload), zero if none
 *   base+0x32 ... base+0xFE  zero fill (the writer zeroes this gap)
 *   base+0xFF (0x200)   resume marker "PSTRSM1" (only while/after the disc was written)
 *   base+0x100          RAW data: `sectors` slots of 0xA00 (2352 data bytes + 208 zero bytes)
 * The first 0x32 blocks (0x6400 bytes) are what build_package_metadata produces. A layout
 * without MH_V2_DF_EXT (older writers) has a 0x1400 byte cheat area (extra at base+0xF) and
 * the art at base+0x14; readers still accept it.
 *
 * Writing rules that matter for the firmware: only 512-byte aligned writes for control
 * blocks, verified by read back; header B before header A; header A invalidated (first 512
 * bytes zeroed) before anything else is touched. */
#ifndef MH_V2FORMAT_H
#define MH_V2FORMAT_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== little endian helpers */

static inline uint16_t mh_v2_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t mh_v2_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t mh_v2_rd64(const uint8_t *p)
{
    return (uint64_t)mh_v2_rd32(p) | ((uint64_t)mh_v2_rd32(p + 4) << 32);
}
static inline void mh_v2_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void mh_v2_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void mh_v2_wr64(uint8_t *p, uint64_t v)
{
    mh_v2_wr32(p, (uint32_t)v);
    mh_v2_wr32(p + 4, (uint32_t)(v >> 32));
}

/* ======================================================================== fixed geometry */

#define MH_V2_BLOCK 512u              /* control blocks and LBAs */
#define MH_V2_SLOT 0xA00u             /* raw sector slot: 2352 data bytes + 208 zero bytes */
#define MH_V2_SLOT_BLOCKS 5u          /* a slot in 512-byte blocks */
#define MH_V2_SECTOR 0x930u           /* 2352 */
#define MH_V2_MIN_IMAGE 0x500000ull   /* smaller files are not V2 images (fixed zone) */
#define MH_V2_FIXED_ZONE 0x500000ull  /* bytes 0..0x500000 */
#define MH_V2_FIXED_BLOCKS 0x2800ull  /* first LBA a disc package may use */
#define MH_V2_PKG_ALIGN 0x100ull      /* package base LBA is a multiple of this (128 KiB) */
#define MH_V2_MAX_CASES 256
#define MH_V2_MAX_DISCS 256
#define MH_V2_MAX_PER_CASE 5
#define MH_V2_MAX_TRACKS 0x63         /* track count in a Disc record: 1..0x63 */

/* header slots */
#define MH_V2_HDR_SIZE 0x930u         /* bytes covered by the header CRC (+ the CRC itself) */
#define MH_V2_HDR_SLOT 0xA00u         /* a slot; bytes 0x930..0xA00 must be zero */
#define MH_V2_HDR_A_OFF 0ull
#define MH_V2_HDR_B_OFF 0xA00ull
#define MH_V2_HDR_CRC_OFF 0x92Cu      /* u32: CRC-32 of bytes 0..0x92B (mh_crc_record of 0x930) */

/* tables and regions (byte offsets) */
#define MH_V2_CASE_OFF 0x1400ull
#define MH_V2_CASE_SIZE 0x50u
#define MH_V2_CASE_TABLE 0x5000u      /* 256 * 0x50 */
#define MH_V2_DISC_OFF 0x6400ull
#define MH_V2_DISC_SIZE 0x100u
#define MH_V2_DISC_TABLE 0x10000u     /* 256 * 0x100 */
#define MH_V2_ART_OFF 0x16800ull
#define MH_V2_ART_SIZE 0x2800u        /* one .art payload: 10240 bytes */
#define MH_V2_ART_REGION 0x280000u    /* 256 * 0x2800, case art only */
#define MH_V2_MENU_OFF 0x296800ull
#define MH_V2_CFG_OFF 0x2D0A00ull
#define MH_V2_MUSIC_OFF 0x2D1400ull
#define MH_V2_MUSIC_MAX 0x22EC00u     /* music length 1..0x22EC00 (so it ends inside the zone) */
#define MH_V2_MENU_LBA 0x14B4ull
#define MH_V2_CFG_LBA 0x1685ull
#define MH_V2_MUSIC_LBA 0x168Aull
#define MH_V2_MENU_MAX_SECTORS 93     /* 1..93: 93 * 5 + 0x14B4 = 0x1685 */
/* bytes rewritten as one unit on commit, between the headers and the end of the fixed zone */
#define MH_V2_BODY_OFF 0x1400ull
#define MH_V2_BODY_SIZE 0x4FEC00ull   /* 0x500000 - 0x1400 */

/* fixed values of the header (see MhV2Header) */
#define MH_V2_HDR_UNIT 0x800u         /* u32 at 0x10 */
#define MH_V2_HDR_B_LBA 5ull          /* u64 at 0x14: LBA of header B (= slot size in blocks) */
#define MH_V2_HDR_CASE_LBA 10ull      /* u64 at 0x1C: 0x1400 / 512 */
#define MH_V2_HDR_DISC_LBA 0x32ull    /* u64 at 0x24: 0x6400 / 512 */
#define MH_V2_HDR_ART_LBA 0xB4ull     /* u64 at 0x2C: 0x16800 / 512 */

/* ============================================================================ the header */

#define MH_V2_MAGIC "PSXRAW"          /* 6 bytes at 0 */
#define MH_V2_VERSION 2               /* byte 6 */
#define MH_V2_HF_BASE 0x01            /* byte 7 bit 0: required */
#define MH_V2_HF_MBR 0x02             /* byte 7 bit 1: bytes 0x1B8..0x1FF hold a valid MBR */
/* Hybrid MBR inside header A (only validated when MH_V2_HF_MBR is set): 0x1B8 u32 disk
 * signature (the generation XOR 0x32545350, "PST2"), 0x1BE one partition entry:
 * status 0, CHS 00 02 00, type 0xDA, CHS FE FF FF, 0x1C6 u32 start LBA = 1, 0x1CA u32 sector
 * count = card blocks - 1 (0xFFFFFFFF when it does not fit), 0x1FE bytes 55 AA. */
#define MH_V2_MBR_SIG_XOR 0x32545350u
#define MH_V2_MBR_TYPE 0xDA

typedef struct MhV2Header {
    uint8_t version;      /* +6, 2 */
    uint8_t flags;        /* +7, MH_V2_HF_* */
    uint16_t case_count;  /* +8, <= 256, must equal the number of used Case records */
    uint16_t disc_count;  /* +10, <= 256, must equal the number of used Disc records */
    uint32_t generation;  /* +12, commit counter; selects the newer header when A and B differ */
    uint32_t unit;        /* +16, 0x800 */
    uint64_t b_lba;       /* +0x14, 5 */
    uint64_t case_lba;    /* +0x1C, 10 */
    uint64_t disc_lba;    /* +0x24, 0x32 */
    uint64_t art_lba;     /* +0x2C, 0xB4 */
    uint64_t menu_lba;    /* +0x34, 0x14B4 */
    uint32_t menu_sectors;/* +0x3C, 1..93 (each 2352 bytes in a 0xA00 slot) */
    uint64_t cfg_lba;     /* +0x40, 0x1685 */
    uint64_t art_size;    /* +0x48, 0x2800 */
    uint32_t crc_cases;   /* +0x50, CRC-32 of the 0x5000 byte Case table */
    uint32_t crc_discs;   /* +0x54, CRC-32 of the 0x10000 byte Disc table */
    uint32_t crc_art;     /* +0x58, CRC-32 of the 0x280000 byte art region */
    uint32_t crc_menu;    /* +0x5C, CRC-32 over the menu: 0x930 bytes of each slot, in order */
    uint8_t menu_sha1[20];/* +0x60, SHA-1 over the same bytes */
    uint32_t mbr_signature; /* +0x1B8, only meaningful with MH_V2_HF_MBR */
    uint32_t mbr_sectors;   /* +0x1CA, partition length in blocks */
} MhV2Header;

/* orig: v2_header_valid on a buffer of at least 0x930 bytes. Checks magic, version 2,
 * flags bit 0, counts <= 256, the constants (unit 0x800, LBAs 5, 10, 0x32, 0xB4, 0x14B4,
 * 0x1685, art size 0x2800), menu_sectors 1..93, the MBR fields when flagged, and the CRC. */
bool mh_v2_header_valid(const uint8_t *h);
/* orig: v2_header_raw_valid on a buffer of 0xA00 bytes: valid plus bytes 0x930..0xA00 zero.
 * (sd_v2_header_raw_valid is the same test, it just checks the CRC with plain crc32 and
 * does not test the MBR fields or the counts; see mh_v2_sd_header_raw_valid in v2.h.) */
bool mh_v2_header_raw_valid(const uint8_t *h);
/* Decodes the fields (no validation). */
void mh_v2_header_decode(const uint8_t *h, MhV2Header *out);
/* Encodes a complete 0xA00 slot: zero fill, constants, fields from `in` (version, flags,
 * counts, generation, menu_sectors, CRCs, sha1, mbr_*), MBR bytes when flags has MH_V2_HF_MBR
 * and mbr_sectors was set by the caller (the writer sets it to blocks-1 or 0xFFFFFFFF; with
 * the flag set and mbr_sectors == 0 the slot would not validate), and finally the CRC at
 * 0x92C. The constants of `in` are ignored: they always get the fixed values. */
void mh_v2_header_encode(const MhV2Header *in, uint8_t out[MH_V2_HDR_SLOT]);
/* The value the writer stores in mbr_sectors for a card of `card_blocks` 512-byte blocks:
 * card_blocks - 1, or 0xFFFFFFFF when that does not fit in 32 bits. card_blocks must be > 1. */
uint32_t mh_v2_mbr_sectors(uint64_t card_blocks);
/* Header used when only the body survives (recover_v2_header_from_body): flags 1 (no MBR),
 * generation 1, counts and menu_sectors from the arguments, CRCs of the two tables, CRC of
 * the art region / menu fields left 0 (they are not verified in a recovered header). */
void mh_v2_header_recovered(uint16_t cases, uint16_t discs, uint32_t menu_sectors,
                            uint32_t crc_cases, uint32_t crc_discs, uint8_t out[MH_V2_HDR_SLOT]);

/* ====================================================================== Case record (0x50) */

#define MH_V2_CASE_MAGIC "CASE"       /* 4 bytes at 0 (the original compared 4 bytes of "CASE\0") */
#define MH_V2_CASE_NAME_MAX 0x20      /* +8, NUL terminated, at most 31 characters */
#define MH_V2_CASE_CODE_MAX 0x10      /* +0x28, NUL terminated, at most 15 characters */
#define MH_V2_ART_NONE 0xFFFFu

typedef struct MhV2CaseRec {
    uint8_t used;           /* +4, 0 = empty slot (the whole record is then ignored) */
    uint8_t disc_count;     /* +5, 1..5 */
    uint8_t index;          /* +6, must equal the table position */
    uint8_t variant;        /* +7, copied from the project Case (+4 byte 1); opaque */
    char name[MH_V2_CASE_NAME_MAX + 1]; /* +8; decode always adds a NUL (the field may be unterminated on a corrupt card) */
    char code[MH_V2_CASE_CODE_MAX + 1]; /* +0x28 (the Case serial), NUL added by decode */
    uint16_t art_id;        /* +0x38, 0xFFFF = none, else must equal the Case index */
    uint8_t disc_ids[5];    /* +0x3A, Disc table indexes; entries >= disc_count must be 0 */
    uint8_t long_name;      /* +0x3F, long names extension: 0 = name is whole, 1 = continues in case_ext of slot 0 (docs/LONG_NAMES.md) */
    uint32_t crc;           /* +0x4C, CRC-32 of bytes 0..0x4B */
} MhV2CaseRec;

/* Decode does not validate; `crc_ok` (may be NULL) tells whether the stored CRC matches. */
void mh_v2_case_decode(const uint8_t rec[MH_V2_CASE_SIZE], MhV2CaseRec *out, bool *crc_ok);
/* Encodes magic, fields and the CRC. */
void mh_v2_case_encode(const MhV2CaseRec *in, uint8_t rec[MH_V2_CASE_SIZE]);
/* True when the 4 magic bytes are "CASE". */
bool mh_v2_case_magic(const uint8_t rec[MH_V2_CASE_SIZE]);
/* Long names (docs/LONG_NAMES.md): +0x3F is 0 or 1, bytes +0x40..+0x4B are zero, and with 1 the
 * name field holds exactly 31 printable characters (0x20..0x7E) and a NUL. */
bool mh_v2_case_ext_valid(const uint8_t rec[MH_V2_CASE_SIZE]);

/* ===================================================================== Disc record (0x100) */

#define MH_V2_DISC_MAGIC "DISC"       /* 4 bytes at 0 */
#define MH_V2_DF_TRACKS_FIXED 0x1     /* flags bit 0 = project disc flag MH_DF_TRACKS_FIXED (0x40) */
#define MH_V2_DF_EXT 0x2              /* flags bit 1: extended package (0x2800 cheat area, art at +0x1E) */
#define MH_V2_DF_MASK 0x3             /* flags must be < 4 */
#define MH_V2_SERIAL_MAX 0x10
#define MH_V2_TITLE_MAX 0x20

/* Long names extension (card format 2.1), Disc record bytes +0x94..+0xFB:
 *   +0x94 "LNX1"  +0x98 title_ext_len (0..40)  +0x99 case_ext_len (0..40, slot 0 only)
 *   +0x9A title_ext[40]  +0xC2 case_ext[40]  +0xEA 18 zero bytes.
 * The 31 character base stays in the 0x20 byte field; the rest of the name goes in the extension. */
#define MH_V2_LNX_OFF 0x94u
#define MH_V2_LNX_MAGIC "LNX1"
#define MH_V2_LNX_EXT_MAX 40u
#define MH_V2_NAME_BASE 31u           /* characters kept in the 0x20 byte fields */
#define MH_V2_NAME_LONG_MAX 71u       /* base + extension */
#define MH_V2_CASE_FLAG_OFF 0x3Fu

typedef struct MhV2DiscRec {
    uint8_t used;           /* +4 */
    uint8_t id;             /* +5, must equal the table position */
    uint8_t case_index;     /* +6 */
    uint8_t slot;           /* +7, position inside the Case */
    uint32_t flags;         /* +8, MH_V2_DF_* */
    char serial[MH_V2_SERIAL_MAX + 1]; /* +0xC, NUL added by decode */
    char title[MH_V2_TITLE_MAX + 1]; /* +0x1C (short name, 31 characters at most), NUL added by decode */
    uint64_t base_lba;      /* +0x3C package start: >= 0x2800, multiple of 256 */
    uint64_t track_lba;     /* +0x44 track map, == base_lba */
    uint64_t raw_lba;       /* +0x4C raw data, == base_lba + 0x100 */
    uint64_t raw_blocks;    /* +0x54 == sectors * 5 */
    uint32_t sectors;       /* +0x5C 2352-byte sectors, > 0 */
    uint8_t track_count;    /* +0x60 1..0x63 */
    uint8_t bin_count;      /* +0x61 number of BIN files of the source CUE (informational) */
    uint16_t art_id;        /* +0x62 0xFFFF = no disc art, else must equal the Disc id */
    uint64_t cheat_lba;     /* +0x64 == base_lba + 5 */
    uint32_t cheat_size;    /* +0x6C bytes of the cheat blob, 0 = none, <= 0x2800 (0x1400 without EXT) */
    uint64_t extra_lba;     /* +0x70 == base_lba + 0x19 (+0xF without EXT) */
    uint32_t extra_size;    /* +0x78 == 0xA00 */
    uint32_t crc32;         /* +0x7C CRC-32 of the RAW data (0x930 bytes of each slot) */
    uint8_t sha1[20];       /* +0x80 SHA-1 over the same bytes */
    bool has_ext;           /* +0x94 magic "LNX1" present */
    uint8_t title_ext_len;  /* +0x98 raw (may be > 40 on a corrupt card) */
    uint8_t case_ext_len;   /* +0x99 raw */
    char title_ext[MH_V2_LNX_EXT_MAX + 1]; /* +0x9A, NUL added by decode */
    char case_ext[MH_V2_LNX_EXT_MAX + 1];  /* +0xC2, NUL added by decode */
    uint32_t crc;           /* +0xFC CRC-32 of bytes 0..0xFB */
} MhV2DiscRec;

void mh_v2_disc_decode(const uint8_t rec[MH_V2_DISC_SIZE], MhV2DiscRec *out, bool *crc_ok);
void mh_v2_disc_encode(const MhV2DiscRec *in, uint8_t rec[MH_V2_DISC_SIZE]);
bool mh_v2_disc_magic(const uint8_t rec[MH_V2_DISC_SIZE]);
/* Reserved bytes +0x94..+0xFB of a Disc record (docs/LONG_NAMES.md). Without the magic they must all
 * be zero. With it: lengths <= 40, bytes after each length zero, extension bytes printable
 * (0x20..0x7E), case_ext_len zero unless slot == 0, +0xEA..+0xFB zero, and a non zero length needs
 * the matching 0x20 byte field to be 31 printable characters and a NUL. `case_rec` (may be NULL)
 * is the record of the Case that owns the Disc and supplies the Case name field for case_ext_len. */
bool mh_v2_disc_ext_valid(const uint8_t rec[MH_V2_DISC_SIZE], const uint8_t *case_rec);
/* Full Disc title: the base field plus title_ext when the record carries a valid extension.
 * Always NUL terminated, never longer than 71 characters. */
void mh_v2_disc_full_title(const uint8_t rec[MH_V2_DISC_SIZE], char *out, size_t n);
/* Full Case name from a Case record and the Disc record in slot 0 (may be NULL). The extension is
 * used only when the Case is flagged and the Disc supplies a valid case_ext; otherwise the 31
 * character base name is returned (a reader never fails over it). */
void mh_v2_case_full_name(const uint8_t case_rec[MH_V2_CASE_SIZE], const uint8_t *slot0_rec, char *out, size_t n);
/* Splits `name` for the writer: base (32 byte field, 31 characters + NUL) and extension. Characters
 * outside 0x20..0x7E become spaces and the name is cut at 71. Returns the extension length. */
unsigned mh_v2_name_split(const char *name, char base[MH_V2_TITLE_MAX], char ext[MH_V2_LNX_EXT_MAX + 1]);
/* LBAs of the parts of a package with MH_V2_DF_EXT, from its base LBA. */
#define MH_V2_PKG_TRACK(base) ((base) + 0)
#define MH_V2_PKG_CHEAT(base) ((base) + 5)
#define MH_V2_PKG_EXTRA(base) ((base) + 0x19)
#define MH_V2_PKG_ART(base) ((base) + 0x1E)
#define MH_V2_PKG_META_BYTES 0x6400u   /* track map + cheats + extra + art = 50 blocks */
#define MH_V2_PKG_META_BLOCKS 0x32u
#define MH_V2_PKG_CHEAT_BYTES 0x2800u  /* reserved cheat area (MH_CHEAT_BLOB_MAX) */
#define MH_V2_PKG_MARKER(raw_lba) ((raw_lba) - 1)  /* resume marker block */
/* Package size in blocks: 0x100 + sectors * 5. */
#define MH_V2_PKG_BLOCKS(sectors) (0x100ull + (uint64_t)(sectors) * 5ull)
/* Art id stored in a record: 0xFFFF when absent, the index otherwise. */

/* ================================================================== track map (PSTTRK1) */

/* The sector validator is mh_track_sector_valid in cdsector.h. Layout (0x930 bytes, stored
 * in a 0xA00 slot at the Disc record track_lba):
 *   +0  "PSTTRK1" (7 bytes) +7 zero   +8 u8 track count 1..99   +9 u8 0x10 (entry size)
 *   +0xA u16 flags (bit 0: SBI block present)   +0xC u32 CRC-32 of the sector with this
 *   field zeroed   +0x10 entries of 16 bytes   +0x640 SBI block (u16 length 4..0x2E8,
 *   u32 CRC-32 of the data, then the data which starts with "SBI\0") */
#define MH_V2_TRK_MAGIC "PSTTRK1"
#define MH_V2_TRK_ENTRY_OFF 0x10u
#define MH_V2_TRK_ENTRY_SIZE 16u
#define MH_V2_TRK_F_SBI 0x1u
#define MH_V2_TRK_SBI_OFF 0x640u
#define MH_V2_SBI_MAX 0x2E8u
#define MH_V2_SBI_MAGIC "SBI\0"

typedef struct MhV2TrackEntry {
    uint32_t start;     /* +0 first sector of the track including its pregap (INDEX 00 or index1 - pregap) */
    uint32_t index1;    /* +4 INDEX 01, absolute sector on the disc */
    uint32_t length;    /* +8 sectors from `start` to the next track's start (last: to the end) */
    uint8_t type;       /* +0xC 0 audio, 1 MODE1, 2 MODE2 */
    uint8_t kind_bits;  /* +0xD bit 1: DCP flag, bit 2: data track */
    uint8_t flag_bits;  /* +0xE bit 0: has INDEX 00, bit 1: DCP, bit 2: ISRC */
} MhV2TrackEntry;
void mh_v2_trk_entry_decode(const uint8_t e[16], MhV2TrackEntry *out);
void mh_v2_trk_entry_encode(const MhV2TrackEntry *in, uint8_t e[16]);

/* ====================================================================== menu config sector */

/* "PSTCFG2" at 0x2D0A00 (0xA00 slot, 0x930 bytes used, rest zero):
 *   +0 "PSTCFG2\0"  +8 u16 version 1  +0xA u16 1  +0xC..0x1B zero
 *   +0x1C u64 music LBA (0x168A)  +0x24 u32 music length (0 = none, <= 0x22EC00)
 *   +0x28 u32 CRC-32 of the music bytes  +0x2C 0x4C bytes of text:
 *   "theme=umbrella\nmusic=embedded:umbrella.vag\nmusic_loop=true\nmusic_volume=68\n"
 *   +0x92C u32 CRC-32 of bytes 0..0x92B.  The rest is zero. */
#define MH_V2_CFG_MAGIC "PSTCFG2"
#define MH_V2_CFG_SIZE 0x930u
#define MH_V2_CFG_CRC_OFF 0x92Cu
#define MH_V2_CFG_TEXT_OFF 0x2Cu
#define MH_V2_CFG_TEXT_LEN 0x4Cu
#define MH_V2_CFG_TEXT \
    "theme=umbrella\nmusic=embedded:umbrella.vag\nmusic_loop=true\nmusic_volume=68\n"
typedef struct MhV2Config {
    uint16_t version;     /* +8 */
    uint16_t version2;    /* +0xA */
    uint64_t music_lba;   /* +0x1C */
    uint32_t music_size;  /* +0x24 */
    uint32_t music_crc;   /* +0x28 */
} MhV2Config;
bool mh_v2_cfg_valid(const uint8_t *cfg); /* magic, version 1, CRC (as v2_validate_image) */
void mh_v2_cfg_decode(const uint8_t *cfg, MhV2Config *out);
/* Builds the 0x930 byte sector (zero padded to a 0xA00 slot by the caller buffer size). */
void mh_v2_cfg_encode(uint32_t music_size, uint32_t music_crc, uint8_t out[MH_V2_SLOT]);

/* ================================================================== session / resume blocks */

/* Written by the writer, one 512-byte block each, always with write+flush+readback:
 *   block 0 while a write transaction runs: "PSTWIP1" (7 bytes) then zeros. While it is there
 *     the card has no valid header A; recover_v2_header_from_body refuses such a card.
 *   base+0xFF (raw_lba - 1), after a disc was written and confirmed: "PSTRSM1\0" (8 bytes),
 *     +8 u64 timestamp (QueryPerformanceCounter in the original), +0x40 the 0x100 byte Disc
 *     record, optionally +0x140 "PSTVFY1\0" and +0x148 u64 verification stamp, +0x1FC u32
 *     CRC-32 of bytes 0..0x1FB.
 */
#define MH_V2_WIP_MAGIC "PSTWIP1"
#define MH_V2_RSM_MAGIC "PSTRSM1"
#define MH_V2_VFY_MAGIC "PSTVFY1"
#define MH_V2_RSM_TIME_OFF 8u
#define MH_V2_RSM_REC_OFF 0x40u
#define MH_V2_RSM_VFY_OFF 0x140u
#define MH_V2_RSM_VFY_VAL_OFF 0x148u
#define MH_V2_RSM_CRC_OFF 0x1FCu
typedef struct MhV2Resume {
    uint64_t stamp;
    uint8_t disc_rec[MH_V2_DISC_SIZE];
    bool has_verify;
    uint64_t verify_stamp;
} MhV2Resume;
/* Builds / parses the 512-byte block. parse returns false on bad magic or CRC. */
void mh_v2_resume_encode(const MhV2Resume *in, uint8_t blk[512]);
bool mh_v2_resume_decode(const uint8_t blk[512], MhV2Resume *out);

/* ====================================================================== GPT tail handling */

/* Cards formatted by an OS carry a backup GPT in their last blocks. After a write the tool
 * (clear_backup_gpt_tail) looks at the last block of the card: if it starts with "EFI PART"
 * it zeroes (verified, 512 bytes at a time) every non-zero block in the last 1 MiB that lies
 * beyond the used part of the card. */
#define MH_V2_GPT_SIG "EFI PART"
#define MH_V2_GPT_TAIL 0x100000ull

#ifdef __cplusplus
}
#endif
#endif
