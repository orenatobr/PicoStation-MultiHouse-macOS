/* base module: CD sector EDC/ECC tooling and the V2 track map sector. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mh/base.h"

static int g_checks;
#define CHECK(c)                                                                       \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(c)) {                                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);      \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static uint32_t g_rng = 12345;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

/* The tables as the original's lazy initialiser computed them (decompiled loops of
 * cd_sector_check.part.0 and repair_sector): xtime, its inverse and the EDC CRC table. The
 * exe's .data holds them zeroed at rest (filled on first use), so there are no bytes in the
 * image to compare with; the real-disc test below is the proof against actual sectors. */
static void test_tables(void)
{
    const uint32_t *edc = mh_cd_edc_table();
    const uint8_t *fwd = mh_cd_ecc_forward_table(), *bwd = mh_cd_ecc_backward_table();
    unsigned i, k;
    for (i = 0; i < 256; i++) {
        unsigned f = i << 1;
        uint32_t e = i;
        if (i & 0x80) f ^= 0x11d;
        CHECK(fwd[i] == (f & 0xFF));
        CHECK(bwd[(f & 0xFF) ^ i] == i);
        for (k = 0; k < 8; k++) e = (e >> 1) ^ ((e & 1) ? 0xD8018001u : 0);
        CHECK(edc[i] == e);
    }
    CHECK(edc[0] == 0 && edc[1] == 0x90910101u && edc[2] == 0x91210201u && edc[128] == 0xD8018001u && edc[255] == 0x7070FF00u);
    CHECK(fwd[1] == 2 && fwd[0x80] == 0x1d && fwd[0xFF] == 0xE3);
}

static void mk_header(uint8_t *s, int mode, unsigned lba)
{
    unsigned m = (lba + 150) / 4500, sec = ((lba + 150) / 75) % 60, f = (lba + 150) % 75;
    memset(s, 0, MH_SECTOR_RAW);
    memset(s + 1, 0xFF, 10);
    s[0xC] = (uint8_t)((m / 10) << 4 | m % 10);
    s[0xD] = (uint8_t)((sec / 10) << 4 | sec % 10);
    s[0xE] = (uint8_t)((f / 10) << 4 | f % 10);
    s[0xF] = (uint8_t)mode;
}

static void fill(uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)rnd();
}

static void mk_mode1(uint8_t *s, unsigned lba)
{
    mk_header(s, 1, lba);
    fill(s + 0x10, 2048);
}
static void mk_m2f1(uint8_t *s, unsigned lba)
{
    mk_header(s, 2, lba);
    s[0x10] = s[0x14] = 1; s[0x11] = s[0x15] = 2; s[0x12] = s[0x16] = 0x08; s[0x13] = s[0x17] = 0;
    fill(s + 0x18, 2048);
}
static void mk_m2f2(uint8_t *s, unsigned lba)
{
    mk_header(s, 2, lba);
    s[0x10] = s[0x14] = 1; s[0x11] = s[0x15] = 2; s[0x12] = s[0x16] = 0x20 | 0x04; s[0x13] = s[0x17] = 0;
    fill(s + 0x18, 2324);
}

static void test_synthetic(void)
{
    uint8_t good[MH_SECTOR_RAW], s[MH_SECTOR_RAW];
    unsigned kind, n;
    for (kind = 1; kind <= 3; kind++) {
        for (n = 0; n < 20; n++) {
            unsigned pos;
            if (kind == 1) mk_mode1(good, n * 1000);
            else if (kind == 2) mk_m2f1(good, n * 1000);
            else mk_m2f2(good, n * 1000);
            CHECK(mh_cd_sector_kind(good) == (int)kind);
            memcpy(s, good, sizeof s);

            /* an unfinished sector: recode reports what was wrong and then it checks clean */
            if (kind == 3) {
                CHECK(mh_cd_sector_check(s) == 0); /* EDC 0 means unused, accepted */
                CHECK(mh_cd_sector_recode(s) == 0 && !memcmp(s, good, sizeof s));
                memcpy(s + 0x92C, "\x01\x00\x00\x00", 4); /* wrong, non zero EDC */
                CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_EDC);
                CHECK(mh_cd_sector_recode(s) == MH_SECTOR_BAD_EDC);
                CHECK(mh_cd_sector_check(s) == 0);
                CHECK(mh_cd_edc(s + 0x10, 0x91C) == (uint32_t)(s[0x92C] | s[0x92D] << 8 | s[0x92E] << 16 | (uint32_t)s[0x92F] << 24));
                memcpy(good, s, sizeof s);
            } else {
                CHECK(mh_cd_sector_check(s) == (MH_SECTOR_BAD_EDC | MH_SECTOR_BAD_ECC));
                CHECK(mh_cd_sector_recode(s) == (MH_SECTOR_BAD_EDC | MH_SECTOR_BAD_ECC));
                CHECK(mh_cd_sector_check(s) == 0);
                memcpy(good, s, sizeof s);
                CHECK(mh_cd_sector_recode(s) == 0 && !memcmp(s, good, sizeof s)); /* idempotent */
            }

            if (kind == 3) {
                for (pos = 0x18; pos < 0x92C; pos += 97) {
                    memcpy(s, good, sizeof s);
                    s[pos] ^= 0x40;
                    CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_EDC);
                    CHECK(mh_cd_sector_recode(s) == MH_SECTOR_BAD_EDC);
                    CHECK(mh_cd_sector_check(s) == 0);
                }
                continue;
            }
            /* single byte corruptions everywhere after the header, expected result bits */
            for (pos = kind == 1 ? 0x10 : 0x18; pos < MH_SECTOR_RAW; pos += (pos < 0x830 ? 13 : 1)) {
                int want, got;
                memcpy(s, good, sizeof s);
                s[pos] ^= (uint8_t)(1u << (pos & 7));
                if (kind == 1) want = pos < 0x814 ? 6 : 4; /* 0x814..0x81B: reserved, ECC only */
                else want = pos < 0x81C ? 6 : 4;     /* EDC bytes lie inside the ECC range */
                got = mh_cd_sector_check(s);
                if (got != want) {
                    fprintf(stderr, "kind %u pos 0x%x: got %d want %d\n", kind, pos, got, want);
                    abort();
                }
                CHECK(mh_cd_sector_recode(s) == want);
                /* recode makes the sector self-consistent with its (damaged) content */
                CHECK(mh_cd_sector_check(s) == 0);
                if (pos >= 0x81C) {
                    CHECK(!memcmp(s, good, sizeof s)); /* only parity was damaged: fully restored */
                } else {
                    s[pos] ^= (uint8_t)(1u << (pos & 7)); /* undo the damage: EDC/ECC are stale again */
                    CHECK(mh_cd_sector_check(s) != 0);
                    mh_cd_sector_recode(s);
                    CHECK(!memcmp(s, good, sizeof s));
                }
            }
            /* wrong MSF in the header of a Mode 1 sector is covered by EDC and ECC; in Mode 2
             * the header is not protected at all */
            memcpy(s, good, sizeof s);
            s[0xE] ^= 0x01;
            CHECK(mh_cd_sector_check(s) == (kind == 1 ? 6 : 0));
        }
    }
    CHECK(1);
}

static void test_malformed(void)
{
    uint8_t s[MH_SECTOR_RAW], keep[MH_SECTOR_RAW];
    unsigned i;
    mk_m2f1(s, 5000);
    mh_cd_sector_recode(s);
    CHECK(mh_cd_sector_check(s) == 0);
    for (i = 0; i <= 11; i++) {
        memcpy(keep, s, sizeof s);
        s[i] ^= 0x01;
        CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_FORMAT && mh_cd_sector_kind(s) == 0);
        CHECK(mh_cd_sector_recode(s) == MH_SECTOR_BAD_FORMAT);
        s[i] ^= 0x01;
        CHECK(!memcmp(s, keep, sizeof s)); /* nothing was touched */
    }
    for (i = 0; i < 4; i++) { /* subheader copies */
        s[0x14 + i] ^= 0x80;
        CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_FORMAT);
        s[0x14 + i] ^= 0x80;
    }
    s[0xF] = 0;
    CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_FORMAT);
    s[0xF] = 3;
    CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_FORMAT);
    fill(s, sizeof s); /* audio-like noise */
    CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_FORMAT && mh_cd_sector_kind(s) == 0);
    memset(s, 0, sizeof s);
    CHECK(mh_cd_sector_check(s) == MH_SECTOR_BAD_FORMAT);

    /* the original compared the two subheader copies only for Mode 2 */
    mk_mode1(s, 10);
    mh_cd_sector_recode(s);
    s[0x14] ^= 0xFF; /* inside Mode 1 user data: just corruption */
    CHECK(mh_cd_sector_check(s) == 6);
}

static void test_ecc_header_handling(void)
{
    uint8_t s[MH_SECTOR_RAW], keep[MH_SECTOR_RAW], other[MH_SECTOR_RAW];
    mk_m2f1(s, 77);
    memcpy(keep, s, sizeof s);
    mh_cd_ecc(s);
    CHECK(!memcmp(s, keep, 0x81C)); /* header and data untouched */
    memcpy(other, s, sizeof s);
    other[0xC] = 0x99; /* a different MSF gives the same parity in Mode 2 */
    mh_cd_ecc(other);
    CHECK(!memcmp(other + 0x81C, s + 0x81C, MH_SECTOR_RAW - 0x81C) && other[0xC] == 0x99);

    /* repair_sector = EDC + ECC of a Form 1 sector without looking at anything */
    mh_cd_sector_recode(s);
    memcpy(keep, s, sizeof s);
    s[0x18 + 100] ^= 0x55;
    s[0x18 + 1000] ^= 0xAA;
    CHECK(mh_cd_sector_check(s) == 6);
    mh_cd_repair_sector(s);
    CHECK(mh_cd_sector_check(s) == 0);
    s[0x18 + 100] ^= 0x55;
    s[0x18 + 1000] ^= 0xAA;
    CHECK(!memcmp(s + 0x18, keep + 0x18, 2048));
    mh_cd_repair_sector(s);
    CHECK(!memcmp(s, keep, sizeof s));
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void seal(uint8_t *s)
{
    put32(s + 0xC, 0);
    put32(s + 0xC, mh_crc32(s, MH_SECTOR_RAW));
}

static void test_track_sector(void)
{
    uint8_t s[MH_SECTOR_RAW], t[MH_SECTOR_RAW];
    uint8_t ext[20] = {'S', 'B', 'I', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    memset(s, 0, sizeof s);
    memcpy(s, "PSTTRK1", 7);
    s[8] = 2;
    s[9] = 0x10;
    put32(s + 0x10, 0); put32(s + 0x14, 0); put32(s + 0x18, 1000); s[0x1C] = 1;
    put32(s + 0x20, 1000); put32(s + 0x24, 1150); put32(s + 0x28, 500); s[0x2C] = 2;
    seal(s);
    CHECK(mh_track_sector_valid(s, 1500));
    CHECK(!mh_track_sector_valid(s, 1499) && !mh_track_sector_valid(s, 1501));

#define MUT(stmt, total)                                  \
    do {                                                  \
        memcpy(t, s, sizeof t);                           \
        stmt;                                             \
        seal(t);                                          \
        CHECK(!mh_track_sector_valid(t, total));          \
    } while (0)
    MUT(t[0] = 'X', 1500);
    MUT(t[8] = 0, 1500);
    MUT(t[8] = 100, 1500);
    MUT(t[9] = 0x11, 1500);
    MUT(put32(t + 0x18, 0), 1500);             /* zero length */
    MUT(put32(t + 0x10, 1), 1500);             /* first entry must start at 0 */
    MUT(put32(t + 0x14, 1), 1500);             /* ... and have index1 0 */
    MUT(t[0x1C] = 0, 1500);                    /* ... and a non zero type */
    MUT(t[0x2C] = 3, 1500);                    /* type > 2 */
    MUT(put32(t + 0x20, 999), 1500);           /* gap or overlap */
    MUT(put32(t + 0x24, 999), 1500);           /* index1 < start */
    MUT(put32(t + 0x24, 1500), 1500);          /* index1 >= end */
    MUT(put32(t + 0x28, 0xFFFFFFFFu), 1500);   /* overflow */
    MUT(put32(t + 0x28, 600), 1500);           /* beyond total */
    memcpy(t, s, sizeof t);
    t[100] ^= 1; /* crc not refreshed */
    CHECK(!mh_track_sector_valid(t, 1500));
    memcpy(t, s, sizeof t);
    t[0xA] = 1; /* extension block announced but absent */
    seal(t);
    CHECK(!mh_track_sector_valid(t, 1500));
    t[0x640] = 20; t[0x641] = 0;
    memcpy(t + 0x648, ext, 20);
    put32(t + 0x644, mh_crc32(ext, 20));
    seal(t);
    CHECK(mh_track_sector_valid(t, 1500));
    t[0x640] = 3; seal(t);
    CHECK(!mh_track_sector_valid(t, 1500)); /* length below 4 */
    t[0x640] = 20; put32(t + 0x644, 0); seal(t);
    CHECK(!mh_track_sector_valid(t, 1500)); /* bad extension crc */
    put32(t + 0x644, mh_crc32(ext, 20)); t[0x648] = 'X'; seal(t);
    CHECK(!mh_track_sector_valid(t, 1500)); /* bad magic */
    memcpy(t + 0x648, ext, 20);
    t[0x640] = 0xE8; t[0x641] = 0x02; /* 0x2E8, the largest accepted */
    memset(t + 0x648 + 20, 7, 0x2E8 - 20);
    put32(t + 0x644, mh_crc32(t + 0x648, 0x2E8));
    seal(t);
    CHECK(mh_track_sector_valid(t, 1500));
    t[0x640] = 0xE9; seal(t);
    CHECK(!mh_track_sector_valid(t, 1500));
}

static long g_form1, g_form2, g_form2_edc;
static void test_real_disc(const char *path, long stride)
{
    FILE *f = fopen(path, "rb");
    uint8_t s[MH_SECTOR_RAW], orig[MH_SECTOR_RAW];
    long total, i, n1 = 0, n2 = 0, edc0 = 0;
    if (!f) {
        printf("skip: %s not found\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    total = ftell(f) / MH_SECTOR_RAW;
    CHECK(total > 1000);
    for (i = 0; i < total; i += (i < 4000 ? 1 : stride)) {
        int kind, pos;
        fseek(f, i * MH_SECTOR_RAW, SEEK_SET);
        CHECK(fread(orig, 1, sizeof orig, f) == sizeof orig);
        kind = mh_cd_sector_kind(orig);
        if (kind == 0) continue; /* the PS1 pregap or audio, none expected here */
        CHECK(mh_cd_sector_check(orig) == 0);
        if (kind == 2) {
            n1++;
            pos = 0x18 + (int)(rnd() % 2048);
        } else {
            n2++;
            if (!(orig[0x92C] | orig[0x92D] | orig[0x92E] | orig[0x92F])) { edc0++; continue; }
            g_form2_edc++;
            pos = 0x18 + (int)(rnd() % 2324);
        }
        memcpy(s, orig, sizeof s);
        s[pos] ^= (uint8_t)(1 + rnd() % 255);
        CHECK(mh_cd_sector_check(s) == (kind == 2 ? 6 : 2));
        CHECK(mh_cd_sector_recode(s) == (kind == 2 ? 6 : 2));
        CHECK(mh_cd_sector_check(s) == 0);
        s[pos] = orig[pos];            /* undo the damage and regenerate EDC/ECC */
        mh_cd_sector_recode(s);
        CHECK(!memcmp(s, orig, sizeof s)); /* regenerated bytes equal the real dump */
        if (kind == 2) { /* a damaged parity region alone */
            s[0x81C + (int)(rnd() % 276)] ^= 0x10;
            CHECK(mh_cd_sector_check(s) == 4);
            CHECK(mh_cd_sector_recode(s) == 4 && !memcmp(s, orig, sizeof s));
        }
    }
    printf("real disc %s: %ld form 1 and %ld form 2 sectors verified (%ld form 2 with unused EDC)\n", path, n1, n2, edc0);
    g_form1 += n1;
    g_form2 += n2;
    fclose(f);
}

int main(void)
{
    test_tables();
    test_synthetic();
    test_malformed();
    test_ecc_header_handling();
    test_track_sector();
    test_real_disc("/Volumes/Renato/PicoStation BR/Alundra (USA) (Rev 1).bin", 7);
    test_real_disc("/Volumes/Renato/PicoStation BR/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 2).bin", 11);
    if (g_form1) CHECK(g_form1 > 1000 && g_form2 > 100);
    if (g_form2_edc == 0) printf("note: no real Form 2 sector with a non zero EDC was available\n");
    printf("test_base_cd: %d checks passed\n", g_checks);
    return 0;
}
