/* v2 part A: mutation fuzzer for mh_v2_validate_image / mh_v2_load_image on hostile images.
 * The image comes from the builder of test_v2_image.c (3 discs, a cheat blob, disc and case art).
 * Mutations are applied in place on a file (only the 4 KiB pages that differ are rewritten) so an
 * iteration costs a few milliseconds. Three flavours: raw byte damage (mostly stopped by the CRC
 * checks), field damage with every CRC recomputed (reaches the deep checks) and a random subset of
 * the CRC fixes. Nothing may crash (ASan/UBSan), hang (alarm) or break the invariants below.
 * MH_FUZZ_ITERS (default 1000) and MH_FUZZ_SEED (default 1) override the run. */
#include <signal.h>

#pragma clang diagnostic ignored "-Wunused-function"
#define main image_test_main
#include "test_v2_image.c"
#undef main

#define PAGE 4096u

static uint64_t g_rs;
static uint64_t rnd64(void)
{
    g_rs ^= g_rs >> 12;
    g_rs ^= g_rs << 25;
    g_rs ^= g_rs >> 27;
    return g_rs * 0x2545F4914F6CDD1Dull;
}
static uint32_t rn(uint32_t n) { return n ? (uint32_t)(rnd64() % n) : 0; }

typedef struct Zone {
    uint64_t off, len;
} Zone;
static Zone g_zones[64];
static unsigned g_nzones, g_nstruct; /* the first g_nstruct zones are header / Case / Disc records */

static void zone(uint64_t off, uint64_t len)
{
    if (off + len > g_size) return;
    g_zones[g_nzones].off = off;
    g_zones[g_nzones].len = len;
    g_nzones++;
}

static const uint64_t k_interesting[] = {
    0, 1, 2, 3, 4, 5, 6, 0x62, 0x63, 0x64, 0x7f, 0x80, 0xff, 0x100, 0x101, 0x7fff, 0x8000, 0xffff, 0x2800,
    0x2801, 0x5000, 0x10000, 0x7fffffffull, 0x80000000ull, 0xffffffffull, 0x100000000ull, 0x7fffffffffffffffull,
    0x8000000000000000ull, 0xffffffffffffff00ull, 0x0080000000000000ull, 0x00ffffffffffff00ull, 0x14b4, 0x1685,
    0x22ec00, 0x22ec01, 0x2e8, 0x2e9, 0x640, 0x930, 0xa00};

static void put_n(uint8_t *c, uint64_t off, uint64_t v, unsigned w)
{
    unsigned i;
    if (off + w > g_size) return;
    for (i = 0; i < w; i++) c[off + i] = (uint8_t)(v >> (8 * i));
}

static void mutate(uint8_t *c)
{
    unsigned n = 1 + rn(4), k;
    for (k = 0; k < n; k++) {
        uint32_t pick = rn(100);
        uint64_t off;
        if (pick < 8) {
            off = rnd64() % 0x500000; /* anywhere in the first 5 MiB */
            c[off] ^= (uint8_t)(1u << rn(8));
        } else {
            const Zone *z = &g_zones[rn(10) < 6 ? rn(g_nstruct) : rn(g_nzones)];
            unsigned w = 1u << rn(4);
            uint64_t v = rn(3) == 0 ? rnd64() : k_interesting[rn(sizeof k_interesting / sizeof *k_interesting)];
            off = z->off + rnd64() % z->len;
            if (rn(4) == 0) c[off] ^= (uint8_t)(1u << rn(8));
            else put_n(c, off, v, w);
        }
    }
    if (rn(12) == 0) { /* a resume marker with a verification stamp on a random disc */
        MhV2Resume rs;
        unsigned i = rn(NDISC);
        uint8_t blk[512];
        memset(&rs, 0, sizeof rs);
        memcpy(rs.disc_rec, c + MH_V2_DISC_OFF + (size_t)i * MH_V2_DISC_SIZE, MH_V2_DISC_SIZE);
        rs.stamp = rnd64();
        rs.has_verify = rn(2);
        rs.verify_stamp = rnd64();
        mh_v2_resume_encode(&rs, blk);
        if (rn(4) == 0) blk[rn(512)] ^= 0xff;
        memcpy(c + (BASE[i] + 0xff) * 512, blk, 512);
    }
}

static void fixup(uint8_t *c, unsigned mask)
{
    unsigned i;
    uint32_t art_crc;
    if (mask & 1)
        for (i = 0; i < 256; i++) {
            uint8_t *r = c + MH_V2_CASE_OFF + (size_t)i * MH_V2_CASE_SIZE;
            if (r[4]) mh_v2_wr32(r + 0x4c, mh_crc_record(r, MH_V2_CASE_SIZE));
        }
    if (mask & 2)
        for (i = 0; i < 256; i++) {
            uint8_t *r = c + MH_V2_DISC_OFF + (size_t)i * MH_V2_DISC_SIZE;
            if (r[4]) mh_v2_wr32(r + 0xfc, mh_crc_record(r, MH_V2_DISC_SIZE));
        }
    if (mask & 4)
        for (i = 0; i < 256; i++) {
            const uint8_t *r = c + MH_V2_DISC_OFF + (size_t)i * MH_V2_DISC_SIZE;
            uint64_t lba = mh_v2_rd64(r + 0x44);
            if (r[4] && lba < (1ull << 40) && (lba << 9) + 0x930 <= g_size) {
                uint8_t *t = c + (lba << 9);
                if (memcmp(t, "PSTTRK1", 7) == 0) {
                    mh_v2_wr32(t + 0xc, 0);
                    mh_v2_wr32(t + 0xc, mh_crc32(t, 0x930));
                }
            }
        }
    if (mask & 8) mh_v2_wr32(c + MH_V2_CFG_OFF + MH_V2_CFG_CRC_OFF, mh_crc32(c + MH_V2_CFG_OFF, MH_V2_CFG_CRC_OFF));
    if (mask & 16)
        for (i = 0; i < 256; i++) {
            const uint8_t *r = c + MH_V2_DISC_OFF + (size_t)i * MH_V2_DISC_SIZE;
            uint64_t lba = mh_v2_rd64(r + 0x64);
            uint32_t len = mh_v2_rd32(r + 0x6c);
            if (r[4] && len >= 0x40 && len <= 0x2800 && lba < (1ull << 40) && (lba << 9) + len <= g_size) {
                uint8_t *b = c + (lba << 9);
                if (memcmp(b, "PSTCHT1", 7) == 0) {
                    uint32_t cr = mh_crc32_begin();
                    static const uint8_t zero[4];
                    cr = mh_crc32_update(cr, b, 0x3c);
                    cr = mh_crc32_update(cr, zero, 4);
                    cr = mh_crc32_update(cr, b + 0x40, len - 0x40);
                    mh_v2_wr32(b + 0x3c, mh_crc32_end(cr));
                }
            }
        }
    if (mask & 32) {
        if (memcmp(c + MH_V2_ART_OFF, g_img + MH_V2_ART_OFF, MH_V2_ART_REGION) == 0) art_crc = mh_v2_rd32(g_img + 0x58);
        else art_crc = mh_crc32(c + MH_V2_ART_OFF, MH_V2_ART_REGION);
        mh_v2_wr32(c + 0x50, mh_crc32(c + MH_V2_CASE_OFF, MH_V2_CASE_TABLE));
        mh_v2_wr32(c + 0x54, mh_crc32(c + MH_V2_DISC_OFF, MH_V2_DISC_TABLE));
        mh_v2_wr32(c + 0x58, art_crc);
    }
    if (mask & 64) mh_v2_wr32(c + MH_V2_HDR_CRC_OFF, mh_crc_record(c, MH_V2_HDR_SIZE));
    if (mask & 128) memcpy(c + 0xa00, c, 0xa00);
}

static void on_alarm(int s)
{
    (void)s;
    static const char m[] = "FUZZ HANG\n";
    (void)!write(2, m, sizeof m - 1);
    _exit(3);
}

/* histogram of the validator / loader texts reached (shows which rules the run exercised) */
static struct { char msg[96]; unsigned long n; char who; } g_hist[128];
static unsigned g_nhist;
static void hist(char who, const char *msg)
{
    unsigned i;
    for (i = 0; i < g_nhist; i++)
        if (g_hist[i].who == who && strncmp(g_hist[i].msg, msg, sizeof g_hist[i].msg - 1) == 0) {
            g_hist[i].n++;
            return;
        }
    if (g_nhist < 128) {
        snprintf(g_hist[g_nhist].msg, sizeof g_hist[g_nhist].msg, "%s", msg);
        g_hist[g_nhist].who = who;
        g_hist[g_nhist++].n = 1;
    }
}

static int g_fd;
static uint8_t *g_cur;
static uint8_t g_dirty[(0x600000 / PAGE) + 2];

int main(void)
{
    const char *e;
    unsigned long iters = 1000, seed = 1, it, nvalid = 0, nload = 0, nrec = 0;
    uint64_t cur_size;
    MhV2Env env;
    char cache[300], cmd[512];
    MhV2Result r;
    uint64_t pages;
    uint64_t sz_file;

    if ((e = getenv("MH_FUZZ_ITERS"))) iters = strtoul(e, NULL, 10);
    if ((e = getenv("MH_FUZZ_SEED"))) seed = strtoul(e, NULL, 10);
    snprintf(g_dir, sizeof g_dir, "/tmp/mhv2fuzz.XXXXXX");
    if (!mkdtemp(g_dir)) return 1;
    if (!mh_read_entire_file("resources/embedded/picostation_menu.bin", &g_menu, &g_menu_size) ||
        !mh_read_entire_file("resources/embedded/umbrella_music.vag", &g_music, &g_music_size)) {
        puts("fixtures missing, skipped");
        return 0;
    }
    snprintf(g_path, sizeof g_path, "%s/img.bin", g_dir);
    snprintf(cache, sizeof cache, "%s/cache", g_dir);
    g_size = build(&g_img);
    save(g_path, g_img, g_size);
    sz_file = g_size;
    cur_size = g_size;
    memset(&env, 0, sizeof env);
    env.menu = g_menu;
    env.menu_size = g_menu_size;
    assert(mh_v2_validate_image(g_path, NULL, &r));

    g_fd = open(g_path, O_RDWR);
    assert(g_fd >= 0);
    g_cur = malloc(g_size);
    assert(g_cur);
    pages = (g_size + PAGE - 1) / PAGE;

    zone(0, 0x100);
    zone(0x1b8, 0x48);
    zone(0xa00, 0x100);
    zone(MH_V2_CASE_OFF, 0x200);
    zone(MH_V2_CASE_OFF, 0x1e0);
    zone(MH_V2_DISC_OFF, 0x400);
    zone(MH_V2_DISC_OFF, 0x400);
    zone(MH_V2_DISC_OFF + 0x400, 0x200);
    g_nstruct = g_nzones;
    {
        unsigned i;
        for (i = 0; i < NDISC; i++) {
            zone(BASE[i] * 512, 0x930);
            zone(BASE[i] * 512 + 0x640, 0x300);
            zone((BASE[i] + 0x100) * 512, 0xa00 * 2);
            zone((BASE[i] + 0xff) * 512, 512);
        }
        zone((BASE[0] + 5) * 512, 0x200);
        zone((BASE[0] + 5) * 512, 0x100);
    }
    zone(MH_V2_CFG_OFF, 0x930);
    zone(MH_V2_MENU_OFF, 0xa00 * 2);
    zone(MH_V2_ART_OFF, 0x2800);
    zone(MH_V2_MUSIC_OFF, 0x2000);

    signal(SIGALRM, on_alarm);
    for (it = 0; it < iters; it++) {
        unsigned flavour, mask;
        uint64_t trunc = 0;
        bool vok, lok;
        unsigned long long p;
        MhProject *proj;
        MhV2Result vr, lr;

        g_rs = (seed + 1) * 0x9E3779B97F4A7C15ull ^ (it + 1) * 0xD1B54A32D192ED03ull;
        rnd64();
        memcpy(g_cur, g_img, g_size);
        flavour = rn(8);
        mutate(g_cur);
        mask = flavour < 2 ? 0 : flavour < 6 ? 0xff : rn(256);
        if (rn(10) == 0) mask |= 0xff;
        if (rn(10) == 0) { /* both headers gone: forces recover_v2_header_from_body */
            memset(g_cur, 0, 0x40);
            memset(g_cur + 0xa00, 0, 0x40);
            mask &= ~(64u | 128u);
        }
        fixup(g_cur, mask);
        if (rn(16) == 0) trunc = rnd64() % (g_size + 1);

        if (cur_size != sz_file) {
            assert(ftruncate(g_fd, (off_t)g_size) == 0);
            sz_file = g_size;
        }
        for (p = 0; p < pages; p++) {
            uint64_t off = p * PAGE, n = g_size - off > PAGE ? PAGE : g_size - off;
            bool want = memcmp(g_cur + off, g_img + off, n) != 0;
            if (want || g_dirty[p]) {
                assert(pwrite(g_fd, g_cur + off, n, (off_t)off) == (ssize_t)n);
                g_dirty[p] = want;
            }
        }
        cur_size = g_size;
        if (trunc) {
            assert(ftruncate(g_fd, (off_t)trunc) == 0);
            for (p = trunc / PAGE; p < pages; p++) g_dirty[p] = 1;
            cur_size = trunc;
            sz_file = trunc;
        }

        alarm(60);
        memset(&vr, 0, sizeof vr);
        vok = mh_v2_validate_image(g_path, NULL, &vr);
        assert(vok == (vr.ok != 0));
        proj = calloc(1, sizeof *proj);
        assert(proj);
        mh_project_init(proj);
        memset(&lr, 0, sizeof lr);
        lok = mh_v2_load_image(g_path, cache, &env, proj, &lr);
        assert(lok == (lr.ok != 0));
        hist('v', vok ? "ok" : vr.msg);
        hist('l', lok ? "ok" : lr.msg);
        if (vok) {
            unsigned z;
            bool padding_dirty = false;
            nvalid++;
            /* the validator reads 0x930 bytes of each header slot, the loader 0xA00 and wants the
             * padding zero (both as in the original): the one legitimate disagreement */
            for (z = MH_V2_HDR_SIZE; z < MH_V2_HDR_SLOT; z++)
                if (g_cur[z] || g_cur[0xa00 + z]) padding_dirty = true;
            if (!lok && !padding_dirty) {
                printf("FAIL it %lu: validate ok but load failed: %s\n", it, lr.msg);
                abort();
            }
        }
        if (lok) {
            unsigned ci, k;
            nload++;
            if (strstr(lr.msg, "recovered from the fixed tables")) nrec++;
            mh_v2_hydrate_project(proj, &env);
            for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
                const MhCase *cs = &proj->cases[ci];
                if (!cs->in_use) continue;
                assert(cs->disc_count >= 1 && cs->disc_count <= MH_CASE_MAX_DISCS);
                for (k = 0; k < cs->disc_count; k++) {
                    const MhDisc *d = cs->discs[k];
                    assert(d && d->ntracks <= MH_DISC_MAX_TRACKS && d->nfiles <= MH_DISC_MAX_FILES);
                    assert(strlen(d->serial) < sizeof d->serial && strlen(d->title) < sizeof d->title);
                }
            }
        }
        mh_project_clear(proj);
        free(proj);
        alarm(0);
    }
    printf("fuzz: %lu iterations (seed %lu), %lu valid images, %lu loaded, %lu recovered\n", iters, seed, nvalid,
           nload, nrec);
    {
        unsigned i;
        char tag[2] = {0, 0};
        for (i = 0; i < g_nhist; i++) {
            tag[0] = g_hist[i].who;
            if (getenv("MH_FUZZ_HIST")) printf("  %s %6lu  %s\n", tag, g_hist[i].n, g_hist[i].msg[0] ? g_hist[i].msg : "(no text)");
        }
        printf("fuzz: %u distinct validator/loader outcomes\n", g_nhist);
    }
    close(g_fd);
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
    (void)!system(cmd);
    return 0;
}
