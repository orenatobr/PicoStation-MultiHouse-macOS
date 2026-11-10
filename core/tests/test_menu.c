/* test_menu: the embedded PicoStation menu through every validator and the defaults patcher. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mh/base.h"
#include "mh/menu.h"

#define MENU "resources/embedded/picostation_menu.bin"

static uint8_t *emb;
static uint64_t emb_n;

static uint8_t *copy_emb(void) {
    uint8_t *c = malloc(emb_n);
    memcpy(c, emb, emb_n);
    return c;
}

static void expect_err_at(int line, const uint8_t *img, uint64_t n, uint32_t stride, const char *msg) {
    char err[256] = "x";
    assert(!mh_menu_validate_blob(img, n, stride, err, sizeof err));
    if (strcmp(err, msg) != 0) { fprintf(stderr, "line %d\ngot: %s\nwant: %s\n", line, err, msg); abort(); }
    assert(!mh_menu_validate_blob(img, n, stride, NULL, 0));
}

#define expect_err(...) expect_err_at(__LINE__, __VA_ARGS__)

static void test_defaults_record(void) {
    uint8_t rec[64], rec2[64];
    uint64_t off;
    assert(mh_menu_defaults_read(emb, emb_n, rec, &off));
    assert(off == 141272 && off / MH_MENU_SECTOR == 60 && off % MH_MENU_SECTOR == 152);
    assert(mh_menu_defaults_valid(rec));
    MhMenuDefaults d;
    assert(mh_menu_defaults_parse(rec, &d));
    printf("embedded defaults: %ux%u theme=%u lang=%u visual=%u fav=%u music=%u vol=%u\n", d.width, d.height,
           d.theme, d.language, d.visual, d.favorites, d.music, d.volume);
    /* round trip through build */
    assert(mh_menu_defaults_build(rec2, &d) && !memcmp(rec, rec2, 64));
    mh_menu_defaults_factory(rec2);
    assert(mh_menu_defaults_valid(rec2));
    assert(mh_menu_defaults_parse(rec2, &d) && d.width == 0x140 && d.height == 0xf0 && d.theme == 1 &&
           d.language == 0 && d.visual == 0 && d.favorites == 0 && d.music == 1 && d.volume == 68);
    /* every rule of the validator */
    for (int bad = 0; bad < 12; bad++) {
        memcpy(rec2, rec, 64);
        switch (bad) {
        case 0: rec2[0] = 'Q'; break;
        case 1: rec2[0x10] = 2; break;
        case 2: rec2[0x14] = 0x41; break;
        case 3: rec2[0x16] = 0x10; break;
        case 4: rec2[0x18] = 3; break;
        case 5: rec2[0x19] = 3; break;
        case 6: rec2[0x1a] = 2; break;
        case 7: rec2[0x1b] = 2; break;
        case 8: rec2[0x1c] = 2; break;
        case 9: rec2[0x1d] = 101; break;
        case 10: rec2[0x20] = 1; break;
        case 11: rec2[0x3c] ^= 1; break;
        }
        if (bad >= 1 && bad <= 10) mh_menu_defaults_seal(rec2); /* only the rule under test may fail */
        assert(!mh_menu_defaults_valid(rec2));
    }
    memcpy(rec2, rec, 64); rec2[0x1d] = 100; mh_menu_defaults_seal(rec2);
    assert(mh_menu_defaults_valid(rec2));
    MhMenuDefaults bad = {0x140, 0xf0, 0, 0, 0, 0, 0, 101};
    assert(!mh_menu_defaults_build(rec2, &bad));
    bad.volume = 5; bad.width = 0x150;
    assert(!mh_menu_defaults_build(rec2, &bad));
}

static void test_patch(void) {
    uint8_t *img = copy_emb();
    uint8_t rec[64], back[64];
    MhMenuDefaults d = {0x200, 0x100, 2, 1, 1, 1, 0, 33};
    assert(mh_menu_defaults_build(rec, &d));
    assert(mh_menu_defaults_patch(img, emb_n, rec));
    assert(mh_menu_defaults_read(img, emb_n, back, NULL) && !memcmp(rec, back, 64));
    /* only sector 60 changed, and it is a valid Mode 2 Form 1 sector again */
    for (uint64_t s = 0; s < emb_n / MH_MENU_SECTOR; s++) {
        int same = !memcmp(img + s * MH_MENU_SECTOR, emb + s * MH_MENU_SECTOR, MH_MENU_SECTOR);
        assert(s == 60 ? !same : same);
    }
    assert(mh_cd_sector_check(img + 60 * MH_MENU_SECTOR) == 0);
    assert(mh_cd_sector_check(emb + 60 * MH_MENU_SECTOR) == 0);
    assert(mh_menu_validate_blob(img, emb_n, MH_MENU_SECTOR, NULL, 0));
    assert(mh_menu_defaults_compatible(img, emb_n, emb, emb_n));
    assert(mh_menu_defaults_compatible(emb, emb_n, emb, emb_n));
    /* patching with an invalid record is refused and touches nothing */
    uint8_t *img2 = copy_emb();
    uint8_t badrec[64];
    memcpy(badrec, rec, 64); badrec[0x3c] ^= 1;
    assert(!mh_menu_defaults_patch(img2, emb_n, badrec) && !memcmp(img2, emb, emb_n));
    assert(!mh_menu_defaults_patch(img2, emb_n, NULL));
    /* compatible is false when anything else differs or the EDC is broken */
    img2[100 * 0 + 24 * MH_MENU_SECTOR + 0x30] ^= 0xff;
    assert(!mh_menu_defaults_compatible(img2, emb_n, emb, emb_n));
    memcpy(img2, img, emb_n);
    img2[60 * MH_MENU_SECTOR + 0x30] ^= 1; /* sector now has a bad EDC */
    assert(!mh_menu_defaults_compatible(img2, emb_n, emb, emb_n));
    assert(!mh_menu_defaults_compatible(img, emb_n, emb, emb_n - MH_MENU_SECTOR));
    /* two records in an image make it ambiguous; a record outside Form 1 data is an error */
    memcpy(img2, emb, emb_n);
    memcpy(img2 + 62 * MH_MENU_SECTOR + 0x100, rec, 64);
    assert(!mh_menu_defaults_read(img2, emb_n, NULL, NULL));
    memcpy(img2, emb, emb_n);
    memcpy(img2 + 62 * MH_MENU_SECTOR + 0x900, rec, 64); /* beyond 0x818 */
    assert(!mh_menu_defaults_read(img2, emb_n, NULL, NULL));
    memcpy(img2, emb, emb_n);
    memset(img2 + 141272, 0, 64); /* none at all */
    assert(!mh_menu_defaults_read(img2, emb_n, NULL, NULL));
    assert(!mh_menu_defaults_read(emb, emb_n - 1, NULL, NULL));
    assert(!mh_menu_defaults_read(NULL, emb_n, NULL, NULL));
    free(img);
    free(img2);
}

static void test_files(void) {
    char dir[] = "/tmp/mh_menu_test_XXXXXX";
    assert(mkdtemp(dir));
    char dat[MH_PATH_MAX], tmp[MH_PATH_MAX], menu[MH_PATH_MAX];
    mh_path_join(dir, "menu-defaults.dat", dat, sizeof dat);
    mh_path_join(dir, "menu-defaults.tmp", tmp, sizeof tmp);
    uint8_t rec[64], got[64];
    mh_menu_defaults_factory(rec);
    assert(!mh_menu_defaults_load(dat, got)); /* missing */
    assert(mh_menu_defaults_save(dat, rec));
    assert(!mh_file_exists(tmp));
    assert(mh_menu_defaults_load(dat, got) && !memcmp(got, rec, 64));
    uint8_t bad[64];
    memcpy(bad, rec, 64); bad[0x3c] ^= 1;
    assert(!mh_menu_defaults_save(dat, bad));
    assert(mh_write_entire_file(dat, bad, 64) && !mh_menu_defaults_load(dat, got));
    uint8_t big[65];
    memcpy(big, rec, 64); big[64] = 0;
    assert(mh_write_entire_file(dat, big, 65) && !mh_menu_defaults_load(dat, got));

    char err[256];
    mh_path_join(dir, "menu.bin", menu, sizeof menu);
    assert(!mh_menu_validate_file(menu, false, emb, emb_n, err, sizeof err));
    assert(!strcmp(err, "Could not read the selected menu."));
    assert(!mh_menu_validate_file("", false, emb, emb_n, NULL, 0));
    assert(mh_write_entire_file(menu, emb, emb_n));
    assert(mh_menu_validate_file(menu, true, emb, emb_n, err, sizeof err) && err[0] == 0);
    uint8_t *img = copy_emb();
    img[30 * MH_MENU_SECTOR + 0x40] ^= 1; /* inside the executable, not a defaults change */
    assert(mh_write_entire_file(menu, img, emb_n));
    assert(mh_menu_validate_file(menu, false, emb, emb_n, err, sizeof err));
    assert(!mh_menu_validate_file(menu, true, emb, emb_n, err, sizeof err));
    assert(!strcmp(err, "The selected menu does not match the PSn00bSDK menu built into this app."));
    free(img);
    uint8_t *e2;
    uint64_t n2;
    assert(mh_menu_embedded_load(MENU, &e2, &n2) && n2 == emb_n);
    free(e2);
    char cmd[MH_PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    assert(system(cmd) == 0);
}

static void test_iso(void) {
    uint32_t lba, size;
    assert(mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "SYSTEM.CNF", true, &lba, &size) && lba == 23 && size == 60);
    assert(mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "system.cnf", true, &lba, &size) && lba == 23);
    assert(!mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "SYSTEM.CNF", false, &lba, &size)); /* ";1" present */
    assert(mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "system.cnf;1", false, &lba, &size) && lba == 23);
    assert(mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "SCES_313.37;1", false, &lba, &size) && lba == 24 && size == 98304);
    assert(mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "PICO.DAT", true, &lba, &size) && lba == 72 && size == 924);
    assert(!mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "NOPE.TXT", true, &lba, &size));
    assert(!mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 22, 1, "", true, &lba, &size));
    assert(!mh_menu_find_root_file(emb, MH_MENU_SECTOR, 24, 22, 1, "SCES_313.37;1", false, &lba, &size)); /* start beyond end */
    assert(!mh_menu_find_root_file(emb, MH_MENU_SECTOR, 60, 22, 1, "SCES_313.37;1", false, &lba, &size)); /* does not fit */
    assert(!mh_menu_find_root_file(NULL, MH_MENU_SECTOR, 93, 22, 1, "PICO.DAT", true, &lba, &size));
    assert(!mh_menu_find_root_file(emb, MH_MENU_SECTOR, 93, 21, 1, "PICO.DAT", true, &lba, &size)); /* LBA 21 is not a directory */
    const uint8_t *d;
    uint32_t dl;
    assert(mh_menu_user_sector(emb, MH_MENU_SECTOR, 93, 16, &d, &dl) && dl == 0x800 && d == emb + 16 * 0x930 + 0x18);
    assert(!mh_menu_user_sector(emb, MH_MENU_SECTOR, 93, 93, &d, &dl));
    assert(!mh_menu_user_sector(NULL, MH_MENU_SECTOR, 93, 0, &d, &dl));
    uint8_t s[MH_MENU_SECTOR];
    memcpy(s, emb + 16 * 0x930, sizeof s);
    s[0x12] |= 0x20; s[0x16] |= 0x20; /* Form 2 */
    assert(mh_menu_user_sector(s, MH_MENU_SECTOR, 1, 0, &d, &dl) && dl == 0x914);
    s[0xf] = 1;
    assert(mh_menu_user_sector(s, MH_MENU_SECTOR, 1, 0, &d, &dl) && dl == 0x800 && d == s + 0x10);
    s[0xf] = 2; s[0x14] ^= 1;
    assert(!mh_menu_user_sector(s, MH_MENU_SECTOR, 1, 0, &d, &dl));
    s[0x14] ^= 1; s[0xf] = 0;
    assert(!mh_menu_user_sector(s, MH_MENU_SECTOR, 1, 0, &d, &dl));
}

/* put new text in SYSTEM.CNF (sector 23, user data at +0x18) and set the file size recorded in
 * its directory entry */
static uint8_t *with_cnf_n(const char *txt, uint32_t fsize) {
    uint8_t *img = copy_emb();
    memset(img + 23 * 0x930 + 0x18, 0, 2048);
    memcpy(img + 23 * 0x930 + 0x18, txt, strlen(txt));
    uint8_t *dir = img + 22 * 0x930 + 0x18;
    for (uint32_t p = 0; dir[p]; p += dir[p])
        if (!memcmp(dir + p + 0x21, "SYSTEM.CNF", 10)) {
            for (int i = 0; i < 4; i++) { dir[p + 10 + i] = (uint8_t)(fsize >> (8 * i)); dir[p + 17 - i] = (uint8_t)(fsize >> (8 * i)); }
        }
    return img;
}
static uint8_t *with_cnf(const char *txt) { return with_cnf_n(txt, 2048); }

static void test_validate(void) {
    assert(mh_menu_validate_blob(emb, emb_n, MH_MENU_SECTOR, NULL, 0));
    char err[256];
    assert(mh_menu_validate_blob(emb, emb_n, MH_MENU_SECTOR, err, sizeof err) && err[0] == 0);
    const char *e_count = "The menu must have 1 to 93 complete RAW sectors for this firmware.";
    expect_err(NULL, 0, MH_MENU_SECTOR, e_count);
    expect_err(emb, 0, MH_MENU_SECTOR, e_count);
    expect_err(emb, emb_n - 1, MH_MENU_SECTOR, e_count);
    expect_err(emb, emb_n, 2048, e_count);
    uint8_t *big = calloc(94, MH_MENU_SECTOR);
    memcpy(big, emb, emb_n);
    expect_err(big, 94 * MH_MENU_SECTOR, MH_MENU_SECTOR, e_count);
    free(big);
    /* PSXRAW slots: pad every sector to 0xA00 with zeros */
    uint8_t *raw = calloc(93, MH_MENU_SLOT);
    for (int i = 0; i < 93; i++) memcpy(raw + (size_t)i * MH_MENU_SLOT, emb + (size_t)i * MH_MENU_SECTOR, MH_MENU_SECTOR);
    assert(mh_menu_validate_blob(raw, 93 * MH_MENU_SLOT, MH_MENU_SLOT, NULL, 0));
    raw[5 * MH_MENU_SLOT + 0x935] = 1;
    expect_err(raw, 93 * MH_MENU_SLOT, MH_MENU_SLOT, "The menu's PSXRAW padding is not zeroed.");
    free(raw);

    const char *e_raw = "The menu has an invalid RAW CD-ROM sector.";
    uint8_t *img = copy_emb();
    img[40 * 0x930] = 1;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_raw);
    memcpy(img, emb, emb_n); img[40 * 0x930 + 5] = 0;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_raw);
    memcpy(img, emb, emb_n); img[40 * 0x930 + 0xb] = 1;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_raw);
    memcpy(img, emb, emb_n); img[40 * 0x930 + 0xf] = 0;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_raw);
    memcpy(img, emb, emb_n); img[40 * 0x930 + 0x14] ^= 0x55;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_raw);
    /* a valid sector never needs a good EDC for the validator */
    memcpy(img, emb, emb_n); img[40 * 0x930 + 0x100] ^= 0x55;
    assert(mh_menu_validate_blob(img, emb_n, MH_MENU_SECTOR, NULL, 0));

    const char *e_pvd = "The menu has no valid ISO9660 PVD at sector 16.";
    memcpy(img, emb, emb_n); img[16 * 0x930 + 0x18] = 2;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_pvd);
    memcpy(img, emb, emb_n); img[16 * 0x930 + 0x19] = 'X';
    expect_err(img, emb_n, MH_MENU_SECTOR, e_pvd);
    memcpy(img, emb, emb_n); img[16 * 0x930 + 0x18 + 6] = 2;
    expect_err(img, emb_n, MH_MENU_SECTOR, e_pvd);
    expect_err(emb, 16 * MH_MENU_SECTOR, MH_MENU_SECTOR, e_pvd); /* too short: the original read past the end */

    const uint32_t pv = 16 * 0x930 + 0x18;
    memcpy(img, emb, emb_n); img[pv + 0x9c] = 0x21;
    expect_err(img, emb_n, MH_MENU_SECTOR, "The menu's ISO9660 root directory is invalid.");
    memcpy(img, emb, emb_n); img[pv + 0x9e] = 93;
    expect_err(img, emb_n, MH_MENU_SECTOR, "The ISO9660 root directory points outside the menu.");
    memcpy(img, emb, emb_n); memset(img + pv + 0xa6, 0, 4);
    expect_err(img, emb_n, MH_MENU_SECTOR, "The ISO9660 root directory points outside the menu.");
    memcpy(img, emb, emb_n); img[pv + 0xa8] = 0x05; /* 0x50800 bytes: more than 8 sectors */
    expect_err(img, emb_n, MH_MENU_SECTOR, "The ISO9660 root directory exceeds the menu's safe limit.");
    memcpy(img, emb, emb_n); img[pv + 0x9e] = 90; img[pv + 0xa7] = 0x30; /* 6 sectors starting at 90: past the end */
    expect_err(img, emb_n, MH_MENU_SECTOR, "The ISO9660 root directory exceeds the menu's safe limit.");

    const char *e_cnf = "SYSTEM.CNF was not found in the menu root.";
    memcpy(img, emb, emb_n); img[22 * 0x930 + 0x18 + 0x60 + 0x21] = 'X'; /* renames the third entry */
    free(img);
    img = copy_emb();
    {   /* find the SYSTEM.CNF entry and rename it */
        uint8_t *dir = img + 22 * 0x930 + 0x18;
        for (uint32_t p = 0; dir[p];) { if (!memcmp(dir + p + 0x21, "SYSTEM.CNF", 10)) dir[p + 0x21] = 'X'; p += dir[p]; }
    }
    expect_err(img, emb_n, MH_MENU_SECTOR, e_cnf);

    const char *e_boot = "The menu's SYSTEM.CNF has no valid BOOT=cdrom entry.";
    const char *e_exe = "The executable named by SYSTEM.CNF is not a valid PS-X EXE.";
    struct { const char *txt; const char *err; } cases[] = {
        {"BOOT=cdrom:\\SCES_313.37;1\r\nTCB=4\r\n", NULL},
        {"boot = CDROM:SCES_313.37;1\n", NULL},
        {"  \t\r\n\r\nTCB=4\r\n  BOOT2 =  cdrom:\\\\SCES_313.37;1  \t\r\n", NULL},
        {"BOOT=cdrom:/SCES_313.37;1", NULL},
        {"BOOT=cdrom:\\DIR\\SUB\\sces_313.37;1\n", NULL},
        {"TCB=4\nBOOT=cdrom0:\\SCES_313.37;1\nBOOT=cdrom:\\SCES_313.37;1\n", NULL}, /* first BOOT line is not cdrom: */
        {"BOOT=cdrom:\\SCES_313.37\r\n", e_exe},          /* the lookup needs the exact ";1" */
        {"BOOT=cdrom:\\PICO.DAT\r\n", e_exe},               /* exists but is not a PS-X EXE */
        {"BOOT=cdrom:\\NOFILE.EXE;1\r\n", e_exe},
        {"BOOT=cdrom:\r\n", e_boot},
        {"BOOT=cdrom:\\\r\n", e_boot},
        {"BOOT=\r\n", e_boot},
        {"BOOT\r\n", e_boot},
        {"BOT=cdrom:\\SCES_313.37;1\r\n", e_boot},
        {"BOOT=cdrom:\\SCES\x01_313.37;1\r\n", e_boot},      /* control character in the name */
        {"BOOT=cdrom0:\\SCES_313.37;1\r\n", e_boot},
        {"", e_boot},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        free(img);
        img = with_cnf(cases[i].txt);
        /* keep the directory entry size at least as large as the text */
        if (cases[i].err) expect_err(img, emb_n, MH_MENU_SECTOR, cases[i].err);
        else if (!mh_menu_validate_blob(img, emb_n, MH_MENU_SECTOR, err, sizeof err)) { fprintf(stderr, "case %zu: %s\n", i, err); abort(); }
    }
    /* the name limit: 127 characters pass the parser (then fail the lookup), 128 fail it */
    char longtxt[300];
    for (int n = 126; n <= 128; n++) {
        char nm[200];
        memset(nm, 'A', n); nm[n] = 0;
        snprintf(longtxt, sizeof longtxt, "BOOT=cdrom:\\%s\r\n", nm);
        free(img);
        img = with_cnf(longtxt);
        expect_err(img, emb_n, MH_MENU_SECTOR, n < 128 ? e_exe : e_boot);
    }
    /* the SYSTEM.CNF directory size limits how much is read: 60 bytes, a BOOT line after that is ignored */
    free(img);
    img = with_cnf_n("TCB=4\r\nSTACK=801FFFF0\r\nEVENT=10\r\nXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\r\nBOOT=cdrom:\\SCES_313.37;1\r\n", 60);
    expect_err(img, emb_n, MH_MENU_SECTOR, e_boot);
    /* executable magic */
    free(img);
    img = copy_emb();
    img[24 * 0x930 + 0x18 + 3] = 'x';
    expect_err(img, emb_n, MH_MENU_SECTOR, e_exe);
    free(img);
}

int main(void) {
    if (!mh_read_entire_file(MENU, &emb, &emb_n)) { puts("skip: run from the repo root"); return 0; }
    assert(emb_n == 218736);
    test_defaults_record();
    test_patch();
    test_iso();
    test_validate();
    test_files();
    free(emb);
    puts("test_menu: ok");
    return 0;
}
