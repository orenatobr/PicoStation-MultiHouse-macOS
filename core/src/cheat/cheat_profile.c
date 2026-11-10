/* Cheat profiles: container, limits, validation, catalog building.
 * orig: cheat_profile_* in docs/map/cheat.md. The profile struct is the original 0x38 byte
 * block; entries are the 0x58 byte records. */
#include "cheat_internal.h"

#define NONE32 0xFFFFFFFFu

static void set_err(char *err, size_t errn, const char *msg) { mh_err_set(err, errn, "%s", msg); }

/* ---- life cycle ------------------------------------------------------------------------- */
/* orig: cheat_profile_new @0x14000dbd0 */
MhCheatProfile *mh_cheat_profile_new(void)
{
    MhCheatProfile *p = mh_calloc(sizeof *p);
    if (p) p->title = p->variant = NONE32;
    return p;
}

static void entries_clear(MhCheatProfile *p)
{
    for (uint32_t i = 0; i < p->count; i++) mh_cheat_entry_clear(&p->entries[i]);
}

void ch_profile_release(MhCheatProfile *p)
{
    entries_clear(p);
    mh_free(p->entries);
    p->entries = NULL;
    p->count = p->cap = 0;
}

/* orig: cheat_profile_free @0x14000c9b0 */
void mh_cheat_profile_free(MhCheatProfile *p)
{
    if (!p) return;
    ch_profile_release(p);
    mh_free(p);
}

void ch_profile_reset_provenance(MhCheatProfile *p)
{
    p->game = 0;
    p->db_crc = 0;
    p->title = p->variant = NONE32;
}

/* Grows the entry array with the original policy: 16, doubling, at most 256. */
static int grow(MhCheatProfile *p, uint32_t need)
{
    if (p->cap < need) {
        uint32_t n = p->cap ? p->cap * 2 : 16;
        MhCheatEntry *e;
        if (n <= need) n = need;
        if (n > 256) n = 256;
        e = mh_realloc(p->entries, (size_t)n * sizeof *e);
        if (!e) return 0;
        memset(e + p->cap, 0, (size_t)(n - p->cap) * sizeof *e);
        p->entries = e;
        p->cap = n;
    }
    return 1;
}

/* Deep copy of an entry into a zeroed slot. */
static int entry_copy(MhCheatEntry *d, const MhCheatEntry *s)
{
    memset(d, 0, sizeof *d);
    mh_str_copy(d->name, MH_CHEAT_NAME_MAX, s->name);
    d->flags = s->flags;
    d->game = s->game;
    d->nlines = s->nlines;
    d->variant = s->variant;
    d->id = s->id;
    if (s->nlines) {
        d->lines = mh_alloc((size_t)s->nlines * MH_CHEAT_LINE_SIZE);
        if (!d->lines) return 0;
        memcpy(d->lines, s->lines, (size_t)s->nlines * MH_CHEAT_LINE_SIZE);
    }
    return 1;
}

/* orig: cheat_profile_clone @0x14000b7a0 */
MhCheatProfile *mh_cheat_profile_clone(const MhCheatProfile *src)
{
    MhCheatProfile *p = mh_calloc(sizeof *p);
    if (!p) return NULL;
    p->title = p->variant = NONE32;
    if (!src) return p;
    p->title = src->title;
    p->variant = src->variant;
    p->game = src->game;
    p->db_crc = src->db_crc;
    mh_str_copy(p->serial, sizeof p->serial, src->serial);
    p->removed = src->removed;
    p->state = src->state;
    if (src->count > 0x100 || !grow(p, src->count)) goto bad;
    for (uint32_t i = 0; i < src->count; i++) {
        if (!entry_copy(&p->entries[p->count], &src->entries[i])) {
            p->count++;
            goto bad;
        }
        p->count++;
    }
    return p;
bad:
    mh_cheat_profile_free(p);
    return NULL;
}

/* orig: cheat_profile_remove @0x14000dc30 / .part.0 */
static void remove_unchecked(MhCheatProfile *p, uint32_t idx)
{
    uint32_t n = p->count;
    mh_cheat_entry_clear(&p->entries[idx]);
    memmove(&p->entries[idx], &p->entries[idx + 1], (size_t)(n - idx - 1) * sizeof(MhCheatEntry));
    p->count = n - 1;
    memset(&p->entries[n - 1], 0, sizeof(MhCheatEntry));
    for (uint32_t i = 0; i < p->count; i++)
        if (p->entries[i].flags & MH_CHEAT_F_AUDITED) return;
    ch_profile_reset_provenance(p);
}

int mh_cheat_profile_remove(MhCheatProfile *p, uint32_t index)
{
    if (!p || index >= p->count) return 0;
    remove_unchecked(p, index);
    return 1;
}

/* orig: cheat_profile_find_audited @0x14000c960 */
int mh_cheat_profile_find_audited(const MhCheatProfile *p, uint32_t variant, uint32_t id)
{
    if (p)
        for (uint32_t i = 0; i < p->count; i++) {
            const MhCheatEntry *e = &p->entries[i];
            if (!(e->flags & MH_CHEAT_F_CUSTOM) && e->variant == variant && e->id == id) return (int)i;
        }
    return -1;
}

/* orig: cheat_profile_enabled_count @0x14000c3e0 */
int mh_cheat_profile_enabled_count(const MhCheatProfile *p)
{
    int n = 0;
    if (p)
        for (uint32_t i = 0; i < p->count; i++) n += p->entries[i].flags & MH_CHEAT_F_ENABLED;
    return n;
}

/* orig: cheat_profile_total_lines @0x14000fac0 */
int mh_cheat_profile_total_lines(const MhCheatProfile *p)
{
    int n = 0;
    if (p)
        for (uint32_t i = 0; i < p->count; i++) n += p->entries[i].nlines;
    return n;
}

/* orig: cheat_profile_active_c0_count @0x140009bf0 (always 0) */
int mh_cheat_profile_active_c0_count(const MhCheatProfile *p)
{
    (void)p;
    return 0;
}

/* orig: cheat_profile_prepare_hardware @0x14000dc00 */
int mh_cheat_profile_prepare_hardware(MhCheatProfile *p, const char *serial)
{
    if (p && serial && serial != p->serial) mh_str_copy(p->serial, sizeof p->serial, serial);
    return 0;
}

/* orig: cheat_entry_hardware_note @0x140008fe0 */
const char *mh_cheat_entry_hardware_note(void)
{
    return "Use a regiao e a revisao corretas do jogo. Consulte PT/EN e o codigo original no catalogo. "
           "Condicoes por botao precisam do controle inicializado pelo jogo.";
}

/* orig: cheat_entry_hardware_exclusion @0x140008fd0 (always 0) */
int mh_cheat_entry_hardware_exclusion(const MhCheatEntry *e)
{
    (void)e;
    return 0;
}

/* ---- compact / wire decision and size --------------------------------------------------- */

/* orig: profile_needs_wire @0x140026a70 */
int mh_cheat_profile_needs_wire(const MhCheatProfile *p)
{
    const MhCheatBank *bank;
    uint32_t customs = 0, groups;
    if (!p || p->count == 0) return 0;
    bank = ch_bank_active();
    if (p->game != 1 || !bank || p->title >= bank->n_titles) return 1;
    groups = bank->count[p->title];
    for (uint32_t i = 0; i < p->count; i++) {
        const MhCheatEntry *e = &p->entries[i];
        if (!(e->flags & MH_CHEAT_F_CUSTOM)) {
            if (!(e->flags & MH_CHEAT_F_AUDITED)) return 1;
            if (e->game != 1) return 1;
            if (ch_compact_position(bank, p->title, e) < 0) return 1;
        } else {
            customs++;
            if (customs + groups > 0x100) return 1;
            if (strlen(e->name) > 0x2f) return 1;
        }
    }
    return 0;
}

/* orig: cheat_profile_serialized_size @0x14000f9f0 */
unsigned mh_cheat_profile_serialized_size(const MhCheatProfile *p)
{
    unsigned sz = MH_CHEAT_BLOB_HEADER;
    int wire = mh_cheat_profile_needs_wire(p);
    if (!p || p->count == 0) return 0;
    for (uint32_t i = 0; i < p->count; i++) {
        const MhCheatEntry *e = &p->entries[i];
        if (wire)
            sz += 0x60 + (unsigned)e->nlines * 6;
        else
            sz += (unsigned)strlen(e->name) + 9 + (unsigned)e->nlines * 6;
    }
    return sz;
}

/* ---- validation ------------------------------------------------------------------------- */
#define ERR_SYNTAX "Um cheat contem nome, sintaxe ou formato de codigo nao suportado."

/* orig: cheat_profile_validate @0x14000fc80 */
int ch_profile_validate(const MhCheatProfile *p, char *err, size_t errn)
{
    uint32_t n, total = 0, active = 0;
    int audited = 0;
    if (err && errn) err[0] = 0;
    if (!p) return 1;
    n = p->count;
    if (n > 0x100) {
        set_err(err, errn, "O catalogo excede 256 cheats.");
        return 0;
    }
    if (n != 0) {
        if (!p->entries) {
            set_err(err, errn, ERR_SYNTAX);
            return 0;
        }
        for (uint32_t i = 0; i < n; i++) {
            const MhCheatEntry *e = &p->entries[i];
            unsigned len = 0;
            uint8_t fl;
            while (e->name[len] != 0) {
                uint8_t c = (uint8_t)e->name[len];
                if (c == '[' || c == ']' || (uint8_t)(c - 0x20) > 0x5e || ++len == MH_CHEAT_NAME_MAX) {
                    set_err(err, errn, ERR_SYNTAX);
                    return 0;
                }
            }
            if (len == 0 || !e->lines || (uint16_t)(e->nlines - 1) >= 0xff ||
                !ch_lines_structure_ok(e->lines, e->nlines)) {
                set_err(err, errn, ERR_SYNTAX);
                return 0;
            }
            fl = e->flags;
            if (fl & 0x80) {
                set_err(err, errn, "Um cheat possui flags desconhecidas.");
                return 0;
            }
            if (((fl & MH_CHEAT_F_CUSTOM) != 0) != ((fl & MH_CHEAT_F_AUDITED) == 0)) {
                set_err(err, errn, "Cada cheat precisa ser exatamente AUDITED xor CUSTOM.");
                return 0;
            }
            if ((fl & MH_CHEAT_F_MANDATORY) && (fl & 9) != 9) {
                set_err(err, errn, "Um cheat obrigatorio precisa ser auditado e permanecer ativo.");
                return 0;
            }
            if (fl & MH_CHEAT_F_AUDITED) {
                if ((uint8_t)(e->game - 1) > 2 || e->game != p->game || e->variant == NONE32 ||
                    e->variant != p->variant || e->id == NONE32) {
                    set_err(err, errn, "Um cheat auditado perdeu ou divergiu da proveniencia do header.");
                    return 0;
                }
                audited++;
            }
            if ((fl & MH_CHEAT_F_CUSTOM) &&
                ((fl & MH_CHEAT_F_DB_DEFAULT) || e->game != 0 || e->variant != NONE32 || e->id != NONE32)) {
                set_err(err, errn, "Um cheat personalizado possui proveniencia/flags oficiais invalidas.");
                return 0;
            }
            total += e->nlines;
            if (total > 0x1fe) {
                set_err(err, errn, "O catalogo excede 510 linhas de codigo.");
                return 0;
            }
            if (fl & MH_CHEAT_F_ENABLED) {
                active += e->nlines;
                if (!ch_v5_program(e->lines, e->nlines, NULL)) {
                    set_err(err, errn, "Nao foi possivel calcular o custo do programa ativo.");
                    return 0;
                }
            }
        }
        if (active > 0x1fe) {
            set_err(err, errn, "Os cheats ativos excedem o limite interno do perfil. Desative alguns codigos.");
            return 0;
        }
    }
    if (mh_cheat_profile_serialized_size(p) > MH_CHEAT_BLOB_MAX) {
        set_err(err, errn, "O catalogo de cheats excede a regiao reservada de 10.240 bytes.");
        return 0;
    }
    if (audited != 0) {
        if ((uint8_t)(p->game - 1) < 3 && p->title != NONE32 && p->variant != NONE32 &&
            p->db_crc == MH_CHEAT_DB_CRC)
            return 1;
        set_err(err, errn, "Perfil auditado possui header de proveniencia invalido.");
        return 0;
    }
    if (p->game == 0 && p->title == NONE32 && p->variant == NONE32 && p->db_crc == 0) return 1;
    set_err(err, errn, "Perfil somente CUSTOM deve usar proveniencia sentinela e CRC zero.");
    return 0;
}

int mh_cheat_profile_validate(const MhCheatProfile *p, char *err, size_t errn)
{
    return ch_profile_validate(p, err, errn);
}

/* orig: cheat_profile_validate_hardware @0x140010130 is a jump to the same code. */
int mh_cheat_profile_validate_hardware(const MhCheatProfile *p, char *err, size_t errn)
{
    return ch_profile_validate(p, err, errn);
}

/* orig: profile_active_stats.part.0 @0x140026930 */
void mh_cheat_profile_active_stats(const MhCheatProfile *p, uint32_t *enabled, uint32_t *lines,
                                   uint32_t *cost, uint32_t *has_audited)
{
    for (uint32_t i = 0; i < p->count; i++) {
        const MhCheatEntry *e = &p->entries[i];
        uint32_t c = 0;
        if (!(e->flags & MH_CHEAT_F_ENABLED)) continue;
        if (enabled) (*enabled)++;
        if (lines) *lines += e->nlines;
        if ((uint16_t)(e->nlines - 1) < 0xff && e->lines && ch_lines_structure_ok(e->lines, e->nlines) &&
            ch_v5_program(e->lines, e->nlines, &c) && cost)
            *cost += c;
        if ((e->flags & MH_CHEAT_F_AUDITED) && has_audited) *has_audited = 1;
    }
}

/* orig: cheat_profiles_validate_combined @0x140010140 */
int mh_cheat_profiles_validate_combined(const MhCheatProfile *a, const MhCheatProfile *b, char *err, size_t errn)
{
    uint32_t enabled = 0, lines = 0, cost_a = 0, cost_b = 0, aud_a = 0, aud_b = 0;
    if (err && errn) err[0] = 0;
    if (!ch_profile_validate(a, err, errn) || !ch_profile_validate(b, err, errn)) return 0;
    if (!a && !b) return 1;
    if (a) mh_cheat_profile_active_stats(a, &enabled, &lines, &cost_a, &aud_a);
    if (b) mh_cheat_profile_active_stats(b, &enabled, &lines, &cost_b, &aud_b);
    if (enabled >= 0x101) {
        set_err(err, errn, "Case e disco somam cheats demais. Desative alguns codigos.");
        return 0;
    }
    if (lines >= 0x1ff) {
        set_err(err, errn, "Case e disco excedem o limite interno de linhas. Desative alguns codigos.");
        return 0;
    }
    if (aud_a && aud_b && a && b && (a->variant != b->variant || a->game != b->game)) {
        set_err(err, errn,
                "Cheats auditados da Case e do disco pertencem a variantes diferentes. Use a variante oficial "
                "no disco ou mantenha a Case apenas com cheats personalizados.");
        return 0;
    }
    return 1;
}

/* ---- adding entries --------------------------------------------------------------------- */

/* orig: cheat_profile_add_entry @0x14000a8e0 */
int mh_cheat_profile_add_entry(MhCheatProfile *p, const MhCheatEntry *e)
{
    uint32_t sum = 0;
    char tmp[128];
    if (!p || !e || !e->name[0] || (uint16_t)(e->nlines - 1) >= 0xff || !e->lines) return 0;
    if (!ch_lines_structure_ok(e->lines, e->nlines)) return 0;
    for (uint32_t i = 0; i < p->count; i++) sum += p->entries[i].nlines;
    if ((unsigned)e->nlines + sum >= 0x1ff || p->count + 1 >= 0x101) return 0;
    if (!grow(p, p->count + 1)) return 0;
    if (!entry_copy(&p->entries[p->count], e)) {
        mh_cheat_entry_clear(&p->entries[p->count]);
        return 0;
    }
    p->count++;
    if (ch_profile_validate(p, tmp, sizeof tmp)) return 1;
    if (p->count) remove_unchecked(p, p->count - 1);
    return 0;
}

/* orig: cheat_profile_add_audited @0x140009c00 */
int mh_cheat_profile_add_audited(MhCheatProfile *p, const MhCheatDb *db, uint32_t variant, uint32_t entry)
{
    MhCheatVariantInfo vi;
    MhCheatEntry e;
    int r;
    if (!p || !db || db->crc != MH_CHEAT_DB_CRC || !mh_cheat_db_variant_info(db, variant, &vi)) return 0;
    if (p->game == 0 && p->variant == NONE32) {
        p->game = vi.game;
        p->title = vi.title;
        p->variant = variant;
        p->db_crc = db->crc;
    } else if (p->title != vi.title || p->variant != variant || p->game != vi.game || p->db_crc != db->crc) {
        return 0;
    }
    if (!mh_cheat_db_copy_entry(db, variant, entry, &e)) return 0;
    if (p->count) {
        int at = mh_cheat_profile_find_audited(p, variant, e.id);
        if (at >= 0) {
            mh_cheat_entry_clear(&e);
            return 1;
        }
    }
    r = mh_cheat_profile_add_entry(p, &e);
    mh_cheat_entry_clear(&e);
    return r;
}

/* orig: cheat_profile_add_catalog_entry @0x140009d80 */
int mh_cheat_profile_add_catalog_entry(MhCheatProfile *p, const MhCheatDb *db, uint32_t variant, uint32_t entry,
                                       unsigned budget, char *err, size_t errn)
{
    MhCheatVariantInfo vi;
    MhCheatEntry e;
    int any_audited = 0, r;
    uint32_t sv_title, sv_variant, sv_crc;
    uint8_t sv_game;
    if (err && errn) err[0] = 0;
    if (!p || !db || !db->loaded || !mh_cheat_db_variant_info(db, variant, &vi) ||
        !mh_cheat_db_copy_entry(db, variant, entry, &e)) {
        set_err(err, errn, "Este grupo nao e suportado pelo motor atual.");
        return 0;
    }
    for (uint32_t i = 0; i < p->count; i++) {
        const MhCheatEntry *x = &p->entries[i];
        if (x->flags & MH_CHEAT_F_AUDITED) any_audited = 1;
        if (x->nlines == e.nlines && mh_str_icmp(x->name, e.name) == 0 &&
            memcmp(x->lines, e.lines, (size_t)e.nlines * 6) == 0) {
            mh_cheat_entry_clear(&e);
            return 1;
        }
    }
    if (budget < p->count * 4 + 0x44) {
        mh_cheat_entry_clear(&e);
        set_err(err, errn, "O grupo inteiro nao cabe. Remova outro grupo da lista preparada antes de adicionar este.");
        return 0;
    }
    e.flags &= 0xfc; /* prepared, not active */
    sv_title = p->title;
    sv_variant = p->variant;
    sv_game = p->game;
    sv_crc = p->db_crc;
    if (any_audited) {
        if (p->variant != variant || p->game != vi.game) {
            mh_cheat_entry_clear(&e);
            set_err(err, errn, "Remova os cheats do titulo anterior antes de adicionar outro jogo original.");
            return 0;
        }
    } else {
        p->game = vi.game;
        p->title = vi.title;
        p->variant = variant;
        p->db_crc = db->crc;
    }
    r = mh_cheat_profile_add_entry(p, &e);
    mh_cheat_entry_clear(&e);
    if (!r) {
        p->game = sv_game;
        p->db_crc = sv_crc;
        p->title = sv_title;
        p->variant = sv_variant;
        set_err(err, errn, "O grupo inteiro excede o limite da selecao. Nada foi adicionado.");
        return 0;
    }
    return 1;
}

/* orig: cheat_profile_add_custom @0x14000a0a0 and _disabled @0x14000a4c0 (same body,
 * different flag mask). */
static int add_custom_flags(MhCheatProfile *p, const char *name, const uint8_t *lines, uint16_t nlines, uint8_t fl)
{
    MhCheatEntry e;
    char tmp[256];
    int r;
    memset(&e, 0, sizeof e);
    mh_str_copy(e.name, MH_CHEAT_NAME_MAX, name);
    e.flags = fl;
    e.variant = NONE32;
    e.id = NONE32;
    if (!p || (uint16_t)(nlines - 1) >= 0xff || !e.name[0] || !lines) return 0;
    if (!ch_lines_structure_ok(lines, nlines)) return 0;
    {
        uint32_t sum = 0;
        for (uint32_t i = 0; i < p->count; i++) sum += p->entries[i].nlines;
        if ((unsigned)nlines + sum >= 0x1ff || p->count + 1 >= 0x101) return 0;
    }
    if (!grow(p, p->count + 1)) return 0;
    e.nlines = nlines;
    e.lines = (uint8_t *)lines; /* borrowed, copied by entry_copy below */
    if (!entry_copy(&p->entries[p->count], &e)) {
        mh_cheat_entry_clear(&p->entries[p->count]);
        return 0;
    }
    p->count++;
    r = ch_profile_validate(p, tmp, sizeof tmp);
    if (r) return 1;
    if (p->count) remove_unchecked(p, p->count - 1);
    return 0;
}

int mh_cheat_profile_add_custom(MhCheatProfile *p, const char *name, const uint8_t *lines, uint16_t nlines, uint8_t hint)
{
    return add_custom_flags(p, name, lines, nlines, (uint8_t)((hint & 0xe5) | 5));
}

int mh_cheat_profile_add_custom_disabled(MhCheatProfile *p, const char *name, const uint8_t *lines,
                                         uint16_t nlines, uint8_t hint)
{
    return add_custom_flags(p, name, lines, nlines, (uint8_t)((hint & 0xe4) | 4));
}

/* ---- restoring the user's choices on a fresh catalog -------------------------------------- */
static int same_audited(const MhCheatEntry *a, const MhCheatEntry *b)
{
    return a->game == b->game && a->variant == b->variant && a->id == b->id && a->nlines == b->nlines &&
           memcmp(a->lines, b->lines, (size_t)a->nlines * 6) == 0;
}

/* orig: cheat_profile_restore_user_state @0x14000ddc0 */
int mh_cheat_profile_restore_user_state(MhCheatProfile *fresh, const MhCheatProfile *saved, char *err, size_t errn)
{
    if (err && errn) err[0] = 0;
    if (!fresh || !saved) return 1;
    if (fresh->game != saved->game || fresh->variant != saved->variant) {
        set_err(err, errn,
                "O perfil gravado foi preservado. Outra biblioteca pode ser consultada no editor sem alterar "
                "suas escolhas.");
        return 0;
    }
    /* every audited group the user had must still exist in the new catalog */
    for (uint32_t i = 0; i < saved->count; i++) {
        const MhCheatEntry *s = &saved->entries[i];
        int found = 0;
        if (s->flags & MH_CHEAT_F_CUSTOM) continue;
        for (uint32_t j = 0; j < fresh->count && !found; j++) {
            const MhCheatEntry *f = &fresh->entries[j];
            if (!(f->flags & MH_CHEAT_F_CUSTOM) && same_audited(s, f)) found = 1;
        }
        if (!found) {
            set_err(err, errn,
                    "O perfil gravado foi preservado: a nova selecao nao contem todos os grupos anteriores.");
            return 0;
        }
    }
    /* carry the enabled state over */
    for (uint32_t i = 0; i < fresh->count; i++) {
        MhCheatEntry *f = &fresh->entries[i];
        if (f->flags & MH_CHEAT_F_CUSTOM) continue;
        for (uint32_t j = 0; j < saved->count; j++) {
            const MhCheatEntry *s = &saved->entries[j];
            if (!(s->flags & MH_CHEAT_F_CUSTOM) && same_audited(s, f)) {
                if (!(f->flags & MH_CHEAT_F_MANDATORY) && !(s->flags & MH_CHEAT_F_ENABLED))
                    f->flags &= 0xfe;
                else
                    f->flags |= MH_CHEAT_F_ENABLED;
                break;
            }
        }
    }
    /* then the custom entries */
    for (uint32_t i = 0; i < saved->count; i++) {
        if (!(saved->entries[i].flags & MH_CHEAT_F_CUSTOM)) continue;
        if (!mh_cheat_profile_add_entry(fresh, &saved->entries[i])) {
            set_err(err, errn,
                    "O catalogo completo e os cheats personalizados nao cabem juntos no bloco RAW. Nada foi "
                    "descartado.");
            return 0;
        }
    }
    /* and follow the saved order for everything that matches */
    for (uint32_t si = 0; si < saved->count && si < fresh->count; si++) {
        const MhCheatEntry *s = &saved->entries[si];
        for (uint32_t fi = si; fi < fresh->count; fi++) {
            const MhCheatEntry *f = &fresh->entries[fi];
            int match;
            if ((s->flags ^ f->flags) & MH_CHEAT_F_CUSTOM) continue;
            if (s->nlines != f->nlines || memcmp(s->lines, f->lines, (size_t)f->nlines * 6) != 0) continue;
            if (!(s->flags & MH_CHEAT_F_CUSTOM))
                match = s->game == f->game && s->variant == f->variant && s->id == f->id;
            else
                match = mh_str_icmp(s->name, f->name) == 0;
            if (!match) continue;
            if (fi != si) {
                MhCheatEntry moved = fresh->entries[fi];
                memmove(&fresh->entries[si + 1], &fresh->entries[si], (size_t)(fi - si) * sizeof moved);
                fresh->entries[si] = moved;
            }
            break;
        }
    }
    return ch_profile_validate(fresh, err, errn);
}

/* ---- building the catalog of a title ------------------------------------------------------- */
#define ERR_NOFIT "Nenhuma variante auditada cabe com seguranca no PSTCHT1 estendido de 10.240 bytes."

/* orig: cheat_profile_build_exact_catalog @0x14000ac80 */
int mh_cheat_profile_build_exact_catalog(const MhCheatDb *db, uint32_t title, const char *serial,
                                         MhCheatProfile **out, char *err, size_t errn)
{
    MhCheatProfile *best = NULL;
    uint32_t best_count = 0, nvar;
    if (out) *out = NULL;
    if (err && errn) err[0] = 0;
    if (!db || !db->loaded || !out) {
        set_err(err, errn, "Banco de cheats auditado indisponivel.");
        return 0;
    }
    nvar = db->data && title < db->n_titles ? mh_cheat_db_title_variant_count(db, title) : 0;
    if (nvar == 0) {
        set_err(err, errn, ERR_NOFIT);
        return 0;
    }
    for (unsigned vi_idx = 0; vi_idx < nvar; vi_idx++) {
        MhCheatVariantInfo vi;
        MhCheatProfile *p;
        int abandon = 0;
        char tmp[128];
        if (!mh_cheat_db_title_variant_at(db, title, vi_idx, &vi)) continue;
        p = mh_calloc(sizeof *p);
        if (!p) continue;
        p->title = vi.title;
        p->variant = vi.variant;
        p->game = vi.game;
        p->db_crc = db->crc;
        for (uint32_t k = 0; k < vi.entry_count && !abandon; k++) {
            MhCheatEntryInfo info;
            MhCheatEntry e;
            uint32_t lines = 0;
            if (!mh_cheat_db_entry_info(db, vi.variant, k, &info)) continue; /* skipped, not fatal */
            if (!mh_cheat_db_copy_entry(db, vi.variant, k, &e)) {
                abandon = 1;
                break;
            }
            if (mh_cheat_profile_find_audited(p, vi.variant, e.id) >= 0) {
                mh_cheat_entry_clear(&e);
                continue;
            }
            if (!(e.flags & MH_CHEAT_F_MANDATORY)) e.flags &= 0xfe;
            if (p->count < 0x100) {
                for (uint32_t i = 0; i < p->count; i++) lines += p->entries[i].nlines;
                if (lines + e.nlines < 0x1ff && p->count * 4 + 0x44 < 0x2801 &&
                    mh_cheat_profile_add_entry(p, &e)) {
                    mh_cheat_entry_clear(&e);
                    continue;
                }
            }
            mh_cheat_entry_clear(&e);
            abandon = 1;
        }
        if (abandon || p->count == 0 || !ch_profile_validate(p, tmp, sizeof tmp)) {
            mh_cheat_profile_free(p);
            continue;
        }
        if (best && p->count <= best_count) {
            mh_cheat_profile_free(p);
            continue;
        }
        mh_cheat_profile_free(best);
        best = p;
        best_count = p->count;
    }
    if (!best) {
        set_err(err, errn, ERR_NOFIT);
        return 0;
    }
    mh_str_copy(best->serial, sizeof best->serial, serial);
    *out = best;
    return 1;
}

/* orig: cheat_profile_build_for_disc_active @0x14000b670 */
int mh_cheat_profile_build_for_disc(const MhCheatDb *db, const char *serial, MhCheatProfile **out, char *err,
                                    size_t errn)
{
    uint32_t title;
    if (out) *out = NULL;
    if (!db || !db->loaded) {
        set_err(err, errn, "Banco de cheats incorporado indisponivel.");
        return 0;
    }
    if (serial && *serial && db->crc == MH_CHEAT_DB_CRC && mh_cheat_db_find_title_for_disc(db, serial, &title))
        return mh_cheat_profile_build_exact_catalog(db, title, serial, out, err, errn);
    set_err(err, errn, "Disco confirmado nao possui catalogo de cheats inequivoco.");
    return 0;
}
