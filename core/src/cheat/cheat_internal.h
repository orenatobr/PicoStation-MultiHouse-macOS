/* Private helpers of the cheat module. */
#ifndef MH_CHEAT_INTERNAL_H
#define MH_CHEAT_INTERNAL_H

#include <string.h>

#include "mh/base.h"
#include "mh/cheat.h"

/* ---- little endian access (rule 3: never overlay structs on file bytes) ------------------- */
static inline uint16_t ch_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t ch_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void ch_wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void ch_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ---- the V5 compact bank (orig: v5_compact_bank, 497601 bytes in .rdata) --------------------
 * Derived from the database; see cheat_bank.c. A group is one cheat of a title. */
typedef struct ChBankGroup {
    char name[MH_CHEAT_NAME_MAX]; /* normalised, unique inside the title */
    uint16_t id;                  /* entry id (orig +0x0a) */
    uint16_t nlines;              /* orig +0x08 */
    uint32_t pool;                /* offset of the "decoded" lines in MhCheatBank.pool; the
                                     device form (orig +0x0c) follows right after */
} ChBankGroup;

struct MhCheatBank {
    uint32_t n_titles;
    uint32_t *first;  /* per title: index of its first group */
    uint16_t *count;  /* per title: number of groups */
    uint32_t n_groups;
    ChBankGroup *groups;
    uint8_t *pool;
};

MhCheatBank *ch_bank_build(const MhCheatDb *db);
void ch_bank_free(MhCheatBank *b);
void ch_bank_set_active(const MhCheatDb *db);
const MhCheatBank *ch_bank_active(void);
/* orig: v5_ref_group. NULL when title or position is out of range. */
const ChBankGroup *ch_bank_group(const MhCheatBank *b, uint32_t title, uint32_t pos);
static inline const uint8_t *ch_bank_lines_a(const MhCheatBank *b, const ChBankGroup *g)
{
    return b->pool + g->pool;
}
static inline const uint8_t *ch_bank_lines_b(const MhCheatBank *b, const ChBankGroup *g)
{
    return b->pool + g->pool + (size_t)g->nlines * MH_CHEAT_LINE_SIZE;
}
/* orig: compact_position. Index of the group of `title` equal to entry e, or -1. */
int ch_compact_position(const MhCheatBank *b, uint32_t title, const MhCheatEntry *e);

/* ---- program checks ---------------------------------------------------------------------- */
/* The structure scan used everywhere: lines start with 30/31/70/80/90, 70 and 90 are
 * prefixes that need another line after them. n must be 1..255 (not checked here). */
int ch_lines_structure_ok(const uint8_t *lines, unsigned n);
/* orig: v5_program @0x14003d6e0. cost = n * 0x2c. */
int ch_v5_program(const uint8_t *lines, unsigned n, uint32_t *cost);

/* orig: mh_string. Raw bytes of string idx inside the database image. */
int ch_db_string_raw(const MhCheatDb *db, uint32_t idx, const uint8_t **s, uint32_t *len);

/* ---- profile internals -------------------------------------------------------------------- */
void ch_profile_reset_provenance(MhCheatProfile *p);
/* Frees the entries and array but not the struct itself. */
void ch_profile_release(MhCheatProfile *p);
int ch_profile_validate(const MhCheatProfile *p, char *err, size_t errn);

/* A UTF-16 text view for the importer (the original worked on WCHAR). */
typedef uint16_t chw;

#endif
