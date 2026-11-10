/* v2 part A: validator and loader against hand built synthetic images, every failure text. */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mh/v2.h"
#include "v2_stubs.inc"

static uint8_t *g_menu;
static uint64_t g_menu_size;
static uint8_t *g_music;
static uint64_t g_music_size;
static char g_dir[256];

static uint32_t g_seed = 12345;
static uint8_t rnd(void) { g_seed = g_seed * 1664525u + 1013904223u; return (uint8_t)(g_seed >> 24); }

#define NDISC 3
static const uint32_t SECT[NDISC] = {40, 25, 33};
static uint64_t BASE[NDISC];

static void put(uint8_t *img, uint64_t off, const void *p, size_t n) { memcpy(img + off, p, n); }

static void fix_header(uint8_t *img)
{
    MhV2Header h;
    uint8_t slot[MH_V2_HDR_SLOT];
    mh_v2_header_decode(img, &h);
    h.crc_cases = mh_crc32(img + MH_V2_CASE_OFF, MH_V2_CASE_TABLE);
    h.crc_discs = mh_crc32(img + MH_V2_DISC_OFF, MH_V2_DISC_TABLE);
    h.crc_art = mh_crc32(img + MH_V2_ART_OFF, MH_V2_ART_REGION);
    mh_v2_header_encode(&h, slot);
    memcpy(img, slot, sizeof slot);
    memcpy(img + 0xa00, slot, sizeof slot);
}

/* builds an image in memory; returns size */
static uint64_t build(uint8_t **out)
{
    uint64_t size = 0, cur = MH_V2_FIXED_BLOCKS;
    uint8_t *img;
    unsigned i, k;
    MhV2Header h;
    uint8_t cfg[MH_V2_SLOT];
    for (i = 0; i < NDISC; i++) {
        BASE[i] = cur;
        cur += (0x100 + (uint64_t)SECT[i] * 5 + 0xff) & ~(uint64_t)0xff;
    }
    size = cur * 512;
    img = calloc(1, size);
    assert(img);
    /* menu */
    assert(g_menu_size % 0x930 == 0);
    for (k = 0; k < g_menu_size / 0x930; k++) put(img, MH_V2_MENU_OFF + (uint64_t)k * 0xa00, g_menu + (size_t)k * 0x930, 0x930);
    /* cfg + music */
    mh_v2_cfg_encode((uint32_t)g_music_size, mh_crc32(g_music, g_music_size), cfg);
    put(img, MH_V2_CFG_OFF, cfg, sizeof cfg);
    put(img, MH_V2_MUSIC_OFF, g_music, g_music_size);
    /* case art for case 0 */
    for (k = 0; k < MH_V2_ART_SIZE; k++) img[MH_V2_ART_OFF + k] = rnd();
    /* cases: 0 -> discs 0,1 ; 1 -> disc 2 */
    {
        MhV2CaseRec c;
        uint8_t rec[MH_V2_CASE_SIZE];
        memset(&c, 0, sizeof c);
        c.used = 1; c.disc_count = 2; c.index = 0; c.variant = 3; c.art_id = 0; c.disc_ids[0] = 0; c.disc_ids[1] = 1;
        strcpy(c.name, "GAME ONE"); strcpy(c.code, "SLUS-00001");
        mh_v2_case_encode(&c, rec);
        put(img, MH_V2_CASE_OFF, rec, sizeof rec);
        memset(&c, 0, sizeof c);
        c.used = 1; c.disc_count = 1; c.index = 1; c.art_id = 0xffff; c.disc_ids[0] = 2;
        strcpy(c.name, "GAME TWO"); strcpy(c.code, "SLUS-00002");
        mh_v2_case_encode(&c, rec);
        put(img, MH_V2_CASE_OFF + MH_V2_CASE_SIZE, rec, sizeof rec);
    }
    for (i = 0; i < NDISC; i++) {
        MhV2DiscRec d;
        uint8_t rec[MH_V2_DISC_SIZE], trk[0xa00];
        uint64_t base = BASE[i];
        MhSha1 sha;
        uint32_t crc = mh_crc32_begin();
        memset(&d, 0, sizeof d);
        d.used = 1; d.id = (uint8_t)i; d.case_index = i == 2 ? 1 : 0; d.slot = i == 1 ? 1 : 0; d.flags = MH_V2_DF_EXT;
        snprintf(d.serial, sizeof d.serial, "SLUS-0000%u", i + 1); snprintf(d.title, sizeof d.title, "Disc %u", i + 1);
        d.base_lba = base; d.track_lba = base; d.raw_lba = base + 0x100; d.raw_blocks = (uint64_t)SECT[i] * 5;
        d.sectors = SECT[i]; d.track_count = 1; d.bin_count = 1; d.art_id = i == 0 ? 0 : 0xffff;
        d.cheat_lba = base + 5; d.cheat_size = 0; d.extra_lba = base + 0x19; d.extra_size = 0xa00;
        /* track map */
        memset(trk, 0, sizeof trk);
        memcpy(trk, "PSTTRK1", 7); trk[8] = 1; trk[9] = 0x10;
        mh_v2_wr32(trk + 0x18, SECT[i]); trk[0x1c] = 2; trk[0x1d] = 4;
        mh_v2_wr32(trk + 0xc, mh_crc32(trk, 0x930));
        put(img, base * 512, trk, sizeof trk);
        /* raw data */
        mh_sha1_init(&sha);
        for (k = 0; k < SECT[i]; k++) {
            uint8_t *s = img + (d.raw_lba + (uint64_t)k * 5) * 512;
            unsigned b;
            for (b = 0; b < 0x930; b++) s[b] = rnd();
            s[15] = 2;
            crc = mh_crc32_update(crc, s, 0x930);
            mh_sha1_update(&sha, s, 0x930);
        }
        d.crc32 = mh_crc32_end(crc);
        mh_sha1_final(&sha, d.sha1);
        if (i == 0) { /* disc art + a cheat blob */
            MhCheatProfile *p = mh_cheat_profile_new();
            uint8_t line[6] = {0x80, 0x01, 0x02, 0x03, 0x00, 0x05}, *blob = NULL;
            uint32_t blen = 0;
            char e[256];
            for (k = 0; k < MH_V2_ART_SIZE; k++) img[(base + 0x1e) * 512 + k] = rnd();
            if (p && mh_cheat_profile_add_custom(p, "TEST", line, 1, 0) &&
                mh_cheat_profile_serialize(p, 2, &blob, &blen, e, sizeof e) && blen) {
                put(img, (base + 5) * 512, blob, blen);
                d.cheat_size = blen;
            }
            mh_free(blob);
            mh_cheat_profile_free(p);
        }
        mh_v2_disc_encode(&d, rec);
        put(img, MH_V2_DISC_OFF + (uint64_t)i * MH_V2_DISC_SIZE, rec, sizeof rec);
    }
    memset(&h, 0, sizeof h);
    h.flags = 3; h.case_count = 2; h.disc_count = NDISC; h.generation = 4; h.menu_sectors = (uint32_t)(g_menu_size / 0x930);
    {
        MhSha1 sha;
        uint32_t crc = mh_crc32_begin();
        mh_sha1_init(&sha);
        for (k = 0; k < h.menu_sectors; k++) {
            crc = mh_crc32_update(crc, g_menu + (size_t)k * 0x930, 0x930);
            mh_sha1_update(&sha, g_menu + (size_t)k * 0x930, 0x930);
        }
        h.crc_menu = mh_crc32_end(crc);
        mh_sha1_final(&sha, h.menu_sha1);
    }
    h.mbr_sectors = mh_v2_mbr_sectors(size / 512);
    {
        uint8_t slot[MH_V2_HDR_SLOT];
        mh_v2_header_encode(&h, slot);
        memcpy(img, slot, sizeof slot);
        memcpy(img + 0xa00, slot, sizeof slot);
    }
    fix_header(img);
    *out = img;
    return size;
}

static void save(const char *path, const uint8_t *img, uint64_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0 && write(fd, img, size) == (ssize_t)size);
    close(fd);
}

static int cancel_cb_hits;
static const char *validate_msg(const char *path)
{
    static MhV2Result r;
    bool ok = mh_v2_validate_image(path, NULL, &r);
    assert(ok == (r.ok != 0));
    return ok ? "" : r.msg;
}

static uint8_t *g_img;
static uint64_t g_size;
static char g_path[300];

/* mutate a copy with fn, write it, return the validator message */
typedef void (*MutFn)(uint8_t *img);
static const char *mutated(MutFn fn, bool fix)
{
    uint8_t *c = malloc(g_size);
    const char *m;
    memcpy(c, g_img, g_size);
    fn(c);
    if (fix) fix_header(c);
    save(g_path, c, g_size);
    free(c);
    m = validate_msg(g_path);
    return m;
}
#define EXPECT(fn, fix, text) do { const char *m_ = mutated(fn, fix); if (strcmp(m_, text) != 0) { printf("FAIL %s: got '%s'\n", #fn, m_); abort(); } } while (0)

static void case_rec_fix(uint8_t *img, unsigned i)
{
    uint8_t *r = img + MH_V2_CASE_OFF + (size_t)i * MH_V2_CASE_SIZE;
    mh_v2_wr32(r + 0x4c, mh_crc_record(r, MH_V2_CASE_SIZE));
}
static void disc_rec_fix(uint8_t *img, unsigned i)
{
    uint8_t *r = img + MH_V2_DISC_OFF + (size_t)i * MH_V2_DISC_SIZE;
    mh_v2_wr32(r + 0xfc, mh_crc_record(r, MH_V2_DISC_SIZE));
}
static uint8_t *crec(uint8_t *img, unsigned i) { return img + MH_V2_CASE_OFF + (size_t)i * MH_V2_CASE_SIZE; }
static uint8_t *drec(uint8_t *img, unsigned i) { return img + MH_V2_DISC_OFF + (size_t)i * MH_V2_DISC_SIZE; }

static void m_hdr(uint8_t *i) { i[0x20] ^= 1; }
static void m_hdrb(uint8_t *i) { i[0xa00 + 0x20] ^= 1; }
static void m_tab_nofix(uint8_t *i) { i[MH_V2_CASE_OFF + 8] ^= 1; }
static void m_case_magic(uint8_t *i) { crec(i, 0)[0] = 'X'; case_rec_fix(i, 0); }
static void m_case_idx(uint8_t *i) { crec(i, 1)[6] = 9; case_rec_fix(i, 1); }
static void m_case_cnt(uint8_t *i) { crec(i, 1)[5] = 6; case_rec_fix(i, 1); }
static void m_case_art(uint8_t *i) { mh_v2_wr16(crec(i, 0) + 0x38, 7); case_rec_fix(i, 0); }
static void m_case_dup(uint8_t *i) { crec(i, 1)[0x3a] = 0; case_rec_fix(i, 1); }
static void m_case_junk(uint8_t *i) { crec(i, 1)[0x3b] = 2; case_rec_fix(i, 1); }
static void m_case_count(uint8_t *i) { i[8] = 3; }
static void m_disc_empty(uint8_t *i) { drec(i, 2)[4] = 0; }
static void m_disc_slot(uint8_t *i) { drec(i, 1)[7] = 0; disc_rec_fix(i, 1); }
static void m_disc_unref(uint8_t *i) { drec(i, 5)[4] = 1; }
static void m_disc_layout(uint8_t *i) { mh_v2_wr64(drec(i, 0) + 0x64, 1); disc_rec_fix(i, 0); }
static void m_disc_crc(uint8_t *i) { drec(i, 0)[0xc] ^= 1; }
static void m_trk(uint8_t *i) { i[BASE[0] * 512 + 0x40] ^= 1; }
static void m_trk_pad(uint8_t *i) { i[BASE[0] * 512 + 0x930 + 3] = 1; }
static void m_cheat(uint8_t *i) { i[(BASE[0] + 5) * 512 + 20] ^= 0xff; }
static void m_dart(uint8_t *i) { mh_v2_wr16(drec(i, 1) + 0x62, 3); disc_rec_fix(i, 1); }
static void m_raw_pad(uint8_t *i) { i[(BASE[1] + 0x100) * 512 + 0x935] = 1; }
static void m_raw_data(uint8_t *i) { i[(BASE[2] + 0x100) * 512 + 100] ^= 1; }
static void m_disc_count(uint8_t *i) { i[10] = 9; }
static void m_menu_pad(uint8_t *i) { i[MH_V2_MENU_OFF + 0x931] = 1; }
static void m_menu_data(uint8_t *i) { i[MH_V2_MENU_OFF + 0x100] ^= 1; }
static void m_cfg(uint8_t *i) { i[MH_V2_CFG_OFF + 0x30] ^= 1; }
static void m_music(uint8_t *i) { i[MH_V2_MUSIC_OFF + 1000] ^= 1; }

static void cancel_cb(void *u, uint64_t d, uint64_t t, const char *x) { (void)u; (void)d; (void)t; (void)x; cancel_cb_hits++; }

static void fixup_art(uint8_t *i) { (void)i; }

int main(void)
{
    uint8_t *tmp;
    char cmd[512];
    MhV2Result r;
    snprintf(g_dir, sizeof g_dir, "/tmp/mhv2test.XXXXXX");
    if (!mkdtemp(g_dir)) return 1;
    if (!mh_read_entire_file("resources/embedded/picostation_menu.bin", &g_menu, &g_menu_size) ||
        !mh_read_entire_file("resources/embedded/umbrella_music.vag", &g_music, &g_music_size)) {
        puts("fixtures missing, skipped");
        return 0;
    }
    (void)tmp; (void)fixup_art;
    snprintf(g_path, sizeof g_path, "%s/img.bin", g_dir);
    g_size = build(&g_img);
    save(g_path, g_img, g_size);

    /* the good image */
    assert(mh_v2_validate_image(g_path, NULL, &r));
    assert(r.ok && r.cases == 2 && r.discs == 3 && r.size == g_size);
    {
        uint8_t sha[20];
        mh_sha1(g_img, g_size, sha);
        assert(memcmp(sha, r.sha1, 20) == 0 && r.crc32 == mh_crc32(g_img, g_size));
    }
    assert(strstr(r.msg, "Valid PSXRAW V2") == r.msg);
    assert(mh_v2_validate_image(g_path, NULL, NULL));
    {
        MhProgress p;
        mh_progress_init(&p, cancel_cb, NULL);
        assert(mh_v2_validate_image(g_path, &p, &r) && cancel_cb_hits >= 3);
        mh_progress_request_cancel(&p);
        assert(!mh_v2_validate_image(g_path, &p, &r) && strcmp(r.msg, "Validation cancelled.") == 0);
    }
    assert(strcmp(validate_msg("/nonexistent/x"), "V2 image is missing or too small.") == 0);

    EXPECT(m_hdr, false, "V2 header is invalid or differs from its backup.");
    EXPECT(m_hdrb, false, "V2 header is invalid or differs from its backup.");
    EXPECT(m_tab_nofix, false, "CRC of the V2 tables and art does not match.");
    EXPECT(m_case_magic, true, "Invalid V2 Case record.");
    EXPECT(m_case_idx, true, "Invalid V2 Case record.");
    EXPECT(m_case_cnt, true, "Invalid V2 Case record.");
    EXPECT(m_case_art, true, "Invalid Case art ID.");
    EXPECT(m_case_dup, true, "A Case has a duplicate or invalid Disc reference.");
    EXPECT(m_case_junk, true, "A Case has stray data after its last Disc.");
    EXPECT(m_case_count, true, "case_count does not match.");
    EXPECT(m_disc_empty, true, "A Case references an empty DiscRecord.");
    EXPECT(m_disc_slot, true, "Invalid V2 DiscRecord.");
    EXPECT(m_disc_unref, true, "Invalid V2 DiscRecord.");
    EXPECT(m_disc_layout, true, "Invalid V2 DiscRecord.");
    EXPECT(m_disc_crc, true, "Invalid V2 DiscRecord.");
    EXPECT(m_trk, true, "Invalid V2 Track Table.");
    EXPECT(m_trk_pad, true, "Track Table padding is not zeroed.");
    EXPECT(m_cheat, true, "Invalid V2 cheats block.");
    EXPECT(m_dart, true, "Invalid disc_art_id.");
    EXPECT(m_raw_pad, true, "RAW data padding is not zeroed.");
    EXPECT(m_raw_data, true, "Hash of a V2 Disc does not match.");
    EXPECT(m_disc_count, true, "disc_count does not match.");
    EXPECT(m_menu_pad, true, "V2 menu padding is not zeroed.");
    EXPECT(m_menu_data, true, "V2 menu does not match.");
    EXPECT(m_cfg, true, "Invalid V2 menu global configuration.");
    EXPECT(m_music, true, "Invalid V2 menu global music.");

    /* the loader */
    save(g_path, g_img, g_size);
    {
        MhProject *p = calloc(1, sizeof *p);
        char cache[300];
        MhV2Env env;
        uint8_t *a;
        uint64_t asz;
        memset(&env, 0, sizeof env);
        env.menu = g_menu; env.menu_size = g_menu_size;
        snprintf(cache, sizeof cache, "%s/cache", g_dir);
        mh_project_init(p);
        assert(mh_v2_load_image(g_path, cache, &env, p, &r));
        assert(r.ok && r.cases == 2 && r.discs == 3 && r.size == g_size);
        assert(strcmp(r.msg, "PSXRAW V2 image loaded for editing.") == 0);
        assert(p->case_count == 2 && p->disc_count == 3 && p->image_loaded && !p->card_loaded && !p->dirty);
        assert(p->reserved_c == 4 && strcmp(p->image_path, g_path) == 0);
        assert(p->cases[0].disc_count == 2 && p->cases[1].disc_count == 1 && p->cases[0].pad5 == 3);
        assert(strcmp(p->cases[0].title, "GAME ONE") == 0 && strcmp(p->cases[0].serial, "SLUS-00001") == 0);
        {
            MhDisc *d = p->cases[0].discs[0];
            assert(d->kind == 5 && d->status == 1 && d->flags == (0xc001 | 8 | 0x1000) && d->ntracks == 1);
            assert(d->total_sectors == 40 && d->total_bytes == 40 * 2352 && d->nfiles == 1);
            assert(strcmp(d->serial, "SLUS-00001") == 0 && strcmp(d->title, "Disc 1") == 0 && d->card_index == 0);
            assert(d->tracks[0].kind == 2 && d->tracks[0].mode == 2 && d->tracks[0].length == 40 && d->tracks[0].number == 1);
            assert(d->v2_pkg_a == BASE[0] && d->v2_raw_lba512 == BASE[0] + 0x100 && d->v2_raw_blocks == 200);
            assert(d->cheats != NULL && d->cheats->count == 1);
            assert(mh_read_entire_file(d->cd_art_path, &a, &asz) && asz == 0x2800 &&
                   memcmp(a, g_img + (BASE[0] + 0x1e) * 512, 0x2800) == 0);
            free(a);
            assert(strcmp(p->cases[0].art_path, "") != 0);
            assert(mh_read_entire_file(p->cases[0].art_path, &a, &asz) && asz == 0x2800 &&
                   memcmp(a, g_img + MH_V2_ART_OFF, 0x2800) == 0);
            free(a);
        }
        assert(p->has_menu_defaults == 1);
        {
            char mp[400];
            snprintf(mp, sizeof mp, "%s/menu.bin", cache);
            assert(mh_read_entire_file(mp, &a, &asz) && asz == g_menu_size && memcmp(a, g_menu, asz) == 0);
            free(a);
        }
        mh_v2_hydrate_project(p, &env);
        mh_project_clear(p);

        /* failure leaves the project alone */
        mh_project_init(p);
        p->dirty = 7;
        {
            uint8_t *c = malloc(g_size);
            memcpy(c, g_img, g_size);
            m_case_magic(c);
            fix_header(c);
            save(g_path, c, g_size);
            free(c);
        }
        assert(!mh_v2_load_image(g_path, cache, &env, p, &r) && p->dirty == 7);
        assert(strcmp(r.msg, "The card has an invalid Case record.") == 0);

        /* both headers gone: recovered from the body */
        {
            uint8_t *c = malloc(g_size);
            memcpy(c, g_img, g_size);
            memset(c, 0, 0x1400);
            save(g_path, c, g_size);
            assert(mh_v2_load_image(g_path, cache, &env, p, &r));
            assert(strstr(r.msg, "V2 library recovered from the fixed tables") == r.msg && p->dirty == 1);
            mh_project_clear(p);
            /* an interrupted write marker refuses the recovery */
            memcpy(c, "PSTWIP1", 7);
            save(g_path, c, g_size);
            assert(!mh_v2_load_image(g_path, cache, &env, p, &r));
            assert(strstr(r.msg, "Library has no valid commit.") == r.msg);
            /* damaged single-track map: rebuilt from the first RAW sector */
            memcpy(c, g_img, g_size);
            c[BASE[1] * 512 + 0x40] ^= 1;
            save(g_path, c, g_size);
            assert(mh_v2_load_image(g_path, cache, &env, p, &r));
            assert(strstr(r.msg, "PSXRAW V2 recovered without changing the layout: 1 single-track Track Table") == r.msg);
            assert(p->dirty == 1 && p->cases[0].discs[1]->ntracks == 1 && p->cases[0].discs[1]->tracks[0].mode == 2);
            assert(strstr(p->cases[0].discs[1]->notes, "rebuilt exactly") != NULL);
            mh_project_clear(p);
            free(c);
        }
        free(p);
    }
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
    (void)!system(cmd);
    puts("v2 image ok");
    return 0;
}
