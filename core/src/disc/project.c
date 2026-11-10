/* Project, case and disc list operations, with the limits and edge cases of the original
 * (256 case slots, 5 discs per case, 256 discs in total). */
#include "mh/disc.h"

#include <stdlib.h>
#include <string.h>

/* orig: project_init @0x140027210 */
void mh_project_init(MhProject *p)
{
    memset(p, 0, sizeof *p);
    p->reserved_c = 1;
    p->device = -1;
}

static void free_case_discs(MhCase *c)
{
    for (unsigned i = 0; i < c->disc_count && i < MH_CASE_MAX_DISCS; i++) {
        mh_disc_free(c->discs[i]);
        c->discs[i] = NULL;
    }
}

/* orig: project_clear @0x1400270c0 */
void mh_project_clear(MhProject *p)
{
    for (unsigned i = 0; i < MH_PROJECT_MAX_CASES; i++)
        if (p->cases[i].in_use && p->cases[i].disc_count) free_case_discs(&p->cases[i]);
    mh_project_init(p);
}

/* orig: project_recount @0x140027bb0 (the original was vectorised) */
void mh_project_recount(MhProject *p)
{
    unsigned cases = 0, discs = 0;
    for (unsigned i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        if (p->cases[i].in_use) {
            cases++;
            discs += p->cases[i].disc_count;
        }
    }
    p->case_count = (uint16_t)cases;
    p->disc_count = (uint16_t)discs;
}

/* orig: project_add_case @0x140026c50 */
MhCase *mh_project_add_case(MhProject *p, uint8_t from)
{
    unsigned k;
    for (k = 0; k < 256; k++) {
        unsigned slot = (from + k) & 0xff;
        MhCase *c = &p->cases[slot];
        if (c->in_use == 0) {
            memset(c, 0, sizeof *c);
            c->slot = (uint8_t)slot;
            c->in_use = 1;
            mh_str_copy(c->title, sizeof c->title, MH_NEW_CASE_TITLE);
            p->dirty = 1;
            mh_project_recount(p);
            return c;
        }
    }
    return NULL;
}

/* orig: project_remove_case @0x140027f80 */
bool mh_project_remove_case(MhProject *p, uint8_t slot)
{
    MhCase *c = &p->cases[slot];
    if (!c->in_use) return false;
    free_case_discs(c);
    memset(c, 0, sizeof *c);
    p->dirty = 1;
    mh_project_recount(p);
    return true;
}

/* orig: project_move_case @0x140027240. Swaps two slots (the target may be unused). */
bool mh_project_move_case(MhProject *p, uint8_t a, uint8_t b)
{
    MhCase tmp;
    if (!p || a == b) return false;
    if (!p->cases[a].in_use) return false;
    tmp = p->cases[a];
    p->cases[a] = p->cases[b];
    p->cases[b] = tmp;
    p->cases[a].slot = a;
    p->cases[b].slot = b;
    p->dirty = 1;
    return true;
}

/* Packs the used cases in slot order into list[]; ranks of two slots are returned (0xffff when
 * not found). Returns the number of used cases. */
static unsigned pack_cases(const MhProject *p, MhCase *list, unsigned slot_a, int slot_b, unsigned *rank_a,
                           unsigned *rank_b)
{
    unsigned n = 0;
    *rank_a = *rank_b = 0xffff;
    for (unsigned i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        if (!p->cases[i].in_use) continue;
        if (i == slot_a) *rank_a = n;
        if (slot_b >= 0 && i == (unsigned)slot_b) *rank_b = n;
        list[n++] = p->cases[i];
    }
    return n;
}

static void move_in_list(MhCase *list, unsigned from, unsigned to)
{
    MhCase moving = list[from];
    if (from < to) {
        for (unsigned i = from; i < to; i++) list[i] = list[i + 1];
    } else {
        for (unsigned i = from; i > to; i--) list[i] = list[i - 1];
    }
    list[to] = moving;
}

static void unpack_cases(MhProject *p, const MhCase *list, unsigned n)
{
    memset(p->cases, 0, sizeof p->cases);
    for (unsigned i = 0; i < n; i++) {
        p->cases[i] = list[i];
        p->cases[i].slot = (uint8_t)i;
    }
    p->dirty = 1;
    mh_project_recount(p);
}

/* orig: project_reorder_case @0x140028430 */
bool mh_project_reorder_case(MhProject *p, uint8_t slot, uint8_t target, uint8_t *out_slot)
{
    MhCase *list;
    unsigned n, ra, rb;
    if (out_slot) *out_slot = slot;
    if (!p || !p->cases[slot].in_use || !p->cases[target].in_use) return false;
    list = mh_alloc(sizeof(MhCase) * MH_PROJECT_MAX_CASES);
    if (!list) return false;
    n = pack_cases(p, list, slot, target, &ra, &rb);
    if (ra == 0xffff || rb == 0xffff || ra == rb) {
        mh_free(list);
        return false;
    }
    move_in_list(list, ra, rb);
    unpack_cases(p, list, n);
    mh_free(list);
    if (out_slot) *out_slot = (uint8_t)rb;
    return true;
}

/* orig: project_reorder_case_index @0x140028a70 */
bool mh_project_reorder_case_index(MhProject *p, uint8_t slot, uint16_t index, uint8_t *out_slot)
{
    MhCase *list;
    unsigned n, ra, rb;
    if (out_slot) *out_slot = slot;
    if (!p || !p->cases[slot].in_use) return false;
    list = mh_alloc(sizeof(MhCase) * MH_PROJECT_MAX_CASES);
    if (!list) return false;
    n = pack_cases(p, list, slot, -1, &ra, &rb);
    if (ra == 0xffff || n == 0) {
        mh_free(list);
        return false;
    }
    if (index >= n) index = (uint16_t)(n - 1);
    if (index == ra) {
        mh_free(list);
        return false;
    }
    move_in_list(list, ra, index);
    unpack_cases(p, list, n);
    mh_free(list);
    if (out_slot) *out_slot = (uint8_t)index;
    return true;
}

/* orig: project_move_disc @0x140027300 */
bool mh_project_move_disc(MhProject *p, uint8_t from_case, uint8_t from_idx, uint8_t to_case, uint8_t to_idx,
                          uint8_t *out_case, uint8_t *out_idx)
{
    MhCase *ca, *cb;
    unsigned na, nb;
    MhDisc *disc;
    if (out_case) *out_case = from_case;
    if (out_idx) *out_idx = from_idx;
    if (!p) return false;
    ca = &p->cases[from_case];
    cb = &p->cases[to_case];
    if (!ca->in_use || !cb->in_use) return false;
    na = ca->disc_count;
    if (na <= from_idx) return false;
    nb = cb->disc_count;
    if (ca != cb) {
        if (nb > 4) return false;
        if (to_idx > nb) to_idx = (uint8_t)nb;
        disc = ca->discs[from_idx];
        for (unsigned k = from_idx; k + 1 < na; k++) ca->discs[k] = ca->discs[k + 1];
        ca->disc_count = (uint8_t)(na - 1);
        ca->discs[na - 1] = NULL;
        for (unsigned k = nb; k > to_idx; k--) cb->discs[k] = cb->discs[k - 1];
        cb->discs[to_idx] = disc;
        cb->disc_count = (uint8_t)(nb + 1);
        if (ca->disc_count == 0) memset(ca, 0, sizeof *ca);
        p->dirty = 1;
        mh_project_recount(p);
        if (out_case) *out_case = to_case;
        if (out_idx) *out_idx = to_idx;
        return true;
    }
    if (to_idx >= nb) to_idx = (uint8_t)(nb - 1);
    if (from_idx == to_idx || nb <= to_idx || nb <= from_idx) return false;
    disc = ca->discs[from_idx];
    if (from_idx < to_idx) {
        for (unsigned k = from_idx; k < to_idx; k++) ca->discs[k] = ca->discs[k + 1];
    } else {
        for (unsigned k = from_idx; k > to_idx; k--) ca->discs[k] = ca->discs[k - 1];
    }
    ca->discs[to_idx] = disc;
    p->dirty = 1;
    if (out_case) *out_case = to_case;
    if (out_idx) *out_idx = to_idx;
    return true;
}

/* orig: project_find_case_title @0x1400271a0 */
MhCase *mh_project_find_case_title(MhProject *p, const char *title)
{
    if (!p || !title || !title[0]) return NULL;
    for (unsigned i = 0; i < MH_PROJECT_MAX_CASES; i++)
        if (p->cases[i].in_use && mh_str_icmp(p->cases[i].title, title) == 0) return &p->cases[i];
    return NULL;
}

/* orig: manager_contains_cue @0x1400228c0 */
bool mh_project_contains_cue(const MhProject *p, const char *cue_path)
{
    for (unsigned i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        const MhCase *c = &p->cases[i];
        if (!c->in_use || c->disc_count == 0) continue;
        for (unsigned j = 0; j < c->disc_count; j++)
            if (c->discs[j] && mh_path_equal_full(c->discs[j]->cue_path, cue_path)) return true;
    }
    return false;
}

/* orig: manager_group_case @0x140022950 */
MhCase *mh_project_group_case(MhProject *p, const MhDisc *d)
{
    char title[0x60], other[0x60], gdir[MH_PATH_MAX], odir[MH_PATH_MAX];
    if (!mh_case_title_for_disc(d, title, sizeof title)) return NULL;
    mh_game_directory(d->cue_path, gdir, sizeof gdir);
    for (unsigned i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        MhCase *c = &p->cases[i];
        const MhDisc *first;
        if (!c->in_use || c->disc_count == 0 || !c->discs[0]) continue;
        first = c->discs[0];
        mh_case_title_for_disc(first, other, sizeof other);
        if (mh_str_icmp(title, other) != 0) continue;
        mh_game_directory(first->cue_path, odir, sizeof odir);
        if (mh_str_icmp(gdir, odir) == 0) return c;
    }
    return NULL;
}

/* orig: case_add_disc @0x140003390 */
bool mh_case_add_disc(MhProject *p, MhCase *c, MhDisc *d)
{
    if (!p || !c || !d) return false;
    if (c->disc_count >= MH_CASE_MAX_DISCS || p->disc_count >= MH_PROJECT_MAX_DISCS) return false;
    c->discs[c->disc_count++] = d;
    if (c->serial[0] == 0 && d->serial[0]) mh_str_copy(c->serial, sizeof c->serial, d->serial);
    p->dirty = 1;
    mh_project_recount(p);
    return true;
}

/* orig: case_remove_disc @0x1400039a0 */
bool mh_case_remove_disc(MhProject *p, MhCase *c, uint8_t idx)
{
    unsigned n;
    if (!p || !c) return false;
    n = c->disc_count;
    if (idx >= n) return false;
    if (c->discs[idx]) mh_disc_free(c->discs[idx]);
    for (unsigned k = idx; k + 1 < n; k++) c->discs[k] = c->discs[k + 1];
    c->disc_count = (uint8_t)(n - 1);
    c->discs[n - 1] = NULL;
    p->dirty = 1;
    mh_project_recount(p);
    return true;
}

/* orig: case_move_disc @0x1400037f0 */
bool mh_case_move_disc(MhCase *c, uint8_t from, uint8_t to)
{
    MhDisc *d;
    if (!c) return false;
    if ((from > to ? from : to) >= c->disc_count || from == to) return false;
    d = c->discs[from];
    if (from < to) {
        for (unsigned k = from; k < to; k++) c->discs[k] = c->discs[k + 1];
    } else {
        for (unsigned k = from; k > to; k--) c->discs[k] = c->discs[k - 1];
    }
    c->discs[to] = d;
    return true;
}

/* orig: project_use_default_menu.constprop.0 @0x140029090, the part after the file was
 * read and validated by mh_menu_defaults_load. */
void mh_project_set_menu_defaults(MhProject *p, const uint8_t rec[64])
{
    p->cache_tag = 0;
    memcpy(p->menu_defaults, rec, 64);
    p->has_menu_defaults = 1;
}
