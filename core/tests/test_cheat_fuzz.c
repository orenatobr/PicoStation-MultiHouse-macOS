/* cheat module: seeded mutation fuzzing of every parser that takes untrusted bytes: the CDXLZ1/LZ4 decoder and
 * the database structure checks, PSTCHT1 validate/deserialize (v4, v5, v6), the code text parser and the TXT/CHT
 * importer (UTF-8 and UTF-16LE). Deterministic (fixed seeds), no crash or hang under ASan/UBSan is the
 * property; where a parser accepts the input the result must also be self-consistent. `make test-cheat`.
 * MH_FUZZ_SCALE=<n> multiplies the iteration counts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/cheat/cheat_internal.h"
#include "mh/base.h"
#include "mh/cheat.h"
#include "mh/path.h"

static int g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            abort();                                                              \
        }                                                                         \
    } while (0)

#define CDX_PATH "resources/embedded/cdx_cheats.bin"
static uint8_t *g_blob;
static uint64_t g_blob_len;
static MhCheatDb g_db;
static char err[512];
static int g_scale = 1;
static unsigned g_acc[8], g_rej[8]; /* per blob version: accepted / rejected by deserialize */

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}
static uint32_t rn(uint32_t n) { return n ? (uint32_t)(rnd() % n) : 0; }

static const uint32_t interesting[] = {0, 1, 2, 3, 4, 6, 7, 8, 0x3f, 0x40, 0x41, 0x60, 0xff, 0x100, 0x101,
                                       0x1ff, 0x200, 0x2800, 0x2801, 0x7fff, 0x8000, 0xffff, 0x10000,
                                       0x7fffffff, 0x80000000u, 0xfffffffeu, 0xffffffffu};
static uint32_t intr(void) { return interesting[rn(sizeof interesting / sizeof *interesting)]; }

/* an exact-size heap copy so ASan sees every over-read */
static uint8_t *xdup(const uint8_t *s, size_t n)
{
    uint8_t *p = malloc(n ? n : 1);
    CHECK(p);
    if (n) memcpy(p, s, n);
    return p;
}

static uint8_t *slurp(const char *path, uint64_t *n)
{
    uint8_t *d = NULL;
    return mh_read_entire_file(path, &d, n) ? d : NULL;
}

static void mutate(uint8_t *b, size_t n)
{
    unsigned k = 1 + rn(4);
    while (k-- && n) {
        size_t at = rn((uint32_t)n);
        switch (rn(6)) {
        case 0: b[at] ^= (uint8_t)(1u << rn(8)); break;
        case 1: b[at] = (uint8_t)rnd(); break;
        case 2: if (n >= 4) { at = rn((uint32_t)n - 3); ch_wr32(b + at, intr()); } break;
        case 3: if (n >= 2) { at = rn((uint32_t)n - 1); ch_wr16(b + at, intr()); } break;
        case 4: at = rn(n < 0x40 ? (uint32_t)n : 0x40); b[at] = (uint8_t)intr(); break; /* header bias */
        default: if (n >= 0x40) { at = 0x10 + rn(0x2c); b[at] = (uint8_t)rnd(); } break;
        }
    }
}

static void fix_crc(uint8_t *b, size_t n)
{
    if (n >= 0x40) {
        uint32_t c = mh_crc32_begin();
        static const uint8_t z[4];
        c = mh_crc32_update(c, b, 0x3c);
        c = mh_crc32_update(c, z, 4);
        if (n > 0x40) c = mh_crc32_update(c, b + 0x40, n - 0x40);
        ch_wr32(b + 0x3c, mh_crc32_end(c));
    }
}

/* A successful load makes that database the active one and freeing it leaves none (as in the original), so
 * the main database is loaded again after tests that load others. */
static void reload_main(void)
{
    mh_cheat_db_free(&g_db);
    CHECK(mh_cheat_db_load(&g_db, g_blob, (size_t)g_blob_len));
    CHECK(mh_cheat_db_active() == &g_db && ch_bank_active() == g_db.bank);
}

/* ---- 1. LZ4 / CDXLZ1 ---------------------------------------------------------------------------- */
static void fuzz_lz4(void)
{
    MhCheatDb t;
    unsigned it, iters = 6000u * (unsigned)g_scale;
    /* a: pure random streams behind a correct header: nearly all fail inside the decoder */
    for (it = 0; it < iters; it++) {
        size_t n = 13 + rn(200), i;
        uint8_t *b = malloc(n);
        memcpy(b, "CDXLZ1\0\0", 8);
        ch_wr32(b + 8, MH_CHEAT_DB_RAW_SIZE);
        for (i = 12; i < n; i++) b[i] = (uint8_t)(rn(3) ? rnd() : (rn(2) ? 0xff : 0xf0));
        if (rn(2)) b[12] |= 0xf0;
        CHECK(!mh_cheat_db_load(&t, b, n));
        CHECK(t.data == NULL && t.load_error != 0);
        free(b);
    }
    /* b: mutate / truncate / extend the real container; the exact-size copy exposes over-reads */
    for (it = 0; it < 150u * (unsigned)g_scale; it++) {
        size_t n = (size_t)g_blob_len;
        uint8_t *b;
        switch (rn(4)) {
        case 0: n = rn(40); break;                          /* tiny */
        case 1: n = (size_t)g_blob_len - 1 - rn(2000); break; /* truncated */
        case 2: n = 12 + rn(5000); break;                   /* prefix of the stream */
        default: break;
        }
        b = xdup(g_blob, n);
        if (rn(2)) mutate(b, n < 3000 ? n : 3000);
        if (n > 12 && rn(3) == 0) b[n - 1 - rn(n < 8 ? (uint32_t)n - 1 : 8)] ^= (uint8_t)(1u << rn(8));
        if (n != g_blob_len || memcmp(b, g_blob, n) != 0) CHECK(!mh_cheat_db_load(&t, b, n) || mh_cheat_db_last_error(&t) == 0);
        if (t.loaded) mh_cheat_db_free(&t);
        free(b);
    }
    reload_main();
}

/* literal-only LZ4 stream of a raw image: the shortest valid CDXLZ1 container */
static uint8_t *cdx_literals(const uint8_t *raw, size_t *out_n)
{
    size_t rawn = MH_CHEAT_DB_RAW_SIZE, ext = (rawn - 15) / 255 + 1, n = 12 + 1 + ext + rawn, o = 12, i;
    uint8_t *b = malloc(n);
    memcpy(b, "CDXLZ1\0\0", 8);
    ch_wr32(b + 8, MH_CHEAT_DB_RAW_SIZE);
    b[o++] = 0xf0;
    for (i = 0; i + 1 < ext; i++) b[o++] = 255;
    b[o++] = (uint8_t)((rawn - 15) - 255 * (ext - 1));
    memcpy(b + o, raw, rawn);
    *out_n = o + rawn;
    return b;
}

/* ---- 2. database structure checks: a valid container around a mutated image ----------------------- */
static void fuzz_db_image(void)
{
    uint8_t *raw = xdup(g_db.data, MH_CHEAT_DB_RAW_SIZE);
    size_t n;
    uint8_t *c = cdx_literals(raw, &n);
    MhCheatDb t;
    unsigned it, iters = 700u * (unsigned)g_scale;
    uint32_t strs = ch_rd32(raw + 0x4c);
    /* the unmodified image through the literal container loads: proves the harness */
    CHECK(mh_cheat_db_load(&t, c, n));
    mh_cheat_db_free(&t);
    for (it = 0; it < iters; it++) {
        uint8_t *d = c + n - MH_CHEAT_DB_RAW_SIZE;
        unsigned k = 1 + rn(3);
        uint8_t saved[64];
        size_t pos[3] = {0, 0, 0}, i;
        k = k > 3 ? 3 : k;
        for (i = 0; i < k; i++) {
            size_t at;
            switch (rn(4)) {
            case 0: at = rn(0x74); break;                       /* header */
            case 1: at = 0x74 + rn(strs - 0x74 < 400000 ? strs - 0x74 : 400000); break; /* tables, front */
            case 2: at = 0x74 + rn(strs - 0x74); break;         /* any table */
            default: at = strs + rn(60000); break;              /* string table / start */
            }
            pos[i] = at;
        }
        for (i = 0; i < k; i++) {
            saved[i] = d[pos[i]];
            if (rn(3) == 0) d[pos[i]] = (uint8_t)intr();
            else d[pos[i]] ^= (uint8_t)(1u << rn(8));
        }
        {
            uint8_t hc[4];
            memcpy(hc, d + 0x70, 4);
            if (rn(2)) ch_wr32(d + 0x70, mh_crc32(d, 0x70)); /* reach the checks behind the header CRC */
            memcpy(saved + 60, hc, 4);
        }
        {
            int changed = 0;
            for (i = 0; i < k; i++) changed |= d[pos[i]] != saved[i];
            if (mh_cheat_db_load(&t, c, n)) { /* only an image equal to the original may load */
                CHECK(!changed || memcmp(d, raw, MH_CHEAT_DB_RAW_SIZE) == 0);
                mh_cheat_db_free(&t);
            } else {
                CHECK(mh_cheat_db_last_error(&t) != 0 && t.data == NULL);
            }
        }
        memcpy(d + 0x70, saved + 60, 4);
        for (i = k; i-- > 0;) d[pos[i]] = saved[i];
    }
    free(c);
    free(raw);
    reload_main();
}

/* ---- 3. PSTCHT1 ---------------------------------------------------------------------------------- */
#define MAXSEEDS 16
static uint8_t *g_seed[MAXSEEDS];
static uint32_t g_seed_len[MAXSEEDS];
static int g_nseeds;

static void add_seed(const uint8_t *b, uint32_t n)
{
    CHECK(g_nseeds < MAXSEEDS);
    g_seed[g_nseeds] = xdup(b, n);
    g_seed_len[g_nseeds++] = n;
}

static void make_seeds(void)
{
    static const uint8_t W8[6] = {0x30, 0x80, 0x12, 0x34, 0x00, 0x7f};
    static const uint8_t COND[12] = {0x70, 0x80, 0x12, 0x34, 0x00, 0x01, 0x30, 0x80, 0x12, 0x38, 0x00, 0x09};
    uint32_t titles[3] = {0, 301, 787}, t;
    for (t = 0; t < 3; t++) {
        MhCheatProfile *p = NULL;
        uint8_t *bl;
        uint32_t len;
        unsigned i;
        CHECK(mh_cheat_profile_build_exact_catalog(&g_db, titles[t], "SLUS-00001", &p, err, sizeof err));
        for (i = 0; i < p->count; i += 3) p->entries[i].flags |= MH_CHEAT_F_ENABLED;
        if (t == 1) {
            CHECK(mh_cheat_profile_add_custom(p, "MY CODE", W8, 1, 0));
            CHECK(mh_cheat_profile_add_custom_disabled(p, "COND", COND, 2, 0));
        }
        CHECK(mh_cheat_profile_serialize(p, (uint8_t)(1 + (t & 1)), &bl, &len, err, sizeof err) && bl);
        CHECK(ch_rd16(bl + 8) == 6);
        add_seed(bl, len);
        if (t == 2) {                                    /* the same profile as a v5 references-only blob */
            uint8_t v5[0x40 + 4 * 40];
            unsigned n = p->count < 40 ? p->count : 40, k;
            memset(v5, 0, sizeof v5);
            memcpy(v5, "PSTCHT1", 7);
            ch_wr16(v5 + 8, 5);
            ch_wr16(v5 + 10, 0x40);
            ch_wr32(v5 + 0x10, n);
            ch_wr32(v5 + 0x1c, 4);
            ch_wr32(v5 + 0x20, 0x40);
            ch_wr32(v5 + 0x24, 0x40 + 4 * n);
            ch_wr32(v5 + 0x28, 0x40 + 4 * n);
            ch_wr32(v5 + 0x2c, titles[t]);
            ch_wr32(v5 + 0x30, titles[t]);
            ch_wr32(v5 + 0x34, MH_CHEAT_DB_CRC);
            v5[0x38] = 1;
            v5[0x39] = 1;
            {
                uint32_t en = 0, lines = 0;
                for (k = 0; k < n; k++) {
                    const ChBankGroup *g = ch_bank_group(g_db.bank, titles[t], k);
                    ch_wr16(v5 + 0x40 + 4 * k, k);
                    v5[0x42 + 4 * k] = (uint8_t)(0x08 | (k % 3 == 0));
                    en += k % 3 == 0;
                    lines += g->nlines;
                }
                ch_wr32(v5 + 0x14, en);
                ch_wr32(v5 + 0x18, lines);
            }
            fix_crc(v5, 0x40 + 4 * n);
            CHECK(mh_cheat_blob_validate(v5, 0x40 + 4 * n, err, sizeof err));
            add_seed(v5, 0x40 + 4 * n);
        }
        free(bl);
        mh_cheat_profile_free(p);
    }
    {   /* version 4: custom only, and an audited entry that differs from the bank */
        MhCheatProfile *p = mh_cheat_profile_new(), *q = NULL;
        uint8_t *bl;
        uint32_t len;
        CHECK(mh_cheat_profile_add_custom(p, "INFINITE LIVES", W8, 1, 0));
        CHECK(mh_cheat_profile_add_custom_disabled(p, "COND WRITE", COND, 2, 0x40));
        CHECK(mh_cheat_profile_serialize(p, 1, &bl, &len, err, sizeof err) && ch_rd16(bl + 8) == 4);
        add_seed(bl, len);
        free(bl);
        mh_cheat_profile_free(p);
        CHECK(mh_cheat_profile_build_exact_catalog(&g_db, 5, "SLUS-00001", &q, err, sizeof err));
        q->entries[0].lines[5] ^= 1; /* no longer the bank group */
        CHECK(mh_cheat_profile_needs_wire(q));
        CHECK(mh_cheat_profile_serialize(q, 2, &bl, &len, err, sizeof err) && ch_rd16(bl + 8) == 4);
        add_seed(bl, len);
        free(bl);
        mh_cheat_profile_free(q);
    }
}

static void check_consistent(const uint8_t *b, uint32_t len)
{
    MhCheatProfile *p = NULL;
    int v = mh_cheat_blob_validate(b, len, err, sizeof err);
    int ok = mh_cheat_profile_deserialize(b, len, &p, err, sizeof err);
    (void)mh_cheat_blob_scope(b, len);
    (void)mh_cheat_disc_bank_valid(b, len);
    {
        unsigned ver = len >= 0x40 ? ch_rd16(b + 8) : 0;
        (ok ? g_acc : g_rej)[ver < 7 ? ver : 7]++;
    }
    if (ok) CHECK(v); /* deserialize validates first */
    if (!ok) CHECK(p == NULL);
    if (ok && p) {
        uint8_t *bl = NULL;
        uint32_t l2 = 0;
        CHECK(mh_cheat_profile_validate(p, err, sizeof err));
        CHECK(p->count >= 1 && p->count <= 256);
        if (mh_cheat_profile_serialize(p, 1, &bl, &l2, err, sizeof err)) {
            CHECK(bl && l2 >= 0x40 && l2 <= MH_CHEAT_BLOB_MAX);
            CHECK(mh_cheat_blob_validate(bl, l2, err, sizeof err));
            CHECK(mh_cheat_blob_scope(bl, l2) == 1);
            free(bl);
        }
        mh_cheat_profile_free(p);
    }
}

static void fuzz_blob(void)
{
    unsigned it, iters = 60000u * (unsigned)g_scale;
    int i;
    for (i = 0; i < g_nseeds; i++) check_consistent(g_seed[i], g_seed_len[i]);
    for (it = 0; it < iters; it++) {
        int s = (int)rn((uint32_t)g_nseeds);
        size_t n = g_seed_len[s];
        uint8_t *b;
        switch (rn(8)) {
        case 0: n = rn((uint32_t)n + 1); break;                          /* truncated */
        case 1: n += rn(40); break;                                       /* extended with garbage */
        default: break;
        }
        b = malloc(n ? n : 1);
        if (n <= g_seed_len[s]) memcpy(b, g_seed[s], n);
        else { memcpy(b, g_seed[s], g_seed_len[s]); for (size_t j = g_seed_len[s]; j < n; j++) b[j] = (uint8_t)rnd(); }
        mutate(b, n);
        if (rn(3)) {                                                      /* make the common fields consistent */
            if (rn(2) && n >= 0x40) ch_wr32(b + 0x28, (uint32_t)n);
            fix_crc(b, n);
        }
        check_consistent(b, (uint32_t)n);
        free(b);
    }
}

/* ---- 4. code text and importer ----------------------------------------------------------------------- */
static const char *text_seeds[] = {
    "MHDialect=V5ORIG_BANK_V01\r\n[INFINITE LIVES]\r\n30801234 007F\r\n\r\n[GOD]\r\n70801234 0001\r\n30801238 0009\r\n",
    "MHDialect=GAMESHARK_CLASSIC_V1\n[SLIDE]\n50000204 0001\n80100000 0001\n[COND]\nD0100000 0001\n30100004 0009\n",
    "# comment\n; other\nMHDialect=GAMESHARK_CLASSIC_V1\nMONEY 80100000 7FFF\nHEALTH - 30100010 0064\n30100011 0001 ; x\n",
    "MHDialect=V5ORIG_BANK_V01\n\n\n30 80 12 34 00 7f // c\n12345678 1234 \\\n[A]\n[B]\n9080123400 01\n",
    "[NAME]\n30801234 007F\n",
    "MHDialect=V5ORIG_BANK_V01\nJust a title\n30801234 007F\nAnother Title\n80801236 0001\n#type=cheat\n",
};

static void write_file(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    CHECK(f);
    if (n) CHECK(fwrite(d, 1, n, f) == n);
    fclose(f);
}

static void fuzz_text(const char *dir)
{
    char path[512];
    unsigned it, iters = 14000u * (unsigned)g_scale;
    static const char *dialects[] = {NULL, "MHDialect=V5ORIG_BANK_V01", "MHDialect=GAMESHARK_CLASSIC_V1", "bogus",
                                     "mhdialect=v5orig_bank_v01"};
    snprintf(path, sizeof path, "%s/f.txt", dir);
    for (it = 0; it < iters; it++) {
        const char *s = text_seeds[rn(sizeof text_seeds / sizeof *text_seeds)];
        size_t n = strlen(s), i, cap = n * 2 + 64;
        uint8_t *b = malloc(cap), *f;
        MhCheatProfile *p = mh_cheat_profile_new();
        int imp = -1;
        memcpy(b, s, n);
        for (i = 0, f = b; i < 1 + rn(6); i++) {
            size_t at = rn((uint32_t)n);
            switch (rn(7)) {
            case 0: b[at] = (uint8_t)rnd(); break;
            case 1: b[at] = "0123456789ABCDEF \t\r\n[]=-:;#/\\"[rn(30)]; break;
            case 2: if (n + 1 < cap - 4) { memmove(b + at + 1, b + at, n - at); b[at] = b[rn((uint32_t)n)]; n++; } break;
            case 3: if (n > 1) { memmove(b + at, b + at + 1, n - at - 1); n--; } break;
            case 4: b[at] ^= (uint8_t)(1u << rn(8)); break;
            case 5: { /* duplicate a chunk (long lines, many entries) */
                size_t m = rn((uint32_t)(n - at)), k;
                if (m && n + m < cap - 4) { memmove(b + at + m, b + at, n - at); n += m; for (k = 0; k < m; k++) (void)k; }
                break; }
            default: if (rn(4) == 0) { b[at] = 0xff; } break;
            }
        }
        f = b;
        if (rn(4) == 0) { /* UTF-16LE with BOM */
            uint8_t *u = malloc(n * 2 + 2); /* n <= cap */
            size_t j;
            u[0] = 0xff; u[1] = 0xfe;
            for (j = 0; j < n; j++) { u[2 + j * 2] = b[j]; u[3 + j * 2] = (uint8_t)(rn(40) == 0 ? rnd() : 0); }
            write_file(path, u, n * 2 + 2);
            free(u);
        } else if (rn(5) == 0) { /* UTF-8 BOM */
            uint8_t *u = malloc(n + 3);
            u[0] = 0xef; u[1] = 0xbb; u[2] = 0xbf;
            memcpy(u + 3, f, n);
            write_file(path, u, n + 3);
            free(u);
        } else {
            write_file(path, f, n);
        }
        if (rn(3) == 0) CHECK(mh_cheat_profile_add_custom(p, "BASE", (const uint8_t[]){0x30, 0x80, 0x12, 0x34, 0, 1}, 1, 0));
        {
            int r = mh_cheat_profile_import_text_dialect(p, path, dialects[rn(5)], &imp, err, sizeof err);
            if (r) {
                char e2[256];
                CHECK(imp >= 1 && p->count >= 1);
                CHECK(mh_cheat_profile_validate(p, e2, sizeof e2));
            } else {
                CHECK(imp == 0 && err[0]);
            }
            mh_cheat_profile_free(p);
        }
        /* the code parsers directly */
        {
            uint8_t *lines = NULL, one[6];
            uint16_t nl = 0;
            char *z = malloc(n + 1);
            memcpy(z, b, n);
            z[n] = 0;
            if (mh_cheat_parse_codes(z, &lines, &nl, err, sizeof err)) {
                CHECK(lines && nl >= 1 && nl <= 255 && ch_lines_structure_ok(lines, nl));
                free(lines);
            } else {
                CHECK(lines == NULL && nl == 0 && err[0]);
            }
            (void)mh_cheat_parse_code_line(z, n, one);
            free(z);
        }
        free(b);
    }
    unlink(path);
}

/* giant inputs: lines over 511 units, files over 4 MiB, many blocks, many lines */
static void hostile_files(const char *dir)
{
    char path[512];
    MhCheatProfile *p = mh_cheat_profile_new();
    size_t n;
    uint8_t *b;
    int imp;
    snprintf(path, sizeof path, "%s/g.txt", dir);
    /* 512 character line */
    n = 40 + 512 + 2;
    b = malloc(n + 1);
    memset(b, 'A', n);
    memcpy(b, "MHDialect=V5ORIG_BANK_V01\n", 26);
    b[26 + 512] = '\n';
    write_file(path, b, 26 + 513);
    CHECK(!mh_cheat_profile_import_text(p, path, &imp, err, sizeof err));
    CHECK(strcmp(err, "A importacao foi cancelada: existe uma linha maior que 511 caracteres.") == 0);
    free(b);
    /* just over 4 MiB, and exactly 4 MiB of comments */
    b = malloc(0x400001);
    memset(b, '#', 0x400001);
    write_file(path, b, 0x400001);
    CHECK(!mh_cheat_profile_import_text(p, path, &imp, err, sizeof err));
    CHECK(strcmp(err, "Nao foi possivel ler o TXT/CHT.") == 0);
    for (size_t i = 0; i < 0x400000; i += 100) b[i] = '\n';
    write_file(path, b, 0x400000);
    CHECK(!mh_cheat_profile_import_text(p, path, &imp, err, sizeof err));
    CHECK(strcmp(err, "O arquivo nao informa MHDialect.") == 0);
    /* a huge number of code lines under one header: the 255 line limit must stop it */
    {
        size_t cap = 0x3f0000, o = 0;
        free(b);
        b = malloc(cap + 64);
        o += (size_t)snprintf((char *)b, 64, "MHDialect=V5ORIG_BANK_V01\n[X]\n");
        while (o + 16 < cap) o += (size_t)snprintf((char *)b + o, 16, "30801234 007F\n");
        write_file(path, b, o);
        CHECK(!mh_cheat_profile_import_text(p, path, &imp, err, sizeof err));
        CHECK(strcmp(err, "Linha de cheat invalida. Use XXXXXXXX XXXX.") == 0);
        /* thousands of one line blocks */
        o = (size_t)snprintf((char *)b, 64, "MHDialect=V5ORIG_BANK_V01\n");
        while (o + 40 < cap) o += (size_t)snprintf((char *)b + o, 40, "[B%u]\n30801234 007F\n", (unsigned)(o & 0xffff));
        write_file(path, b, o);
        CHECK(!mh_cheat_profile_import_text(p, path, &imp, err, sizeof err));
        CHECK(p->count == 0);
    }
    free(b);
    unlink(path);
    mh_cheat_profile_free(p);
}

int main(void)
{
    char tmpl[] = "/tmp/mh_cheat_fuzz_XXXXXX";
    const char *sc = getenv("MH_FUZZ_SCALE");
    if (sc && atoi(sc) > 0) g_scale = atoi(sc);
    g_blob = slurp(CDX_PATH, &g_blob_len);
    if (!g_blob) { printf("SKIP test_cheat_fuzz: %s not found (run from the repo root)\n", CDX_PATH); return 0; }
    CHECK(mh_cheat_db_load(&g_db, g_blob, (size_t)g_blob_len));
    if (!mkdtemp(tmpl)) return 1;
    make_seeds();
    fuzz_blob();
    fuzz_lz4();
    fuzz_db_image();
    hostile_files(tmpl);
    fuzz_text(tmpl);
    rmdir(tmpl);
    printf("test_cheat_fuzz: %d checks, no crash (blobs accepted/rejected: v4 %u/%u, v5 %u/%u, v6 %u/%u)\n", g_checks,
           g_acc[4], g_rej[4], g_acc[5], g_rej[5], g_acc[6], g_rej[6]);
    return 0;
}
