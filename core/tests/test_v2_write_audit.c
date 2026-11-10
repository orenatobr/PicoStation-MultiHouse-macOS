/* Audit tests of the V2 writer (docs/audit/v2-writer.md). Expected bytes here were derived from the
 * decompilation of v2_write_handle_direct and friends (constants dumped from .rdata of mh64.exe),
 * not from the port's own helpers. Re-uses the fixtures and helpers of test_v2_write.c. */
#define main write_suite_main
#include "test_v2_write.c"
#undef main

#include <fcntl.h>
#include <sys/stat.h>

/* ---- helpers ------------------------------------------------------------------------------- */

static bool audit_setup(void)
{
    uint8_t *blob;
    uint64_t blen;
    if (access(EMBEDDED "/picostation_menu.bin", R_OK) != 0 || access(STRIDER, R_OK) != 0) return false;
    CHECK(mh_db_load_dir(&g_db, EMBEDDED, FIXTURES));
    blob = slurp(EMBEDDED "/cdx_cheats.bin", &blen);
    CHECK(blob && mh_cheat_db_load(&g_cdb, blob, blen));
    g_menu = slurp(EMBEDDED "/picostation_menu.bin", &g_menu_size);
    g_music = slurp(EMBEDDED "/umbrella_music.vag", &g_music_size);
    CHECK(g_menu && g_music);
    g_env.menu = g_menu;
    g_env.menu_size = g_menu_size;
    g_env.db = &g_db;
    g_env.cheats = &g_cdb;
    g_w.env = &g_env;
    g_w.music = g_music;
    g_w.music_size = g_music_size;
    g_w.prog = &g_prog;
    g_w.stamp = fixed_stamp;
    reset_progress();
    return true;
}

static uint64_t le(const uint8_t *p, int n)
{
    uint64_t v = 0;
    while (n--) v = (v << 8) | p[n];
    return v;
}

static bool all_zero(const uint8_t *p, size_t n)
{
    while (n--)
        if (*p++) return false;
    return true;
}

/* ---- 1. golden bytes of the fixed zone (constants from .rdata 0x140f1a690..0x140f1a6fc) ---------- */

static void test_audit_golden_zone(void)
{
    MhProject *p = new_project();
    MhV2Result res;
    char cue[MH_PATH_MAX], *img = tpath("golden.img");
    uint8_t *z, *pk;
    uint8_t exp_hdr[0x74];
    MhV2DiscRec dr;
    unsigned i;

    make_synth("golden", 600, 300, false, 21, cue);
    project_add(p, cue, 256);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    z = pread_file(img, 0, 0x500000);

    /* header A/B: byte for byte the constants of the original (0x14003cb... stores) */
    memset(exp_hdr, 0, sizeof exp_hdr);
    memcpy(exp_hdr, "PSXRAW", 6);
    exp_hdr[6] = 2;                        /* DAT_140f1a6ec: 02 03 */
    exp_hdr[7] = 3;
    exp_hdr[8] = 1;                        /* cases */
    exp_hdr[10] = 1;                       /* discs */
    exp_hdr[12] = 1;                       /* generation of a first write */
    exp_hdr[0x11] = 0x08;                  /* 0x800 */
    exp_hdr[0x14] = 5;                     /* DAT_140f1a6a0 */
    exp_hdr[0x1c] = 0x0a;                  /* UNK_140f1a6a8 */
    exp_hdr[0x24] = 0x32;                  /* DAT_140f1a6b0 */
    exp_hdr[0x2c] = 0xb4;                  /* UNK_140f1a6b8 */
    exp_hdr[0x34] = 0xb4;                  /* 0x14b4 */
    exp_hdr[0x35] = 0x14;
    exp_hdr[0x3c] = (uint8_t)(g_menu_size / 0x930);
    exp_hdr[0x40] = 0x85;                  /* DAT_140f1a6c0 = 0x1685 */
    exp_hdr[0x41] = 0x16;
    exp_hdr[0x49] = 0x28;                  /* UNK_140f1a6c8 = 0x2800 */
    CHECK(memcmp(z, exp_hdr, 0x50) == 0);
    CHECK(memcmp(z + 0xa00, exp_hdr, 0x50) == 0);
    CHECK(all_zero(z + 0x74, 0x1b8 - 0x74));
    CHECK(le(z + 0x1b8, 4) == (1u ^ 0x32545350u));                       /* generation xor "PST2" */
    CHECK(z[0x1bc] == 0 && z[0x1bd] == 0);
    {
        static const uint8_t ent[8] = {0x00, 0x00, 0x02, 0x00, 0xda, 0xfe, 0xff, 0xff}; /* DAT_140f1a6f0 */
        CHECK(memcmp(z + 0x1be, ent, 8) == 0);
    }
    CHECK(le(z + 0x1c6, 4) == 1);
    CHECK(le(z + 0x1ca, 4) == file_size(img) / 512 - 1);
    CHECK(all_zero(z + 0x1ce, 0x1fe - 0x1ce) && z[0x1fe] == 0x55 && z[0x1ff] == 0xaa);
    CHECK(all_zero(z + 0x200, 0x92c - 0x200));
    CHECK(le(z + 0x92c, 4) == mh_crc32(z, 0x92c));
    CHECK(all_zero(z + 0x930, 0xa00 - 0x930));

    /* PSTCFG2 */
    {
        static const char text[] = "theme=umbrella\nmusic=embedded:umbrella.vag\nmusic_loop=true\nmusic_volume=68\n";
        const uint8_t *c = z + 0x2d0a00;
        CHECK(memcmp(c, "PSTCFG2\0", 8) == 0);
        CHECK(le(c + 8, 4) == 0x00010001);                                  /* DAT_140f1a6e8 */
        CHECK(all_zero(c + 0xc, 0x10));
        CHECK(le(c + 0x1c, 8) == 0x168a);
        CHECK(le(c + 0x24, 4) == g_music_size);
        CHECK(le(c + 0x28, 4) == mh_crc32(g_music, (size_t)g_music_size));
        CHECK(memcmp(c + 0x2c, text, sizeof text) == 0 && sizeof text == 0x4c);
        CHECK(all_zero(c + 0x78, 0x92c - 0x78));
        CHECK(le(c + 0x92c, 4) == mh_crc32(c, 0x92c));
        CHECK(all_zero(c + 0x930, 0xa00 - 0x930));
    }

    /* Case record */
    CHECK(memcmp(z + 0x1400, "CASE", 4) == 0 && z[0x1404] == 1 && z[0x1405] == 1 && z[0x1406] == 0 && z[0x1407] == 0);
    CHECK(le(z + 0x1400 + 0x38, 2) == 0xffff);        /* no case art */
    CHECK(z[0x1400 + 0x3a] == 0 && all_zero(z + 0x1400 + 0x3b, 4));
    CHECK(le(z + 0x1400 + 0x4c, 4) == mh_crc32(z + 0x1400, 0x4c));
    CHECK(all_zero(z + 0x1400 + 0x50, 0x5000 - 0x50));

    /* Disc record: the stores of v2_write_handle_direct at 0x14003b25c..0x14003b7f2 */
    {
        const uint8_t *r = z + 0x6400;
        uint64_t base = le(r + 0x3c, 8);
        CHECK(memcmp(r, "DISC", 4) == 0 && r[4] == 1 && r[5] == 0 && r[6] == 0 && r[7] == 0);
        CHECK(le(r + 8, 4) == 2);                                           /* (flags>>6 & 1) | 2 */
        CHECK(base == 0x2800 && le(r + 0x44, 8) == base && le(r + 0x4c, 8) == base + 0x100);
        CHECK(le(r + 0x54, 8) == 5ull * 1050 && le(r + 0x5c, 4) == 1050);
        CHECK(r[0x60] == 2 && r[0x61] == 1 && le(r + 0x62, 2) == 0xffff);
        CHECK(le(r + 0x64, 8) == base + 5 && le(r + 0x6c, 4) == 0 && le(r + 0x70, 8) == base + 0x19 && le(r + 0x78, 4) == 0xa00);
        CHECK(all_zero(r + 0x68, 4) && all_zero(r + 0x94, 0xfc - 0x94));
        CHECK(le(r + 0xfc, 4) == mh_crc32(r, 0xfc));
        CHECK(all_zero(z + 0x6400 + 0x100, 0x10000 - 0x100));
        mh_v2_disc_decode(r, &dr, NULL);
    }
    /* the package head: PSTTRK1 at +0, cheats and "extra" zero, art id absent */
    pk = pread_file(img, dr.base_lba << 9, 0x6400 + 512);
    CHECK(memcmp(pk, "PSTTRK1\0", 8) == 0 && pk[8] == 2 && pk[9] == 0x10 && le(pk + 0xa, 2) == 0);
    {
        uint8_t t[0x930];
        memcpy(t, pk, 0x930);
        memset(t + 0xc, 0, 4);
        CHECK(le(pk + 0xc, 4) == mh_crc32(t, 0x930));
    }
    /* track 1: data MODE1, begin 0; track 2: audio with INDEX 00 at 600 and INDEX 01 at 750 */
    CHECK(le(pk + 0x10, 4) == 0 && le(pk + 0x14, 4) == 0 && le(pk + 0x18, 4) == 600);
    CHECK(pk[0x1c] == 1 && pk[0x1d] == 4 && pk[0x1e] == 0 && pk[0x1f] == 0);
    CHECK(le(pk + 0x20, 4) == 600 && le(pk + 0x24, 4) == 750 && le(pk + 0x28, 4) == 450);
    CHECK(pk[0x2c] == 0 && pk[0x2d] == 0 && pk[0x2e] == 1 && pk[0x2f] == 0); /* audio, has INDEX 00 */
    CHECK(all_zero(pk + 0x30, 0x930 - 0x30) && all_zero(pk + 0x930, 0x6400 - 0x930 + 512));
    /* resume marker: "PSTRSM1\0", stamp at 8, record at 0x40, verify block at 0x140 */
    {
        const uint8_t *m = pk + 0x6400;
        uint8_t full[512];
        uint8_t *mm = pread_file(img, (dr.raw_lba - 1) << 9, 512);
        memcpy(full, mm, 512);
        free(mm);
        (void)m;
        CHECK(memcmp(full, "PSTRSM1\0", 8) == 0 && le(full + 8, 8) == 0x1122334455667788ull);
        CHECK(all_zero(full + 0x10, 0x30) && memcmp(full + 0x40, z + 0x6400, 0x100) == 0);
        CHECK(memcmp(full + 0x140, "PSTVFY1\0", 8) == 0);
        CHECK(le(full + 0x148, 8) == ((uint64_t)p->cases[0].discs[0]->verify_issues |
                                       ((uint64_t)p->cases[0].discs[0]->verify_accepted << 32)));
        CHECK(all_zero(full + 0x150, 0x1fc - 0x150) && le(full + 0x1fc, 4) == mh_crc32(full, 0x1fc));
    }
    for (i = 0; i < 4; i++) CHECK(z[0x16400 + i] == 0);
    free(pk);
    free(z);
    mh_project_clear(p);
    free(p);
}

/* ---- 2. SBI blob, flags, MODE2 and bad inputs of the track map (build_package_metadata) -------- */

static void test_audit_track_map(void)
{
    static MhDisc d;
    uint8_t rec[MH_V2_DISC_SIZE], out[MH_V2_PKG_META_BYTES];
    static uint8_t sbi[0x2e8];
    char err[256];
    unsigned i;

    memset(&d, 0, sizeof d);
    d.total_sectors = 1000;
    d.ntracks = 3;
    d.tracks[0] = (MhTrack){1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0};          /* MODE2 data */
    d.tracks[0].start = 0;
    d.tracks[1] = (MhTrack){2, 1, 0, 0x7, 0, 0, 100, 0, 0, 0, 0};      /* audio: DCP + ISRC + INDEX 00 */
    d.tracks[1].start = 400;
    d.tracks[1].pregap = 100;
    d.tracks[2] = (MhTrack){3, 1, 0, 0x1, 0, 0, 0, 0, 0, 0, 0};        /* audio: DCP only */
    d.tracks[2].start = 700;
    memset(rec, 0xee, sizeof rec);
    CHECK(mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    CHECK(le(rec + 0x6c, 4) == 0 && rec[0] == 0xee);                    /* only +0x6c is touched */
    CHECK(memcmp(out, "PSTTRK1\0", 8) == 0 && out[8] == 3 && out[9] == 0x10);
    /* orig: kind 2 -> kind bits 4; flags bit0 -> 2; flag byte (f*2&6)|(f>>2&1) */
    CHECK(out[0x1c] == 2 && out[0x1d] == 4 && out[0x1e] == 0);
    CHECK(le(out + 0x20, 4) == 300 && le(out + 0x24, 4) == 400 && le(out + 0x28, 4) == 400);
    CHECK(out[0x2c] == 0 && out[0x2d] == 2 && out[0x2e] == (uint8_t)(((0x7 << 1) & 6) | 1));
    CHECK(le(out + 0x30, 4) == 700 && le(out + 0x34, 4) == 700 && le(out + 0x38, 4) == 300);
    CHECK(out[0x3c] == 0 && out[0x3d] == 2 && out[0x3e] == 2);
    CHECK(mh_track_sector_valid(out, 1000));

    /* SBI from memory: lengths 4 and 0x2e8 accepted, 3 and 0x2e9 refused; wrong magic refused */
    memset(sbi, 0x5a, sizeof sbi);
    memcpy(sbi, "SBI\0", 4);
    d.sbi_data = sbi;
    d.sbi_len = 4;
    CHECK(mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    CHECK(le(out + 0xa, 2) == 1 && le(out + 0x640, 2) == 4 && le(out + 0x644, 4) == mh_crc32(sbi, 4));
    CHECK(memcmp(out + 0x648, sbi, 4) == 0 && all_zero(out + 0x64c, 0x930 - 0x64c));
    CHECK(mh_track_sector_valid(out, 1000));
    d.sbi_len = 0x2e8;
    CHECK(mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    CHECK(le(out + 0x640, 2) == 0x2e8 && memcmp(out + 0x648, sbi, 0x2e8) == 0 && mh_track_sector_valid(out, 1000));
    d.sbi_len = 3;
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    d.sbi_len = 0x2e9;
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    d.sbi_len = 4;
    sbi[1] = 'X';
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    CHECK_STR(err, "Nao foi possivel montar a Track Table deste disco.");
    sbi[1] = 'B';
    d.sbi_data = NULL;
    d.sbi_len = 0;

    /* an audio track 1, MODE3, equal begins, a gap in numbering: each refused */
    d.tracks[0].kind = 1;
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    d.tracks[0].kind = 2;
    d.tracks[0].mode = 0;
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    d.tracks[0].mode = 2;
    d.tracks[2].start = 400 - 100 + 100; /* begin 400 == begin of track 2 + 0: not strictly after */
    d.tracks[2].pregap = 100;
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    d.tracks[2].start = 700;
    d.tracks[2].pregap = 0;
    d.tracks[2].number = 4;
    CHECK(!mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    d.tracks[2].number = 3;
    CHECK(mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    /* 98 tracks use up to 0x10 + 98 * 16 = 0x640: the SBI offset */
    memset(&d, 0, sizeof d);
    d.total_sectors = 99 * 10;
    d.ntracks = 98;
    for (i = 0; i < 98; i++) {
        d.tracks[i].number = (uint8_t)(i + 1);
        d.tracks[i].kind = i == 0 ? 2 : 1;
        d.tracks[i].mode = i == 0 ? 1 : 0;
        d.tracks[i].start = i * 10;
    }
    CHECK(mh_v2_build_package_metadata(&d, rec, out, err, sizeof err));
    CHECK(mh_track_sector_valid(out, d.total_sectors));
}

/* ---- 3. edit a library in place: kept packages are not touched, a new disc takes the hole ------- */

static void test_audit_edit_inplace(void)
{
    MhProject *p = new_project(), *q = new_project();
    MhV2Result res, vres;
    char cue[MH_PATH_MAX], cache[MH_PATH_MAX], *img = tpath("edit.img"), *snap = tpath("edit_snap.img");
    uint8_t *before, *after, *ha;
    uint64_t szb, sza;
    MhV2DiscRec rec[3];
    unsigned i;
    MhV2Header h;
    uint8_t *tab;

    make_synth("ed_a", 600, 200, false, 31, cue);
    project_add(p, cue, 256);
    make_synth("ed_b", 800, 200, true, 32, cue);
    project_add(p, cue, 256);
    make_synth("ed_c", 500, 0, false, 33, cue);
    project_add(p, cue, 256);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    tab = pread_file(img, 0x6400, 0x300);
    for (i = 0; i < 3; i++) mh_v2_disc_decode(tab + i * 0x100, &rec[i], NULL);
    free(tab);

    snprintf(cache, sizeof cache, "%s/cache_ed", g_tmp);
    mh_ensure_dir(cache);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    mh_project_recount(q);
    CHECK(q->disc_count == 3);
    before = slurp_all(img, &szb);

    /* drop the middle case, add a smaller disc: it must land in the freed hole */
    CHECK(mh_project_remove_case(q, 1));
    make_synth("ed_d", 300, 0, false, 34, cue);
    CHECK(add_cue(q, cue, 256) == MH_ADD_OK);
    mh_project_recount(q);
    CHECK(q->disc_count == 3);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, q, img, &res));
    if (!res.ok) fprintf(stderr, "edit: %s\n", res.msg);
    CHECK(res.ok && res.discs == 3);
    CHECK(g_count_gravando >= 1);                  /* only the new disc is streamed */
    after = slurp_all(img, &sza);
    ha = after;
    mh_v2_header_decode(ha, &h);
    CHECK(h.generation == 2);
    {
        unsigned kept = 0;
        MhV2DiscRec nr;
        for (i = 0; i < 3; i++) {
            const uint8_t *r = after + 0x6400 + i * 0x100;
            mh_v2_disc_decode(r, &nr, NULL);
            if (nr.base_lba == rec[0].base_lba || nr.base_lba == rec[2].base_lba) {
                const MhV2DiscRec *o = nr.base_lba == rec[0].base_lba ? &rec[0] : &rec[2];
                /* the whole package of a kept disc is byte identical: head, gap, marker, RAW */
                size_t n = (size_t)MH_V2_PKG_BLOCKS(o->sectors) * 512;
                CHECK(memcmp(before + (o->base_lba << 9), after + (o->base_lba << 9), n) == 0);
                CHECK(nr.crc32 == o->crc32 && memcmp(nr.sha1, o->sha1, 20) == 0);
                kept++;
            } else {
                CHECK(nr.base_lba == rec[1].base_lba);   /* first fit: the hole of the removed disc */
                CHECK(nr.sectors == 450);   /* 300 data + the 150 sector tail make_synth adds */
            }
        }
        CHECK(kept == 2);
    }
    CHECK(sza <= szb);
    memset(&vres, 0, sizeof vres);
    CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
    if (!vres.ok) fprintf(stderr, "edit validate: %s\n", vres.msg);
    free(after);

    /* the same edit interrupted while the new disc is streamed: the kept discs survive byte for byte,
     * the header is invalid, and re-applying completes the library with the same result */
    {
        uint8_t *mid;
        uint64_t szm;
        unsigned k;
        MhProject *r = new_project();
        after = slurp_all(img, &sza);
        (void)after;
        free(after);
        /* restore the pre-edit image and reload it */
        {
            FILE *f = fopen(snap, "wb");
            assert(f && fwrite(before, 1, (size_t)szb, f) == szb);
            fclose(f);
        }
        snprintf(cache, sizeof cache, "%s/cache_ed2", g_tmp);
        mh_ensure_dir(cache);
        CHECK(mh_v2_load_image(snap, cache, &g_env, r, &vres));
        CHECK(mh_project_remove_case(r, 1));
        make_synth("ed_e", 300, 0, false, 35, cue);
        CHECK(add_cue(r, cue, 256) == MH_ADD_OK);
        mh_project_recount(r);
        reset_progress();
        g_cancel_text = "Gravando: ed_e.bin";
        CHECK(!mh_v2_write_image_direct(&g_w, r, snap, &res));
        CHECK(rd_fail_text(&res, "Gravacao interrompida. O disco atual ficou pendente"));
        mid = slurp_all(snap, &szm);
        CHECK(header_a_is_wip(snap));
        memset(&vres, 0, sizeof vres);
        CHECK(!mh_v2_validate_image(snap, NULL, &vres));
        for (k = 0; k < 3; k += 2) {
            size_t n = (size_t)MH_V2_PKG_BLOCKS(rec[k].sectors) * 512;
            CHECK(memcmp(before + (rec[k].base_lba << 9), mid + (rec[k].base_lba << 9), n) == 0);
        }
        free(mid);
        reset_progress();
        CHECK(mh_v2_write_image_direct(&g_w, r, snap, &res) && res.ok);
        memset(&vres, 0, sizeof vres);
        CHECK(mh_v2_validate_image(snap, NULL, &vres) && vres.ok);
        mh_project_clear(r);
        free(r);
    }
    free(before);
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}

/* ---- 4. an output path that is only a different name of a source file ----------------------- */

static void test_audit_alias_clash(void)
{
    MhProject *p = new_project();
    MhV2Result res;
    char cue[MH_PATH_MAX], lnk[MH_PATH_MAX], hard[MH_PATH_MAX];
    uint64_t before;
    MhDisc *d;

    make_synth("alias", 600, 0, false, 41, cue);
    project_add(p, cue, 256);
    d = p->cases[0].discs[0];
    before = file_size(d->files[0].path);
    snprintf(lnk, sizeof lnk, "%s/alias_link.img", g_tmp);
    snprintf(hard, sizeof hard, "%s/alias_hard.img", g_tmp);
    CHECK(symlink(d->files[0].path, lnk) == 0);
    CHECK(link(d->files[0].path, hard) == 0);
    reset_progress();
    CHECK(!mh_v2_write_image_direct(&g_w, p, lnk, &res));
    CHECK_STR(res.msg, "A imagem substituiria um BIN de origem.");
    CHECK(!mh_v2_write_image_direct(&g_w, p, hard, &res));
    CHECK_STR(res.msg, "A imagem substituiria um BIN de origem.");
    CHECK(file_size(d->files[0].path) == before);
    /* the cue through a path with a dot-dot and a doubled slash is the same file */
    {
        char alt[MH_PATH_MAX];
        snprintf(alt, sizeof alt, "%s//x/../alias.cue", g_tmp);
        CHECK(!mh_v2_write_image_direct(&g_w, p, alt, &res));
        CHECK_STR(res.msg, "A imagem substituiria um arquivo de origem.");
    }
    mh_project_clear(p);
    free(p);
}

/* ---- 4b. a project whose discs live in an image, saved onto that image without the "loaded" flag -- */

static void test_audit_source_is_target(void)
{
    MhProject *p = new_project(), *q = new_project();
    MhV2Result res, vres;
    char cue[MH_PATH_MAX], cache[MH_PATH_MAX], *img = tpath("src_is_tgt.img");
    uint64_t n1, n2;
    uint8_t *a, *b;

    make_synth("sit", 600, 200, false, 51, cue);
    project_add(p, cue, 256);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    snprintf(cache, sizeof cache, "%s/cache_sit", g_tmp);
    mh_ensure_dir(cache);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    a = slurp_all(img, &n1);
    q->image_loaded = 0;                 /* the flag the original used to pick OPEN_EXISTING */
    q->image_path[0] = 0;
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, q, img, &res));
    if (!res.ok) fprintf(stderr, "sit: %s\n", res.msg);
    b = slurp_all(img, &n2);
    CHECK(n1 == n2);
    CHECK(memcmp(a + 0x500000, b + 0x500000, (size_t)(n1 - 0x500000)) == 0);   /* RAW data survived */
    CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
    free(a);
    free(b);
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}

/* ---- 5. size of a raw device (macOS lseek(SEEK_END) returns 0 on /dev/rdisk) ------------------- */

static void test_audit_device_size(void)
{
    char raw[MH_PATH_MAX], cmd[2 * MH_PATH_MAX + 128], dev[64] = "", line[256];
    FILE *pp;
    MhIo io;
    uint64_t sz = 0;
    const uint64_t SZ = 8u << 20;
    int fd;

    snprintf(raw, sizeof raw, "%s/devsize.raw", g_tmp);
    fd = open(raw, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0 || ftruncate(fd, (off_t)SZ) != 0) { if (fd >= 0) close(fd); return; }
    close(fd);
    snprintf(cmd, sizeof cmd,
             "/usr/bin/hdiutil attach -nomount -nobrowse -imagekey diskimage-class=CRawDiskImage '%s' 2>/dev/null | head -1 | awk '{print $1}'",
             raw);
    pp = popen(cmd, "r");
    if (!pp) return;
    if (fgets(line, sizeof line, pp)) sscanf(line, "%63s", dev);
    pclose(pp);
    if (strncmp(dev, "/dev/disk", 9) != 0) {
        printf("device size test skipped (no disk image device)\n");
        return;
    }
    {   /* the raw node of the whole disk we just attached, read only */
        char rdev[80];
        snprintf(rdev, sizeof rdev, "/dev/r%s", dev + 5);
        if (mh_io_open(&io, rdev, MH_IO_READ, false, NULL)) {
            CHECK(mh_io_size(&io, &sz));
            CHECK(sz == SZ);
            mh_io_close(&io);
        } else {
            printf("device size test skipped (%s not readable)\n", rdev);
        }
    }
    snprintf(cmd, sizeof cmd, "/usr/bin/hdiutil detach '%s' >/dev/null 2>&1", dev);
    (void)system(cmd);
}

int main(void)
{
    const char *t = getenv("TMPDIR");
    snprintf(g_tmp, sizeof g_tmp, "%s/mhv2a-XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(g_tmp)) { perror("mkdtemp"); return 2; }
    if (!audit_setup()) {
        printf("fixtures absent, skipping the audit tests\n");
    } else {
        const char *only = getenv("MH_V2A_ONLY");
#define RUN(name, fn) do { if (!only || strstr(only, name)) fn(); } while (0)
        RUN("golden", test_audit_golden_zone);
        RUN("track", test_audit_track_map);
        RUN("edit", test_audit_edit_inplace);
        RUN("alias", test_audit_alias_clash);
        RUN("source", test_audit_source_is_target);
        RUN("devsize", test_audit_device_size);
#undef RUN
    }
    if (getenv("MH_V2W_KEEP")) printf("kept %s\n", g_tmp); else rm_rf(g_tmp);
    printf("test_v2_write_audit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
