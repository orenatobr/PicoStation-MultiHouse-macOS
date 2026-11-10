/* Embedded GameShark V5 database: CDXLZ1 decompression, verification and queries.
 * orig: cheat_db_load and friends, see docs/map/cheat.md. Layout of the MHGSF1 image and the
 * error codes are in docs/EMBEDDED_DATA.md. */
#include <stdatomic.h>

#include "cheat_internal.h"

/* orig: kExpectedSha256 @0x140f1d080 */
static const uint8_t k_expected_sha256[32] = {
    0x1b, 0x48, 0x39, 0x1f, 0x53, 0x00, 0x1a, 0x0b, 0x95, 0xb8, 0x13, 0xa9, 0x73, 0x9e, 0x65, 0x75,
    0x00, 0x7e, 0x8b, 0xa7, 0xf2, 0xcc, 0x73, 0x59, 0xfb, 0x26, 0x19, 0x6a, 0x42, 0x3c, 0xf1, 0xa3};

/* orig: kCheatSerialBindings @0x140f1a9e0 */
typedef struct {
    const char *serial;
    uint32_t title;
} Binding;
static const Binding k_bindings[] = {
#include "serial_bindings.inc"
};
#define N_BINDINGS (sizeof k_bindings / sizeof k_bindings[0])

/* Header field offsets of the MHGSF1 image. */
enum {
    H_GAMES = 0x30, H_TITLES = 0x34, H_VARIANTS = 0x38, H_TEXTS = 0x3c,
    H_CHEATS = 0x40, H_PROGS = 0x44, H_LINES = 0x48, H_STRS = 0x4c
};
#define REC_GAME 0x54
#define REC_TITLE 0xc
#define REC_VARIANT 0x38
#define REC_TEXT 0x1c
#define REC_CHEAT 0x10
#define REC_PROG 8

static _Atomic(const MhCheatDb *) g_active_db; /* orig: g_active_cheat_db */

const MhCheatDb *mh_cheat_db_active(void) { return atomic_load(&g_active_db); }

static void clear_active_if(const MhCheatDb *db)
{
    const MhCheatDb *exp = db;
    atomic_compare_exchange_strong(&g_active_db, &exp, NULL);
}

const MhCheatBank *ch_bank_active(void)
{
    const MhCheatDb *db = atomic_load(&g_active_db);
    return db ? db->bank : NULL;
}

/* ---- CDXLZ1: 12 byte header ("CDXLZ1\0\0", u32 raw size) and an LZ4 block stream ----------- */
static int cdx_decompress(const uint8_t *src, uint64_t n, uint8_t *dst)
{
    const uint64_t RAW = MH_CHEAT_DB_RAW_SIZE;
    uint64_t ip = 12, op = 0;
    if (n < 13 || memcmp(src, "CDXLZ1\0\0", 8) != 0 || ch_rd32(src + 8) != RAW) return 0;
    for (;;) {
        uint8_t token = src[ip++];
        uint64_t lit = token >> 4, mlen = (uint64_t)(token & 15) + 4;
        unsigned ml = token & 15;
        if (lit == 15) {
            uint8_t b;
            do {
                if (ip == n) return 0;
                b = src[ip++];
                if (RAW - b < lit) return 0;
                lit += b;
            } while (b == 255);
        }
        if (n - ip < lit || RAW - op < lit) return 0;
        if (lit) memcpy(dst + op, src + ip, lit);
        ip += lit;
        op += lit;
        if (ip == n) return op == RAW && ml == 0;
        if (n - ip == 1) return 0;
        {
            uint64_t off = ch_rd16(src + ip);
            ip += 2;
            if (off == 0 || op < off) return 0;
            if (ml == 15) {
                uint8_t b;
                mlen = 0x13;
                do {
                    if (ip == n) return 0;
                    b = src[ip++];
                    if (RAW - b < mlen) return 0;
                    mlen += b;
                } while (b == 255);
            }
            if (RAW - op < mlen) return 0;
            for (uint64_t i = 0; i < mlen; i++) dst[op + i] = dst[op + i - off];
            op += mlen;
        }
        if (n <= ip) return 0;
    }
}

/* Strict UTF-8 (the original used MultiByteToWideChar with MB_ERR_INVALID_CHARS). */
static int utf8_valid(const uint8_t *s, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint32_t need, cp;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; }
        else return 0;
        if (n - i <= need) return 0;
        for (uint32_t k = 1; k <= need; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if ((need == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
            (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)))
            return 0;
        i += need + 1;
    }
    return 1;
}

/* orig: mh_string @0x1400249e0 */
int ch_db_string_raw(const MhCheatDb *db, uint32_t idx, const uint8_t **s, uint32_t *len)
{
    const uint8_t *d;
    uint64_t strs, base, off;
    uint32_t n, l;
    if (!db || !(d = db->data)) return 0;
    strs = ch_rd32(d + H_STRS);
    if (strs + 4 > db->size) return 0;
    n = ch_rd32(d + strs);
    if (idx >= n) return 0;
    base = strs + 4 + (uint64_t)n * 8;
    if (base > db->size) return 0;
    off = base + ch_rd32(d + strs + 4 + (uint64_t)idx * 8);
    l = ch_rd32(d + strs + 8 + (uint64_t)idx * 8);
    if (off > db->size || l > db->size - off) return 0;
    *s = d + off;
    *len = l;
    return 1;
}

/* orig: mh_string_w @0x140024a70 (UTF-16 there, UTF-8 here) */
int mh_cheat_db_string(const MhCheatDb *db, uint32_t idx, char *out, size_t n)
{
    const uint8_t *s;
    uint32_t l;
    if (!out || n == 0) return 0;
    out[0] = 0;
    if (!ch_db_string_raw(db, idx, &s, &l)) return 0;
    if ((size_t)l + 1 <= n) {
        memcpy(out, s, l);
        out[l] = 0;
    } else {
        /* too long: cut at n-1 bytes without splitting a sequence */
        size_t cut = n - 1;
        while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;
        memcpy(out, s, cut);
        out[cut] = 0;
    }
    return 1;
}

/* ---- load ----------------------------------------------------------------------------------- */
#define LFAIL(code) do { err = (code); goto fail; } while (0)

/* orig: cheat_db_load @0x1400070f0. Error codes are the original ones. */
int mh_cheat_db_load(MhCheatDb *db, const uint8_t *blob, size_t len)
{
    int err = 0;
    uint8_t *d = NULL;
    uint64_t size, off, need, o34, o38, o3c, o40, o44, o48, o4c, base, run;
    uint32_t ng, nt, nv, nx, nc, np, nl, h2c, nstr, i;
    uint32_t sumlines;
    uint8_t digest[32];

    if (!db) return 0;
    memset(db, 0, sizeof *db);
    mh_str_copy(db->description, sizeof db->description, "GameShark V5 original embedded in the Manager");
    d = mh_alloc(MH_CHEAT_DB_RAW_SIZE);
    if (!d) {
        db->load_error = 60000;
        return 0;
    }
    if (!blob || !cdx_decompress(blob, len, d)) LFAIL(0xea61);
    db->data = d;
    db->size = size = MH_CHEAT_DB_RAW_SIZE;

    if (memcmp(d, "MHGSF1\0\0", 8) != 0 || ch_rd16(d + 8) != 2 || ch_rd16(d + 10) != 0x74 ||
        ch_rd32(d + 0xc) > 7 || ch_rd32(d + 0x10) == 0 || ch_rd32(d + 0x14) == 0 ||
        ch_rd32(d + 0x18) == 0 || ch_rd32(d + 0x1c) == 0 || ch_rd32(d + 0x20) == 0 ||
        ch_rd32(d + 0x24) == 0)
        LFAIL(2);
    if (mh_crc32(d, 0x70) != ch_rd32(d + 0x70)) LFAIL(3);

    ng = ch_rd32(d + 0x10); nt = ch_rd32(d + 0x14); nv = ch_rd32(d + 0x18);
    nx = ch_rd32(d + 0x1c); nc = ch_rd32(d + 0x20); np = ch_rd32(d + 0x24);
    nl = ch_rd32(d + 0x28); h2c = ch_rd32(d + 0x2c);

    /* section bounds: each table starts where the previous one ends */
    if (ch_rd32(d + H_GAMES) != 0x74 || size < 0x74 || size - 0x74 < (uint64_t)ng * REC_GAME) LFAIL(4);
    o34 = ch_rd32(d + H_TITLES);
    if (0x74 + (uint64_t)ng * REC_GAME != o34 || size < o34 || size - o34 < (uint64_t)nt * REC_TITLE) LFAIL(5);
    off = o34 + (uint64_t)nt * REC_TITLE;
    o38 = ch_rd32(d + H_VARIANTS);
    if (off != o38 || size < off || size - off < (uint64_t)nv * REC_VARIANT) LFAIL(6);
    off += (uint64_t)nv * REC_VARIANT;
    o3c = ch_rd32(d + H_TEXTS);
    if (off != o3c || size < off || size - off < (uint64_t)nx * REC_TEXT) LFAIL(7);
    off += (uint64_t)nx * REC_TEXT;
    o40 = ch_rd32(d + H_CHEATS);
    if (off != o40 || size < off || size - off < (uint64_t)nc * REC_CHEAT) LFAIL(8);
    off += (uint64_t)nc * REC_CHEAT;
    o44 = ch_rd32(d + H_PROGS);
    if (off != o44 || size < o44 || size - o44 < (uint64_t)np * REC_PROG) LFAIL(9);
    off = o44 + (uint64_t)np * REC_PROG;
    o48 = ch_rd32(d + H_LINES);
    if (off != o48 || size - off < (uint64_t)nl * 6) LFAIL(10);
    off += (uint64_t)nl * 6;
    o4c = ch_rd32(d + H_STRS);
    if (off != o4c || size < o4c + 4) LFAIL(11);
    nstr = ch_rd32(d + o4c);
    need = (uint64_t)nstr * 8 + 4;
    if (nstr == 0 || need > size - o4c) LFAIL(12);
    base = o4c + need;

    /* string table: contiguous, no NUL, valid UTF-8 */
    run = 0;
    for (i = 0; i < nstr; i++) {
        const uint8_t *rec = d + o4c + 4 + (uint64_t)i * 8;
        uint32_t slen = ch_rd32(rec + 4);
        uint64_t pos = run + base;
        if (ch_rd32(rec) != run || size < pos || size - pos < slen) LFAIL(100 + (int)i);
        if (slen) {
            for (uint32_t k = 0; k < slen; k++)
                if (d[pos + k] == 0) LFAIL(100 + (int)i);
            if (!utf8_valid(d + pos, slen)) LFAIL(100 + (int)i);
        }
        run += slen;
    }
    if (run + base != size) LFAIL(900);

    db->loaded = 0;
    db->n_games = ng; db->n_titles = nt; db->n_variants = nv;
    db->n_texts = nx; db->n_cheats = nc; db->n_programs = np;

    /* games */
    for (i = 0; i < ng; i++) {
        const uint8_t *g = d + 0x74 + (uint64_t)i * REC_GAME;
        uint8_t id = g[0];
        uint32_t cnt = 0, lines = 0, j;
        const uint8_t *s;
        uint32_t sl;
        if (id == 0 || id > ng) LFAIL(1000 + (int)i);
        if (!ch_db_string_raw(db, ch_rd32(g + 4), &s, &sl) || !ch_db_string_raw(db, ch_rd32(g + 8), &s, &sl))
            LFAIL(1000 + (int)i);
        for (j = 0; j < i; j++)
            if (d[0x74 + (uint64_t)j * REC_GAME] == id) LFAIL(0x4b0 + (int)i);
        for (j = 0; j < nv; j++) {
            const uint8_t *v = d + o38 + (uint64_t)j * REC_VARIANT;
            if (v[0] == id) {
                cnt++;
                lines += ch_rd16(v + 0x14);
            }
        }
        if (cnt != ch_rd32(g + 0xc) || lines != ch_rd32(g + 0x10)) LFAIL(0x578 + (int)i);
    }

    /* titles */
    run = 0;
    for (i = 0; i < nt; i++) {
        const uint8_t *t = d + o34 + (uint64_t)i * REC_TITLE;
        uint32_t cnt, k;
        const uint8_t *s;
        uint32_t sl;
        if (ch_rd16(t + 10) != 0 || ch_rd16(t + 8) == 0 || ch_rd32(t + 4) != run) LFAIL(2000 + (int)i);
        cnt = ch_rd16(t + 8);
        if (nv - run < cnt) LFAIL(2000 + (int)i);
        if (!ch_db_string_raw(db, ch_rd32(t), &s, &sl)) LFAIL(2000 + (int)i);
        for (k = 0; k < cnt; k++) {
            if (nv <= run + k || ch_rd32(d + o38 + (uint64_t)(run + k) * REC_VARIANT + 4) != i)
                LFAIL(0x960 + (int)i);
        }
        run += cnt;
    }
    if (run != nv) LFAIL(0xaf0);

    /* variants */
    run = 0;
    sumlines = 0;
    for (i = 0; i < nv; i++) {
        const uint8_t *v = d + o38 + (uint64_t)i * REC_VARIANT;
        uint32_t cnt = ch_rd32(v + 0x10), k, j;
        if (ch_rd32(v + 4) >= nt || ch_rd16(v + 2) != 0 || ch_rd32(v + 0xc) != run ||
            nc - run < cnt || ch_rd16(v + 0x14) < cnt || ch_rd16(v + 0x14) < ch_rd16(v + 0x16))
            LFAIL(3000 + (int)i);
        for (j = 0; j < ng; j++)
            if (d[0x74 + (uint64_t)j * REC_GAME] == v[0]) break;
        if (j == ng) LFAIL(3000 + (int)i);
        for (k = 0; k < cnt; k++) {
            if (nc <= run + k || ch_rd32(d + o40 + (uint64_t)(run + k) * REC_CHEAT) != i)
                LFAIL(0x12c0 + (int)i);
        }
        run += cnt;
        sumlines += ch_rd16(v + 0x16);
    }
    if (run != nc || sumlines != h2c) LFAIL(4999);

    /* cheats */
    for (i = 0; i < nc; i++) {
        const uint8_t *c = d + o40 + (uint64_t)i * REC_CHEAT;
        uint32_t tx = ch_rd32(c + 4);
        uint8_t lc;
        if (ch_rd32(c) >= nv || tx >= nx || ch_rd16(c + 0xe) != 0 || c[0xd] > 3) LFAIL(5000 + (int)i);
        lc = c[0xc] & 0x7f;
        if (lc != 0 && lc != d[o3c + (uint64_t)tx * REC_TEXT + 8]) LFAIL(8000 + (int)i);
    }

    /* programs */
    run = 0;
    sumlines = 0;
    for (i = 0; i < np; i++) {
        const uint8_t *p = d + o44 + (uint64_t)i * REC_PROG;
        uint8_t pn = p[6];
        if (pn == 0 || p[7] > 7 || ch_rd32(p) != run || ch_rd16(p + 4) != (uint32_t)pn * 6 ||
            (o4c - o48) - run < (uint32_t)pn * 6)
            LFAIL(22000 + (int)i);
        if (!ch_v5_program(d + o48 + run, pn, NULL)) LFAIL(26000 + (int)i);
        run += (uint32_t)pn * 6;
        sumlines += pn;
    }
    if (run != o4c - o48 || sumlines != nl) LFAIL(32000);

    /* texts (the groups) */
    for (i = 0; i < nx; i++) {
        const uint8_t *x = d + o3c + (uint64_t)i * REC_TEXT;
        uint32_t prog = ch_rd32(x + 4);
        static const int fields[5] = {0, 0xc, 0x10, 0x14, 0x18};
        const uint8_t *s;
        uint32_t sl;
        if (ch_rd16(x + 10) != 0 || x[9] > 7 || x[8] == 0 || prog >= np) LFAIL(33000 + (int)i);
        for (int f = 0; f < 5; f++)
            if (!ch_db_string_raw(db, ch_rd32(x + fields[f]), &s, &sl)) LFAIL(33000 + (int)i);
        if (d[o44 + (uint64_t)prog * REC_PROG + 6] != x[8]) LFAIL(36000 + (int)i);
    }

    /* SHA-256 then CRC-32 of the whole image */
    mh_sha256(d, size, digest);
    memcpy(db->sha256, digest, 32);
    if (memcmp(digest, k_expected_sha256, 32) != 0) {
        for (i = 0; i < 32; i++)
            if (digest[i] != k_expected_sha256[i]) LFAIL(50000 + (int)i * 0x100 + digest[i]);
    }
    db->crc = mh_crc32(d, size);
    if (db->crc != MH_CHEAT_DB_CRC) LFAIL(59000);

    db->loaded = 1;
    db->bank = ch_bank_build(db);
    atomic_store(&g_active_db, db);
    return 1;

fail:
    clear_active_if(db);
    mh_free(d);
    memset(db, 0, sizeof *db);
    db->load_error = err;
    return 0;
}

/* orig: cheat_db_free @0x140007080 */
void mh_cheat_db_free(MhCheatDb *db)
{
    if (!db) return;
    clear_active_if(db);
    mh_free(db->data);
    ch_bank_free(db->bank);
    memset(db, 0, sizeof *db);
}

/* orig: cheat_db_last_error @0x1400070e0 */
int mh_cheat_db_last_error(const MhCheatDb *db) { return db ? db->load_error : 0; }

/* ---- queries -------------------------------------------------------------------------------- */
static inline const uint8_t *rec(const MhCheatDb *db, int hdr, uint32_t i, int sz)
{
    return db->data + ch_rd32(db->data + hdr) + (uint64_t)i * sz;
}
static inline int db_ok(const MhCheatDb *db) { return db && db->data; }

/* orig: cheat_db_title_w @0x140008bc0 */
int mh_cheat_db_title(const MhCheatDb *db, uint32_t title, char *out, size_t n)
{
    if (!db_ok(db) || title >= ch_rd32(db->data + 0x14)) return 0;
    return mh_cheat_db_string(db, ch_rd32(rec(db, H_TITLES, title, REC_TITLE)), out, n);
}

/* orig: cheat_db_title_variant_count @0x140008b90 */
unsigned mh_cheat_db_title_variant_count(const MhCheatDb *db, uint32_t title)
{
    if (!db_ok(db) || title >= ch_rd32(db->data + 0x14)) return 0;
    return ch_rd16(rec(db, H_TITLES, title, REC_TITLE) + 8);
}

/* orig: cheat_db_title_variant_at @0x140008b50 */
int mh_cheat_db_title_variant_at(const MhCheatDb *db, uint32_t title, unsigned k, MhCheatVariantInfo *out)
{
    const uint8_t *t;
    if (!db_ok(db) || title >= ch_rd32(db->data + 0x14)) return 0;
    t = rec(db, H_TITLES, title, REC_TITLE);
    if (ch_rd16(t + 8) <= k) return 0;
    return mh_cheat_db_variant_info(db, ch_rd32(t + 4) + k, out);
}

/* orig: cheat_db_variant_info @0x140008e60 */
int mh_cheat_db_variant_info(const MhCheatDb *db, uint32_t variant, MhCheatVariantInfo *out)
{
    const uint8_t *v, *d;
    uint32_t ng, i;
    if (!db_ok(db) || variant >= ch_rd32(db->data + 0x18) || !out) return 0;
    d = db->data;
    v = rec(db, H_VARIANTS, variant, REC_VARIANT);
    ng = ch_rd32(d + 0x10);
    for (i = 0; i < ng; i++) {
        const uint8_t *g = d + ch_rd32(d + H_GAMES) + (uint64_t)i * REC_GAME;
        if (g[0] != v[0]) continue;
        memset(out, 0, sizeof *out);
        out->variant = variant;
        out->title = ch_rd32(v + 4);
        out->aux = ch_rd32(v + 8);
        out->first_entry = ch_rd32(v + 0xc);
        out->entry_count = ch_rd32(v + 0x10);
        out->stat_a = ch_rd16(v + 0x14);
        out->stat_b = ch_rd16(v + 0x16);
        out->game = v[0];
        out->game_aux = v[1];
        mh_cheat_db_string(db, ch_rd32(g + 8), out->game_code, sizeof out->game_code);
        mh_cheat_db_string(db, ch_rd32(g + 4), out->game_name, sizeof out->game_name);
        return 1;
    }
    return 0;
}

/* Resolves (variant, entry) to the cheat, text and program records. */
typedef struct {
    const uint8_t *variant, *cheat, *text, *prog, *lines;
    uint32_t global;
} Resolved;

static int resolve(const MhCheatDb *db, uint32_t variant, uint32_t entry, Resolved *r)
{
    const uint8_t *d = db->data;
    uint32_t g, tx, pi;
    r->variant = rec(db, H_VARIANTS, variant, REC_VARIANT);
    if (entry >= ch_rd32(r->variant + 0x10)) return 0;
    g = entry + ch_rd32(r->variant + 0xc);
    if (g >= ch_rd32(d + 0x20)) return 0;
    r->cheat = rec(db, H_CHEATS, g, REC_CHEAT);
    tx = ch_rd32(r->cheat + 4);
    if (ch_rd32(r->cheat) != variant || tx >= ch_rd32(d + 0x1c)) return 0;
    r->text = rec(db, H_TEXTS, tx, REC_TEXT);
    pi = ch_rd32(r->text + 4);
    if (pi >= ch_rd32(d + 0x24)) return 0;
    r->prog = rec(db, H_PROGS, pi, REC_PROG);
    if (r->text[8] != r->prog[6] || r->prog[6] == 0) return 0;
    r->lines = d + ch_rd32(d + H_LINES) + ch_rd32(r->prog);
    r->global = g;
    return 1;
}

/* orig: cheat_db_entry_info @0x140006490 */
int mh_cheat_db_entry_info(const MhCheatDb *db, uint32_t variant, uint32_t entry, MhCheatEntryInfo *out)
{
    Resolved r;
    uint8_t fl, f;
    if (!db_ok(db) || variant >= ch_rd32(db->data + 0x18) || !out) return 0;
    if (!resolve(db, variant, entry, &r)) return 0;
    if (!ch_lines_structure_ok(r.lines, r.prog[6])) return 0;
    memset(out, 0, sizeof *out);
    out->index = r.global;
    out->text = ch_rd32(r.cheat + 4);
    out->variant = variant;
    out->id = ch_rd32(r.cheat + 8);
    out->nlines = r.prog[6];
    out->game = r.variant[0];
    fl = r.cheat[0xd];
    f = 0x08;
    if (fl & 1) f |= 0x10;
    if (fl & 2) f |= 0x03;
    out->flags = f;
    mh_cheat_db_string(db, ch_rd32(r.text), out->name, sizeof out->name);
    return 1;
}

/* orig: cheat_db_entry_text @0x140006680 */
int mh_cheat_db_entry_text(const MhCheatDb *db, uint32_t variant, uint32_t entry, int kind, char *out, size_t n)
{
    Resolved r;
    uint32_t idx;
    if (out && n) out[0] = 0;
    if (!db_ok(db) || variant >= ch_rd32(db->data + 0x18)) return 0;
    {
        const uint8_t *v = rec(db, H_VARIANTS, variant, REC_VARIANT);
        uint32_t g, tx;
        if (entry >= ch_rd32(v + 0x10)) return 0;
        g = entry + ch_rd32(v + 0xc);
        if (g >= ch_rd32(db->data + 0x20)) return 0;
        tx = ch_rd32(rec(db, H_CHEATS, g, REC_CHEAT) + 4);
        if (tx >= ch_rd32(db->data + 0x1c)) return 0;
        r.text = rec(db, H_TEXTS, tx, REC_TEXT);
    }
    switch (kind) {
    case 0: idx = ch_rd32(r.text); break;
    case 1: idx = ch_rd32(r.text + 0xc); break;
    case 2: idx = ch_rd32(r.text + 0x10); break;
    case 3: idx = ch_rd32(r.text + 0x14); break;
    case 4: idx = ch_rd32(r.text + 0x18); break;
    default: return 0;
    }
    return mh_cheat_db_string(db, idx, out, n);
}

/* orig: cheat_entry_clear @0x140008f90 */
void mh_cheat_entry_clear(MhCheatEntry *e)
{
    if (!e) return;
    mh_free(e->lines);
    memset(e, 0, sizeof *e);
    e->variant = 0xFFFFFFFFu;
}

/* orig: cheat_db_copy_entry @0x140006240 */
int mh_cheat_db_copy_entry(const MhCheatDb *db, uint32_t variant, uint32_t entry, MhCheatEntry *out)
{
    MhCheatEntryInfo info;
    Resolved r;
    size_t bytes;
    if (!out) return 0;
    if (!mh_cheat_db_entry_info(db, variant, entry, &info)) return 0;
    if (!resolve(db, variant, entry, &r)) return 0;
    if (!ch_lines_structure_ok(r.lines, r.prog[6])) return 0;
    memset(out, 0, sizeof *out);
    mh_normalize_title(info.name, out->name, MH_CHEAT_NAME_MAX);
    if (db->crc == MH_CHEAT_DB_CRC) {
        const ChBankGroup *g = ch_bank_group(db->bank, variant, entry);
        if (!g) return 0;
        mh_str_copy(out->name, MH_CHEAT_NAME_MAX, g->name);
    }
    out->flags = info.flags;
    out->game = info.game;
    out->nlines = info.nlines;
    out->variant = variant;
    out->id = info.id;
    bytes = (size_t)out->nlines * MH_CHEAT_LINE_SIZE;
    out->lines = mh_alloc(bytes);
    if (!out->lines) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    memcpy(out->lines, r.lines, bytes);
    return 1;
}

/* ---- title search ---------------------------------------------------------------------------- */
static void upper_ascii(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s -= 32;
}

/* Upper-cased A-Z 0-9 only, at most 159 characters. */
static void alnum_key(const char *src, char *dst)
{
    size_t o = 0;
    for (; *src && o < 159; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) dst[o++] = c;
    }
    dst[o] = 0;
}

/* Case-insensitive "hay contains needle" (empty needle never calls this). */
static int contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t k = 0;
        while (k < nl && hay[k]) {
            char a = hay[k], b = needle[k];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) break;
            k++;
        }
        if (k == nl) return 1;
    }
    return 0;
}

static int title_norm(const MhCheatDb *db, uint32_t title, char *out160)
{
    const uint8_t *s;
    uint32_t l;
    char tmp[256];
    if (!ch_db_string_raw(db, ch_rd32(rec(db, H_TITLES, title, REC_TITLE)), &s, &l)) return 0;
    /* orig: MultiByteToWideChar capped at 255 wide characters, then normalised to 159 */
    {
        size_t cut = l < 255 ? l : 255;
        memcpy(tmp, s, cut);
        tmp[cut] = 0;
    }
    mh_normalize_title(tmp, out160, 160);
    return 1;
}

/* orig: cheat_db_title_matches @0x1400082c0 */
int mh_cheat_db_title_matches(const MhCheatDb *db, uint32_t title, const char *filter)
{
    char tn[160], tk[160], fk[160];
    if (!db_ok(db) || title >= ch_rd32(db->data + 0x14)) return 0;
    if (!filter || !*filter) return 1;
    if (!title_norm(db, title, tn)) return 0;
    if (*tn && contains_ci(tn, filter)) return 1;       /* filter inside the title */
    if (!*tn) return 1;
    if (contains_ci(filter, tn)) return 1;              /* title inside the filter */
    alnum_key(tn, tk);
    alnum_key(filter, fk);
    if (!*fk) return 1;
    if (*tk && contains_ci(tk, fk)) return 1;
    if (!*tk) return 1;
    return contains_ci(fk, tk);
}

/* orig: cheat_db_find_title_normalized @0x140006830 */
int mh_cheat_db_find_title_normalized(const MhCheatDb *db, const char *name, uint32_t *title)
{
    char key[160], cut[160], cutkey[160], tn[160], tk[160];
    int have_cut = 0, found = 0;
    uint32_t i, first = 0, nt;
    if (title) *title = 0xFFFFFFFFu;
    if (!db || !name || !*name) return 0;
    alnum_key(name, key);
    mh_str_copy(cut, sizeof cut, name);
    {
        char *p = strstr(cut, " (");
        if (p && p != cut) {
            *p = 0;
            have_cut = 1;
        }
    }
    cutkey[0] = 0;
    if (have_cut) alnum_key(cut, cutkey);
    if (!db_ok(db)) return 0;
    nt = db->n_titles;
    for (i = 0; i < nt && i < ch_rd32(db->data + 0x14); i++) {
        if (!title_norm(db, i, tn)) continue;
        alnum_key(tn, tk);
        if (mh_str_icmp(tn, name) == 0 || mh_str_icmp(tk, key) == 0 ||
            (have_cut && mh_str_icmp(tk, cutkey) == 0)) {
            if (found && i != first) return 0; /* ambiguous */
            found = 1;
            first = i;
        }
    }
    if (found && title) *title = first;
    return found;
}

/* orig: cheat_db_find_title_for_disc @0x140006770 */
int mh_cheat_db_find_title_for_disc(const MhCheatDb *db, const char *serial, uint32_t *title)
{
    uint32_t lo = 0, hi = (uint32_t)N_BINDINGS;
    if (title) *title = 0xFFFFFFFFu;
    if (!db || !db->loaded || !serial || !*serial || db->crc != MH_CHEAT_DB_CRC) return 0;
    while (lo < hi) {
        uint32_t mid = ((hi - lo) >> 1) + lo;
        int c = mh_str_icmp(serial, k_bindings[mid].serial);
        if (c == 0) {
            if (title) *title = k_bindings[mid].title;
            return 1;
        }
        if (c < 0) {
            if (mid <= lo) return 0;
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return 0;
}

/* orig: filter_match @0x14001b0c0 */
int mh_cheat_filter_match(const char *text, const char *filter)
{
    char t[128], f[128];
    mh_str_copy(t, sizeof t, text);
    mh_str_copy(f, sizeof f, filter);
    upper_ascii(t);
    upper_ascii(f);
    if (!*f) return 1;
    return strstr(t, f) != NULL;
}
