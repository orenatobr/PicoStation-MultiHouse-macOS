/* mh/cheat.h: embedded GameShark V5 database, cheat profiles and the PSTCHT1 on-card format.
 *
 * Map of original functions to these names: docs/map/cheat.md. Formats: docs/EMBEDDED_DATA.md.
 *
 * Lines. A cheat program is an array of 6-byte lines: op, addr[3] (big endian 24 bit), value[2]
 * (big endian). Text form: "XXXXXXXX XXXX". Native ops of the audited runtime: 30 (write8),
 * 31, 70 and 90 (condition prefixes, consume the next line), 80 (write16).
 *
 * Active bank. The original kept the embedded V5 bank in .rdata, read by every profile
 * function (validate, serialized_size, serialize, deserialize...). Here that bank is derived
 * from the verified database and made globally active by a successful mh_cheat_db_load, the
 * same way the original published g_active_cheat_db. Without a loaded database the profile
 * functions still work, but audited entries cannot use the compact format and v6/v5 blobs are
 * rejected.
 *
 * Error text out-parameters (`char *err, size_t errn`) get the exact original ASCII messages. */
#ifndef MH_CHEAT_H
#define MH_CHEAT_H

#include <stddef.h>
#include <stdint.h>

#include "mh/str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- constants (every one is observable in a file format or an error text) ---------------- */
#define MH_CHEAT_DB_CRC 0xA06CFC31u        /* CRC-32 of the decompressed database ("audited" id) */
#define MH_CHEAT_DB_LEGACY_CRC 0x3CC8BE42u /* older database whose audited entries are dropped */
#define MH_CHEAT_DB_RAW_SIZE 0x34D836u     /* decompressed database size */
#define MH_CHEAT_NAME_MAX 64               /* entry name buffer; names have 1..63 characters */
#define MH_CHEAT_LINE_SIZE 6
#define MH_CHEAT_MAX_ENTRIES 256           /* entries per profile */
#define MH_CHEAT_MAX_LINES_PER_ENTRY 255
#define MH_CHEAT_MAX_LINES 510             /* total lines per profile (also enabled lines) */
#define MH_CHEAT_BLOB_MAX 0x2800           /* the reserved 10240 byte region on the card */
#define MH_CHEAT_BUDGET_CASE 0x1400        /* add_catalog_entry budget for a Case profile */
#define MH_CHEAT_BUDGET_DISC 0x2800        /* ... and for a disc profile */
#define MH_CHEAT_BLOB_HEADER 0x40

/* Entry flag bits (MhCheatEntry.flags). Bit 7 is invalid. */
enum {
    MH_CHEAT_F_ENABLED = 0x01,   /* selected by the user */
    MH_CHEAT_F_MANDATORY = 0x02, /* official required group: must be audited and enabled */
    MH_CHEAT_F_CUSTOM = 0x04,    /* user code; exactly one of CUSTOM and AUDITED is set */
    MH_CHEAT_F_AUDITED = 0x08,   /* taken from the embedded database */
    MH_CHEAT_F_DB_DEFAULT = 0x10,/* database says "on by default"; never set on custom */
    MH_CHEAT_F_COMPACT = 0x20,   /* marker on custom entries read from a compact blob */
    MH_CHEAT_F_SIMPLE = 0x40     /* classic-import origin / only write+conditional-write lines */
};

/* Profile state byte (MhCheatProfile.state). */
enum {
    MH_CHEAT_STATE_NONE = 0,
    MH_CHEAT_STATE_FULL_SNAPSHOT = 1, /* read from a full (v4) blob of the audited game: the
                                         caller rebuilds the catalog and restores the choices */
    MH_CHEAT_STATE_REFS_ONLY = 2      /* read from a references-only (v5) blob */
};

/* ---- entry and profile ---------------------------------------------------------------------- */

/* orig: 0x58 byte entry. variant/id are the database provenance of an audited entry
 * (0xFFFFFFFF for both on custom entries). */
typedef struct MhCheatEntry {
    char name[MH_CHEAT_NAME_MAX]; /* +0x00 */
    uint8_t flags;                /* +0x40 */
    uint8_t game;                 /* +0x41, 1..3 for audited, 0 for custom */
    uint16_t nlines;              /* +0x42 */
    uint32_t variant;             /* +0x44 */
    uint32_t id;                  /* +0x48 */
    uint8_t *lines;               /* +0x50, nlines * 6 bytes, owned */
} MhCheatEntry;

typedef struct MhCheatProfile MhCheatProfile;

/* orig: 0x38 byte profile ("cheat_profile"). Fields may be read and the flags of entries
 * edited by the UI; use the functions for everything that changes the shape. */
struct MhCheatProfile {
    uint32_t title;      /* +0x00 database title index, 0xFFFFFFFF = none */
    uint32_t variant;    /* +0x04 database variant index, 0xFFFFFFFF = none */
    uint8_t game;        /* +0x08 database game id (1..3), 0 = custom only */
    uint8_t state;       /* +0x09 MH_CHEAT_STATE_* */
    uint32_t count;      /* +0x0c entries in use */
    uint32_t cap;        /* +0x10 entries allocated */
    uint32_t db_crc;     /* +0x14 MH_CHEAT_DB_CRC for audited profiles, else 0 */
    MhCheatEntry *entries; /* +0x18 */
    char serial[16];     /* +0x20 disc serial for the hardware loader (prepare_hardware) */
    uint32_t removed;    /* +0x30 cheats dropped for documented incompatibility (not set here) */
};

/* orig: cheat_entry_clear @0x140008f90. Frees the lines, zeroes the entry, variant = -1. */
void mh_cheat_entry_clear(MhCheatEntry *e);

/* ---- database ------------------------------------------------------------------------------- */
typedef struct MhCheatBank MhCheatBank;

/* orig: 0x850 byte block. After a failed load every field is zero except load_error. */
typedef struct MhCheatDb {
    uint8_t *data;        /* decompressed MHGSF1 image */
    uint64_t size;
    uint32_t crc;         /* CRC-32 of data (== MH_CHEAT_DB_CRC when verified) */
    uint8_t sha256[32];
    int loaded;
    uint32_t n_games, n_titles, n_variants, n_texts, n_cheats, n_programs;
    char description[1024];
    int load_error;       /* orig: cheat_db_last_error (a global there) */
    MhCheatBank *bank;    /* derived V5 compact bank, NULL if the layout does not allow it */
} MhCheatDb;

/* orig: cheat_db_entry_info output (0x114 bytes). */
typedef struct MhCheatEntryInfo {
    uint32_t index;   /* global cheat index */
    uint32_t text;    /* text record */
    uint32_t variant;
    uint32_t id;      /* entry id inside the database */
    uint16_t nlines;
    uint8_t game;
    uint8_t flags;    /* initial MH_CHEAT_F_* bits (AUDITED, DB_DEFAULT, MANDATORY|ENABLED) */
    char name[256];
} MhCheatEntryInfo;

/* orig: cheat_db_variant_info output (0xfc bytes). */
typedef struct MhCheatVariantInfo {
    uint32_t variant;
    uint32_t title;
    uint32_t aux;          /* variant+0x08, unused by the original callers */
    uint32_t first_entry;
    uint32_t entry_count;
    uint16_t stat_a, stat_b; /* variant+0x14, +0x16 */
    uint8_t game;
    uint8_t game_aux;
    char game_code[48];    /* e.g. "V5ORIG" */
    char game_name[288];   /* e.g. "GameShark V5 original" */
} MhCheatVariantInfo;

/* orig: cheat_db_load @0x1400070f0. Decompresses (CDXLZ1 = LZ4 block stream), checks structure
 * table by table, SHA-256 and CRC-32, and on success makes this database (and its bank) the
 * active one. `blob` is the content of resources/embedded/cdx_cheats.bin. Returns 1 or 0
 * (then db->load_error holds the original error code). */
int mh_cheat_db_load(MhCheatDb *db, const uint8_t *blob, size_t len);
/* orig: cheat_db_free @0x140007080. */
void mh_cheat_db_free(MhCheatDb *db);
/* orig: cheat_db_last_error @0x1400070e0. */
int mh_cheat_db_last_error(const MhCheatDb *db);
/* The database most recently loaded and not freed (orig g_active_cheat_db), or NULL. */
const MhCheatDb *mh_cheat_db_active(void);

/* orig: mh_string / mh_string_w. String table lookup. out is UTF-8, truncated to n-1 bytes
 * (never inside a sequence). */
int mh_cheat_db_string(const MhCheatDb *db, uint32_t idx, char *out, size_t n);
/* orig: cheat_db_title_w. */
int mh_cheat_db_title(const MhCheatDb *db, uint32_t title, char *out, size_t n);
/* orig: cheat_db_title_variant_count / _at. */
unsigned mh_cheat_db_title_variant_count(const MhCheatDb *db, uint32_t title);
int mh_cheat_db_title_variant_at(const MhCheatDb *db, uint32_t title, unsigned k,
                                 MhCheatVariantInfo *out);
/* orig: cheat_db_variant_info @0x140008e60. */
int mh_cheat_db_variant_info(const MhCheatDb *db, uint32_t variant, MhCheatVariantInfo *out);
/* orig: cheat_db_entry_info @0x140006490. Entry number `entry` inside the variant. */
int mh_cheat_db_entry_info(const MhCheatDb *db, uint32_t variant, uint32_t entry,
                           MhCheatEntryInfo *out);
/* orig: cheat_db_entry_text @0x140006680. kind: 0 name, 1 display name, 2 PT note, 3 EN
 * note, 4 JSON details. Returns 1 and fills out, or 0 with out = "". */
int mh_cheat_db_entry_text(const MhCheatDb *db, uint32_t variant, uint32_t entry, int kind,
                           char *out, size_t n);
/* orig: cheat_db_copy_entry @0x140006240. Builds an owned entry (free with
 * mh_cheat_entry_clear). The name is the bank name when the database is the audited one. */
int mh_cheat_db_copy_entry(const MhCheatDb *db, uint32_t variant, uint32_t entry,
                           MhCheatEntry *out);
/* orig: cheat_db_title_matches @0x1400082c0. Search filter used by the catalog list. */
int mh_cheat_db_title_matches(const MhCheatDb *db, uint32_t title, const char *filter);
/* orig: cheat_db_find_title_normalized @0x140006830. Unique title whose normalised name equals
 * the given game name (see the .c for the three comparisons). */
int mh_cheat_db_find_title_normalized(const MhCheatDb *db, const char *name, uint32_t *title);
/* orig: cheat_db_find_title_for_disc @0x140006770. Binary search in kCheatSerialBindings
 * (618 serials). *title = 0xFFFFFFFF when not found. */
int mh_cheat_db_find_title_for_disc(const MhCheatDb *db, const char *serial, uint32_t *title);
/* The generic list filter of the catalog/profile lists (orig: filter_match @0x14001b0c0):
 * ASCII case-insensitive substring, both sides cut to 127 bytes, empty filter matches. */
int mh_cheat_filter_match(const char *text, const char *filter);

/* ---- profile -------------------------------------------------------------------------------- */

/* orig: cheat_profile_new / _free / _clone. clone(NULL) returns a new empty profile; clone
 * fails (NULL) for more than 256 entries. */
MhCheatProfile *mh_cheat_profile_new(void);
void mh_cheat_profile_free(MhCheatProfile *p); /* accepts NULL */
MhCheatProfile *mh_cheat_profile_clone(const MhCheatProfile *p);

/* orig: cheat_profile_add_entry @0x14000a8e0. Copies e (flags/provenance verbatim), then
 * validates the whole profile; on failure the entry is removed again. */
int mh_cheat_profile_add_entry(MhCheatProfile *p, const MhCheatEntry *e);
/* orig: cheat_profile_add_audited @0x140009c00. variant/entry as in the database. Returns 1
 * also when the same entry is already present. */
int mh_cheat_profile_add_audited(MhCheatProfile *p, const MhCheatDb *db, uint32_t variant,
                                 uint32_t entry);
/* orig: cheat_profile_add_catalog_entry @0x140009d80. Adds the entry "prepared" (not enabled).
 * budget is MH_CHEAT_BUDGET_CASE or _DISC. */
int mh_cheat_profile_add_catalog_entry(MhCheatProfile *p, const MhCheatDb *db, uint32_t variant,
                                       uint32_t entry, unsigned budget, char *err, size_t errn);
/* orig: cheat_profile_add_custom @0x14000a0a0 / _add_custom_disabled @0x14000a4c0.
 * flags_hint keeps only bits 5..7 of the caller (and bit 0 only for the enabled variant). */
int mh_cheat_profile_add_custom(MhCheatProfile *p, const char *name, const uint8_t *lines,
                                uint16_t nlines, uint8_t flags_hint);
int mh_cheat_profile_add_custom_disabled(MhCheatProfile *p, const char *name,
                                         const uint8_t *lines, uint16_t nlines,
                                         uint8_t flags_hint);
/* orig: cheat_profile_remove @0x14000dc30. Resets the provenance when no audited entry is
 * left. */
int mh_cheat_profile_remove(MhCheatProfile *p, uint32_t index);
/* orig: cheat_profile_find_audited. Index of the audited entry with (variant, id), or -1. */
int mh_cheat_profile_find_audited(const MhCheatProfile *p, uint32_t variant, uint32_t id);
/* orig: cheat_profile_enabled_count / _total_lines / _active_c0_count (always 0). */
int mh_cheat_profile_enabled_count(const MhCheatProfile *p);
int mh_cheat_profile_total_lines(const MhCheatProfile *p);
int mh_cheat_profile_active_c0_count(const MhCheatProfile *p);
/* orig: cheat_profile_prepare_hardware. Stores the disc serial (15 chars) for the loader. */
int mh_cheat_profile_prepare_hardware(MhCheatProfile *p, const char *serial);

/* orig: cheat_profile_validate @0x14000fc80 (== _validate_hardware, same code). NULL is
 * valid. err gets the first violated rule. */
int mh_cheat_profile_validate(const MhCheatProfile *p, char *err, size_t errn);
int mh_cheat_profile_validate_hardware(const MhCheatProfile *p, char *err, size_t errn);
/* orig: cheat_profiles_validate_combined @0x140010140. Case profile + disc profile. */
int mh_cheat_profiles_validate_combined(const MhCheatProfile *case_p, const MhCheatProfile *disc_p,
                                        char *err, size_t errn);
/* orig: cheat_profile_restore_user_state @0x14000ddc0. Copies enabled choices and custom
 * entries of `saved` onto the freshly built catalog `fresh` (in place). */
int mh_cheat_profile_restore_user_state(MhCheatProfile *fresh, const MhCheatProfile *saved,
                                        char *err, size_t errn);
/* orig: cheat_profile_build_exact_catalog @0x14000ac80. Whole catalog of a title: the variant
 * with the most groups that fits in 10240 bytes, all groups disabled except mandatory ones. */
int mh_cheat_profile_build_exact_catalog(const MhCheatDb *db, uint32_t title, const char *serial,
                                         MhCheatProfile **out, char *err, size_t errn);
/* orig: cheat_profile_build_for_disc_active @0x14000b670, with the db passed explicitly. */
int mh_cheat_profile_build_for_disc(const MhCheatDb *db, const char *serial,
                                    MhCheatProfile **out, char *err, size_t errn);
/* orig: profile_active_stats.part.0. Accumulates over the enabled entries. */
void mh_cheat_profile_active_stats(const MhCheatProfile *p, uint32_t *enabled, uint32_t *lines,
                                   uint32_t *cost, uint32_t *has_audited);

/* ---- PSTCHT1 -------------------------------------------------------------------------------- */
/* Versions: 4 full ("wire", 0x60 byte records), 5 references only (4 byte records, read only),
 * 6 compact (8 byte records, names and custom code inline). See docs/EMBEDDED_DATA.md. */

/* orig: profile_needs_wire @0x140026a70: 1 when the compact format cannot hold the profile. */
int mh_cheat_profile_needs_wire(const MhCheatProfile *p);
/* orig: cheat_profile_serialized_size @0x14000f9f0. 0 for NULL or empty. */
unsigned mh_cheat_profile_serialized_size(const MhCheatProfile *p);
/* orig: cheat_profile_serialize @0x14000e2e0. scope 1 = Case, 2 = disc (stored at +0x38).
 * On success *blob is malloc'ed (free with mh_free; NULL and *len = 0 for an empty profile). */
int mh_cheat_profile_serialize(const MhCheatProfile *p, uint8_t scope, uint8_t **blob,
                               uint32_t *len, char *err, size_t errn);
/* orig: cheat_profile_deserialize @0x14000baa0. *out may be NULL on success (blob of a legacy
 * database with only audited entries). */
int mh_cheat_profile_deserialize(const uint8_t *blob, uint32_t len, MhCheatProfile **out,
                                 char *err, size_t errn);
/* orig: cheat_blob_validate @0x140005850 / cheat_blob_scope @0x140005810 /
 * disc_bank_valid @0x140017be0 (the version 6 checker). */
int mh_cheat_blob_validate(const uint8_t *blob, uint32_t len, char *err, size_t errn);
uint8_t mh_cheat_blob_scope(const uint8_t *blob, uint32_t len);
int mh_cheat_disc_bank_valid(const uint8_t *blob, uint32_t len);

/* ---- code text ------------------------------------------------------------------------------ */
/* orig: parse_code_line @0x140025300. One line "XXXXXXXX XXXX" (12 hex digits, separators
 * space/tab/-/: allowed, ends at ';' '#' '//' or a trailing backslash) into 6 bytes. */
int mh_cheat_parse_code_line(const char *text, size_t len, uint8_t out[6]);
/* orig: cheat_parse_codes_w @0x140008ff0. Text must start with MHDialect=V5ORIG_BANK_V01 or
 * MHDialect=GAMESHARK_CLASSIC_V1; classic codes (30, 80, D0, D1, 50) are converted. *lines is
 * malloc'ed (6 * *nlines bytes). */
int mh_cheat_parse_codes(const char *text, uint8_t **lines, uint16_t *nlines, char *err,
                         size_t errn);
/* orig: cheat_codes_to_text @0x140006110. Appends "XXXXXXXX XXXX\r\n" per line. */
int mh_cheat_codes_to_text(const uint8_t *lines, uint16_t nlines, MhWbuf *out);
/* orig: cheat_profile_import_text_dialect @0x14000ca70 / cheat_profile_import_text. dialect is
 * NULL or "MHDialect=V5ORIG_BANK_V01" / "MHDialect=GAMESHARK_CLASSIC_V1". Reads a UTF-8 (with
 * or without BOM) or UTF-16LE file; on success *p is replaced by the extended copy and
 * *imported is the number of cheats. */
int mh_cheat_profile_import_text_dialect(MhCheatProfile *p, const char *path, const char *dialect,
                                         int *imported, char *err, size_t errn);
int mh_cheat_profile_import_text(MhCheatProfile *p, const char *path, int *imported, char *err,
                                 size_t errn);
/* orig: cheat_profile_export_text @0x14000c7c0. UTF-16LE with BOM, CRLF lines, V5ORIG
 * dialect. */
int mh_cheat_profile_export_text(const MhCheatProfile *p, const char *path, char *err,
                                 size_t errn);

/* orig: cheat_entry_hardware_note @0x140008fe0 (fixed text) and _exclusion (always 0). */
const char *mh_cheat_entry_hardware_note(void);
int mh_cheat_entry_hardware_exclusion(const MhCheatEntry *e);

#ifdef __cplusplus
}
#endif
#endif
