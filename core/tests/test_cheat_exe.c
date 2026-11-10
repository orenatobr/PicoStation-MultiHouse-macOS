/* Independent byte-for-byte comparison of the derived V5 compact bank against the bank embedded in
 * the original executable (v5_compact_bank, VA 0x140f1d220, file offset 0xF1C220, 497601 bytes).
 * Runs only when MH_EXE points at mh64.exe, otherwise it skips. `make test-cheat`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cheat/cheat_internal.h"

static int g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            abort();                                                              \
        }                                                                         \
    } while (0)

/* .rdata VA 0x140f09000 = file 0xF08000 */
#define RDATA_OFF(va) (0xF08000u + ((va) - 0x140f09000u))
#define BANK_OFF 0xF1C220u
#define BANK_SIZE 497601u

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *p;
    long l;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    l = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = malloc((size_t)l);
    if (!p || fread(p, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(p); return NULL; }
    fclose(f);
    *n = (size_t)l;
    return p;
}

/* kCheatSerialBindings, as in cheat_db.c */
static const struct {
    const char *serial;
    uint32_t title;
} k_bind[] = {
#include "../src/cheat/serial_bindings.inc"
};

int main(void)
{
    const char *exe = getenv("MH_EXE");
    size_t en, bn;
    uint8_t *e, *blob;
    MhCheatDb db;
    const MhCheatBank *bk;
    const uint8_t *b;
    uint32_t nt, ng, to, go, sp, lp, tot, g = 0, t;
    uint8_t *cov_n, *cov_l;
    uint64_t names_pos = 0, lines_pos = 0;

    if (!exe || !*exe) { printf("SKIP test_cheat_exe: set MH_EXE=/path/to/mh64.exe\n"); return 0; }
    e = slurp(exe, &en);
    if (!e || en < BANK_OFF + BANK_SIZE) { printf("SKIP test_cheat_exe: cannot read %s\n", exe); return 0; }
    blob = slurp("resources/embedded/cdx_cheats.bin", &bn);
    if (!blob) { printf("SKIP test_cheat_exe: run from the repo root\n"); return 0; }
    CHECK(mh_cheat_db_load(&db, blob, bn));
    bk = ch_bank_active();
    CHECK(bk && bk == db.bank);

    /* the embedded container is byte for byte the one in .data, and its SHA-256 is the constant at 0x140f1d080 */
    CHECK(bn == 1060037 && memmem(e, en, blob, bn) != NULL);
    CHECK(memcmp(db.sha256, e + RDATA_OFF(0x140f1d080u), 32) == 0);
    /* 618 serial bindings: same records in the same order, the table ends where the SHA-256 constant starts */
    CHECK(sizeof k_bind / sizeof k_bind[0] == 618 && 0x140f1a9e0u + 618 * 16 == 0x140f1d080u);
    for (unsigned i = 0; i < 618; i++) {
        const uint8_t *r = e + RDATA_OFF(0x140f1a9e0u) + 16 * i;
        char ser[13];
        uint32_t t;
        memcpy(ser, r, 12);
        ser[12] = 0;
        CHECK(strcmp(ser, k_bind[i].serial) == 0 && ch_rd32(r + 12) == k_bind[i].title);
        CHECK(mh_cheat_db_find_title_for_disc(&db, ser, &t) && t == k_bind[i].title);
        CHECK(i == 0 || strcmp(k_bind[i - 1].serial, k_bind[i].serial) < 0);
    }

    b = e + BANK_OFF;
    CHECK(memcmp(b, "V5REF01\0", 8) == 0);
    nt = ch_rd32(b + 8); ng = ch_rd32(b + 12); to = ch_rd32(b + 16); go = ch_rd32(b + 20);
    sp = ch_rd32(b + 24); lp = ch_rd32(b + 28); tot = ch_rd32(b + 32);
    CHECK(ch_rd32(b + 36) == MH_CHEAT_DB_CRC);
    CHECK(tot == BANK_SIZE && nt == bk->n_titles && ng == bk->n_groups);
    CHECK(nt == 1004 && ng == 10118);
    /* regions are contiguous: header, titles, groups, names, lines */
    CHECK(to == 0x28 && go == to + 4 * nt && sp == go + 16 * ng && lp > sp && tot > lp);
    cov_n = calloc(lp - sp, 1);
    cov_l = calloc(tot - lp, 1);
    CHECK(cov_n && cov_l);

    for (t = 0; t < nt; t++) {
        CHECK(ch_rd16(b + to + 4 * t) == bk->first[t]);
        CHECK(ch_rd16(b + to + 4 * t + 2) == bk->count[t]);
        CHECK(bk->first[t] == g);
        for (uint32_t j = 0; j < bk->count[t]; j++, g++) {
            const ChBankGroup *bg = ch_bank_group(bk, t, j);
            const uint8_t *r = b + go + 16u * g;
            uint32_t no = ch_rd32(r), ao = ch_rd32(r + 4), bo = ch_rd32(r + 12);
            unsigned cnt = ch_rd16(r + 8);
            size_t nl;
            const char *nm;
            CHECK(bg);
            CHECK(sp + no < lp);
            nm = (const char *)b + sp + no;
            nl = strnlen(nm, lp - sp - no);
            CHECK(sp + no + nl < lp);
            CHECK(strcmp(nm, bg->name) == 0);
            CHECK(ch_rd16(r + 10) == bg->id);
            CHECK(cnt == bg->nlines && cnt > 0);
            CHECK((uint64_t)lp + ao + cnt * 6u <= tot && (uint64_t)lp + bo + cnt * 6u <= tot);
            CHECK(memcmp(b + lp + ao, ch_bank_lines_a(bk, bg), cnt * 6u) == 0);
            CHECK(memcmp(b + lp + bo, ch_bank_lines_b(bk, bg), cnt * 6u) == 0);
            /* the pools are allocated sequentially, first use wins, equal data is shared */
            if (no >= names_pos) { CHECK(no == names_pos); names_pos += nl + 1; }
            memset(cov_n + no, 1, nl + 1);
            if (ao >= lines_pos) { CHECK(ao == lines_pos); lines_pos += cnt * 6u; }
            memset(cov_l + ao, 1, cnt * 6u);
            if (bo >= lines_pos) { CHECK(bo == lines_pos); lines_pos += cnt * 6u; }
            memset(cov_l + bo, 1, cnt * 6u);
        }
    }
    CHECK(g == ng);
    /* nothing in the original bank is left unexplained */
    CHECK(names_pos == lp - sp && lines_pos == tot - lp);
    for (uint32_t i = 0; i < lp - sp; i++) CHECK(cov_n[i]);
    for (uint32_t i = 0; i < tot - lp; i++) CHECK(cov_l[i]);
    free(cov_n); free(cov_l); free(e); free(blob);
    mh_cheat_db_free(&db);
    printf("test_cheat_exe: %d checks OK (bank %u groups identical to the exe)\n", g_checks, ng);
    return 0;
}
