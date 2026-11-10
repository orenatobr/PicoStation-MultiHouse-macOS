/* cheat module: database load and queries, bank, profile rules. `make test-cheat`. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cheat/cheat_internal.h"
#include "mh/base.h"
#include "mh/cheat.h"

static int g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            abort();                                                              \
        }                                                                         \
    } while (0)
#define CHECK_STR(a, b)                                                                   \
    do {                                                                                  \
        const char *_a = (a), *_b = (b);                                                  \
        g_checks++;                                                                       \
        if (strcmp(_a, _b) != 0) {                                                        \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b);     \
            abort();                                                                      \
        }                                                                                 \
    } while (0)

#define CDX_PATH "resources/embedded/cdx_cheats.bin"

static uint8_t *g_blob;
static uint64_t g_blob_len;
static MhCheatDb g_db;

/* canonical digest of the derived bank, computed offline from the original 497601 byte bank */
static const char *BANK_DIGEST = "7717fa0235eaf3647eaa8a62d7c46d9635d89e3b1caecee681d1247aa0c2d0ea";

static void test_load_and_integrity(void)
{
    MhCheatDb bad;
    uint8_t *copy;
    CHECK(mh_cheat_db_load(&g_db, g_blob, (size_t)g_blob_len));
    CHECK(g_db.loaded && g_db.crc == MH_CHEAT_DB_CRC && g_db.size == MH_CHEAT_DB_RAW_SIZE);
    CHECK(mh_cheat_db_last_error(&g_db) == 0);
    CHECK(g_db.n_games == 1 && g_db.n_titles == 1004 && g_db.n_variants == 1004);
    CHECK(g_db.n_texts == 10118 && g_db.n_cheats == 10118 && g_db.n_programs == 9891);
    CHECK(mh_cheat_db_active() == &g_db);
    CHECK_STR(g_db.description, "GameShark V5 original embedded in the Manager");
    CHECK(g_db.sha256[0] == 0x1b && g_db.sha256[31] == 0xa3);

    /* a truncated or tampered container is rejected with the original code 60001 */
    CHECK(!mh_cheat_db_load(&bad, g_blob, (size_t)g_blob_len - 1));
    CHECK(mh_cheat_db_last_error(&bad) == 0xea61 && !bad.loaded && bad.data == NULL);
    CHECK(!mh_cheat_db_load(&bad, g_blob, 12));
    CHECK(mh_cheat_db_last_error(&bad) == 0xea61);
    copy = malloc((size_t)g_blob_len);
    memcpy(copy, g_blob, (size_t)g_blob_len);
    copy[0] = 'X';
    CHECK(!mh_cheat_db_load(&bad, copy, (size_t)g_blob_len));
    CHECK(mh_cheat_db_last_error(&bad) == 0xea61);
    memcpy(copy, g_blob, (size_t)g_blob_len);
    /* flip one letter inside the string area: the structure stays valid, only SHA-256 notices */
    {
        size_t p = 12, op = 0, hit = 0;
        while (p < g_blob_len && !hit) {
            uint8_t tok = copy[p++];
            size_t lit = tok >> 4, ml = tok & 15;
            if (lit == 15) { uint8_t b; do { b = copy[p++]; lit += b; } while (b == 255); }
            if (op >= 0xe0000) {
                for (size_t i = 0; i < lit && !hit; i++)
                    if (copy[p + i] >= 'a' && copy[p + i] <= 'z') hit = p + i;
            }
            p += lit;
            op += lit;
            if (p >= g_blob_len) break;
            p += 2;
            if (ml == 15) { uint8_t b; ml = 15; do { b = copy[p++]; ml += b; } while (b == 255); }
            op += ml + 4;
        }
        CHECK(hit != 0);
        copy[hit] ^= 0x01;
    }
    CHECK(!mh_cheat_db_load(&bad, copy, (size_t)g_blob_len));
    CHECK(mh_cheat_db_last_error(&bad) >= 50000 && mh_cheat_db_last_error(&bad) < 50000 + 32 * 256);
    /* the failed attempts must not have displaced the good database */
    CHECK(mh_cheat_db_active() == &g_db);
    free(copy);
}

static void test_bank(void)
{
    const MhCheatBank *b = ch_bank_active();
    MhSha256 h;
    uint8_t d[32];
    char hex[65];
    uint32_t t, total = 0;
    CHECK(b && b == g_db.bank);
    CHECK(b->n_titles == 1004 && b->n_groups == 10118);
    mh_sha256_init(&h);
    for (t = 0; t < b->n_titles; t++) {
        uint8_t hd[4];
        ch_wr16(hd, b->first[t]);
        ch_wr16(hd + 2, b->count[t]);
        mh_sha256_update(&h, hd, 4);
        for (uint32_t j = 0; j < b->count[t]; j++) {
            const ChBankGroup *g = ch_bank_group(b, t, j);
            uint8_t m[4];
            mh_sha256_update(&h, g->name, strlen(g->name) + 1);
            ch_wr16(m, g->id);
            ch_wr16(m + 2, g->nlines);
            mh_sha256_update(&h, m, 4);
            mh_sha256_update(&h, ch_bank_lines_a(b, g), (size_t)g->nlines * 6);
            mh_sha256_update(&h, ch_bank_lines_b(b, g), (size_t)g->nlines * 6);
            total++;
        }
    }
    mh_sha256_final(&h, d);
    mh_bytes_to_hex_lower(d, 32, hex, sizeof hex);
    CHECK(total == 10118);
    CHECK_STR(hex, BANK_DIGEST);
    CHECK(ch_bank_group(b, 1004, 0) == NULL && ch_bank_group(b, 0, 5) == NULL);
    CHECK_STR(ch_bank_group(b, 0, 0)->name, "HAVE ALL WEAPONS");
    /* the three names that collide after normalisation get " <id>" */
    CHECK_STR(ch_bank_group(b, 301, 17)->name, "ZIDANE 20");
    CHECK_STR(ch_bank_group(b, 787, 3)->name, "UNLIMITED HEALTH 5");
}

static void test_queries(void)
{
    char buf[512];
    uint32_t t;
    MhCheatVariantInfo vi;
    MhCheatEntryInfo ei;
    MhCheatEntry e;
    CHECK(mh_cheat_db_title(&g_db, 0, buf, sizeof buf));
    CHECK_STR(buf, "007 Racing");
    CHECK(mh_cheat_db_title(&g_db, 175, buf, sizeof buf));
    CHECK_STR(buf, "Crash Bandicoot 2: Cortex Strikes Back");
    CHECK(!mh_cheat_db_title(&g_db, 1004, buf, sizeof buf));
    CHECK(mh_cheat_db_title_variant_count(&g_db, 0) == 1 && mh_cheat_db_title_variant_count(&g_db, 5000) == 0);
    CHECK(mh_cheat_db_title_variant_at(&g_db, 0, 0, &vi) && vi.variant == 0 && vi.title == 0);
    CHECK(vi.first_entry == 0 && vi.entry_count == 5 && vi.game == 1);
    CHECK_STR(vi.game_code, "V5ORIG");
    CHECK_STR(vi.game_name, "GameShark V5 original");
    CHECK(!mh_cheat_db_title_variant_at(&g_db, 0, 1, &vi));
    CHECK(mh_cheat_db_variant_info(&g_db, 1, &vi) && vi.first_entry == 5 && vi.entry_count == 8);

    CHECK(mh_cheat_db_entry_info(&g_db, 0, 0, &ei));
    CHECK(ei.index == 0 && ei.variant == 0 && ei.id == 0 && ei.nlines == 26 && ei.game == 1);
    CHECK_STR(ei.name, "HAVE ALL WEAPONS");
    CHECK(ei.flags == 0x08 || ei.flags == 0x18 || ei.flags == 0x0b || ei.flags == 0x1b);
    CHECK(!mh_cheat_db_entry_info(&g_db, 0, 5, &ei));
    CHECK(!mh_cheat_db_entry_info(&g_db, 1004, 0, &ei));
    CHECK(mh_cheat_db_entry_text(&g_db, 0, 0, 1, buf, sizeof buf));
    CHECK_STR(buf, "Have All Weapons");
    CHECK(mh_cheat_db_entry_text(&g_db, 0, 0, 2, buf, sizeof buf) && strstr(buf, "Banco original V5"));
    CHECK(mh_cheat_db_entry_text(&g_db, 0, 0, 3, buf, sizeof buf) && strstr(buf, "Original V5 database"));
    CHECK(mh_cheat_db_entry_text(&g_db, 0, 0, 4, buf, sizeof buf) && strstr(buf, "\"decoded\""));
    CHECK(!mh_cheat_db_entry_text(&g_db, 0, 0, 5, buf, sizeof buf) && buf[0] == 0);
    CHECK(!mh_cheat_db_entry_text(&g_db, 0, 77, 0, buf, sizeof buf) && buf[0] == 0);

    CHECK(mh_cheat_db_copy_entry(&g_db, 0, 0, &e));
    CHECK_STR(e.name, "HAVE ALL WEAPONS");
    CHECK(e.nlines == 26 && e.game == 1 && e.variant == 0 && e.id == 0 && e.lines != NULL);
    CHECK(e.lines[0] == 0x70 && e.lines[6] == 0x80); /* decoded form keeps 80 */
    mh_cheat_entry_clear(&e);
    CHECK(e.lines == NULL && e.variant == 0xFFFFFFFFu);

    /* serial bindings (618 entries, case-insensitive, sorted) */
    CHECK(mh_cheat_db_find_title_for_disc(&g_db, "SCUS-94154", &t) && t == 175);
    CHECK(mh_cheat_db_find_title_for_disc(&g_db, "scus-94154", &t) && t == 175);
    CHECK(mh_cheat_db_find_title_for_disc(&g_db, "SLUS-00402", &t) && t == 835);
    CHECK(mh_cheat_db_find_title_for_disc(&g_db, "SCES-00005", &t) && t == 832);
    CHECK(mh_cheat_db_find_title_for_disc(&g_db, "SLUS-84261", &t) && t == 982);
    CHECK(!mh_cheat_db_find_title_for_disc(&g_db, "SLUS-99999", &t) && t == 0xFFFFFFFFu);
    CHECK(!mh_cheat_db_find_title_for_disc(&g_db, "", &t));
    CHECK(!mh_cheat_db_find_title_for_disc(NULL, "SCUS-94154", &t));
    CHECK(mh_cheat_db_title(&g_db, 835, buf, sizeof buf));
    CHECK_STR(buf, "Tekken 3");

    /* normalised title lookup */
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Tekken 3", &t) && t == 835);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "TEKKEN-3", &t) == 0 || t == 835);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "tekken3", &t) && t == 835);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Tekken 3 (USA) (Disc 1)", &t) && t == 835);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Metal Gear Solid - V1.0", &t) && t == 478);
    /* names of the real library (disc image names carry the region and revision) */
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Chrono Cross (USA, Canada)", &t) && t == 148);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Xenogears (USA)", &t) && t == 998);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Parasite Eve (USA)", &t) && t == 627);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Syphon Filter 2 (USA)", &t) && t == 825);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Valkyrie Profile (USA)", &t) && t == 933);
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Driver 2 (USA) (Rev 1)", &t) && t == 250);
    /* the database has no Final Fantasy VII, and spells VIII and IX with digits */
    CHECK(!mh_cheat_db_find_title_normalized(&g_db, "Final Fantasy VII (USA)", &t));
    CHECK(!mh_cheat_db_find_title_normalized(&g_db, "Final Fantasy VIII (USA)", &t));
    CHECK(!mh_cheat_db_find_title_normalized(&g_db, "Metal Gear Solid (USA) (Rev 1)", &t));
    CHECK(mh_cheat_db_find_title_normalized(&g_db, "Final Fantasy 9", &t) && t == 301);
    /* the serials of those discs have no automatic binding (FF7 USA: SCUS-94163/4/5, MGS: SLUS-00594) */
    CHECK(!mh_cheat_db_find_title_for_disc(&g_db, "SCUS-94163", &t) && !mh_cheat_db_find_title_for_disc(&g_db, "SLUS-00594", &t));
    CHECK(!mh_cheat_db_find_title_normalized(&g_db, "No Such Game Exists", &t) && t == 0xFFFFFFFFu);
    CHECK(!mh_cheat_db_find_title_normalized(&g_db, "", &t));
    CHECK(!mh_cheat_db_find_title_normalized(&g_db, NULL, &t));

    /* search filter */
    CHECK(mh_cheat_db_title_matches(&g_db, 835, ""));
    CHECK(mh_cheat_db_title_matches(&g_db, 835, "tekken"));
    CHECK(mh_cheat_db_title_matches(&g_db, 835, "Tekken 3 USA"));  /* title inside the filter */
    CHECK(mh_cheat_db_title_matches(&g_db, 835, "tek-ken"));       /* alphanumeric keys */
    CHECK(!mh_cheat_db_title_matches(&g_db, 835, "crash"));
    CHECK(!mh_cheat_db_title_matches(&g_db, 5000, "tekken"));
    CHECK(mh_cheat_filter_match("Final Fantasy", "FANT") && !mh_cheat_filter_match("Final", "xyz") &&
          mh_cheat_filter_match("x", ""));
}

/* orig-level numbers: how many titles can be built and the limits they respect */
static void test_catalogs(void)
{
    uint32_t built = 0, tried = 0, i;
    char err[256];
    for (i = 0; i < g_db.n_titles; i++) {
        MhCheatProfile *p = NULL;
        tried++;
        if (mh_cheat_profile_build_exact_catalog(&g_db, i, "SLUS-00001", &p, err, sizeof err)) {
            built++;
            CHECK(p->count > 0 && p->count <= 256);
            CHECK(mh_cheat_profile_total_lines(p) <= 510);
            CHECK(mh_cheat_profile_serialized_size(p) <= MH_CHEAT_BLOB_MAX);
            CHECK(mh_cheat_profile_validate(p, err, sizeof err));
            CHECK_STR(p->serial, "SLUS-00001");
            mh_cheat_profile_free(p);
        } else {
            CHECK_STR(err, "No audited variant fits safely in the extended 10,240 byte PSTCHT1.");
            CHECK(p == NULL);
        }
    }
    fprintf(stderr, "catalogs: %u of %u titles fit\n", built, tried);
    CHECK(built > 900);
}

int main(void)
{
    if (!mh_read_entire_file(CDX_PATH, &g_blob, &g_blob_len)) {
        fprintf(stderr, "SKIP: %s not found (run from the repository root)\n", CDX_PATH);
        return 0;
    }
    test_load_and_integrity();
    test_bank();
    test_queries();
    test_catalogs();
    mh_cheat_db_free(&g_db);
    CHECK(mh_cheat_db_active() == NULL && g_db.data == NULL);
    free(g_blob);
    printf("test_cheat: %d checks passed\n", g_checks);
    return 0;
}
