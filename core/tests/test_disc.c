/* disc module tests: serial recovery (synthetic ISO images), CUE parsing and writing, the
 * project model, the embedded database and real fixtures (skipped when absent). The slow
 * Redump verification of full discs is in test_disc_redump.c. */
#include <time.h>
#include "disc_util.inc"
#include "../src/disc/disc_internal.h"

/* ======================================================================== serial text */

static void test_canonical(void)
{
    char o[16];
    CHECK(mh_canonical_serial("SLUS-00969", o)); CHECK_STR(o, "SLUS-00969");
    CHECK(mh_canonical_serial("slus-00969", o)); CHECK_STR(o, "SLUS-00969");
    CHECK(mh_canonical_serial("LSP-010170", o)); CHECK_STR(o, "LSP-010170");   /* 6 digits */
    CHECK(mh_canonical_serial("DTL-S2170", o) == false);                       /* needs 5 digits */
    CHECK(mh_canonical_serial("SLUS-00842A", o)); CHECK_STR(o, "SLUS-00842A"); /* trailing letter */
    CHECK(mh_canonical_serial("SLUS-00969-SLUS", o)); CHECK_STR(o, "SLUS-00969");
    CHECK(mh_canonical_serial("SLUS-00969-SLU", o));                           /* the repeat may stop at 3 letters */
    CHECK(mh_canonical_serial("SLUS-00969-SLUX", o) == false);
    CHECK(mh_canonical_serial("SLE-00969-SLE", o));                            /* 3 letter prefix */
    CHECK(mh_canonical_serial("SLUS-00969-SLUS--", o));                        /* trailing dashes after the repeat */
    CHECK(mh_canonical_serial("SLUS-00969--", o) == false);
    CHECK(mh_canonical_serial("SLUS-00969-", o) == false);
    CHECK(mh_canonical_serial("SLUS-00969-SL", o) == false);
    CHECK(mh_canonical_serial("SLUS-009690000", o) == false);                  /* too many digits */
    CHECK(mh_canonical_serial("SLUS-0096", o) == false);
    CHECK(mh_canonical_serial("SL-00969", o) == false);                        /* prefix too short */
    CHECK(mh_canonical_serial("SLUSX-00969", o) == false);                     /* prefix too long */
    CHECK(mh_canonical_serial("", o) == false);
    CHECK(mh_canonical_serial("-00969", o) == false);
    CHECK(mh_canonical_serial("SLUS_00969", o) == false);
}

static void test_parse_filename(void)
{
    char raw[32], c[16];
    CHECK(mh_psserial_parse_filename("SLUS_009.69;1", raw, c)); CHECK_STR(raw, "SLUS_009.69"); CHECK_STR(c, "SLUS-00969");
    CHECK(mh_psserial_parse_filename("slus_009.69", raw, c)); CHECK_STR(c, "SLUS-00969");
    CHECK(mh_psserial_parse_filename("SLUS-00969.EXE", raw, c)); CHECK_STR(raw, "SLUS-00969");
    CHECK(mh_psserial_parse_filename("SCES_123.45.exe;1", raw, c)); CHECK_STR(c, "SCES-12345");
    CHECK(mh_psserial_parse_filename("ESPM_123.45", raw, c));        /* first table entry */
    CHECK(mh_psserial_parse_filename("SPUS_123.45", raw, c));        /* last table entry */
    CHECK(mh_psserial_parse_filename("SLXX_123.45", raw, c) == false);
    CHECK(mh_psserial_parse_filename("SLUS_009.69X", raw, c) == false);  /* must fill the name */
    CHECK(mh_psserial_parse_filename("SLUS_009.6", raw, c) == false);
    CHECK(mh_psserial_parse_filename("SLUS_00969", raw, c) == false);
    CHECK(mh_psserial_parse_filename("PSX.EXE", raw, c) == false);
    CHECK(mh_psserial_parse_filename("", raw, c) == false);
    CHECK(mh_psserial_parse_filename(NULL, raw, c) == false);
}

static void test_status_texts(void)
{
    CHECK_STR(mh_disc_status_text(1), "OK");
    CHECK_STR(mh_disc_status_text(2), "Review");
    CHECK_STR(mh_disc_status_text(3), "Fixable");
    CHECK_STR(mh_disc_status_text(4), "Error");
    CHECK_STR(mh_disc_status_text(0), "Empty");
    CHECK_STR(mh_disc_status_text(9), "Empty");
    CHECK_STR(mh_psserial_status_text(0), "NOT_FOUND");
    CHECK_STR(mh_psserial_status_text(1), "STRONG_INTERNAL");
    CHECK_STR(mh_psserial_status_text(2), "PROBABLE");
    CHECK_STR(mh_psserial_status_text(3), "AMBIGUOUS");
    CHECK_STR(mh_psserial_status_text(4), "IO_ERROR");
    CHECK_STR(mh_psserial_status_text(5), "INVALID_IMAGE");
    CHECK_STR(mh_psserial_status_text(6), "TRACKSET_HASH_CONFIRMED");
    CHECK_STR(mh_psserial_status_text(77), "NOT_FOUND");
    CHECK_STR(mh_psserial_source_text(0), "NONE");
    CHECK_STR(mh_psserial_source_text(1), "SYSTEM_CNF");
    CHECK_STR(mh_psserial_source_text(2), "EXECUTABLE_NAME");
    CHECK_STR(mh_psserial_source_text(3), "EXECUTABLE_CONTENT");
    CHECK_STR(mh_psserial_source_text(4), "HASH_TRACKSET");
}

/* ============================================================== serial from ISO images */

static void test_serial_images(void)
{
    char p[MH_PATH_MAX];
    MhPsSerial r;
    uint8_t *exe = make_exe(4000, NULL);

    /* 1. SYSTEM.CNF -> BOOT target with a serial name: STRONG_INTERNAL */
    {
        const char *cnf = "BOOT = cdrom:\\SLUS_009.69;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 4, 0);
        iso_write(&iso, "a.bin");
        tmp_path("a.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_STRONG);
        CHECK_STR(r.serial, "SLUS-00969"); CHECK_STR(r.token, "SLUS_009.69"); CHECK_STR(r.path, "SLUS_009.69");
        CHECK(r.source == MH_PSSRC_SYSTEM_CNF);
        CHECK(r.system_cnf_found == 1 && r.boot_path_found == 1 && r.boot_exe_valid == 1);
        CHECK(r.cand_count == 1 && r.needs_hash == 1);
        CHECK(r.iso_sectors == iso.sectors && r.file_sectors == iso.sectors && r.truncated == 0);
        CHECK_STR(r.message, "Serial extracted from the SYSTEM.CNF BOOT entry; use the trackset hash to confirm the physical revision.");
        CHECK_STR(mh_psserial_status_text(r.status), "STRONG_INTERNAL");
        {
            char s[16];
            CHECK(mh_psserial_extract_serial(s, p)); CHECK_STR(s, "SLUS-00969");
            CHECK(mh_cue_scan_serial_file(p, s, sizeof s)); CHECK_STR(s, "SLUS-00969");
        }
        iso_free(&iso);
    }
    /* 1b. same with mode 1 (0x10) and with a plain 2048 byte ISO */
    {
        const char *cnf = "boot=cdrom:SCES_123.45;1\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SCES_123.45;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x10, 2, 0);
        iso_write(&iso, "m1.bin");
        tmp_path("m1.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_STRONG); CHECK_STR(r.serial, "SCES-12345");
        iso_free(&iso);
        iso_build(&iso, f, 2, 2048, 0, 2, 0);
        iso_write(&iso, "cooked.iso");
        tmp_path("cooked.iso", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_STRONG); CHECK_STR(r.serial, "SCES-12345");
        iso_free(&iso);
    }
    /* 2. BOOT target with a non serial name, serial inside the executable: PROBABLE */
    {
        const char *cnf = "BOOT = \"cdrom:\\GAME\\MAIN.EXE;1\"\r\n";
        uint8_t *e2 = make_exe(3000, "xx SLPS_012.34 yy");
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"MAIN.EXE;1", e2, 3000, "GAME"}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "b.bin");
        tmp_path("b.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_PROBABLE);
        CHECK_STR(r.serial, "SLPS-01234"); CHECK(r.source == MH_PSSRC_EXE_CONTENT);
        CHECK_STR(r.path, "GAME/MAIN.EXE");
        CHECK_STR(r.message, "Single serial found heuristically in the boot executable.");
        { char s[16]; CHECK(mh_psserial_extract_serial(s, p)); CHECK_STR(s, "SLPS-01234"); }
        iso_free(&iso); free(e2);
    }
    /* 2b. same executable but two different serials: AMBIGUO */
    {
        const char *cnf = "BOOT=cdrom:\\MAIN.EXE;1\r\n";
        uint8_t *e2 = make_exe(3000, "SLPS_012.34 and SLUS-00555");
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"MAIN.EXE;1", e2, 3000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "b2.bin");
        tmp_path("b2.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_AMBIGUOUS);
        CHECK(r.cand_count == 2); CHECK_STR(r.cand[0], "SLPS-01234"); CHECK_STR(r.cand[1], "SLUS-00555");
        CHECK_STR(r.message, "Multiple serials found in the boot executable.");
        { char s[16]; CHECK(!mh_psserial_extract_serial(s, p)); }
        iso_free(&iso); free(e2);
    }
    /* 2c. a serial glued to identifier characters is not a token */
    {
        const char *cnf = "BOOT=cdrom:\\MAIN.EXE;1\r\n";
        uint8_t *e2 = make_exe(3000, "XSLPS_012.34Y SLPS_012.340");
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"MAIN.EXE;1", e2, 3000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "b3.bin");
        tmp_path("b3.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_NOT_FOUND);
        iso_free(&iso); free(e2);
    }
    /* 3. conflicting BOOT lines: SYSTEM.CNF ignored, executables decide */
    {
        const char *cnf = "BOOT = cdrom:\\SLUS_009.69;1\r\nBOOT2 = cdrom:\\SLUS_111.11;1\r\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "c.bin");
        tmp_path("c.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_PROBABLE);   /* only one exe has a serial name */
        CHECK(r.source == MH_PSSRC_EXE_NAME); CHECK_STR(r.serial, "SLUS-00969");
        CHECK_STR(r.message, "PS-X EXE executable with a single serial name, no SYSTEM.CNF.");
        iso_free(&iso);
    }
    /* 3b. same value twice is fine; BOOT2 accepted */
    {
        const char *cnf = "BOOT2 = cdrom:\\SLUS_009.69;1\r\nBOOT=\\SLUS_009.69\r\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "c2.bin");
        tmp_path("c2.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_STRONG);
        iso_free(&iso);
    }
    /* 4. no SYSTEM.CNF: PSX.EXE content */
    {
        uint8_t *e2 = make_exe(3000, "SCUS_944.55");
        IsoFile f[1] = {{"PSX.EXE;1", e2, 3000, NULL}};
        Iso iso;
        iso_build(&iso, f, 1, 2352, 0x18, 2, 0);
        iso_write(&iso, "d.bin");
        tmp_path("d.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_PROBABLE);
        CHECK_STR(r.serial, "SCUS-94455"); CHECK(r.source == MH_PSSRC_EXE_CONTENT);
        CHECK_STR(r.message, "Valid PSX.EXE; single internal serial found heuristically.");
        iso_free(&iso); free(e2);
    }
    /* 4b. PSX.EXE without a serial */
    {
        IsoFile f[1] = {{"PSX.EXE;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 1, 2352, 0x18, 2, 0);
        iso_write(&iso, "d2.bin");
        tmp_path("d2.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_NOT_FOUND);
        CHECK_STR(r.message, "Valid PSX.EXE found, but it provides no serial; resolve by trackset hash.");
        iso_free(&iso);
    }
    /* 5. nothing identifiable */
    {
        const char *txt = "hello";
        IsoFile f[1] = {{"README.TXT;1", txt, 5, NULL}};
        Iso iso;
        iso_build(&iso, f, 1, 2352, 0x18, 2, 0);
        iso_write(&iso, "e.bin");
        tmp_path("e.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_NOT_FOUND);
        CHECK_STR(r.message, "SYSTEM.CNF and an identifiable PS-X EXE executable were not found; resolve by trackset hash.");
        iso_free(&iso);
    }
    /* 6. several executables, no SYSTEM.CNF: the contents are scanned (PROBABLE / AMBIGUO) */
    {
        uint8_t *e1 = make_exe(3000, "SLUS_000.01"), *e2 = make_exe(3000, "SLUS_000.02");
        IsoFile f[2] = {{"A.EXE;1", e1, 3000, NULL}, {"B.EXE;1", e2, 3000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "f.bin");
        tmp_path("f.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_AMBIGUOUS);
        CHECK_STR(r.message, "Multiple serials found in PS-X EXE executables; boot not confirmed.");
        iso_free(&iso);
        f[1].data = e1;                                   /* same serial in both: unique */
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "f2.bin");
        tmp_path("f2.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_PROBABLE);
        CHECK_STR(r.message, "Single serial found in a PS-X EXE executable; boot not confirmed.");
        iso_free(&iso); free(e1); free(e2);
    }
    /* 7. declared volume larger than the file: truncated */
    {
        const char *cnf = "BOOT = cdrom:\\SLUS_009.69;1\r\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 1);
        iso_write(&iso, "g.bin");
        tmp_path("g.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_STRONG);
        CHECK(r.truncated == 1 && r.iso_sectors == iso.sectors + 100 && r.file_sectors == iso.sectors);
        iso_free(&iso);
    }
    /* 8. a sub directory: nested SYSTEM.CNF is used when no root one exists; nested exe path */
    {
        const char *cnf = "BOOT=cdrom:\\BIN\\SLUS_009.69;1\r\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, "BIN"}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write(&iso, "h.bin");
        tmp_path("h.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_STRONG); CHECK_STR(r.path, "BIN/SLUS_009.69");
        iso_free(&iso);
    }
    /* 9. raw V2 slot (2560 byte slots at a block offset) */
    {
        const char *cnf = "BOOT = cdrom:\\SLUS_009.69;1\r\n";
        IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, NULL}};
        Iso iso;
        iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
        iso_write_raw_slots(&iso, "raw.img", 7);
        tmp_path("raw.img", p);
        CHECK(mh_psserial_extract_raw(&r, p, 7, iso.sectors) == MH_PSS_STRONG);
        CHECK_STR(r.serial, "SLUS-00969");
        CHECK(mh_psserial_extract_raw(&r, p, 0, iso.sectors) == MH_PSS_BAD_IMAGE);   /* wrong offset */
        iso_free(&iso);
    }
    /* 10. errors */
    {
        tmp_path("missing.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_IO_ERROR);
        CHECK_STR(r.message, "Could not open the image.");
        CHECK(mh_psserial_extract(&r, "") == MH_PSS_IO_ERROR);
        CHECK_STR(r.message, "The image path is empty.");
        CHECK(mh_psserial_extract(&r, NULL) == MH_PSS_IO_ERROR);
        put_zero_bin("zeros.bin", 2352 * 40);
        tmp_path("zeros.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_BAD_IMAGE);
        CHECK_STR(r.message, "Invalid ISO9660 PVD/layout, or the root directory is outside the image.");
        put_zero_bin("tiny.bin", 100);
        tmp_path("tiny.bin", p);
        CHECK(mh_psserial_extract(&r, p) == MH_PSS_BAD_IMAGE);
        {
            char s[16];
            CHECK(!mh_cue_scan_serial_file(p, s, sizeof s) && s[0] == 0);
            CHECK(!mh_cue_scan_serial_file(p, NULL, 16));
        }
    }
    free(exe);
}

static void test_recover(void)
{
    char p[MH_PATH_MAX], msg[512];
    const char *cnf = "BOOT = cdrom:\\SLUS_009.69;1\r\n";
    uint8_t *exe = make_exe(4000, NULL);
    IsoFile f[2] = {{"SYSTEM.CNF;1", cnf, (uint32_t)strlen(cnf), NULL}, {"SLUS_009.69;1", exe, 4000, NULL}};
    Iso iso;
    MhProject *proj = mh_calloc(sizeof *proj);
    MhDisc *d = mh_calloc(sizeof *d);
    mh_project_init(proj);
    iso_build(&iso, f, 2, 2352, 0x18, 2, 0);
    iso_write(&iso, "r.bin");
    iso_write_raw_slots(&iso, "r.img", 5);

    d->kind = MH_DISC_KIND_CUE;
    CHECK(mh_disc_recover_serial(d, NULL, proj, false, msg, sizeof msg) == MH_RECOVER_UNAVAILABLE);
    CHECK_STR(msg, "The original CUE/BIN is not available to identify the serial.");
    tmp_path("r.bin", d->files[0].path);
    d->nfiles = 1;
    CHECK(mh_disc_recover_serial(d, NULL, proj, true, msg, sizeof msg) == MH_RECOVER_OK);
    CHECK_STR(msg, "Proven internal serial recovered. Apply the changes to the card.");
    CHECK_STR(d->serial, "SLUS-00969"); CHECK(d->flags & MH_DF_SERIAL); CHECK(proj->dirty);
    CHECK(d->ps.status == MH_PSS_STRONG);
    CHECK(mh_disc_recover_serial(d, NULL, proj, false, msg, sizeof msg) == MH_RECOVER_SKIPPED);   /* has a serial */

    memset(d, 0, sizeof *d);
    d->kind = MH_DISC_KIND_V2_IMAGE;
    CHECK(mh_disc_recover_serial(d, NULL, NULL, false, msg, sizeof msg) == MH_RECOVER_UNAVAILABLE);
    CHECK_STR(msg, "The RAW record has no valid location to read from.");
    d->v2_raw_lba512 = 5; d->total_sectors = iso.sectors;
    CHECK(mh_disc_recover_serial(d, NULL, NULL, false, msg, sizeof msg) == MH_RECOVER_UNAVAILABLE);
    CHECK_STR(msg, "The disc's RAW source is not available.");
    CHECK(mh_disc_recover_serial(d, NULL, NULL, true, msg, sizeof msg) == MH_RECOVER_UNAVAILABLE && msg[0] == 0);
    tmp_path("r.img", d->source_path);
    CHECK(mh_disc_recover_serial(d, NULL, NULL, false, msg, sizeof msg) == MH_RECOVER_OK);
    CHECK_STR(msg, "Internal serial recovered: SLUS-00969. The game's RAW was preserved. Apply the changes to write the identity to the card.");
    /* kind 4 uses the device path given by the caller */
    memset(d, 0, sizeof *d);
    d->kind = MH_DISC_KIND_V2_DEVICE; d->v2_raw_lba512 = 5; d->total_sectors = iso.sectors;
    tmp_path("r.img", p);
    CHECK(mh_disc_recover_serial(d, p, NULL, true, msg, sizeof msg) == MH_RECOVER_OK && !strcmp(d->serial, "SLUS-00969"));
    /* wrong place: nothing proven */
    memset(d, 0, sizeof *d);
    d->kind = MH_DISC_KIND_V2_IMAGE; d->v2_raw_lba512 = 1; d->total_sectors = iso.sectors;
    tmp_path("r.img", d->source_path);
    CHECK(mh_disc_recover_serial(d, NULL, NULL, false, msg, sizeof msg) == MH_RECOVER_NOT_FOUND);
    CHECK_STR(msg, "No proven internal serial was found. No data was changed.\r\n\r\nInvalid ISO9660 PVD/layout, or the root directory is outside the image.");
    CHECK(d->serial[0] == 0);
    d->kind = 3;
    CHECK(mh_disc_recover_serial(d, NULL, NULL, false, msg, sizeof msg) == MH_RECOVER_UNAVAILABLE);
    CHECK_STR(msg, "This source does not allow recovering the serial.");
    CHECK(mh_disc_recover_serial(NULL, NULL, NULL, false, msg, sizeof msg) == MH_RECOVER_SKIPPED);
    iso_free(&iso);
    free(exe);
    mh_disc_free(d);
    mh_project_clear(proj);
    free(proj);
}

/* crafted directory loops must terminate quickly */
static void test_walk_bounds(void)
{
    char p[MH_PATH_MAX];
    MhPsSerial r;
    Iso iso;
    IsoFile f[1] = {{"README.TXT;1", "x", 1, NULL}};
    uint32_t pos;
    clock_t t0;
    iso_build(&iso, f, 1, 2352, 0x18, 2, 0);
    /* root (17) and the sub directory (18) hold ~60 entries each, all pointing to 17 or 18 */
    for (uint32_t dir = 17; dir <= 18; dir++) {
        uint8_t *d = iso_user(&iso, dir);
        pos = 0;
        pos += dirrec(d + pos, "\0", 1, dir, 2048, 1);
        pos += dirrec(d + pos, "\1", 1, 17, 2048, 1);
        for (int i = 0; i < 60 && pos + 40 < 2048; i++) {
            char nm[8];
            snprintf(nm, sizeof nm, "D%d", i);
            pos += dirrec(d + pos, nm, strlen(nm), (i & 1) ? 17 : 18, 2048, 1);
        }
    }
    iso_write(&iso, "loop.bin");
    tmp_path("loop.bin", p);
    t0 = clock();
    (void)mh_psserial_extract(&r, p);
    CHECK(r.status != MH_PSS_STRONG);
    CHECK((double)(clock() - t0) / CLOCKS_PER_SEC < 1.0);
    iso_free(&iso);
}

/* ====================================================================== CUE parsing */

static void test_msf(void)
{
    uint32_t f;
    CHECK(mh_cue_msf("00:00:00", &f) && f == 0);
    CHECK(mh_cue_msf("00:02:00", &f) && f == 150);
    CHECK(mh_cue_msf("60:56:63", &f) && f == (60 * 60 + 56) * 75 + 63);
    CHECK(mh_cue_msf("1:2:3 junk", &f) && f == 75 * 62 + 3);       /* trailing text is ignored */
    CHECK(!mh_cue_msf("00:60:00", &f));
    CHECK(!mh_cue_msf("00:00:75", &f));
    CHECK(!mh_cue_msf("00:00", &f));
    CHECK(!mh_cue_msf("a:00:00", &f));
    CHECK(!mh_cue_msf("", &f));
    CHECK(!mh_cue_msf("99999999999:00:00", &f));                    /* overflow */
}

static const char *note_has(const MhDisc *d, const char *needle) { return strstr(d->notes, needle); }

static void test_cue_valid(void)
{
    MhDisc *d;
    char p[MH_PATH_MAX], q[MH_PATH_MAX];
    put_zero_bin("g1.bin", 2352 * 1000);
    put_zero_bin("g2.bin", 2352 * 300);
    put_zero_bin("g3.bin", 2352 * 200);
    put_text("ok.cue",
             "REM GENRE test\r\nFILE \"g1.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
             "FILE \"g2.bin\" BINARY\r\n  TRACK 02 AUDIO\r\n    FLAGS DCP\r\n    ISRC ABC\r\n    PREGAP 00:02:00\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n"
             "FILE \"g3.bin\" BINARY\r\n  TRACK 03 AUDIO\r\n    INDEX 01 00:00:00\r\n");
    d = parse_tmp_cue("ok.cue");
    CHECK(d != NULL);
    CHECK(d->kind == MH_DISC_KIND_CUE);
    CHECK(d->nfiles == 3 && d->ntracks == 3);
    CHECK(d->total_sectors == 1500 && d->total_bytes == 1500ull * 2352);
    CHECK((d->flags & (MH_DF_STRUCT_OK | MH_DF_INVALID | MH_DF_MISSING_BIN | MH_DF_BAD_ALIGN)) == MH_DF_STRUCT_OK);
    CHECK(d->files[1].start == 1000 && d->files[1].sectors == 300 && d->files[2].start == 1300);
    CHECK(d->tracks[0].kind == 2 && d->tracks[0].mode == 2 && d->tracks[0].start == 0 && d->tracks[0].length == 1000 + 150);
    CHECK(d->tracks[1].kind == 1 && d->tracks[1].pregap == 150 && d->tracks[1].index0 == 0 && d->tracks[1].index1 == 150);
    CHECK(d->tracks[1].start == 1150 && d->tracks[1].length == 1300 - 1150);
    CHECK((d->tracks[1].flags & (MH_TRACK_DCP | MH_TRACK_ISRC | MH_TRACK_HAS_INDEX0)) == (MH_TRACK_DCP | MH_TRACK_ISRC | MH_TRACK_HAS_INDEX0));
    CHECK(d->tracks[2].pregap == 0 && d->tracks[2].start == 1300 && d->tracks[2].length == 200);
    CHECK_STR(d->name, "OK");
    /* ISO is absent: serial unknown, so a note and status Review */
    CHECK(d->serial[0] == 0 && d->status == MH_DISC_STATUS_NOTE);
    CHECK_STR(d->notes, "Serial not identified automatically.");
    CHECK(d->db_index == 0xffffffffu);
    CHECK(d->ps.status == MH_PSS_BAD_IMAGE);

    /* write_corrected then parse again: same tables */
    tmp_path("fixed.cue", p);
    CHECK(mh_cue_write_corrected(d, p));
    {
        MhDisc *e = mh_cue_parse(p);
        CHECK(e->ntracks == 3 && e->nfiles == 3);
        for (int i = 0; i < 3; i++) {
            CHECK(e->tracks[i].kind == d->tracks[i].kind && e->tracks[i].mode == d->tracks[i].mode);
            CHECK(e->tracks[i].start == d->tracks[i].start && e->tracks[i].length == d->tracks[i].length);
            CHECK(e->tracks[i].pregap == d->tracks[i].pregap);
        }
        mh_disc_free(e);
    }
    {
        char *text;
        CHECK(mh_cue_read_text(p, &text));
        CHECK(strstr(text, "FILE \"g1.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n") == text);
        CHECK(strstr(text, "  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n"));
        free(text);
    }
    CHECK(mh_cue_count_files(p) == 3);
    tmp_path("ok.cue", q);
    CHECK(mh_cue_count_files(q) == 3);

    /* rewrite_files: replace the 2nd FILE line, keep the others */
    {
        char dst[MH_PATH_MAX], text_path[MH_PATH_MAX];
        const char *paths[3] = {NULL, "/some/other/dir/new name.bin", ""};
        char *text;
        tmp_path("rw.cue", dst);
        CHECK(mh_cue_rewrite_files(q, dst, paths, 3));
        CHECK(mh_cue_read_text(dst, &text));
        CHECK(strstr(text, "FILE \"g1.bin\" BINARY\r\n"));
        CHECK(strstr(text, "FILE \"/some/other/dir/new name.bin\" BINARY\r\n"));
        CHECK(strstr(text, "FILE \"g3.bin\" BINARY\r\n"));
        CHECK(strstr(text, "REM GENRE test\r\n") == text);
        CHECK(strstr(text, "    FLAGS DCP\r\n"));            /* untouched lines keep their indentation */
        free(text);
        /* the BIN in the same folder as the destination is written with its base name */
        paths[1] = text_path;
        tmp_path("g2.bin", text_path);
        CHECK(mh_cue_rewrite_files(q, dst, paths, 3));
        CHECK(mh_cue_read_text(dst, &text));
        CHECK(strstr(text, "FILE \"g2.bin\" BINARY\r\n"));
        free(text);
    }
    mh_disc_free(d);
}

static void test_cue_text(void)
{
    MhDisc *d;
    char *t;
    char p[MH_PATH_MAX];
    put_zero_bin("é.bin", 2352 * 20);
    /* UTF-8 name, mixed line endings, tabs, lowercase keywords, unquoted FILE name */
    put_text("u8.cue", "file \"é.bin\" binary\n\ttrack 01 mode2/2352\r\r\n  index 01 00:00:00  \n");
    d = parse_tmp_cue("u8.cue");
    CHECK(d->ntracks == 1 && (d->flags & MH_DF_STRUCT_OK));
    mh_disc_free(d);
    /* invalid UTF-8 (Windows-1252 byte E9) decodes as e-acute */
    {
        static const char cp[] = "FILE \"\xe9.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
        put_file("cp.cue", cp, sizeof cp - 1);
    }
    tmp_path("cp.cue", p);
    CHECK(mh_cue_read_text(p, &t));
    CHECK(strstr(t, "\xc3\xa9.bin") != NULL);
    free(t);
    d = parse_tmp_cue("cp.cue");
    CHECK(d->ntracks == 1 && (d->flags & MH_DF_STRUCT_OK));     /* found as the UTF-8 file name */
    mh_disc_free(d);
    /* unquoted file name stops at a blank */
    put_zero_bin("plain.bin", 2352 * 20);
    put_text("unq.cue", "FILE plain.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("unq.cue");
    CHECK(d->nfiles == 1 && d->ntracks == 1 && (d->flags & MH_DF_STRUCT_OK));
    mh_disc_free(d);
    /* BIN found case-insensitively and with a Windows path separator */
    put_text("ci.cue", "FILE \"PLAIN.BIN\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("ci.cue");
    CHECK((d->flags & MH_DF_STRUCT_OK) && d->files[0].size == 2352 * 20);
    mh_disc_free(d);
    put_text("bs.cue", "FILE \"C:\\games\\plain.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("bs.cue");
    CHECK((d->flags & MH_DF_STRUCT_OK));    /* absolute Windows path: falls back to the file name next to the CUE */
    mh_disc_free(d);
    /* unreadable CUE */
    tmp_path("nothere.cue", p);
    d = mh_cue_parse(p);
    CHECK(d->flags == 0 && d->status == MH_DISC_STATUS_ERROR);
    CHECK_STR(d->notes, "Could not open or decode the CUE.");
    mh_disc_free(d);
    CHECK(mh_cue_count_files(p) == 0);
}

static void test_cue_errors(void)
{
    MhDisc *d;
    put_zero_bin("a.bin", 2352 * 50);
    put_zero_bin("b.bin", 2352 * 50);
    put_zero_bin("odd.bin", 2352 * 50 + 1);
    put_zero_bin("empty.bin", 0);

    put_text("e0.cue", "\n");   /* an empty file fails to decode (test_disc_audit.c) */
    d = parse_tmp_cue("e0.cue");
    CHECK_STR(d->notes, "CUE has no valid FILE/TRACK."); CHECK(d->status == MH_DISC_STATUS_ERROR); mh_disc_free(d);

    put_text("e1.cue", "TRACK 01 MODE2/2352\n");
    d = parse_tmp_cue("e1.cue");
    CHECK(note_has(d, "Invalid or out-of-order TRACK line.")); mh_disc_free(d);

    put_text("e2.cue", "FILE \"a.bin\" BINARY\nTRACK 100 MODE2/2352\n");
    d = parse_tmp_cue("e2.cue");
    CHECK(note_has(d, "Invalid or out-of-order TRACK line.")); mh_disc_free(d);
    put_text("e2b.cue", "FILE \"a.bin\" BINARY\nTRACK 00 MODE2/2352\n");
    d = parse_tmp_cue("e2b.cue");
    CHECK(note_has(d, "Invalid or out-of-order TRACK line.")); mh_disc_free(d);
    put_text("e2c.cue", "FILE \"a.bin\" BINARY\nTRACK x MODE2/2352\n");
    d = parse_tmp_cue("e2c.cue");
    CHECK(note_has(d, "Invalid or out-of-order TRACK line.")); mh_disc_free(d);

    put_text("e3.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2336\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e3.cue");
    CHECK(note_has(d, "Unsupported track mode. Use AUDIO, MODE1/2352 or MODE2/2352.")); CHECK(d->flags & MH_DF_INVALID); mh_disc_free(d);

    put_text("e4.cue", "FILE \"a.bin\" WAVE\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e4.cue");
    CHECK(note_has(d, "Only BINARY files are supported in this version.")); CHECK(d->flags & MH_DF_INVALID); mh_disc_free(d);

    put_text("e5.cue", "FILE\nTRACK 01 MODE2/2352\n");
    d = parse_tmp_cue("e5.cue");      /* "FILE" without a blank is not a FILE line: TRACK without FILE */
    CHECK(note_has(d, "Invalid or out-of-order TRACK line.")); mh_disc_free(d);
    put_text("e5b.cue", "FILE \"\" BINARY\nTRACK 01 MODE2/2352\n");
    d = parse_tmp_cue("e5b.cue");
    CHECK(note_has(d, "Invalid FILE line.")); mh_disc_free(d);
    put_text("e5c.cue", "FILE   \nTRACK 01 MODE2/2352\n");
    d = parse_tmp_cue("e5c.cue");     /* trimmed to "FILE": not a FILE line */
    CHECK(note_has(d, "Invalid or out-of-order TRACK line.")); mh_disc_free(d);

    put_text("e6.cue", "FILE \"a.bin\" BINARY\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e6.cue");
    CHECK(note_has(d, "INDEX without a valid TRACK.")); mh_disc_free(d);
    put_text("e6b.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00\n");
    d = parse_tmp_cue("e6b.cue");
    CHECK(note_has(d, "Invalid INDEX time.")); mh_disc_free(d);
    put_text("e6c.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX xx 00:00:00\n");
    d = parse_tmp_cue("e6c.cue");
    CHECK(note_has(d, "INDEX without a valid TRACK.")); mh_disc_free(d);

    put_text("e7.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nPREGAP bad\n");
    d = parse_tmp_cue("e7.cue");
    CHECK(note_has(d, "Invalid PREGAP.")); mh_disc_free(d);
    put_text("e7b.cue", "FILE \"a.bin\" BINARY\nPREGAP 00:02:00\n");
    d = parse_tmp_cue("e7b.cue");
    CHECK(note_has(d, "Invalid PREGAP.")); mh_disc_free(d);

    put_text("e8.cue", "FILE \"a.bin\" BINARY\nFLAGS DCP\n");
    d = parse_tmp_cue("e8.cue");
    CHECK(note_has(d, "FLAGS without a valid TRACK.")); mh_disc_free(d);
    put_text("e8b.cue", "FILE \"a.bin\" BINARY\nISRC ABC\n");
    d = parse_tmp_cue("e8b.cue");
    CHECK(note_has(d, "ISRC without a valid TRACK.")); mh_disc_free(d);

    put_text("e9.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nPOSTGAP 00:02:00\n");
    d = parse_tmp_cue("e9.cue");
    CHECK(note_has(d, "A virtual POSTGAP cannot be accepted safely.")); CHECK(d->flags & MH_DF_INCOMPLETE);
    CHECK(d->flags & MH_DF_STRUCT_OK);     /* a warning only */
    CHECK(d->status == MH_DISC_STATUS_ERROR);   /* ... but refresh_status treats 0x100 as an error */
    mh_disc_free(d);

    /* 100 FILE lines */
    {
        MhWbuf w;
        mh_wbuf_init(&w);
        for (int i = 0; i < 100; i++) mh_wbuf_puts(&w, "FILE \"a.bin\" BINARY\r\n");
        put_text("e10.cue", mh_wbuf_cstr(&w));
        mh_wbuf_free(&w);
        d = parse_tmp_cue("e10.cue");
        CHECK(note_has(d, "More than 99 BIN files in the CUE.")); CHECK(d->flags & MH_DF_INVALID); CHECK(d->nfiles == 99);
        mh_disc_free(d);
        /* exactly 99 is accepted by the line parser (and then reported as unused / duplicated) */
        mh_wbuf_init(&w);
        for (int i = 0; i < 99; i++) mh_wbuf_puts(&w, "FILE \"a.bin\" BINARY\r\n");
        put_text("e10b.cue", mh_wbuf_cstr(&w));
        mh_wbuf_free(&w);
        d = parse_tmp_cue("e10b.cue");
        CHECK(!note_has(d, "Mais de 99")); CHECK(d->nfiles == 99);
        mh_disc_free(d);
    }

    put_text("e11.cue", "FILE \"a.bin\" BINARY\nFILE \"b.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e11.cue");
    CHECK(note_has(d, "There is a FILE with no TRACK.")); mh_disc_free(d);

    put_text("e12.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nFILE \"A.BIN\" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e12.cue");
    CHECK(note_has(d, "The same BIN is declared more than once, so the layout is ambiguous.")); mh_disc_free(d);

    put_text("e13.cue", "FILE \"a.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e13.cue");
    CHECK(note_has(d, "The first track of a PlayStation disc must be a data track.")); mh_disc_free(d);

    put_text("e14.cue", "FILE \"nothere.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e14.cue");
    CHECK(note_has(d, "One or more referenced BINs were not found.")); CHECK(d->flags & MH_DF_MISSING_BIN);
    CHECK(d->status == MH_DISC_STATUS_FIXABLE); mh_disc_free(d);

    put_text("e15.cue", "FILE \"odd.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e15.cue");
    CHECK(note_has(d, "A BIN is not aligned to 2352-byte sectors.")); CHECK(d->flags & MH_DF_BAD_ALIGN);
    CHECK(d->status == MH_DISC_STATUS_ERROR); mh_disc_free(d);
    put_text("e15b.cue", "FILE \"empty.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e15b.cue");
    CHECK(note_has(d, "A BIN is not aligned to 2352-byte sectors.")); mh_disc_free(d);

    put_text("e16.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 03 AUDIO\nINDEX 01 00:00:10\n");
    d = parse_tmp_cue("e16.cue");
    CHECK(note_has(d, "Tracks must be consecutive starting at 01.")); mh_disc_free(d);

    put_text("e17.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 00 00:00:00\n");
    d = parse_tmp_cue("e17.cue");
    CHECK(note_has(d, "INDEX 01 is missing or outside the BIN.")); mh_disc_free(d);
    put_text("e17b.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:01:00\n");   /* 75 > 50 sectors */
    d = parse_tmp_cue("e17b.cue");
    CHECK(note_has(d, "INDEX 01 is missing or outside the BIN.")); mh_disc_free(d);

    put_text("e18.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nPREGAP 00:02:00\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e18.cue");
    CHECK(note_has(d, "Virtual PREGAP without INDEX 00: the image may be incomplete.")); CHECK(d->flags & MH_DF_INCOMPLETE); mh_disc_free(d);

    put_text("e19.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 00 00:00:10\nINDEX 01 00:00:05\n");
    d = parse_tmp_cue("e19.cue");
    CHECK(note_has(d, "Invalid INDEX 00.")); mh_disc_free(d);

    put_text("e20.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nPREGAP 00:00:07\nINDEX 00 00:00:00\nINDEX 01 00:00:05\n");
    d = parse_tmp_cue("e20.cue");
    CHECK(note_has(d, "PREGAP and INDEX 00/01 give different values.")); mh_disc_free(d);

    put_text("e21.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:05\n");
    d = parse_tmp_cue("e21.cue");
    CHECK(note_has(d, "The first track must start at sector 0 with no pregap.")); mh_disc_free(d);

    put_text("e22.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 02 AUDIO\nINDEX 01 00:00:10\nTRACK 03 AUDIO\nINDEX 01 00:00:10\n");
    d = parse_tmp_cue("e22.cue");
    CHECK(note_has(d, "Track INDEX values are out of order or a pregap overlaps.")); mh_disc_free(d);
    put_text("e22b.cue", "FILE \"a.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 02 AUDIO\nINDEX 01 00:00:20\nTRACK 03 AUDIO\nINDEX 00 00:00:10\nINDEX 01 00:00:30\n");
    d = parse_tmp_cue("e22b.cue");    /* the pregap of track 3 reaches back into track 2 */
    CHECK(note_has(d, "Track INDEX values are out of order or a pregap overlaps.")); mh_disc_free(d);

    /* 450000 sector limit (sparse files, 1.06 GB logical) */
    put_zero_bin("big1.bin", 2352ull * 449999);
    put_zero_bin("big2.bin", 2352ull * 2);
    put_text("e23.cue", "FILE \"big1.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nFILE \"big2.bin\" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e23.cue");
    CHECK(note_has(d, "The image exceeds the safe limit of 450,000 CD sectors.")); CHECK(d->flags & MH_DF_INVALID);
    CHECK(d->total_sectors == 449999);
    mh_disc_free(d);
    put_zero_bin("big3.bin", 2352ull * 450001);
    put_text("e24.cue", "FILE \"big3.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e24.cue");
    CHECK(note_has(d, "The image exceeds the safe limit of 450,000 CD sectors.")); mh_disc_free(d);
    put_zero_bin("big4.bin", 2352ull * 450000);
    put_text("e25.cue", "FILE \"big4.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    d = parse_tmp_cue("e25.cue");
    CHECK(!note_has(d, "excede") && (d->flags & MH_DF_STRUCT_OK) && d->total_sectors == 450000); mh_disc_free(d);
}


/* =========================================================================== project */

static MhDisc *fake_disc(const char *name, const char *serial)
{
    MhDisc *d = mh_calloc(sizeof *d);
    d->kind = MH_DISC_KIND_CUE;
    d->db_index = 0xffffffffu;
    snprintf(d->name, sizeof d->name, "%s", name);
    snprintf(d->title, sizeof d->title, "%s", name);
    snprintf(d->serial, sizeof d->serial, "%s", serial);
    return d;
}

static void test_project_discs(void)
{
    MhProject *p = mh_calloc(sizeof *p);
    MhCase *c, *c5;
    MhDisc *d[6];
    uint8_t oc, oi;
    mh_project_init(p);
    CHECK(p->reserved_c == 1 && p->device == -1 && p->dirty == 0 && p->case_count == 0);

    c = mh_project_add_case(p, 0);
    CHECK(c && c->slot == 0 && c->in_use == 1);
    CHECK_STR(c->title, "NEW GAME");
    CHECK(p->dirty == 1 && p->case_count == 1);
    c5 = mh_project_add_case(p, 5);
    CHECK(c5 && c5->slot == 5);
    CHECK(mh_project_add_case(p, 5)->slot == 6);          /* next free slot at or after 5 */
    CHECK(mh_project_add_case(p, 0)->slot == 1);
    CHECK(mh_project_find_case_title(p, "new game") == &p->cases[0]);
    CHECK(mh_project_find_case_title(p, "nope") == NULL);
    CHECK(mh_project_find_case_title(p, "") == NULL);
    CHECK(mh_project_remove_case(p, 6));
    CHECK(mh_project_remove_case(p, 1));
    CHECK(!mh_project_remove_case(p, 1));
    CHECK(p->case_count == 2);

    for (int i = 0; i < 6; i++) d[i] = fake_disc("D", i == 2 ? "SLUS-00001" : "");
    for (int i = 0; i < 5; i++) CHECK(mh_case_add_disc(p, c, d[i]));
    CHECK_STR(c->serial, "SLUS-00001");                    /* first disc with a serial wins */
    CHECK(c->disc_count == 5 && p->disc_count == 5);
    CHECK(!mh_case_add_disc(p, c, d[5]));                  /* sixth disc refused, the caller keeps it */
    CHECK(!mh_case_add_disc(NULL, c, d[5]) && !mh_case_add_disc(p, NULL, d[5]) && !mh_case_add_disc(p, c, NULL));
    mh_disc_free(d[5]);

    CHECK(mh_case_move_disc(c, 0, 4));
    CHECK(c->discs[0] == d[1] && c->discs[1] == d[2] && c->discs[3] == d[4] && c->discs[4] == d[0]);
    CHECK(mh_case_move_disc(c, 4, 1));
    CHECK(c->discs[0] == d[1] && c->discs[1] == d[0] && c->discs[2] == d[2] && c->discs[4] == d[4]);
    CHECK(!mh_case_move_disc(c, 1, 1) && !mh_case_move_disc(c, 0, 5) && !mh_case_move_disc(c, 7, 0));
    CHECK(!mh_case_move_disc(NULL, 0, 1));

    /* inside one case, destination clamped to the last place */
    CHECK(mh_project_move_disc(p, 0, 0, 0, 9, &oc, &oi));
    CHECK(oc == 0 && oi == 4 && c->discs[4] == d[1] && c->discs[0] == d[0]);
    CHECK(!mh_project_move_disc(p, 0, 4, 0, 4, &oc, &oi));
    CHECK(oc == 0 && oi == 4);                              /* outputs keep the source on failure */
    CHECK(!mh_project_move_disc(p, 0, 7, 0, 0, &oc, &oi));
    CHECK(!mh_project_move_disc(p, 0, 0, 6, 0, &oc, &oi) && oc == 0 && oi == 0);   /* unused target */
    CHECK(!mh_project_move_disc(NULL, 0, 0, 0, 1, &oc, &oi));
    /* between cases: c = [d0,d2,d3,d4,d1] */
    CHECK(mh_project_move_disc(p, 0, 0, 5, 0, &oc, &oi));
    CHECK(oc == 5 && oi == 0 && c->disc_count == 4 && c5->disc_count == 1 && c5->discs[0] == d[0]);
    CHECK(c->discs[0] == d[2] && c->discs[3] == d[1] && c->discs[4] == NULL);
    CHECK(p->disc_count == 5);
    for (int i = 0; i < 4; i++) CHECK(mh_case_add_disc(p, c5, fake_disc("X", "")));
    CHECK(c5->disc_count == 5);
    CHECK(!mh_project_move_disc(p, 0, 0, 5, 0, &oc, &oi));   /* target already holds 5 */
    CHECK(c->disc_count == 4 && oc == 0);
    CHECK(mh_project_remove_case(p, 5));                     /* frees its 5 discs */
    CHECK(p->disc_count == 4);
    c5 = mh_project_add_case(p, 5);
    for (int i = 0; i < 4; i++) {
        CHECK(mh_project_move_disc(p, 0, 0, 5, 0, &oc, &oi));
        CHECK(oc == 5 && oi == 0);
    }
    CHECK(c->in_use == 0 && c->disc_count == 0);             /* emptied case is cleared */
    CHECK(c5->disc_count == 4 && c5->discs[0] == d[1] && c5->discs[1] == d[4] && c5->discs[3] == d[2]);
    CHECK(p->case_count == 1 && p->disc_count == 4);
    CHECK(mh_case_remove_disc(p, c5, 1));
    CHECK(c5->disc_count == 3 && c5->discs[3] == NULL && c5->discs[1] == d[3]);
    CHECK(!mh_case_remove_disc(p, c5, 3));
    CHECK(p->disc_count == 3);
    mh_project_clear(p);
    free(p);
}

static void test_project_order(void)
{
    MhProject *p = mh_calloc(sizeof *p);
    MhCase *c;
    uint8_t oc;
    mh_project_init(p);
    for (int i = 0; i < 5; i++) {
        c = mh_project_add_case(p, (uint8_t)(i * 2));         /* slots 0 2 4 6 8 */
        snprintf(c->title, sizeof c->title, "C%d", i);
    }
    CHECK(mh_project_move_case(p, 0, 1));                     /* swap with an empty slot */
    CHECK(p->cases[1].in_use && p->cases[1].slot == 1 && !p->cases[0].in_use);
    CHECK(!mh_project_move_case(p, 3, 4));                    /* source unused */
    CHECK(!mh_project_move_case(p, 1, 1));
    CHECK(mh_project_move_case(p, 1, 2));
    CHECK_STR(p->cases[1].title, "C1"); CHECK_STR(p->cases[2].title, "C0");
    /* used by slot: 1:C1 2:C0 4:C2 6:C3 8:C4 */
    CHECK(mh_project_reorder_case(p, 1, 6, &oc) && oc == 3);
    CHECK_STR(p->cases[0].title, "C0"); CHECK_STR(p->cases[1].title, "C2");
    CHECK_STR(p->cases[2].title, "C3"); CHECK_STR(p->cases[3].title, "C1"); CHECK_STR(p->cases[4].title, "C4");
    CHECK(p->cases[4].slot == 4 && !p->cases[5].in_use && p->dirty);
    CHECK(!mh_project_reorder_case(p, 3, 3, &oc) && oc == 3);
    CHECK(!mh_project_reorder_case(p, 3, 9, &oc) && oc == 3);
    CHECK(mh_project_reorder_case_index(p, 4, 0, &oc) && oc == 0);
    CHECK_STR(p->cases[0].title, "C4"); CHECK_STR(p->cases[4].title, "C1");
    CHECK(mh_project_reorder_case_index(p, 0, 200, &oc) && oc == 4);   /* clamped to the last */
    CHECK_STR(p->cases[4].title, "C4");
    CHECK(!mh_project_reorder_case_index(p, 4, 4, &oc) && oc == 4);
    CHECK(!mh_project_reorder_case_index(p, 40, 0, &oc));
    mh_project_clear(p);
    free(p);
}

static void test_project_limits(void)
{
    MhProject *p = mh_calloc(sizeof *p);
    int added = 0;
    mh_project_init(p);
    for (int i = 0; i < 256; i++) CHECK(mh_project_add_case(p, 0) != NULL);
    CHECK(mh_project_add_case(p, 0) == NULL && p->case_count == 256);
    for (int cs = 0; cs < 52; cs++)
        for (int j = 0; j < 5 && added < 256; j++) {
            MhDisc *x = fake_disc("L", "");
            if (!mh_case_add_disc(p, &p->cases[cs], x)) { mh_disc_free(x); break; }
            added++;
        }
    CHECK(added == 256 && p->disc_count == 256 && p->cases[51].disc_count == 1);
    {
        MhDisc *x = fake_disc("L", "");
        CHECK(!mh_case_add_disc(p, &p->cases[52], x));        /* 257th disc */
        mh_disc_free(x);
    }
    mh_project_clear(p);
    CHECK(p->case_count == 0 && p->disc_count == 0);
    mh_project_set_menu_defaults(p, (const uint8_t *)"PST_DEFAULTS_V1!0123456789012345678901234567890123456789012345678");
    CHECK(p->has_menu_defaults == 1 && p->menu_defaults[0] == 'P');
    mh_project_clear(p);
    CHECK(p->has_menu_defaults == 0);
    free(p);
}

static void test_titles(void)
{
    char o[64];
    MhDisc *d = fake_disc("FINAL FANTASY VII USA DISC 1", "");
    d->title[0] = 0;
    CHECK(mh_case_title_for_disc(d, o, sizeof o)); CHECK_STR(o, "FINAL FANTASY VII USA");     /* from name */
    snprintf(d->title, sizeof d->title, "Final Fantasy VII (USA) (Disc 1)");
    CHECK(mh_case_title_for_disc(d, o, sizeof o)); CHECK_STR(o, "Final Fantasy VII (USA)");
    snprintf(d->title, sizeof d->title, "FINAL FANTASY VII DISC 2 REV 1");
    CHECK(mh_case_title_for_disc(d, o, sizeof o)); CHECK_STR(o, "FINAL FANTASY VII");
    snprintf(d->title, sizeof d->title, "PLAIN GAME   ");
    CHECK(!mh_case_title_for_disc(d, o, sizeof o)); CHECK_STR(o, "PLAIN GAME");

    mh_short_disc_name("SHORT NAME", o, 33); CHECK_STR(o, "SHORT NAME");
    mh_short_disc_name("Final Fantasy VII (USA) (Disc 1)", o, 33); CHECK_STR(o, "Final Fantasy VII (USA) (Disc 1)");
    /* too long: the part before " (DISC " is cut so that the marker survives */
    mh_short_disc_name("Final Fantasy Tactics Advance Special Edition (USA) (Disc 1)", o, 33);
    CHECK_STR(o, "Final Fantasy Tactics (Disc 1)");
    mh_short_disc_name("A VERY LONG TITLE WITHOUT THE MARKER AT ALL, TRUNCATED", o, 33);
    CHECK_STR(o, "A VERY LONG TITLE WITHOUT THE MA");
    mh_short_disc_name("Final Fantasy Tactics Advance Special Edition (Disc 1) with a very long tail here", o, 33);
    CHECK(strlen(o) == 32 && strncmp(o, "(Disc 1) with a very long tail", 30) == 0);   /* no room for the prefix */
    mh_short_disc_name("X (DISC 2)", o, 4);
    CHECK_STR(o, "(DI");

    /* The firmware shows 31 characters at most. A longer name loses the end of the game name, not
     * the region and revision that follow it. */
    {
        const char *raw = "Final Fantasy Tactics Advance Special Edition (USA) (Rev 1)";
        char norm[MH_DISC_TITLE_MAX];
        size_t bl = mh_title_base_len(raw);
        mh_normalize_title(raw, norm, sizeof norm);
        CHECK(bl == strlen("FINAL FANTASY TACTICS ADVANCE SPECIAL EDITION"));
        mh_fit_name(norm, bl, o, MH_NAME_FIT);
        CHECK_STR(o, "FINAL FANTASY TACTICS USA REV 1");
        mh_fit_name("SHORT NAME USA", 10, o, MH_NAME_FIT);
        CHECK_STR(o, "SHORT NAME USA");                       /* fits: untouched */
        mh_fit_name(norm, 0, o, MH_NAME_FIT);
        CHECK_STR(o, "FINAL FANTASY TACTICS ADVANCE S");      /* no tag known: plain cut */
        mh_fit_name("AAAAAAAAAA BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", 10, o, MH_NAME_FIT);
        CHECK_STR(o, "AAAAAAAAAA BBBBBBBBBBBBBBBBBBBB");     /* tail alone does not fit: plain cut */
        CHECK(mh_title_base_len("No Tag Here") == 0 && mh_title_base_len("(USA) Only") == 0);

        snprintf(d->title, sizeof d->title, "%s", norm);
        d->title_base_len = (uint16_t)bl;
        snprintf(d->title + strlen(d->title), sizeof d->title - strlen(d->title), " DISC 1");
        char c33[33];
        CHECK(mh_case_title_for_disc(d, c33, sizeof c33));
        CHECK_STR(c33, "FINAL FANTASY TACTICS USA REV 1");
        mh_fit_name(d->title, d->title_base_len, d->name, MH_NAME_FIT);
        CHECK_STR(d->name, "FINAL FANTASY USA REV 1 DISC 1");
    }

    mh_apply_trackset_title(d, "Spyro 2 - Ripto's Rage! (USA)");
    CHECK_STR(d->title, "SPYRO 2 - RIPTO'S RAGE USA"); CHECK_STR(d->name, "SPYRO 2 - RIPTO'S RAGE USA");
    d->flags |= MH_DF_USER_NAMED;
    mh_apply_trackset_title(d, "Other Game");
    CHECK_STR(d->title, "OTHER GAME"); CHECK_STR(d->name, "SPYRO 2 - RIPTO'S RAGE USA");
    mh_apply_trackset_title(d, "");
    CHECK_STR(d->title, "OTHER GAME");
    mh_apply_trackset_title(d, "!!!");
    CHECK_STR(d->title, "OTHER GAME");
    mh_disc_free(d);
}

static void test_layout_and_kept(void)
{
    uint8_t h[0x80];
    memset(h, 0, sizeof h);
    h[0x3c] = 100; h[0x64] = 105; h[0x70] = 115; h[0x79] = 0x0a;      /* u32@0x78 = 0xa00 */
    CHECK(mh_disc_layout_valid(h));
    h[0x6d] = 0x14; CHECK(mh_disc_layout_valid(h));                    /* 0x1400 */
    h[0x6c] = 1; CHECK(!mh_disc_layout_valid(h));                      /* 0x1401 */
    h[0x6c] = 0; h[0x6d] = 0;
    h[8] = 2; CHECK(!mh_disc_layout_valid(h));                         /* bit 1: needs +0x19 */
    h[0x70] = 100 + 0x19; CHECK(mh_disc_layout_valid(h));
    h[0x6d] = 0x28; CHECK(mh_disc_layout_valid(h)); h[0x6d] = 0x29; CHECK(!mh_disc_layout_valid(h)); h[0x6d] = 0;
    h[8] = 4; CHECK(!mh_disc_layout_valid(h));
    h[8] = 0; h[0x70] = 115; h[0x64] = 106; CHECK(!mh_disc_layout_valid(h));
    h[0x64] = 105; h[0x79] = 0x0b; CHECK(!mh_disc_layout_valid(h));

    CHECK(mh_keep_disc_with_observations(NULL, "x"));

    {
        MhDisc *d = fake_disc("V2", "SLUS-00001");
        d->kind = MH_DISC_KIND_V2_DEVICE; d->v2_drive = 3; d->v2_pkg_a = 111; d->v2_pkg_b = 222; d->v2_slot = 9;
        CHECK(mh_kept_package(d, 3, NULL) == 111);
        CHECK(mh_kept_package(d, 4, NULL) == 0);
        CHECK(mh_kept_package(d, -1, "x") == 0);
        d->flags |= MH_DF_KEEP_PACKAGE;
        CHECK(mh_kept_package(d, 9, NULL) == 222);
        CHECK(mh_kept_package(d, 8, NULL) == 0);
        CHECK(mh_kept_package(d, -1, NULL) == 0);
        snprintf(d->v2_pkg_path, sizeof d->v2_pkg_path, "/a/b.img");
        CHECK(mh_kept_package(d, -1, "/a/b.img") == 222);
        CHECK(mh_kept_package(d, -1, "/a/c.img") == 0);
        CHECK(mh_kept_package(d, 9, "/a/b.img") == 222);
        CHECK(mh_kept_package(d, 9, NULL) == 0);
        d->kind = MH_DISC_KIND_V2_IMAGE;
        snprintf(d->source_path, sizeof d->source_path, "/x/y.img");
        CHECK(mh_kept_package(d, 0, "/x/Y.IMG") == 111);
        mh_disc_free(d);
    }
}

/* ============================================================================= database */

static MhDb g_db;
static bool g_db_ok;

static bool have_db(void)
{
    static bool tried;
    if (!tried) {
        tried = true;
        if (access(EMBEDDED "/disc_index.bin", R_OK) == 0)
            g_db_ok = mh_db_load_dir(&g_db, EMBEDDED, "/nonexistent-dir");
    }
    return g_db_ok;
}

static void test_db_load(void)
{
    MhDbBlobs b;
    uint8_t *data[4];
    uint64_t sz[4];
    static const char *const names[4] = {"disc_index.bin", "redump_index.bin", "aliases.bin", "trackset_index.bin"};
    MhDb *db = &g_db;
    MhDb x;
    if (!have_db()) { fprintf(stderr, "skip db tests (blobs absent)\n"); return; }
    CHECK(db->disc_count == 10637 && db->disc_rec_size == 0x80 && db->disc_entry_size == 0x10);
    CHECK(db->redump_count == 49470 && db->alias_count == 1504 && db->ts_count == 10974);
    CHECK(db->ts_sbi_count == 235 && db->ts_entry_count == 49470 && db->ts_rec_size == 0xc0);
    CHECK_STR(mh_db_record_serial(db, 0), "DK-10794");
    CHECK_STR(mh_db_record_title(db, 0), "Classic Card Games (Russia) (Unl)");
    CHECK_STR(mh_db_record_serial(db, 999999), "");
    CHECK_STR(mh_db_record_title(db, db->disc_count), "");
    {
        uint32_t first, ts; uint16_t nt;
        MhDbTrack t;
        CHECK(mh_db_record_info(db, 0, &first, &nt, &ts) && first == 0 && nt == 9 && ts == 0x214f0);
        CHECK(mh_db_track_entry(db, 0, &t) && t.number == 1 && t.kind == 2 && t.pregap == 0 && t.length == 0x4bb3);
        CHECK(mh_db_track_entry(db, 1, &t) && t.number == 2 && t.kind == 1 && t.pregap == 0x95 && t.length == 0x34f2);
        CHECK(!mh_db_record_info(db, db->disc_count, &first, &nt, &ts));
        CHECK(!mh_db_track_entry(db, 0x7ffffff0, &t));
    }
    {
        char o[32];
        CHECK(mh_db_resolve_alias(db, "LSP-01017001", o, sizeof o)); CHECK_STR(o, "LSP-010170");
        CHECK(mh_db_resolve_alias(db, "lsp-01017001", o, sizeof o)); CHECK_STR(o, "LSP-010170");
        CHECK(mh_db_resolve_alias(db, "SLUS-90094", o, sizeof o)); CHECK_STR(o, "SLUS-90093");
        CHECK(!mh_db_resolve_alias(db, "SLUS-00553", o, sizeof o)); CHECK_STR(o, "SLUS-00553");
        /* every alias key resolves (binary search is exact) */
        for (uint32_t i = 0; i < db->alias_count; i += 37) {
            const char *rec = (const char *)db->aliases + (i + 1) * 0x20;
            CHECK(mh_db_resolve_alias(db, rec, o, sizeof o));
            CHECK_STR(o, rec + 0x10);
        }
    }
    for (int i = 0; i < 4; i++) {
        char p[MH_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", EMBEDDED, names[i]);
        data[i] = NULL;
        CHECK(mh_read_entire_file(p, &data[i], &sz[i]));
    }
    memset(&b, 0, sizeof b);
    b.disc_index = data[0]; b.disc_index_size = sz[0];
    b.redump_index = data[1]; b.redump_index_size = sz[1];
    b.aliases = data[2]; b.aliases_size = sz[2];
    b.trackset = data[3]; b.trackset_size = sz[3];
    CHECK(mh_db_load(&x, &b, "/d")); CHECK_STR(x.dir, "/d"); mh_db_free(&x);
    CHECK(x.disc_index == NULL && x.dir[0] == 0);
    b.disc_index_size = sz[0] - 1;
    CHECK(!mh_db_load(&x, &b, "/d")); CHECK(!x.disc_index && x.redump_index && x.aliases && x.trackset); mh_db_free(&x);
    b.disc_index_size = sz[0];
    data[0][0] = 'X'; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.disc_index); mh_db_free(&x); data[0][0] = 'P';
    data[0][0x40 + 0x80 * 5 + 1] ^= 0x20;              /* upper/lower case change keeps the order */
    CHECK(!mh_db_load(&x, &b, "") || x.disc_index); mh_db_free(&x);
    data[0][0x40 + 0x80 * 5 + 1] ^= 0x20;
    data[0][0x40 + 0x4f] = 'x'; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.disc_index); mh_db_free(&x); data[0][0x40 + 0x4f] = 0;
    data[1][8] = 3; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.redump_index && x.disc_index); mh_db_free(&x); data[1][8] = 2;
    {
        uint8_t keep = data[1][0x40 + 0x80 * 10 + 7];
        data[1][0x40 + 0x80 * 10 + 7] = 0x7f;              /* sizes must not decrease */
        CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.redump_index); mh_db_free(&x);
        data[1][0x40 + 0x80 * 10 + 7] = keep;
    }
    data[2][0x20] = 'Z'; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.aliases); mh_db_free(&x); data[2][0x20] = 'L';
    data[2][0x2f] = 'x'; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.aliases); mh_db_free(&x); data[2][0x2f] = 0;
    data[3][0x2c] ^= 1; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.trackset); mh_db_free(&x); data[3][0x2c] ^= 1;
    data[3][0x40 + 0x4f] = 'x'; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.trackset); mh_db_free(&x); data[3][0x40 + 0x4f] = 0;
    data[3][0x40 + 0x18] = 0; CHECK(!mh_db_load(&x, &b, "")); CHECK(!x.trackset); mh_db_free(&x); data[3][0x40 + 0x18] = 1;   /* 0 tracks */
    CHECK(mh_db_load(&x, &b, "")); mh_db_free(&x);
    b.trackset = NULL; CHECK(!mh_db_load(&x, &b, "")); CHECK(x.disc_index && !x.trackset); mh_db_free(&x);
    CHECK(!mh_db_load(&x, NULL, "")); CHECK(!x.disc_index);
    for (int i = 0; i < 4; i++) free(data[i]);
}

/* A disc shaped like database record `idx` (track kinds and size), serial set. */
static MhDisc *disc_like_record(const MhDb *db, uint32_t idx)
{
    uint32_t first, ts;
    uint16_t nt;
    MhDisc *d = fake_disc("TEST", mh_db_record_serial(db, idx));
    mh_db_record_info(db, idx, &first, &nt, &ts);
    d->ntracks = nt;
    d->nfiles = 1;
    d->total_sectors = ts;
    for (unsigned i = 0; i < nt; i++) {
        MhDbTrack t;
        mh_db_track_entry(db, first + i, &t);
        d->tracks[i].number = t.number;
        d->tracks[i].kind = t.kind;
    }
    d->flags = MH_DF_STRUCT_OK | MH_DF_SERIAL;
    return d;
}

static void test_db_apply(void)
{
    MhDisc *d;
    const MhDb *db = &g_db;
    if (!have_db()) return;
    d = disc_like_record(db, 9634);
    CHECK_STR(d->serial, "SLUS-00553");
    CHECK(mh_db_apply_disc(db, d));
    CHECK(d->db_index == 9634 && (d->flags & MH_DF_DB_MATCH) && !(d->flags & MH_DF_TRACKS_SUSPECT));
    CHECK(d->status == MH_DISC_STATUS_OK);
    mh_disc_free(d);
    d = disc_like_record(db, 9634);
    d->ntracks = 3;
    CHECK(mh_db_apply_disc(db, d));
    CHECK((d->flags & MH_DF_TRACKS_SUSPECT) && note_has(d, "\r\nThe original database lists a different number of tracks. Check or recover the original CUE."));
    CHECK(d->status == MH_DISC_STATUS_NOTE);
    mh_disc_free(d);
    d = disc_like_record(db, 9634);
    d->tracks[0].kind = 1;
    CHECK(mh_db_apply_disc(db, d));
    CHECK((d->flags & MH_DF_TRACKS_SUSPECT) && note_has(d, "\r\nThe track types differ from the original database."));
    mh_disc_free(d);
    d = fake_disc("X", "ZZZZ-00000");
    CHECK(!mh_db_apply_disc(db, d) && !(d->flags & MH_DF_DB_MATCH)); mh_disc_free(d);
    d = fake_disc("X", "");
    CHECK(!mh_db_apply_disc(db, d)); mh_disc_free(d);
    CHECK(!mh_db_apply_disc(db, NULL));
    /* lowercase serial in the disc is matched through canonical_serial */
    d = disc_like_record(db, 9634);
    snprintf(d->serial, sizeof d->serial, "slus-00553");
    CHECK(mh_db_apply_disc(db, d) && d->db_index == 9634);
    mh_disc_free(d);
    /* serial shared by several records with identical size: ambiguous when titles differ */
    {
        uint32_t amb = 0xffffffffu;
        for (uint32_t i = 1; i < db->disc_count && amb == 0xffffffffu; i++) {
            uint32_t f1, f2, t1, t2;
            uint16_t n1, n2;
            if (strcmp(mh_db_record_serial(db, i), mh_db_record_serial(db, i - 1)) != 0) continue;
            mh_db_record_info(db, i - 1, &f1, &n1, &t1);
            mh_db_record_info(db, i, &f2, &n2, &t2);
            if (n1 == n2 && t1 == t2 && strcmp(mh_db_record_title(db, i - 1), mh_db_record_title(db, i)) != 0) {
                bool same = true;
                for (uint32_t k = 0; k < n1; k++) {
                    MhDbTrack a, b;
                    mh_db_track_entry(db, f1 + k, &a);
                    mh_db_track_entry(db, f2 + k, &b);
                    if (a.kind != b.kind) same = false;
                }
                if (same) amb = i;
            }
        }
        if (amb != 0xffffffffu) {
            d = disc_like_record(db, amb);
            CHECK(!mh_db_apply_disc(db, d));
            CHECK(note_has(d, "\r\nThe serial matches more than one edition in the disc database."));
            CHECK(!(d->flags & MH_DF_DB_MATCH));
            mh_disc_free(d);
        } else {
            fprintf(stderr, "note: no tie record in this database build\n");
        }
    }
}

static void test_inherit_cd_art(void)
{
    MhCase c;
    MhDisc *a = fake_disc("A", "SLUS-00001"), *b = fake_disc("B", "SLUS-00002"), *z = fake_disc("C", "SLUS-00003");
    memset(&c, 0, sizeof c);
    c.discs[0] = a; c.discs[1] = b; c.discs[2] = z; c.disc_count = 3;
    CHECK(!mh_disc_inherit_cd_art(&c, b) && b->cd_art_path[0] == 0);          /* nobody has art */
    mh_str_copy(a->cd_art_path, sizeof a->cd_art_path, "/art/A-CD.art");
    a->flags |= MH_DF_CD_ART;
    CHECK(mh_disc_inherit_cd_art(&c, b) && (b->flags & MH_DF_CD_ART));         /* takes the sibling's art */
    CHECK(strcmp(b->cd_art_path, "/art/A-CD.art") == 0);
    mh_str_copy(z->cd_art_path, sizeof z->cd_art_path, "/art/C-CD.art");
    z->flags |= MH_DF_CD_ART;
    CHECK(!mh_disc_inherit_cd_art(&c, z) && strcmp(z->cd_art_path, "/art/C-CD.art") == 0); /* own art stays */
    CHECK(!mh_disc_inherit_cd_art(NULL, b) && !mh_disc_inherit_cd_art(&c, NULL));
    mh_disc_free(a); mh_disc_free(b); mh_disc_free(z);
}

static void test_db_files(void)
{
    MhDb db;
    MhDisc *d;
    char p[MH_PATH_MAX], dir[MH_PATH_MAX];
    if (!have_db()) return;
    db = g_db;
    mh_str_copy(db.dir, sizeof db.dir, g_tmp);
    d = fake_disc("X", "SLUS-00553");
    CHECK(!mh_db_find_art(&db, d, true) && !mh_db_find_art(&db, d, false));
    CHECK(d->case_art_path[0] == 0 && d->cd_art_path[0] == 0);
    {
        char out[MH_PATH_MAX];
        CHECK(!mh_db_find_original_cue(&db, d, out, sizeof out) && out[0] == 0);
        CHECK(!mh_db_find_sbi(&db, d));
        d->sbi_data = (const uint8_t *)"SBI"; d->sbi_len = 3;
        CHECK(mh_db_find_sbi(&db, d) && (d->flags & MH_DF_SBI));
    }
    mh_disc_free(d);
    snprintf(dir, sizeof dir, "%s/database", g_tmp); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/database/art", g_tmp); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/database/art/default", g_tmp); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/database/cues", g_tmp); mkdir(dir, 0755);
    snprintf(p, sizeof p, "%s/database/art/default/SLUS-00553-CASE.art", g_tmp);
    { FILE *f = fopen(p, "wb"); fputs("x", f); fclose(f); }
    snprintf(p, sizeof p, "%s/database/art/default/LSP-010170-CD.art", g_tmp);
    { FILE *f = fopen(p, "wb"); fputs("x", f); fclose(f); }
    snprintf(p, sizeof p, "%s/database/cues/LSP-010170.cue", g_tmp);
    { FILE *f = fopen(p, "wb"); fputs("x", f); fclose(f); }
    d = fake_disc("X", "SLUS-00553");
    CHECK(mh_db_find_art(&db, d, true) && (d->flags & MH_DF_CASE_ART));
    CHECK(strstr(d->case_art_path, "database/art/default/SLUS-00553-CASE.art"));
    CHECK(!mh_db_find_art(&db, d, false));
    mh_disc_free(d);
    d = fake_disc("X", "LSP-01017001");                 /* alias of LSP-010170 */
    CHECK(mh_db_find_art(&db, d, false) && (d->flags & MH_DF_CD_ART));
    CHECK(strstr(d->cd_art_path, "LSP-010170-CD.art"));
    CHECK(!mh_db_find_art(&db, d, true));
    {
        char out[MH_PATH_MAX];
        CHECK(mh_db_find_original_cue(&db, d, out, sizeof out) && strstr(out, "database/cues/LSP-010170.cue"));
    }
    mh_disc_free(d);
}

/* sha-1 list of trackset record i, and its record pointer */
static const uint8_t *ts_rec(const MhDb *db, uint32_t i) { return db->trackset + 0x40 + (size_t)i * 0xc0; }
static unsigned ts_sha(const MhDb *db, uint32_t i, uint8_t *out)
{
    const uint8_t *r = ts_rec(db, i);
    unsigned n = r[0x18] | r[0x19] << 8;
    uint32_t e = r[0x14] | r[0x15] << 8 | r[0x16] << 16 | (uint32_t)r[0x17] << 24;
    memcpy(out, db->trackset + db->ts_entries_off + (size_t)e * 20, (size_t)n * 20);
    return n;
}

static void test_db_trackset(void)
{
    const MhDb *db = &g_db;
    uint8_t sha[99 * 20];
    MhDisc *d;
    uint32_t plain = 0xffffffffu, multi = 0xffffffffu, withsbi = 0xffffffffu;
    if (!have_db()) return;
    for (uint32_t i = 1; i + 1 < db->ts_count; i++) {
        const uint8_t *r = ts_rec(db, i);
        unsigned n = r[0x18] | r[0x19] << 8, sbi = r[0x28] | r[0x29] << 8;
        bool unique = memcmp(r, ts_rec(db, i - 1), 20) != 0 && memcmp(r, ts_rec(db, i + 1), 20) != 0;
        if (!unique) continue;
        if (n == 1 && sbi == 0 && plain == 0xffffffffu) plain = i;
        if (n >= 3 && sbi == 0 && multi == 0xffffffffu) multi = i;
        if (n >= 2 && sbi != 0 && withsbi == 0xffffffffu) withsbi = i;
    }
    CHECK(plain != 0xffffffffu && multi != 0xffffffffu && withsbi != 0xffffffffu);

    /* a single track disc: identified, serial taken from the record, database record linked */
    d = fake_disc("", "");
    d->title[0] = 0;
    d->ps.status = MH_PSS_NOT_FOUND;
    d->ps.status_saved = MH_PSS_NOT_FOUND;
    snprintf(d->ps.message_saved, sizeof d->ps.message_saved, "saved text");
    d->flags = MH_DF_STRUCT_OK;
    d->total_sectors = 1;
    {
        unsigned n = ts_sha(db, plain, sha);
        const uint8_t *r = ts_rec(db, plain);
        char canon[16];
        CHECK(n == 1);
        CHECK(mh_db_match_trackset(db, d, sha, n));
        CHECK((d->flags & (MH_DF_TRACKSET_OK | MH_DF_IDENTITY | MH_DF_TRACKSET_CHECKED)) ==
              (MH_DF_TRACKSET_OK | MH_DF_IDENTITY | MH_DF_TRACKSET_CHECKED));
        CHECK(d->ps.status == MH_PSS_TRACKSET && d->ps.source == MH_PSSRC_TRACKSET && d->ps.needs_hash == 0);
        CHECK_STR(d->ps.trackset_serial, (const char *)r + 0x30);
        CHECK(mh_canonical_serial((const char *)r + 0x30, canon));
        CHECK_STR(d->serial, canon); CHECK(d->flags & MH_DF_SERIAL);
        CHECK(d->ps.trackset_id == (r[0x20] | r[0x21] << 8 | r[0x22] << 16 | (uint32_t)r[0x23] << 24));
        CHECK(d->title[0] != 0);
        CHECK_STR(d->ps.message, "Physical edition confirmed only by the complete, ordered sequence of SHA-1 hashes of all tracks.");
        CHECK(note_has(d, "\r\nTRACKSET_HASH_CONFIRMED: physical identity resolved by all tracks, in CUE order."));
        CHECK(d->flags & MH_DF_DB_MATCH);
        /* damaged hash: not found, state returns to the saved one */
        sha[3] ^= 1;
        CHECK(!mh_db_match_trackset(db, d, sha, n));
        CHECK(d->ps.status == MH_PSS_NOT_FOUND && d->ps.needs_hash == 1 && !(d->flags & MH_DF_TRACKSET_OK));
        CHECK(!(d->flags & MH_DF_SERIAL) && d->serial[0] == 0);       /* strong serial was absent before */
        CHECK((d->flags & MH_DF_INTEGRITY_BAD) && (d->flags & MH_DF_TRACKSET_CHECKED));
        CHECK_STR(d->ps.message, "saved text");
        CHECK(note_has(d, "\r\nComplete trackset not found; no heuristic was promoted."));
        {   /* the note says how the disc differs from the database record of its serial */
            const uint8_t *rec = mh_db_disc_rec(db, d->db_index);
            uint32_t known = rec[0x58] | rec[0x59] << 8 | rec[0x5a] << 16 | (uint32_t)rec[0x5b] << 24;
            d->notes[0] = 0;
            d->total_sectors = known + 7;
            CHECK(!mh_db_match_trackset(db, d, sha, n));
            CHECK(note_has(d, "has 7 more sectors than the known disc"));
            d->notes[0] = 0;
            d->total_sectors = known - 3;
            CHECK(!mh_db_match_trackset(db, d, sha, n));
            CHECK(note_has(d, "has 3 fewer sectors than the known disc"));
            d->notes[0] = 0;
            d->total_sectors = known;
            d->verify_issues = 0;
            CHECK(!mh_db_match_trackset(db, d, sha, n));
            CHECK(note_has(d, "same size as the known disc, but its content differs"));
            CHECK(!note_has(d, "recalculating EDC/ECC"));
            d->notes[0] = 0;
            d->verify_issues = 0x80;
            CHECK(!mh_db_match_trackset(db, d, sha, n));
            CHECK(note_has(d, "recalculating EDC/ECC"));
            d->flags &= ~(uint32_t)MH_DF_DB_MATCH;               /* no record to compare with: nothing added */
            d->notes[0] = 0;
            CHECK(!mh_db_match_trackset(db, d, sha, n));
            CHECK(!note_has(d, "known disc"));
        }
    }
    mh_disc_free(d);

    /* several tracks: all must match and in order */
    d = fake_disc("", "SLUS-00001");
    {
        unsigned n = ts_sha(db, multi, sha);
        uint8_t swapped[99 * 20];
        CHECK(n >= 3);
        CHECK(mh_db_match_trackset(db, d, sha, n));
        CHECK_STR(d->serial, "SLUS-00001");                          /* an existing serial is kept */
        memcpy(swapped, sha, (size_t)n * 20);
        memcpy(swapped + 20, sha + 40, 20); memcpy(swapped + 40, sha + 20, 20);
        if (memcmp(swapped + 20, sha + 20, 40) != 0) CHECK(!mh_db_match_trackset(db, d, swapped, n));
        CHECK(!mh_db_match_trackset(db, d, sha, n - 1));            /* fewer tracks */
        CHECK(!mh_db_match_trackset(db, d, sha, 0) && !mh_db_match_trackset(db, d, sha, 100) &&
              !mh_db_match_trackset(db, d, NULL, 1));
    }
    mh_disc_free(d);

    /* a record with an SBI blob */
    d = fake_disc("", "");
    {
        unsigned n = ts_sha(db, withsbi, sha);
        const uint8_t *r = ts_rec(db, withsbi);
        CHECK(mh_db_match_trackset(db, d, sha, n));
        CHECK(d->sbi_data != NULL && d->sbi_len == (r[0x28] | r[0x29] << 8) && (d->flags & MH_DF_SBI));
        CHECK(memcmp(d->sbi_data, "SBI\0", 4) == 0);
        CHECK(d->sbi_data >= db->trackset && d->sbi_data < db->trackset + db->trackset_size);
        sha[0] ^= 1;
        CHECK(!mh_db_match_trackset(db, d, sha, n));
        CHECK(d->sbi_data == NULL && d->sbi_len == 0 && !(d->flags & MH_DF_SBI));   /* cleared by the new search */
    }
    mh_disc_free(d);
    /* identical key but different tails: the full sequence decides; look for any such pair */
    for (uint32_t i = 1; i < db->ts_count; i++) {
        if (memcmp(ts_rec(db, i), ts_rec(db, i - 1), 20) == 0) {
            unsigned n = ts_sha(db, i, sha);
            d = fake_disc("", "");
            CHECK(mh_db_match_trackset(db, d, sha, n) || (d->ps.status == MH_PSS_AMBIGUOUS));
            mh_disc_free(d);
            break;
        }
    }
}

/* ========================================================== manager_parse and add_cue */

static char g_asked[512];
static bool hook_ask_record(void *ctx, const char *text)
{
    (void)ctx;
    snprintf(g_asked, sizeof g_asked, "%s", text);
    return true;
}
static bool hook_ask_no(void *ctx, const char *text) { (void)ctx; (void)text; return false; }
static bool hook_admit_valid(void *ctx, MhDisc *d) { (void)ctx; return (d->flags & MH_DF_STRUCT_OK) != 0; }

static void test_manager(void)
{
    MhCueHooks h;
    MhDisc *d;
    char p[MH_PATH_MAX];
    put_text("miss.cue", "FILE \"gone.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    tmp_path("miss.cue", p);
    memset(&h, 0, sizeof h);
    d = mh_manager_parse(NULL, p, &h);                       /* no callbacks: stays missing */
    CHECK(d->flags & MH_DF_MISSING_BIN); CHECK(d->status == MH_DISC_STATUS_FIXABLE); CHECK(d->card_index == 0xffff);
    mh_disc_free(d);
    h.ask = hook_ask_no;
    d = mh_manager_parse(NULL, p, &h);
    CHECK(d->flags & MH_DF_MISSING_BIN);
    mh_disc_free(d);
    h.ask = hook_ask_record;                                  /* yes, but nothing to locate with */
    d = mh_manager_parse(NULL, p, &h);
    CHECK(d->flags & MH_DF_MISSING_BIN);
    CHECK_STR(g_asked, "The CUE references a BIN that was not found:\r\n\r\ngone.bin\r\n\r\nDo you want to locate that BIN now?");
    mh_disc_free(d);
}

static bool hook_ask_yes2(void *ctx, const char *text) { (void)ctx; (void)text; return true; }
static bool g_located_ok;
static char g_located_path[MH_PATH_MAX];
static bool hook_locate2(void *ctx, char out[MH_PATH_MAX])
{
    (void)ctx;
    snprintf(out, MH_PATH_MAX, "%s", g_located_path);
    g_located_ok = true;
    return true;
}

static void test_manager_locate(void)
{
    MhCueHooks h;
    MhDisc *d;
    char p[MH_PATH_MAX];
    snprintf(g_located_path, sizeof g_located_path, "%s/found", g_tmp);
    mkdir(g_located_path, 0755);
    snprintf(g_located_path, sizeof g_located_path, "%s/found/real.bin", g_tmp);
    put_zero_bin("found/real.bin", 2352 * 40);
    put_text("miss.cue", "FILE \"gone.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    tmp_path("miss.cue", p);
    memset(&h, 0, sizeof h);
    h.ask = hook_ask_yes2;
    h.locate = hook_locate2;
    d = mh_manager_parse(NULL, p, &h);
    CHECK(g_located_ok);
    CHECK(!(d->flags & MH_DF_MISSING_BIN) && (d->flags & MH_DF_STRUCT_OK));
    CHECK(d->nfiles == 1 && strstr(d->files[0].path, "found/real.bin") != NULL);
    CHECK_STR(d->cue_path, p);
    CHECK(note_has(d, "BIN names and paths were fixed for this write only; original files preserved."));
    mh_disc_free(d);
}

static void test_add_cue(void)
{
    MhProject *proj = mh_calloc(sizeof *proj);
    MhCueHooks h;
    MhAddResult res;
    char p[MH_PATH_MAX], err[256];
    MhAddStatus st;
    mh_project_init(proj);
    memset(&h, 0, sizeof h);
    h.admit = hook_admit_valid;
    put_zero_bin("game1.bin", 2352 * 30);
    put_zero_bin("game2.bin", 2352 * 30);
    put_text("Game (Disc 1).cue", "FILE \"game1.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    put_text("Game (Disc 2).cue", "FILE \"game2.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    put_text("Other.cue", "FILE \"game1.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    put_text("bad.cue", "FILE \"nothere.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    put_text("readme.txt", "x");

    tmp_path("readme.txt", p);
    CHECK(mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err) == MH_ADD_NOT_CUE);
    tmp_path("Game (Disc 1).cue", p);
    st = mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_OK && res.cas == &proj->cases[0] && res.disc_index == 0);
    CHECK_STR(proj->cases[0].title, "GAME");
    CHECK(proj->case_count == 1 && proj->disc_count == 1 && proj->dirty);
    CHECK(mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err) == MH_ADD_DUPLICATE);
    tmp_path("Game (Disc 2).cue", p);
    st = mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_OK && res.cas == &proj->cases[0] && res.disc_index == 1);     /* grouped by title + folder */
    tmp_path("Other.cue", p);
    st = mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_OK && res.cas == &proj->cases[1] && res.disc_index == 0);
    CHECK_STR(proj->cases[1].title, "OTHER");
    /* explicit target slot */
    tmp_path("bad.cue", p);
    CHECK(mh_project_add_cue(proj, NULL, p, 1, &h, &res, err, sizeof err) == MH_ADD_REJECTED);   /* admit refuses */
    h.admit = NULL;
    st = mh_project_add_cue(proj, NULL, p, 1, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_OK && res.cas == &proj->cases[1] && res.disc_index == 1);
    /* a full target case */
    for (int i = 0; i < 3; i++) CHECK(mh_case_add_disc(proj, &proj->cases[1], fake_disc("F", "")));
    CHECK(proj->cases[1].disc_count == 5);
    put_text("Third.cue", "FILE \"game2.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    tmp_path("Third.cue", p);
    st = mh_project_add_cue(proj, NULL, p, 1, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_LIMIT); CHECK_STR(err, "The selected Case already has 5 discs.");
    CHECK(proj->disc_count == 7);
    /* unused target slot falls back to automatic placement */
    st = mh_project_add_cue(proj, NULL, p, 200, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_OK && res.cas->slot == 2);
    /* 256 disc limit */
    mh_project_clear(proj);
    for (int cs = 0; cs < 52; cs++) {
        MhCase *c = mh_project_add_case(proj, 0);
        for (int j = 0; j < 5; j++) if (proj->disc_count < 256) mh_case_add_disc(proj, c, fake_disc("L", ""));
    }
    CHECK(proj->disc_count == 256);
    tmp_path("Third.cue", p);
    st = mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err);
    CHECK(st == MH_ADD_LIMIT); CHECK_STR(err, "V2 accepts at most 256 discs in total.");
    /* auto placement: a case with 5 discs of the same game */
    mh_project_clear(proj);
    {
        MhCase *c = mh_project_add_case(proj, 0);
        MhDisc *first;
        tmp_path("Game (Disc 1).cue", p);
        first = mh_cue_parse(p);
        CHECK(mh_case_add_disc(proj, c, first));
        snprintf(c->title, sizeof c->title, "GAME");
        for (int i = 0; i < 4; i++) CHECK(mh_case_add_disc(proj, c, fake_disc("F", "")));
        tmp_path("Game (Disc 2).cue", p);
        st = mh_project_add_cue(proj, NULL, p, 256, &h, &res, err, sizeof err);
        CHECK(st == MH_ADD_LIMIT); CHECK_STR(err, "The game already has five discs; the extra disc was not added.");
    }
    mh_project_clear(proj);
    free(proj);
}

/* ========================================================================== fixtures */

static void test_fixtures(void)
{
    static const struct { const char *cue, *serial; } known[] = {
        {"Alundra (USA) (Rev 1).cue", "SLUS-00553"},
        {"Breath of Fire III (USA).cue", "SLUS-00422"},
        {"Final Fantasy VII (USA) (Disc 1).cue", "SCUS-94163"},
        {"Crash Bandicoot (USA).cue", "SCUS-94900"},
        {"Tekken 3 (USA).cue", "SLUS-00402"},
        {"Wild Arms (USA).cue", "SCUS-94608"},
    };
    if (access(FIXTURES "/Alundra (USA) (Rev 1).cue", R_OK) != 0) { fprintf(stderr, "skip fixtures (absent)\n"); return; }
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
        char p[MH_PATH_MAX];
        MhDisc *d;
        snprintf(p, sizeof p, "%s/%s", FIXTURES, known[i].cue);
        if (access(p, R_OK) != 0) continue;
        d = mh_manager_parse(have_db() ? &g_db : NULL, p, NULL);
        CHECK(d->flags & MH_DF_STRUCT_OK);
        CHECK_STR(d->serial, known[i].serial);
        CHECK(d->ps.status == MH_PSS_STRONG && d->ps.source == MH_PSSRC_SYSTEM_CNF);
        CHECK(d->status == MH_DISC_STATUS_OK);
        if (have_db()) {
            CHECK(d->flags & MH_DF_DB_MATCH);
            CHECK_STR(mh_db_record_serial(&g_db, d->db_index), known[i].serial);
        }
        mh_disc_free(d);
    }
    /* nested executables */
    {
        char p[MH_PATH_MAX];
        MhDisc *d;
        snprintf(p, sizeof p, "%s/Tekken 3 (USA).cue", FIXTURES);
        d = mh_cue_parse(p);
        CHECK_STR(d->ps.path, "TEKKEN3/SLUS_004.02");
        mh_disc_free(d);
    }
    /* boot files that are not serial shaped */
    {
        char p[MH_PATH_MAX];
        MhDisc *d;
        snprintf(p, sizeof p, "%s/Rayman (USA).cue", FIXTURES);
        if (access(p, R_OK) == 0) {
            d = mh_cue_parse(p);
            CHECK(d->serial[0] == 0 && d->ps.status == MH_PSS_NOT_FOUND && d->status == MH_DISC_STATUS_NOTE);
            CHECK_STR(d->notes, "Serial not identified automatically.");
            mh_disc_free(d);
        }
    }
    /* multi BIN CUE: tracks and files */
    {
        char p[MH_PATH_MAX];
        MhDisc *d;
        snprintf(p, sizeof p, "%s/Breath of Fire III (USA).cue", FIXTURES);
        d = mh_cue_parse(p);
        CHECK(d->nfiles == 2 && d->ntracks == 2 && d->total_sectors == 204224);
        CHECK(d->tracks[1].kind == 1 && d->tracks[1].pregap == 150 && d->tracks[1].start == 188474);
        CHECK(mh_cue_count_files(p) == 2);
        {
            char out[MH_PATH_MAX];
            tmp_path("bof3.cue", out);
            for (int k = 0; k < 2; k++) {                  /* the corrected CUE names the BINs next to itself */
                char base[MH_PATH_MAX], link[MH_PATH_MAX];
                mh_path_basename(d->files[k].path, base, sizeof base);
                tmp_path(base, link);
                CHECK(symlink(d->files[k].path, link) == 0);
            }
            CHECK(mh_cue_write_corrected(d, out));
            {
                MhDisc *e = mh_cue_parse(out);
                CHECK(e->ntracks == 2 && e->nfiles == 2 && e->total_sectors == 204224);
                CHECK(e->tracks[1].start == 188474 && e->tracks[1].pregap == 150);
                mh_disc_free(e);
            }
        }
        mh_disc_free(d);
    }
}

int main(void)
{
    tmp_init("t");
    test_canonical();
    test_parse_filename();
    test_status_texts();
    test_serial_images();
    test_recover();
    test_walk_bounds();
    test_msf();
    test_cue_valid();
    test_cue_text();
    test_cue_errors();
    test_project_discs();
    test_project_order();
    test_project_limits();
    test_titles();
    test_layout_and_kept();
    test_db_load();
    test_db_apply();
    test_db_files();
    test_inherit_cd_art();
    test_db_trackset();
    test_manager();
    test_manager_locate();
    test_add_cue();
    test_fixtures();
    rm_rf(g_tmp);
    printf("disc: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
