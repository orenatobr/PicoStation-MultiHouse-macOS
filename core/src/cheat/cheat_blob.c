/* PSTCHT1: the cheat region of the card. orig: cheat_profile_serialize/deserialize,
 * cheat_blob_validate, cheat_blob_scope, disc_bank_valid. Layout in docs/EMBEDDED_DATA.md. */
#include "cheat_internal.h"

#define NONE32 0xFFFFFFFFu

static void set_err(char *err, size_t errn, const char *msg) { mh_err_set(err, errn, "%s", msg); }

/* CRC-32 of a blob with the crc field (+0x3c) taken as zero. */
static uint32_t blob_crc(const uint8_t *b, uint32_t len)
{
    static const uint8_t zero[4];
    uint32_t c = mh_crc32_begin();
    c = mh_crc32_update(c, b, 0x3c);
    c = mh_crc32_update(c, zero, 4);
    if (len > 0x40) c = mh_crc32_update(c, b + 0x40, len - 0x40);
    return mh_crc32_end(c);
}

static int has_magic(const uint8_t *b) { return memcmp(b, "PSTCHT1\0", 8) == 0; }

uint8_t mh_cheat_blob_scope(const uint8_t *b, uint32_t len)
{
    if (b && len > 0x3f && memcmp(b, "PSTCHT1", 7) == 0) return b[0x38];
    return 0;
}

/* ---- version 6 ------------------------------------------------------------------------------ */

/* orig: disc_bank_valid @0x140017be0 */
int mh_cheat_disc_bank_valid(const uint8_t *b, uint32_t len)
{
    const MhCheatBank *bank = ch_bank_active();
    uint8_t seen[32] = {0};
    uint32_t n, title, run, enabled = 0, lines = 0, i;
    if (len < 0x40 || len > MH_CHEAT_BLOB_MAX || !b || !has_magic(b)) return 0;
    n = ch_rd32(b + 0x10);
    title = ch_rd32(b + 0x2c);
    run = n * 8 + 0x40;
    if (ch_rd16(b + 8) != 6 || ch_rd16(b + 10) != 0x40 || ch_rd32(b + 0xc) != 0 || n - 1 >= 0x100 ||
        ch_rd32(b + 0x1c) != 8 || ch_rd32(b + 0x20) != 0x40 || ch_rd32(b + 0x24) != run || run > len ||
        ch_rd32(b + 0x28) != len || !bank || title >= bank->n_titles || title != ch_rd32(b + 0x30) ||
        ch_rd32(b + 0x34) != MH_CHEAT_DB_CRC || (uint8_t)(b[0x38] - 1) >= 2 || b[0x39] != 1 ||
        ch_rd16(b + 0x3a) != 0)
        return 0;
    if (ch_rd32(b + 0x3c) != blob_crc(b, len)) return 0;
    for (i = 0; i < n; i++) {
        const uint8_t *r = b + 0x40 + (size_t)i * 8;
        uint32_t name_off = ch_rd16(r), code_off = ch_rd16(r + 2), pos = ch_rd16(r + 4), p;
        uint8_t nl = r[6], fl = r[7];
        if (pos > 0xff || nl == 0 || (fl & 0xcc) != 8 || (fl & 3) == 2) return 0;
        if (seen[pos >> 3] & (1u << (pos & 7))) return 0;
        if (name_off != run) return 0;
        seen[pos >> 3] |= (uint8_t)(1u << (pos & 7));
        for (p = run;; p++) {
            uint8_t c, k;
            if (p >= len) return 0;
            c = b[p];
            if (c == 0) break;
            if (p == run + 0x2f) return 0;
            k = (uint8_t)(c - 0x20);
            if (k > 0x3a) return 0;
            if ((0xf8000001fc009f3eull >> k) & 1) return 0;
        }
        if (p == run) return 0;
        if (code_off != p + 1) return 0;
        if (len - code_off < (uint32_t)nl * 6) return 0;
        if (!ch_v5_program(b + code_off, nl, NULL)) return 0;
        run = code_off + (uint32_t)nl * 6;
        enabled += fl & 1;
        lines += nl;
    }
    if (run != len || enabled != ch_rd32(b + 0x14) || lines != ch_rd32(b + 0x18) || lines >= 0x1ff) return 0;
    for (i = 0; i < n; i++) {
        const uint8_t *r = b + 0x40 + (size_t)i * 8;
        const ChBankGroup *g = ch_bank_group(bank, title, ch_rd16(r + 4));
        if (!g) {
            if (!(r[7] & 0x20)) return 0;
        } else {
            if (r[6] != g->nlines) return 0;
            if (mh_str_icmp((const char *)b + ch_rd16(r), g->name) != 0) return 0;
            if (memcmp(b + ch_rd16(r + 2), ch_bank_lines_b(bank, g), (size_t)r[6] * 6) != 0) return 0;
        }
    }
    return 1;
}

/* ---- validation of any version ---------------------------------------------------------------- */

/* version 5: references only */
static int validate_v5(const uint8_t *b, uint32_t len)
{
    const MhCheatBank *bank = ch_bank_active();
    uint8_t seen[32] = {0};
    uint32_t n = ch_rd32(b + 0x10), title = ch_rd32(b + 0x2c), enabled = 0, lines = 0, i;
    if (len > 0x440 || !has_magic(b) || ch_rd16(b + 10) != 0x40 || ch_rd32(b + 0xc) != 0 || n - 1 >= 0x100 ||
        len != n * 4 + 0x40 || ch_rd32(b + 0x1c) != 4 || ch_rd32(b + 0x20) != 0x40 ||
        len != ch_rd32(b + 0x24) || len != ch_rd32(b + 0x28) || title != ch_rd32(b + 0x30) ||
        ch_rd32(b + 0x34) != MH_CHEAT_DB_CRC || (uint8_t)(b[0x38] - 1) >= 2 || b[0x39] != 1 ||
        ch_rd16(b + 0x3a) != 0 || ch_rd32(b + 0x3c) != blob_crc(b, len))
        return 0;
    for (i = 0; i < n; i++) {
        const uint8_t *r = b + 0x40 + (size_t)i * 4;
        uint32_t pos = ch_rd16(r);
        uint8_t fl = r[2];
        const ChBankGroup *g = ch_bank_group(bank, title, pos);
        if (!g || pos > 0xff || r[3] != 0 || (fl & 0xc4) != 0 || !(fl & 8) || (fl & 3) == 2) return 0;
        if (seen[pos >> 3] & (1u << (pos & 7))) return 0;
        seen[pos >> 3] |= (uint8_t)(1u << (pos & 7));
        enabled += fl & 1;
        lines += g->nlines;
    }
    return enabled == ch_rd32(b + 0x14) && lines == ch_rd32(b + 0x18) && lines < 0x1ff;
}

#define E_REC "Registro de cheat PSTCHT1 invalido."

/* orig: cheat_blob_validate @0x140005850 */
int mh_cheat_blob_validate(const uint8_t *b, uint32_t len, char *err, size_t errn)
{
    uint32_t n, code_run, lines = 0, enabled = 0, active = 0, audited = 0, i;
    if (err && errn) err[0] = 0;
    if (!b || len < 0x40) goto missing;
    if (ch_rd16(b + 8) == 6) {
        int ok = mh_cheat_disc_bank_valid(b, len);
        if (!ok) set_err(err, errn, "Catalogo do disco invalido ou divergente do banco original.");
        return ok;
    }
    if (ch_rd16(b + 8) == 5) {
        if (validate_v5(b, len)) return 1;
        set_err(err, errn, "Referencias do catalogo original invalidas.");
        return 0;
    }
    if (len > MH_CHEAT_BLOB_MAX) goto missing;
    n = ch_rd32(b + 0x10);
    if (!(has_magic(b) && ch_rd16(b + 8) == 4 && ch_rd16(b + 10) == 0x40 && ch_rd32(b + 0xc) == 0 &&
          ch_rd16(b + 0x3a) == 0 && ch_rd32(b + 0x1c) == 0x60 && ch_rd32(b + 0x20) == 0x40 && n < 0x101 &&
          ch_rd32(b + 0x24) == n * 0x60 + 0x40 && ch_rd32(b + 0x28) == len && ch_rd32(b + 0x24) <= len &&
          ch_rd32(b + 0x14) <= n && ch_rd32(b + 0x18) < 0x1ff && (uint8_t)(b[0x38] - 1) < 2 &&
          ch_rd32(b + 0x3c) == blob_crc(b, len))) {
        set_err(err, errn, "Cabecalho PSTCHT1 invalido.");
        return 0;
    }
    if (!(n != 0 && b[0x39] < 4 &&
          (b[0x39] == 0 ? (ch_rd32(b + 0x2c) == NONE32 && ch_rd32(b + 0x30) == NONE32 && ch_rd32(b + 0x34) == 0)
                        : (ch_rd32(b + 0x2c) != NONE32 && ch_rd32(b + 0x30) != NONE32 &&
                           ch_rd32(b + 0x34) == MH_CHEAT_DB_CRC)))) {
        set_err(err, errn, "Proveniencia do header PSTCHT1 invalida.");
        return 0;
    }
    code_run = ch_rd32(b + 0x24);
    for (i = 0; i < n; i++) {
        const uint8_t *r = b + 0x40 + (size_t)i * 0x60;
        uint32_t nl = ch_rd16(r + 0x4c), code_off = ch_rd32(r + 0x48), clen = nl * 6, k, len_name = 0;
        uint8_t fl, game;
        while (r[len_name] != 0) {
            uint8_t c = r[len_name];
            if (c == '[' || c == ']' || (uint8_t)(c - 0x20) > 0x5e || ++len_name == 0x40) goto bad_rec;
        }
        if (len_name == 0) goto bad_rec;
        for (k = len_name + 1; k < 0x40; k++)
            if (r[k] != 0) goto bad_rec;
        for (k = 0x54; k < 0x60; k++)
            if (r[k] != 0) goto bad_rec;
        if ((uint16_t)(nl - 1) >= 0xff) goto bad_rec;
        if (len < code_off || code_off != code_run || len - code_off < clen || (r[0x4f] & 0x80)) goto bad_rec;
        if (ch_rd32(r + 0x50) != mh_crc32(b + code_off, clen)) goto bad_rec;
        if (!ch_lines_structure_ok(b + code_off, nl)) goto bad_rec;
        fl = r[0x4f];
        game = r[0x4e];
        if (((fl & 8) == 0) != ((fl >> 2) & 1)) {
            set_err(err, errn, "Registro PSTCHT1 precisa ser exatamente AUDITED xor CUSTOM.");
            return 0;
        }
        if (!(fl & 8)) {
            if (game != 0 || ch_rd32(r + 0x40) != NONE32 || ch_rd32(r + 0x44) != NONE32 || (fl & 0x10)) {
                set_err(err, errn, "Registro CUSTOM possui proveniencia oficial invalida.");
                return 0;
            }
        } else {
            if ((uint8_t)(game - 1) > 2 || b[0x39] != game || ch_rd32(r + 0x40) != ch_rd32(b + 0x30) ||
                ch_rd32(r + 0x44) == NONE32) {
                set_err(err, errn, "Registro AUDITED diverge da proveniencia do header PSTCHT1.");
                return 0;
            }
            audited++;
        }
        if ((fl & 2) && (fl & 9) != 9) {
            set_err(err, errn, "Cheat obrigatorio precisa ser AUDITED e ENABLED no PSTCHT1.");
            return 0;
        }
        if ((fl & 0xc) == 0xc) {
            set_err(err, errn, "Registro PSTCHT1 marcado como auditado e personalizado ao mesmo tempo.");
            return 0;
        }
        lines += nl;
        code_run += clen;
        if (lines > 0x1fe) {
            set_err(err, errn, "PSTCHT1 excede 510 linhas de catalogo.");
            return 0;
        }
        if (fl & 1) {
            enabled++;
            active += nl;
            if (!ch_v5_program(b + code_off, nl, NULL)) {
                set_err(err, errn, "Falha ao calcular custo do programa PSTCHT1 ativo.");
                return 0;
            }
        }
    }
    if (code_run == len && ch_rd32(b + 0x18) == lines && ch_rd32(b + 0x14) == enabled && active < 0x1ff &&
        (b[0x39] == 0 || audited != 0) && n != 0)
        return 1;
    set_err(err, errn,
            "Contagens, efeito, proveniencia ou limite de 31 linhas ativas invalidos no PSTCHT1.");
    return 0;
bad_rec:
    set_err(err, errn, E_REC);
    return 0;
missing:
    set_err(err, errn, "Blob de cheats ausente, truncado ou maior que 10.240 bytes.");
    return 0;
}

/* ---- deserialize -------------------------------------------------------------------------------- */

/* All lines are a write or a prefix+write pair: the "simple" pattern the hardware loader prefers. */
static int lines_simple(const uint8_t *l, unsigned n)
{
    unsigned i = 0;
    while (i < n) {
        uint8_t c = l[i * 6];
        if (c == 0x30 || c == 0x80) {
            i++;
        } else {
            if (!(c == 0x70 || c == 0x90) || n <= i + 1 || (l[(i + 1) * 6] != 0x30 && l[(i + 1) * 6] != 0x80))
                return 0;
            i += 2;
        }
    }
    return 1;
}

static int push_entry(MhCheatProfile *p, const char *name, uint8_t flags, uint8_t game, uint16_t nlines,
                      uint32_t variant, uint32_t id, const uint8_t *lines)
{
    MhCheatEntry *e;
    if (p->count >= p->cap) {
        uint32_t cap = p->cap ? p->cap * 2 : 16;
        MhCheatEntry *ne;
        if (cap > 256) cap = 256;
        if (cap <= p->count) return 0;
        ne = mh_realloc(p->entries, (size_t)cap * sizeof *ne);
        if (!ne) return 0;
        memset(ne + p->cap, 0, (size_t)(cap - p->cap) * sizeof *ne);
        p->entries = ne;
        p->cap = cap;
    }
    e = &p->entries[p->count];
    memset(e, 0, sizeof *e);
    mh_str_copy(e->name, MH_CHEAT_NAME_MAX, name);
    e->flags = flags;
    e->game = game;
    e->nlines = nlines;
    e->variant = variant;
    e->id = id;
    if (nlines) {
        e->lines = mh_alloc((size_t)nlines * 6);
        if (!e->lines) return 0;
        memcpy(e->lines, lines, (size_t)nlines * 6);
    }
    p->count++;
    return 1;
}

/* orig: cheat_profile_deserialize @0x14000baa0 */
int mh_cheat_profile_deserialize(const uint8_t *b, uint32_t len, MhCheatProfile **out, char *err, size_t errn)
{
    MhCheatProfile *p;
    uint32_t n, i;
    uint16_t ver;
    if (out) *out = NULL;
    if (!mh_cheat_blob_validate(b, len, err, errn)) return 0;
    ver = ch_rd16(b + 8);
    n = ch_rd32(b + 0x10);
    p = mh_calloc(sizeof *p);
    if (!p) return 0;
    p->title = p->variant = NONE32;

    if (ver == 5 || ver == 6) {
        const MhCheatBank *bank = ch_bank_active();
        p->title = ch_rd32(b + 0x2c);
        p->variant = ch_rd32(b + 0x30);
        p->game = 1;
        p->db_crc = ch_rd32(b + 0x34);
        if (n > 0x100) goto bad;
        for (i = 0; i < n; i++) {
            const uint8_t *r = b + 0x40 + (size_t)i * (ver == 6 ? 8 : 4);
            const ChBankGroup *g = ch_bank_group(bank, p->title, ch_rd16(ver == 6 ? r + 4 : r));
            uint8_t fl = r[ver == 6 ? 7 : 2];
            if (!g) {
                if (!push_entry(p, (const char *)b + ch_rd16(r), (uint8_t)(((fl << 2) & 0x40) | (fl & 1) | 0x24), 0,
                                r[6], NONE32, NONE32, b + ch_rd16(r + 2)))
                    goto bad;
            } else {
                if (!push_entry(p, g->name, fl, 1, g->nlines, ch_rd32(b + 0x30), g->id, ch_bank_lines_a(bank, g)))
                    goto bad;
            }
        }
        if (!ch_profile_validate(p, err, errn)) goto bad;
        p->state = ver == 5 ? MH_CHEAT_STATE_REFS_ONLY : MH_CHEAT_STATE_NONE;
        *out = p;
        return 1;
    }

    /* version 4 */
    {
        int legacy = 0;
        if (b[0x39] == 0 || ch_rd32(b + 0x34) != MH_CHEAT_DB_LEGACY_CRC) {
            p->title = ch_rd32(b + 0x2c);
            p->variant = ch_rd32(b + 0x30);
            p->game = b[0x39];
            p->db_crc = ch_rd32(b + 0x34);
        } else {
            legacy = 1;
        }
        if (n > 0x100) goto bad;
        for (i = 0; i < n; i++) {
            const uint8_t *r = b + 0x40 + (size_t)i * 0x60;
            uint8_t fl = r[0x4f], game = r[0x4e];
            uint16_t nl = ch_rd16(r + 0x4c);
            const uint8_t *code = b + ch_rd32(r + 0x48);
            if (legacy && !(fl & MH_CHEAT_F_CUSTOM)) continue; /* audited entries of the old database */
            if ((fl & MH_CHEAT_F_CUSTOM) && nl && lines_simple(code, nl)) fl |= MH_CHEAT_F_SIMPLE;
            if (!push_entry(p, (const char *)r, fl, game, nl, ch_rd32(r + 0x40), ch_rd32(r + 0x44), code)) goto bad;
        }
        if (legacy && p->count == 0) {
            mh_cheat_profile_free(p);
            return 1; /* nothing left, *out stays NULL */
        }
        p->state = (b[0x39] == 1 && ch_rd32(b + 0x34) == MH_CHEAT_DB_CRC) ? MH_CHEAT_STATE_FULL_SNAPSHOT
                                                                          : MH_CHEAT_STATE_NONE;
        if (!ch_profile_validate(p, err, errn)) goto bad;
        *out = p;
        return 1;
    }
bad:
    mh_cheat_profile_free(p);
    return 0;
}

/* ---- serialize ------------------------------------------------------------------------------------ */

/* orig: cheat_profile_serialize @0x14000e2e0 */
int mh_cheat_profile_serialize(const MhCheatProfile *src, uint8_t scope, uint8_t **blob, uint32_t *plen,
                               char *err, size_t errn)
{
    MhCheatProfile *p = NULL;
    uint8_t *buf = NULL;
    uint32_t total, i, enabled = 0, lines = 0;
    int ok = 0;
    if (blob) *blob = NULL;
    if (plen) *plen = 0;
    if (!ch_profile_validate(src, err, errn)) return 0;
    if (!src || src->count == 0) return 1;
    p = mh_cheat_profile_clone(src);
    if (!p) {
        set_err(err, errn, "Memoria insuficiente para preparar cheats.");
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        enabled += p->entries[i].flags & MH_CHEAT_F_ENABLED;
        lines += p->entries[i].nlines;
    }

    if (mh_cheat_profile_needs_wire(p)) {
        int any_program = 0;
        uint32_t code_pos;
        if ((uint8_t)(scope - 1) >= 2) {
            set_err(err, errn, "Escopo de cheats invalido.");
            goto done;
        }
        if (!ch_profile_validate(p, err, errn)) goto done;
        for (i = 0; i < p->count && !any_program; i++) {
            const MhCheatEntry *e = &p->entries[i];
            if ((uint16_t)(e->nlines - 1) < 0xff && e->lines && ch_lines_structure_ok(e->lines, e->nlines))
                any_program = 1;
        }
        if (!any_program) {
            set_err(err, errn,
                    "O catalogo nao contem nenhuma programa do banco original; condicionais e C0 isolados nao "
                    "serao serializados.");
            goto done;
        }
        total = MH_CHEAT_BLOB_HEADER;
        for (i = 0; i < p->count; i++) total += 0x60 + (uint32_t)p->entries[i].nlines * 6;
        if (total - 1 > 0x27ff) {
            set_err(err, errn, "O perfil editado excede a regiao de 10 KiB. Remova grupos antes de aplicar.");
            goto done;
        }
        buf = mh_calloc(total);
        if (!buf) {
            set_err(err, errn, "Memoria insuficiente para serializar cheats.");
            goto done;
        }
        memcpy(buf, "PSTCHT1", 7);
        ch_wr16(buf + 8, 4);
        ch_wr16(buf + 10, 0x40);
        ch_wr32(buf + 0x10, p->count);
        ch_wr32(buf + 0x14, enabled);
        ch_wr32(buf + 0x18, lines);
        ch_wr32(buf + 0x1c, 0x60);
        ch_wr32(buf + 0x20, 0x40);
        code_pos = p->count * 0x60 + 0x40;
        ch_wr32(buf + 0x24, code_pos);
        ch_wr32(buf + 0x28, total);
        ch_wr32(buf + 0x2c, p->title);
        ch_wr32(buf + 0x30, p->variant);
        ch_wr32(buf + 0x34, p->db_crc);
        buf[0x38] = scope;
        buf[0x39] = p->game;
        for (i = 0; i < p->count; i++) {
            const MhCheatEntry *e = &p->entries[i];
            uint8_t *r = buf + 0x40 + (size_t)i * 0x60;
            mh_str_copy((char *)r, 0x40, e->name);
            ch_wr32(r + 0x40, e->variant);
            ch_wr32(r + 0x44, e->id);
            ch_wr32(r + 0x48, code_pos);
            ch_wr16(r + 0x4c, e->nlines);
            r[0x4e] = e->game;
            r[0x4f] = e->flags & 0x3f;
            ch_wr32(r + 0x50, mh_crc32(e->lines, (size_t)e->nlines * 6));
            memcpy(buf + code_pos, e->lines, (size_t)e->nlines * 6);
            code_pos += (uint32_t)e->nlines * 6;
        }
        if (code_pos != total) {
            set_err(err, errn, "Falha interna ao calcular o tamanho do PSTCHT1.");
            goto done;
        }
        ch_wr32(buf + 0x3c, blob_crc(buf, total));
    } else {
        const MhCheatBank *bank = ch_bank_active();
        uint32_t name_pos, custom = 0;
        total = MH_CHEAT_BLOB_HEADER;
        for (i = 0; i < p->count; i++) total += (uint32_t)strlen(p->entries[i].name) + 9 + (uint32_t)p->entries[i].nlines * 6;
        if (total >= 0x2801) {
            set_err(err, errn, "Catalogo excede o espaco reservado de 10 KiB.");
            goto done;
        }
        buf = mh_calloc(total);
        if (!buf) goto done;
        memcpy(buf, "PSTCHT1", 7);
        ch_wr16(buf + 8, 6);
        ch_wr16(buf + 10, 0x40);
        ch_wr32(buf + 0x10, p->count);
        ch_wr32(buf + 0x14, enabled);
        ch_wr32(buf + 0x18, lines);
        ch_wr32(buf + 0x1c, 8);
        ch_wr32(buf + 0x20, 0x40);
        name_pos = p->count * 8 + 0x40;
        ch_wr32(buf + 0x24, name_pos);
        ch_wr16(buf + 0x28, total);
        ch_wr32(buf + 0x2c, p->title);
        ch_wr32(buf + 0x30, p->variant);
        ch_wr32(buf + 0x34, p->db_crc);
        buf[0x38] = scope;
        buf[0x39] = p->game;
        for (i = 0; i < p->count; i++) {
            const MhCheatEntry *e = &p->entries[i];
            uint8_t *r = buf + 0x40 + (size_t)i * 8;
            const uint8_t *code;
            size_t nl = strlen(e->name);
            uint32_t pos;
            if (!(e->flags & MH_CHEAT_F_CUSTOM)) {
                int cp = ch_compact_position(bank, p->title, e);
                const ChBankGroup *g;
                if (cp < 0) {
                    set_err(err, errn, "Um grupo diverge do banco original incorporado. Nada foi gravado.");
                    goto done;
                }
                pos = (uint32_t)cp;
                g = ch_bank_group(bank, p->title, pos);
                code = ch_bank_lines_b(bank, g);
            } else {
                pos = (p->title < bank->n_titles ? bank->count[p->title] : 0) + custom++;
                code = e->lines;
            }
            ch_wr16(r, name_pos);
            memcpy(buf + name_pos, e->name, nl + 1);
            name_pos += (uint32_t)nl + 1;
            ch_wr16(r + 2, name_pos);
            memcpy(buf + name_pos, code, (size_t)e->nlines * 6);
            name_pos += (uint32_t)e->nlines * 6;
            ch_wr16(r + 4, pos);
            r[6] = (uint8_t)e->nlines;
            r[7] = (e->flags & MH_CHEAT_F_CUSTOM) ? (uint8_t)(((e->flags >> 2) & 0x10) | (e->flags & 1) | 0x28)
                                                  : (uint8_t)(e->flags & 0x3f);
        }
        ch_wr32(buf + 0x3c, blob_crc(buf, total));
        if (name_pos != total || !mh_cheat_disc_bank_valid(buf, total)) {
            set_err(err, errn, "Catalogo original invalido ou divergente.");
            goto done;
        }
    }
    if (blob) {
        *blob = buf;
        buf = NULL;
    }
    if (plen) *plen = total;
    ok = 1;
done:
    mh_free(buf);
    mh_cheat_profile_free(p);
    return ok;
}
