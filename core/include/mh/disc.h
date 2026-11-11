/* mh/disc.h: disc model, disc identification and the project (cases and discs).
 *
 * Map of original functions to these names: docs/map/disc.md. Binary formats of the embedded
 * database: docs/EMBEDDED_DATA.md.
 *
 * Parts:
 *   1. MhDisc, MhCase, MhProject: the data model. The original kept them in raw blocks
 *      (disc 0x43988 bytes, case 0x868, project 0x879a8) that every module poked at by
 *      offset. Here every field has a name; docs/map/disc.md lists the original offsets.
 *   2. PS1 serial recovery from a CUE/BIN or a raw V2 image (psserial_* and extractSerial*).
 *   3. CUE sheet parsing and writing.
 *   4. MhDb: the embedded database (disc index, Redump index, aliases, trackset index),
 *      lookups, disc verification and track map recovery.
 *   5. Project operations (cases, discs, ordering, limits) and "add a CUE to the project".
 *
 * Text is UTF-8. Messages are the original Portuguese texts, byte for byte. Notes on a disc
 * (MhDisc.notes) are joined with "\r\n" exactly like the original. */
#ifndef MH_DISC_H
#define MH_DISC_H

#include "mh/base.h"
#include "mh/cheat.h" /* MhCheatProfile, mh_cheat_profile_free */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================================= limits */

#define MH_DISC_MAX_FILES 99     /* BIN files per CUE ("More than 99 BIN files in the CUE.") */
#define MH_DISC_MAX_TRACKS 99
#define MH_DISC_MAX_SECTORS 450000 /* "safe limit of 450,000 CD sectors" */
#define MH_SECTOR_SIZE 2352        /* raw CD sector in a BIN */
#define MH_NAME_LONG 71            /* longest card name: 31 characters + the 40 of the long names extension */
#define MH_NAME_MULTI 61           /* longest Disc title of a Case with several Discs (the firmware adds " - DISCO n") */
#define MH_DISC_NAME_MAX 0x48      /* MhDisc.name capacity (71 characters + NUL) */
#define MH_DISC_TITLE_MAX 0x60     /* MhDisc.title capacity */
#define MH_NAME_FIT 0x48           /* buffer size of a name that fits the card: 71 characters + NUL (31 on firmware without the extension) */
#define MH_DISC_NOTES_MAX 0x300    /* MhDisc.notes capacity (original: 0x300 wide chars) */
#define MH_DISC_SERIAL_MAX 16      /* serial buffers: 15 characters + NUL */

#define MH_CASE_MAX_DISCS 5        /* discs per case */
#define MH_CASE_TITLE_MAX 0x48     /* Case name capacity (71 characters + NUL) */
#define MH_PROJECT_MAX_CASES 256   /* case slots */
#define MH_PROJECT_MAX_DISCS 256   /* discs in the whole project ("A V2 aceita no maximo 256 discos") */
#define MH_NEW_CASE_TITLE "NEW GAME"

/* ============================================================================ disc model */

/* MhDisc.kind (orig +0). 1 is what cue_parse creates; 4 and 5 are discs loaded from a V2
 * card or image (the V2 loader creates them; docs/map/disc.md). */
enum {
    MH_DISC_KIND_CUE = 1,
    MH_DISC_KIND_V2_DEVICE = 4, /* raw slot on a physical card (v2_drive) */
    MH_DISC_KIND_V2_IMAGE = 5   /* raw slot in an image file (source_path) */
};

/* MhDisc.status (orig +4); disc_status_text gives the label. 0 = empty. */
enum {
    MH_DISC_STATUS_EMPTY = 0,
    MH_DISC_STATUS_OK = 1,       /* "OK" */
    MH_DISC_STATUS_NOTE = 2,     /* "Review" */
    MH_DISC_STATUS_FIXABLE = 3,  /* "Fixable" (missing BIN: the user can locate it) */
    MH_DISC_STATUS_ERROR = 4     /* "Error" */
};

/* MhDisc.flags (orig +8). */
enum {
    MH_DF_STRUCT_OK = 0x1,       /* CUE/BIN structure validated */
    MH_DF_MISSING_BIN = 0x2,
    MH_DF_BAD_ALIGN = 0x4,       /* a BIN is not a multiple of 2352 bytes (or is empty) */
    MH_DF_SERIAL = 0x8,          /* serial[] holds an internal serial proven from the disc */
    MH_DF_DB_MATCH = 0x10,       /* db_index is valid */
    MH_DF_TRACKS_SUSPECT = 0x20, /* track count/types differ from the database */
    MH_DF_TRACKS_FIXED = 0x40,   /* track map rebuilt from the database (in memory) */
    MH_DF_INVALID = 0x80,        /* CUE rejected */
    MH_DF_INCOMPLETE = 0x100,    /* POSTGAP or virtual PREGAP: image possibly incomplete */
    MH_DF_TRACKSET_OK = 0x200,   /* physical edition confirmed by the full SHA-1 trackset */
    MH_DF_INTEGRITY_BAD = 0x400, /* integrity NOT confirmed (Redump DAT or trackset missing) */
    MH_DF_CASE_ART = 0x800,      /* case_art_path set */
    MH_DF_CD_ART = 0x1000,       /* cd_art_path set */
    MH_DF_SBI = 0x2000,          /* sbi_data / sbi_path usable */
    MH_DF_TRACKSET_CHECKED = 0x4000,
    MH_DF_FROM_V2 = 0x8000,      /* loaded from a V2 card or image */
    MH_DF_USER_NAMED = 0x10000,  /* name[] was typed by the user; databases must not rename */
    MH_DF_IDENTITY = 0x20000,    /* set together with MH_DF_TRACKSET_OK */
    MH_DF_KEEP_PACKAGE = 0x40000 /* kept_package() may reuse the on-card package */
};

/* One track (orig 0x1c bytes at disc+0x41f48 + 0x1c*i). */
typedef struct MhTrack {
    uint8_t number;      /* +0  1-based, contiguous */
    uint8_t kind;        /* +1  1 audio, 2 data (0 = unsupported mode in the CUE) */
    uint8_t mode;        /* +2  0 audio, 1 MODE1/2352, 2 MODE2/2352 */
    uint8_t flags;       /* +3  bit0 DCP flag, bit1 ISRC seen, bit2 has INDEX 00 */
    uint16_t file;       /* +4  index into MhDisc.files */
    uint16_t pad6;       /* +6 */
    uint32_t pregap;     /* +8  sectors between INDEX 00 and INDEX 01 */
    uint32_t index0;     /* +0xc  INDEX 00 (or INDEX 01 when absent), relative to its file */
    uint32_t index1;     /* +0x10 INDEX 01 relative to its file */
    uint32_t start;      /* +0x14 absolute sector of INDEX 01 */
    uint32_t length;     /* +0x18 sectors up to the next track's INDEX 01 / end of disc */
} MhTrack;

#define MH_TRACK_DCP 0x1
#define MH_TRACK_ISRC 0x2
#define MH_TRACK_HAS_INDEX0 0x4

_Static_assert(sizeof(MhTrack) == 0x1c, "MhTrack must stay 28 bytes (orig layout)");

/* One BIN (orig 0xa20 bytes at disc+0x34e8 + 0xa20*i). */
typedef struct MhDiscFile {
    char path[MH_PATH_MAX]; /* +0     resolved path of the BIN */
    char name[MH_PATH_MAX]; /* +0x800 name as written in the CUE (orig 0x104 wide chars) */
    uint64_t size;          /* +0xa08 bytes */
    uint32_t sectors;       /* +0xa10 size / 2352 */
    uint32_t start;         /* +0xa14 first sector of this BIN in the whole disc */
} MhDiscFile;

/* Result block of the serial recovery (orig 0x504 bytes, at disc+0x42a3c). */
typedef struct MhPsSerial {
    uint32_t status;        /* +0     psserial status, MH_PSS_* */
    uint32_t source;        /* +4     MH_PSSRC_* */
    uint32_t status_saved;  /* +8     copy made by disc_scan_serial (restored by the trackset match) */
    uint32_t source_saved;  /* +0xc */
    char serial[16];        /* +0x10  canonical serial, e.g. "SLUS-00969" */
    char token[32];         /* +0x20  the text found, e.g. "SLUS_009.69" */
    char path[512];         /* +0x40  ISO path of the file it came from */
    uint32_t cand_count;    /* +0x240 distinct serial candidates seen (may exceed 16) */
    char cand[16][16];      /* +0x244 first 16 candidates */
    uint32_t system_cnf_found; /* +0x344 */
    uint32_t boot_path_found;  /* +0x348 BOOT target exists in the ISO */
    uint32_t boot_exe_valid;   /* +0x34c it starts with "PS-X EXE" */
    uint32_t needs_hash;       /* +0x350 1 unless a trackset hash settled the edition */
    uint32_t iso_sectors;      /* +0x354 volume size declared by the PVD */
    uint32_t file_sectors;     /* +0x358 sectors actually in the image */
    uint32_t truncated;        /* +0x35c image shorter than the declared volume */
    char trackset_serial[32];  /* +0x360 serial of the matching trackset record */
    uint32_t trackset_id;      /* +0x380 record field at +0x20 (Redump-side id) */
    char message[192];         /* +0x384 */
    char message_saved[192];   /* +0x444 */
} MhPsSerial;

_Static_assert(sizeof(MhPsSerial) == 0x504, "MhPsSerial must stay 0x504 bytes (orig layout)");

enum { /* MhPsSerial.status (psserial_status_text) */
    MH_PSS_NOT_FOUND = 0,   /* "NOT_FOUND" */
    MH_PSS_STRONG = 1,      /* "STRONG_INTERNAL" */
    MH_PSS_PROBABLE = 2,    /* "PROBABLE" */
    MH_PSS_AMBIGUOUS = 3,   /* "AMBIGUOUS" */
    MH_PSS_IO_ERROR = 4,    /* "IO_ERROR" */
    MH_PSS_BAD_IMAGE = 5,   /* "INVALID_IMAGE" */
    MH_PSS_TRACKSET = 6     /* "TRACKSET_HASH_CONFIRMED" */
};
enum { /* MhPsSerial.source (psserial_source_text) */
    MH_PSSRC_NONE = 0,        /* "NONE" */
    MH_PSSRC_SYSTEM_CNF = 1,  /* "SYSTEM_CNF" */
    MH_PSSRC_EXE_NAME = 2,    /* "EXECUTABLE_NAME" */
    MH_PSSRC_EXE_CONTENT = 3, /* "EXECUTABLE_CONTENT" */
    MH_PSSRC_TRACKSET = 4     /* "HASH_TRACKSET" */
};

/* A disc: either a CUE/BIN set (kind 1) or a slot read from a V2 card/image (kind 4, 5).
 * Allocate with mh_calloc, release with mh_disc_free (which also frees the cheat profile). */
typedef struct MhDisc {
    uint32_t kind;              /* +0      MH_DISC_KIND_* */
    uint32_t status;            /* +4      MH_DISC_STATUS_* (mh_disc_refresh_status) */
    uint32_t flags;             /* +8      MH_DF_* */
    char serial[16];            /* +0xc    canonical serial or "" */
    char name[MH_DISC_NAME_MAX];   /* +0x1c   short name shown in lists (71 chars max) */
    char title[MH_DISC_TITLE_MAX]; /* +0x3d   full title (database / Redump / file name) */
    char cue_path[MH_PATH_MAX];    /* +0x9e   CUE file */
    char source_path[MH_PATH_MAX]; /* +0x89e  kind 5: image file holding the raw slot */
    char case_art_path[MH_PATH_MAX]; /* +0x109e  "<db>/art/default/<SERIAL>-CASE.art" or user file */
    char cd_art_path[MH_PATH_MAX];   /* +0x189e  "...-CD.art" (the "Disc Art", exactly 0x2800 bytes) */
    MhCheatProfile *cheats;     /* +0x20a0 owned */
    char sbi_path[MH_PATH_MAX]; /* +0x20a8 external SBI file (read by the package builder) */
    char notes[MH_DISC_NOTES_MAX]; /* +0x28a8 observations, "\r\n" separated */
    /* Verification / admission block (written by the V2 code and manager_admit; kept here so
     * mh_disc_free and the model are complete). */
    char verify_text[512];      /* +0x2ea8 text built by verification_issue_text */
    uint32_t verify_issues;     /* +0x32a8 bit set: 1 no game id, 2 differs from known version,
                                           4 size exceeds file, 8 sector format vs CUE,
                                           0x10 card data mismatch, 0x20 extra data on card,
                                           0x40/0x80 inconsistent sectors */
    uint32_t verify_accepted;   /* +0x32ac issues the user accepted */
    uint32_t verified;          /* +0x32b0 1 when a full read verified the disc */
    uint32_t verified_aux;      /* +0x32b4 (second word of the pair stored with verified) */
    uint32_t verify_crc32;      /* +0x32b8 */
    uint8_t verify_sha1[20];    /* +0x32bc */
    uint32_t cheat_regenerated; /* +0x32d0 hydrate_loaded_project */
    char cheat_note[256];       /* +0x32d4 note about the automatic cheat catalog */
    uint16_t nfiles;            /* +0x34d4 */
    uint16_t ntracks;           /* +0x34d6 */
    uint32_t total_sectors;     /* +0x34d8 */
    uint64_t total_bytes;       /* +0x34e0 total_sectors * 2352 */
    MhDiscFile files[MH_DISC_MAX_FILES];   /* +0x34e8 */
    MhTrack tracks[MH_DISC_MAX_TRACKS];    /* +0x41f48 */
    uint32_t crc32;             /* +0x42a1c CRC-32 of all BIN bytes (db_verify_redump) */
    uint8_t sha1[20];           /* +0x42a20 SHA-1 of all BIN bytes */
    uint64_t v2_image_tag;      /* +0x42a34 (set by the V2 loader) */
    uint32_t db_index;          /* +0x42a38 disc index record, 0xFFFFFFFF = none */
    MhPsSerial ps;              /* +0x42a3c */
    const uint8_t *sbi_data;    /* +0x42f40 SBI blob inside the MhDb trackset blob (not owned) */
    uint16_t sbi_len;           /* +0x42f48 */
    uint16_t card_index;        /* +0x42f4a 0xFFFF for CUE discs, slot on the card for V2 */
    uint32_t v2_drive;          /* +0x42f50 physical drive number (kind 4) */
    uint64_t v2_pkg_a;          /* +0x42f58 value returned by kept_package for a drive/slot match */
    uint64_t v2_raw_lba512;     /* +0x42f60 first 512-byte block of the raw slot */
    uint64_t v2_raw_blocks;     /* +0x42f68 blocks of the raw slot (= total_sectors * 5) */
    int32_t v2_slot;            /* +0x42f70 */
    uint64_t v2_card_blocks;    /* +0x42f78 */
    uint64_t v2_pkg_b;          /* +0x42f80 value returned by kept_package for a path/slot match */
    char v2_pkg_path[MH_PATH_MAX]; /* +0x42f88 image file whose package may be reused */
    uint8_t v2_marker[512];     /* +0x43788 session marker block copy */
    uint16_t title_base_len;    /* port only: length of title before the first "(tag)", 0 = unknown */
} MhDisc;

/* Case: up to 5 discs of one game (orig 0x868 bytes). */
typedef struct MhCase {
    uint32_t in_use;            /* +0 */
    uint8_t slot;               /* +4  index in MhProject.cases */
    uint8_t pad5;
    char serial[16];            /* +6  copied from the first disc that has one */
    char title[MH_CASE_TITLE_MAX]; /* +0x16 */
    char art_path[MH_PATH_MAX]; /* +0x38 case art file */
    uint8_t disc_count;         /* +0x838 */
    MhDisc *discs[MH_CASE_MAX_DISCS]; /* +0x840 owned */
} MhCase;

/* Project: 256 case slots (orig 0x879a8 bytes, global DAT_140e7d490). */
typedef struct MhProject {
    uint32_t dirty;             /* +0     pending changes ("APLICAR ALTERACOES") */
    uint32_t card_loaded;       /* +4     a V2 card is loaded (editing in place) */
    uint32_t image_loaded;      /* +8     a V2 image file is loaded ("V2 image loaded") */
    uint32_t reserved_c;        /* +0xc   initialised to 1, never read in the code seen */
    uint16_t case_count;        /* +0x10  filled by mh_project_recount */
    uint16_t disc_count;        /* +0x12 */
    int32_t device;             /* +0x14  physical drive of the loaded card, -1 none */
    uint8_t reserved_18[8];     /* +0x18 */
    char device_name[192];      /* +0x20  (orig 0x60 wide chars) */
    char device_label[128];     /* +0xe0  (orig 0x40 wide chars) */
    char image_path[MH_PATH_MAX]; /* +0x160 loaded image file */
    uint64_t cache_tag;         /* +0x960 cleared by hydrate/use_default_menu */
    uint8_t menu_defaults[64];  /* +0x1160 user menu defaults record (menu-defaults.dat) */
    uint32_t has_menu_defaults; /* +0x11a0 */
    MhCase cases[MH_PROJECT_MAX_CASES]; /* +0x11a8 */
} MhProject;

/* ======================================================================== text helpers */

const char *mh_disc_status_text(unsigned status);       /* orig: disc_status_text */
const char *mh_psserial_status_text(unsigned status);   /* orig: psserial_status_text */
const char *mh_psserial_source_text(unsigned source);   /* orig: psserial_source_text */

/* Appends msg to notes (capacity MH_DISC_NOTES_MAX), preceded by "\r\n" when notes is not
 * empty. orig: obs_add and the open-coded w_cat pairs. */
void mh_disc_note(MhDisc *d, const char *msg);

/* ===================================================================== serial recovery */

/* orig: canonical_serial. Accepts "AAA-NNNNN[N][L]" and "AAAA-NNNNN[N][L]" (letters and digits
 * in the prefix) with an optional trailing "-AAA[A]" that repeats the prefix and trailing
 * dashes; out receives the upper-cased form without the repeated suffix. */
bool mh_canonical_serial(const char *in, char out[16]);

/* orig: psserial_parse_filename. name is an ISO file name ("SLUS_009.69;1", "SLUS-00969.EXE"):
 * ";version" and ".EXE" are dropped, then it must be a whole serial token. raw receives the
 * token as written, canonical the "SLUS-00969" form. */
bool mh_psserial_parse_filename(const char *name, char raw[32], char canonical[16]);

void mh_psserial_init(MhPsSerial *r); /* zero, needs_hash = 1 */

/* orig: extractSerialDetailed. Walks the ISO 9660 file system of the first BIN (2352, mode 1
 * or mode 2 form 1) or of a plain 2048 byte ISO and fills *r (see MhPsSerial). Returns r->status.
 * Order of evidence: SYSTEM.CNF BOOT/BOOT2 target (STRONG_INTERNAL from its file name, PROBABLE
 * from the executable content), PSX.EXE content, other PS-X EXE files (PROBABLE/AMBIGUO). */
uint32_t mh_psserial_extract(MhPsSerial *r, const char *path);
/* orig: extractSerialRaw. Same for a V2 raw slot: `sectors` sectors of 2352 bytes stored in
 * 2560 byte slots starting at 512-byte block lba512 of path (device or image file). */
uint32_t mh_psserial_extract_raw(MhPsSerial *r, const char *path, uint64_t lba512, uint32_t sectors);
/* orig: extractSerial. True and serial (16 bytes) when status is STRONG_INTERNAL or PROBABLE. */
bool mh_psserial_extract_serial(char serial[16], const char *path);
/* orig: cue_scan_serial_file. out receives the serial found in the BIN at bin_path. */
bool mh_cue_scan_serial_file(const char *bin_path, char *out, size_t n);

/* =================================================================================== CUE */

/* orig: msf. "mm:ss:ff" (ss < 60, ff < 75) to frames; trailing text is ignored. */
bool mh_cue_msf(const char *s, uint32_t *frames);
/* orig: decode_cue. Reads the file and returns it as NUL terminated UTF-8 text in *text (free
 * with mh_free): UTF-8 when valid, otherwise Windows-1252. False for unreadable, empty (like the original) or
 * >= 64 MiB (deviation: the original allowed 2 GiB). */
bool mh_cue_read_text(const char *path, char **text);
/* orig: cue_count_files. Number of lines starting with "FILE ". */
int mh_cue_count_files(const char *path);
/* orig: cue_parse. Always returns a disc (NULL only when out of memory); look at flags,
 * status and notes. Resolves the BIN paths, sizes them and, when the structure is valid,
 * runs mh_disc_scan_serial. Does not touch the database. */
MhDisc *mh_cue_parse(const char *cue_path);
/* orig: cue_rewrite_files. Copies src to dst replacing the first nfiles FILE lines with
 * `FILE "<path>" BINARY` (basename when the BIN is in dst's folder). A NULL or empty
 * paths[i] keeps that line. Lines are re-written with CRLF; blank lines are dropped, but a source that ends
 * with CR/LF gets one empty line (CRLF) at the end, as in the original. */
bool mh_cue_rewrite_files(const char *src, const char *dst, const char *const *paths, unsigned nfiles);
/* orig: cue_write_corrected. Writes a CUE built from the disc's file and track tables. */
bool mh_cue_write_corrected(const MhDisc *d, const char *dst);

/* ================================================================================ disc ops */

void mh_disc_free(MhDisc *d);                 /* orig: disc_free (accepts NULL) */
void mh_disc_refresh_status(MhDisc *d);       /* orig: disc_refresh_status */
/* orig: disc_scan_serial. Runs the serial recovery on the first BIN; sets serial and
 * MH_DF_SERIAL only for STRONG_INTERNAL. Adds the "ISO declares N sectors..." note when the BIN
 * is shorter than the declared volume. Returns true when a strong serial was found. */
bool mh_disc_scan_serial(MhDisc *d);
/* orig: disc_progress.part.0. Reports `text` + d->name through prog. */
bool mh_disc_progress(MhProgress *prog, uint64_t done, uint64_t total, const char *text,
                      const MhDisc *d);
/* orig: disc_layout_valid. hdr points at a V2 header (little endian fields). Checks:
 * u32@8 < 4, u64@0x64 == u64@0x3c + 5, u32@0x78 == 0xa00, and by bit 1 of u32@8: clear:
 * u32@0x6c <= 0x1400 and u64@0x70 == u64@0x3c + 0xf; set: u32@0x6c <= 0x2800 and
 * u64@0x70 == u64@0x3c + 0x19. */
bool mh_disc_layout_valid(const uint8_t *hdr);
typedef enum {
    MH_RECOVER_OK = 0,         /* serial stored in the disc (flags MH_DF_SERIAL, ps copied) */
    MH_RECOVER_SKIPPED,        /* nothing to do: the disc already has a serial (no text) */
    MH_RECOVER_NOT_FOUND,      /* no proven internal serial; msg has the reason */
    MH_RECOVER_UNAVAILABLE     /* source missing or not usable; msg says why */
} MhRecoverStatus;

/* orig: recover_disc_serial_internal.isra.0, without the window code. Re-reads the disc source
 * (kind 1: first BIN; kinds 4 and 5: the raw slot at v2_raw_lba512 of source_path or, for
 * kind 4, device_path) and keeps the serial only when the recovery is STRONG_INTERNAL.
 * device_path is the raw device node of a kind 4 disc (the original built the PhysicalDriveN
 * path from v2_drive); it may be NULL for the others. On success the project (may be NULL) is
 * marked dirty and recounted; the caller still has to call the cheat module's
 * prepare-hardware function when d->cheats is set and refresh its lists.
 * msg gets the text the original showed: the failure text (a message box), or the success
 * text of silent == false ("Internal serial recovered: ..."), or for silent == true the
 * status line "Proven internal serial recovered. Apply the changes to the card.". In silent
 * mode the original showed nothing for the failures: msg is then empty. */
MhRecoverStatus mh_disc_recover_serial(MhDisc *d, const char *device_path, MhProject *p, bool silent,
                                       char *msg, size_t msgn);

/* orig: keep_disc_with_observations. The "add anyway?" answer used when admitting a CUE disc. */
bool mh_keep_disc_with_observations(void *ctx, const char *text);
/* orig: kept_package. For V2 discs: the on-card package value to reuse when the disc is
 * written back, or 0. idx is the card slot to match (-1 = match by path only). */
uint64_t mh_kept_package(const MhDisc *d, int idx, const char *path);

/* orig: case_title_for_disc. Game title for grouping: title (or name), cut at " (DISC " or
 * " DISC ", trailing spaces removed. Returns true when a disc marker was cut. */
bool mh_case_title_for_disc(const MhDisc *d, char *out, size_t n);
/* Port only. Copies the card title norm into out (n bytes). When it does not fit, the region tag goes
 * first so the game name stays whole ("Chrono Cross (USA, Canada) (Disc 1)" -> "Chrono Cross (Disc 1)");
 * only if that is still too long is the game name (the first base_len characters) cut, keeping the
 * revision and disc tags. Plain cut when base_len is 0 or the tags alone do not fit. */
void mh_fit_name(const char *norm, size_t base_len, char *out, size_t n);
/* Port only. The Disc title the writer stores: d->name when it fits (71 characters, 61 when the Case
 * has several Discs), otherwise refit from the full title (region tag first, then the game name cut)
 * for a name the user did not type and the card did not supply, or plainly cut. */
void mh_card_disc_name(const MhDisc *d, unsigned case_discs, char *out, size_t n);
/* orig: short_disc_name. Shortens a title that has " (DISC N)" to fit n-1 characters by
 * cutting the part before the marker. */
void mh_short_disc_name(const char *title, char *out, size_t n);
/* orig: apply_trackset_title. Applies a database title to d->title (and name unless the user
 * named the disc). title is ASCII or UTF-8. */
void mh_apply_trackset_title(MhDisc *d, const char *title);

/* =============================================================================== database */

/* The four embedded blobs, as memory regions. Any of them may be NULL/0 (that part is then
 * unavailable). They are copied by mh_db_load. File names in resources/embedded/. */
typedef struct MhDbBlobs {
    const uint8_t *disc_index;    size_t disc_index_size;    /* disc_index.bin    "PSTDB2"   */
    const uint8_t *redump_index;  size_t redump_index_size;  /* redump_index.bin  "PSTRDX2"  */
    const uint8_t *aliases;       size_t aliases_size;       /* aliases.bin       "PSTALS2"  */
    const uint8_t *trackset;      size_t trackset_size;      /* trackset_index.bin "PSTTS1"  */
} MhDbBlobs;

typedef struct MhDb {
    uint8_t *disc_index;   size_t disc_index_size;
    uint32_t disc_count, disc_rec_size, disc_entry_size;
    uint64_t disc_entries_off;          /* start of the 16 byte track entries */
    uint8_t *redump_index; size_t redump_index_size; uint32_t redump_count, redump_rec_size;
    uint8_t *aliases;      size_t aliases_size;      uint32_t alias_count;
    uint8_t *trackset;     size_t trackset_size;
    uint32_t ts_count, ts_rec_size;
    uint64_t ts_entries_off, ts_data_off;
    uint32_t ts_entry_count, ts_sbi_count;
    char dir[MH_PATH_MAX];              /* base folder of database/art and database/cues */
} MhDb;

/* orig: db_load. Validates and copies the blobs. Returns true when all four are usable;
 * a blob that fails validation is dropped (its lookups then fail) and the others stay.
 * dir is the folder that contains "database/art/default" and "database/cues" (the original
 * passed the folder of the executable). */
bool mh_db_load(MhDb *db, const MhDbBlobs *blobs, const char *dir);
/* Convenience: reads the four .bin files from embedded_dir (resources/embedded). */
bool mh_db_load_dir(MhDb *db, const char *embedded_dir, const char *dir);
void mh_db_free(MhDb *db);                   /* orig: db_free (does not touch the disc SBI pointers) */

/* orig: db_record_serial / db_record_title. "" for a bad index. */
const char *mh_db_record_serial(const MhDb *db, uint32_t idx);
const char *mh_db_record_title(const MhDb *db, uint32_t idx);
/* Fields of a disc index record (idx must be < db->disc_count). */
bool mh_db_record_info(const MhDb *db, uint32_t idx, uint32_t *first_track, uint16_t *ntracks,
                       uint32_t *total_sectors);
/* One 16 byte track entry of a record: number, kind (1 audio, 2 data), pregap, length, crc32. */
typedef struct MhDbTrack { uint8_t number, kind; uint16_t pregap; uint32_t length, crc32; } MhDbTrack;
bool mh_db_track_entry(const MhDb *db, uint32_t entry_index, MhDbTrack *t);

/* orig: db_resolve_alias. out = canonical serial for an alias, or serial itself; true if it was
 * an alias. */
bool mh_db_resolve_alias(const MhDb *db, const char *serial, char *out, size_t n);
/* orig: db_find_art. Looks for "<dir>/database/art/default/<SERIAL>-CASE.art" (or -CD.art),
 * then the same for the canonical serials of every alias record with that key. Sets
 * case_art_path/MH_DF_CASE_ART or cd_art_path/MH_DF_CD_ART. */
bool mh_db_find_art(const MhDb *db, MhDisc *d, bool case_art);
/* orig: db_find_original_cue. "<dir>/database/cues/<SERIAL>.cue" or via the alias. */
bool mh_db_find_original_cue(const MhDb *db, const MhDisc *d, char *out, size_t n);
/* orig: db_find_sbi. Marks MH_DF_SBI when the disc already carries an SBI blob. */
bool mh_db_find_sbi(const MhDb *db, MhDisc *d);
/* orig: db_apply_disc. Picks the disc index record that best matches (serial, track count,
 * total sectors, track types), sets db_index/MH_DF_DB_MATCH and the notes. */
bool mh_db_apply_disc(const MhDb *db, MhDisc *d);
/* orig: db_apply_saved_single_track. V2 discs with one data track: identify by SHA-1. */
bool mh_db_apply_saved_single_track(const MhDb *db, MhDisc *d);
/* orig: db_match_trackset(+_active). sha1s = n concatenated 20 byte SHA-1 of the tracks. */
bool mh_db_match_trackset(const MhDb *db, MhDisc *d, const uint8_t *sha1s, unsigned n);
/* orig: db_recover_track_map_safe. Single BIN with a wrong track table: rebuilds it from the
 * database record, hashes the tracks and accepts only if the trackset matches. */
bool mh_db_recover_track_map(const MhDb *db, MhDisc *d);
/* orig: db_verify_redump. Reads every BIN once (CRC-32 + SHA-1, also of the whole disc into
 * d->crc32/d->sha1) and looks each up in the Redump index. True only if every BIN matches.
 * Deviation: progress/cancel is honoured (the original ignored the callback result); on a
 * cancel it returns false and leaves the disc untouched. */
bool mh_db_verify_redump(const MhDb *db, MhDisc *d, MhProgress *prog);

/* ============================================================================ the project */

void mh_project_init(MhProject *p);               /* orig: project_init */
void mh_project_clear(MhProject *p);              /* orig: project_clear (frees every disc) */
void mh_project_recount(MhProject *p);            /* orig: project_recount */
/* orig: project_add_case. First free slot at or after `from` (wrapping), titled
 * "NEW GAME"; NULL when all 256 are used. Marks the project dirty. */
MhCase *mh_project_add_case(MhProject *p, uint8_t from);
bool mh_project_remove_case(MhProject *p, uint8_t slot);   /* orig: project_remove_case */
bool mh_project_move_case(MhProject *p, uint8_t a, uint8_t b); /* orig: project_move_case (swap) */
/* orig: project_reorder_case: move the case in `slot` to the position (among used cases) of
 * the used case in slot `target`, compacting all cases into slots 0..n-1. out_slot receives
 * the new slot (also on failure: the old one). */
bool mh_project_reorder_case(MhProject *p, uint8_t slot, uint8_t target, uint8_t *out_slot);
/* orig: project_reorder_case_index: same with a position (clamped to the last). */
bool mh_project_reorder_case_index(MhProject *p, uint8_t slot, uint16_t index, uint8_t *out_slot);
/* orig: project_move_disc. Moves a disc between or inside cases. Between cases the target
 * must hold fewer than 5 discs; a source case left empty is cleared. out_* get the new place
 * (on failure the source place). */
bool mh_project_move_disc(MhProject *p, uint8_t from_case, uint8_t from_idx, uint8_t to_case,
                          uint8_t to_idx, uint8_t *out_case, uint8_t *out_idx);
/* orig: project_find_case_title. Case whose title equals `title` (ASCII case-insensitive). */
MhCase *mh_project_find_case_title(MhProject *p, const char *title);
/* orig: manager_contains_cue. */
bool mh_project_contains_cue(const MhProject *p, const char *cue_path);
/* orig: manager_group_case. Case that already holds a disc of the same game (same
 * mh_case_title_for_disc and same game folder). */
MhCase *mh_project_group_case(MhProject *p, const MhDisc *d);

/* orig: case_add_disc. False for NULL, a full case (5) or a full project (disc_count >= 256).
 * Takes ownership of d on success. */
bool mh_case_add_disc(MhProject *p, MhCase *c, MhDisc *d);
/* Gives d the disc art of the first other disc of c that has one. Many discs 2..n have no art of their own in the
 * default art repository; the game's art is better than none. False when d already has art or nobody can lend any. */
bool mh_disc_inherit_cd_art(const MhCase *c, MhDisc *d);
/* orig: case_remove_disc. Frees the disc and closes the gap. */
bool mh_case_remove_disc(MhProject *p, MhCase *c, uint8_t idx);
/* orig: case_move_disc. */
bool mh_case_move_disc(MhCase *c, uint8_t from, uint8_t to);

/* Copies a valid "menu-defaults.dat" record into the project (the hook for
 * project_use_default_menu; reading and validating the file is mh_menu_defaults_load). */
void mh_project_set_menu_defaults(MhProject *p, const uint8_t rec[64]);

/* ------------------------------------------------------------ add a CUE to the project */

/* Callbacks standing in for the dialogs of the original (all optional). */
typedef struct MhCueHooks {
    void *ctx;
    /* orig: common_ask. Yes/no question (Portuguese text with \r\n). */
    bool (*ask)(void *ctx, const char *text);
    /* orig: common_locate. Lets the user pick a BIN; writes its path to out. */
    bool (*locate)(void *ctx, char out[MH_PATH_MAX]);
    /* orig: admit_cue_disc: manager_admit with a full read. False rejects the disc (the
     * UI shows the reason). NULL accepts. */
    bool (*admit)(void *ctx, MhDisc *d);
    /* orig: auto_attach_cheats. NULL skips. */
    void (*attach_cheats)(void *ctx, MhDisc *d);
} MhCueHooks;

/* orig: manager_parse. cue_parse, then locate missing BINs through the hooks (writing a
 * temporary corrected CUE), then the database passes (db_apply_disc, track map recovery,
 * titles, SBI). NULL only when out of memory. db may be NULL (no database). */
MhDisc *mh_manager_parse(const MhDb *db, const char *cue_path, const MhCueHooks *hooks);

typedef enum {
    MH_ADD_OK = 0,
    MH_ADD_NOT_CUE,        /* does not end with ".cue": silently ignored */
    MH_ADD_DUPLICATE,      /* already in the project: silently ignored */
    MH_ADD_PARSE_FAILED,   /* "Could not parse a CUE." */
    MH_ADD_REJECTED,       /* admit hook said no (the hook reports why) */
    MH_ADD_LIMIT,          /* error text in err */
    MH_ADD_NO_ROOM         /* could not create a case */
} MhAddStatus;

typedef struct MhAddResult {
    MhCase *cas;     /* the case that received the disc */
    int disc_index;  /* its index in the case */
} MhAddResult;

/* orig: add_cue_path.constprop.0 minus the window code. target_slot: case slot chosen by the
 * user, or >= 256 to let the project group by game (mh_project_group_case) / create a case.
 * err receives the exact message of the failures that the original showed in a message box. */
MhAddStatus mh_project_add_cue(MhProject *p, const MhDb *db, const char *cue_path, unsigned target_slot,
                               const MhCueHooks *hooks, MhAddResult *res, char *err, size_t errn);

#ifdef __cplusplus
}
#endif
#endif
