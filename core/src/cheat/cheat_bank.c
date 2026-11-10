/* The V5 compact bank and the program checks. orig: v5_compact_bank (a 497601 byte blob in
 * .rdata: "V5REF01", 1004 titles, 10118 groups), v5_ref_group, compact_position, v5_program.
 *
 * Deviation: the blob is not carved. It is a pure function of the verified database (checked
 * for all 10118 groups against the original bytes: names, ids, line counts, both line
 * forms), so it is rebuilt at load time. See docs/EMBEDDED_DATA.md. */
#include <stdio.h>

#include "cheat_internal.h"

/* The valid first bytes of a stored line: 30 31 70 80 90. */
static inline int op_ok(uint8_t b) { return (uint8_t)(b - 0x30) < 2 || b == 0x70 || (b & 0xef) == 0x80; }
static inline int op_prefix(uint8_t b) { return b == 0x70 || b == 0x90; }

/* The opcode scan that the original repeated inline in a dozen functions. */
int ch_lines_structure_ok(const uint8_t *lines, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        uint8_t b = lines[i * 6];
        if (!op_ok(b)) return 0;
        if (op_prefix(b) && i + 1 >= n) return 0;
    }
    return 1;
}

/* orig: v5_program @0x14003d6e0 */
int ch_v5_program(const uint8_t *lines, unsigned n, uint32_t *cost)
{
    if (cost) *cost = 0;
    if (!lines || n == 0) return 0;
    for (unsigned i = 0; i < n; i++) {
        uint8_t b = lines[i * 6];
        if (!((b & 0xee) == 0x20 || (b & 0x7f) == 0 || op_prefix(b) || b == 0xc1)) return 0;
    }
    if (cost) *cost = n * 0x2c;
    return 1;
}

void ch_bank_free(MhCheatBank *b)
{
    if (!b) return;
    mh_free(b->first);
    mh_free(b->count);
    mh_free(b->groups);
    mh_free(b->pool);
    mh_free(b);
}

MhCheatBank *ch_bank_build(const MhCheatDb *db)
{
    const uint8_t *d = db->data;
    uint32_t nt = db->n_titles, nc = db->n_cheats, i, g = 0;
    uint64_t o34 = ch_rd32(d + 0x34), o38 = ch_rd32(d + 0x38), o3c = ch_rd32(d + 0x3c);
    uint64_t o40 = ch_rd32(d + 0x40), o44 = ch_rd32(d + 0x44), o48 = ch_rd32(d + 0x48);
    size_t pool_used = 0;
    MhCheatBank *b;

    /* the bank is indexed by title and holds the cheats of the title's only variant */
    if (nt != db->n_variants) return NULL;
    b = mh_calloc(sizeof *b);
    if (!b) return NULL;
    b->n_titles = nt;
    b->n_groups = nc;
    b->first = mh_calloc((size_t)nt * sizeof(uint32_t));
    b->count = mh_calloc((size_t)nt * sizeof(uint16_t));
    b->groups = mh_calloc((size_t)nc * sizeof(ChBankGroup));
    {
        uint64_t total = 0;
        for (i = 0; i < nc; i++) {
            const uint8_t *x = d + o3c + (uint64_t)ch_rd32(d + o40 + (uint64_t)i * 16 + 4) * 0x1c;
            total += (uint64_t)x[8] * 12;
        }
        b->pool = mh_alloc((size_t)total);
    }
    if (!b->first || !b->count || !b->groups || !b->pool) goto bad;

    for (i = 0; i < nt; i++) {
        const uint8_t *t = d + o34 + (uint64_t)i * 12;
        const uint8_t *v = d + o38 + (uint64_t)i * 0x38;
        uint32_t cnt = ch_rd32(v + 0x10), k, j;
        if (ch_rd16(t + 8) != 1 || ch_rd32(t + 4) != i || ch_rd32(v + 0xc) != g || cnt > 0xffff) goto bad;
        b->first[i] = g;
        b->count[i] = (uint16_t)cnt;
        for (k = 0; k < cnt; k++, g++) {
            const uint8_t *c = d + o40 + (uint64_t)g * 16;
            const uint8_t *x = d + o3c + (uint64_t)ch_rd32(c + 4) * 0x1c;
            const uint8_t *p = d + o44 + (uint64_t)ch_rd32(x + 4) * 8;
            const uint8_t *lines = d + o48 + ch_rd32(p);
            ChBankGroup *bg = &b->groups[g];
            char raw[256];
            uint8_t *a, *dev;
            unsigned n = x[8], u;
            if (ch_rd32(c + 8) > 0xffff) goto bad;
            mh_cheat_db_string(db, ch_rd32(x), raw, sizeof raw);
            mh_normalize_title(raw, bg->name, MH_CHEAT_NAME_MAX);
            /* a name that collides inside the title gets " <id>" appended */
            for (j = b->first[i]; j < g; j++) {
                if (strcmp(b->groups[j].name, bg->name) == 0) {
                    char tmp[96];
                    snprintf(tmp, sizeof tmp, "%s %u", bg->name, (unsigned)ch_rd32(c + 8));
                    mh_str_copy(bg->name, MH_CHEAT_NAME_MAX, tmp);
                    break;
                }
            }
            bg->id = (uint16_t)ch_rd32(c + 8);
            bg->nlines = (uint16_t)n;
            bg->pool = (uint32_t)pool_used;
            a = b->pool + pool_used;
            dev = a + (size_t)n * 6;
            memcpy(a, lines, (size_t)n * 6);
            memcpy(dev, lines, (size_t)n * 6);
            for (u = 0; u < n; u++)
                if (dev[u * 6] == 0x30 || dev[u * 6] == 0x80) dev[u * 6] = 0x00;
            pool_used += (size_t)n * 12;
        }
    }
    return b;
bad:
    ch_bank_free(b);
    return NULL;
}

/* orig: v5_ref_group.constprop.0 @0x14003d760 */
const ChBankGroup *ch_bank_group(const MhCheatBank *b, uint32_t title, uint32_t pos)
{
    if (!b || title >= b->n_titles || pos >= b->count[title]) return NULL;
    return &b->groups[b->first[title] + pos];
}

/* orig: compact_position @0x140012520 */
int ch_compact_position(const MhCheatBank *b, uint32_t title, const MhCheatEntry *e)
{
    if (!b || title >= b->n_titles) return -1;
    for (uint32_t j = 0; j < b->count[title]; j++) {
        const ChBankGroup *g = &b->groups[b->first[title] + j];
        if (g->id == e->id && g->nlines == e->nlines && mh_str_icmp(g->name, e->name) == 0 && e->lines &&
            memcmp(ch_bank_lines_a(b, g), e->lines, (size_t)e->nlines * 6) == 0)
            return (int)j;
    }
    return -1;
}
