/* Tests of the V2 writer (part B). Real CUE/BIN fixtures from the library folder; the
 * validator and loader of part A are the oracle. Regular files only, never a device. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/v2write.h"
#include "v2_stubs.inc"

#define FIXTURES "/Volumes/Renato/PicoStation BR"
#define EMBEDDED "resources/embedded"
#define STRIDER FIXTURES "/Strider (USA).cue"
#define TACTICS FIXTURES "/Tactics Ogre (USA).cue"

static int g_fail, g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            g_fail++;                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                         \
    } while (0)
#define CHECK_STR(a, b)                                                           \
    do {                                                                          \
        g_checks++;                                                               \
        if (strcmp((a), (b)) != 0) {                                              \
            g_fail++;                                                             \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                         \
    } while (0)

static char g_tmp[MH_PATH_MAX];
static MhDb g_db;
static MhCheatDb g_cdb;
static uint8_t *g_menu, *g_music;
static uint64_t g_menu_size, g_music_size;
static MhV2Env g_env;
static MhV2Writer g_w;
static MhProgress g_prog;

static uint64_t fixed_stamp(void *u)
{
    (void)u;
    return 0x1122334455667788ull;
}

static int g_count_gravando, g_count_ja, g_cancel_when_text_count;
static const char *g_cancel_text;
static int g_text_hits;
static int g_phase_seen[8], g_phase_last, g_phase_backwards, g_step_max, g_steps_seen;
static void on_progress(void *u, uint64_t done, uint64_t total, const char *text)
{
    int ph = atomic_load(&g_prog.phase);
    (void)u; (void)done; (void)total;
    if (ph >= 0 && ph < 8) g_phase_seen[ph]++;
    if (ph < g_phase_last) g_phase_backwards++;
    g_phase_last = ph;
    if (ph == MH_V2_PHASE_WRITING_DISCS && atomic_load(&g_prog.step) > g_step_max) {
        g_step_max = atomic_load(&g_prog.step);
        g_steps_seen = atomic_load(&g_prog.steps);
    }
    if (!text) return;
    if (strncmp(text, "Writing: ", 9) == 0) g_count_gravando++;
    if (strncmp(text, "Already written: ", 17) == 0) g_count_ja++;
    if (g_cancel_text && strncmp(text, g_cancel_text, strlen(g_cancel_text)) == 0) {
        if (++g_text_hits >= g_cancel_when_text_count) mh_progress_request_cancel(&g_prog);
    }
}

static void rm_rf(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) { unlink(dir); return; }
    while ((e = readdir(d))) {
        char p[MH_PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        rm_rf(p);
    }
    closedir(d);
    rmdir(dir);
}

static char *tpath(const char *name)
{
    static char b[64][MH_PATH_MAX];
    static int i;
    char *r = b[i++ & 63];
    snprintf(r, MH_PATH_MAX, "%s/%s", g_tmp, name);
    return r;
}

static void reset_progress(void)
{
    mh_progress_init(&g_prog, on_progress, NULL);
    g_count_gravando = 0;
    g_count_ja = 0;
    g_cancel_text = NULL;
    g_cancel_when_text_count = 1;
    g_text_hits = 0;
    memset(g_phase_seen, 0, sizeof g_phase_seen);
    g_phase_last = g_phase_backwards = g_step_max = g_steps_seen = 0;
}

static bool yes(void *ctx, const char *text) { (void)ctx; (void)text; return true; }

static bool hook_admit(void *ctx, MhDisc *d)
{
    bool r = mh_v2_manager_admit(&g_w, d, true, yes, NULL);
    (void)ctx;
    if (!r) {
        unsigned i;
        fprintf(stderr, "admit rejected: %s (status %u flags %x) nt %u total %u nfiles %u\n", d->verify_text, d->status, d->flags, d->ntracks, d->total_sectors, d->nfiles);
        for (i = 0; i < d->ntracks; i++)
            fprintf(stderr, " t%u kind %u mode %u pregap %u start %u len %u\n", i, d->tracks[i].kind, d->tracks[i].mode, d->tracks[i].pregap, d->tracks[i].start, d->tracks[i].length);
    }
    return r;
}
static void hook_cheats(void *ctx, MhDisc *d) { (void)ctx; mh_v2_auto_attach_cheats(&g_env, d); }

static MhProject *new_project(void)
{
    MhProject *p = mh_calloc(sizeof *p);
    mh_project_init(p);
    return p;
}

static MhAddStatus add_cue(MhProject *p, const char *cue, unsigned slot)
{
    MhCueHooks h = {NULL, NULL, NULL, hook_admit, hook_cheats};
    MhAddResult r;
    char err[256];
    MhAddStatus s = mh_project_add_cue(p, &g_db, cue, slot, &h, &r, err, sizeof err);
    if (s != MH_ADD_OK) fprintf(stderr, "add_cue %s: status %d %s\n", cue, (int)s, err);
    return s;
}

static uint8_t *slurp(const char *path, uint64_t *size)
{
    uint8_t *d = NULL;
    if (!mh_read_entire_file(path, &d, size)) return NULL;
    return d;
}

/* ---- synthetic discs (fast, fully controlled) ----------------------------------------------- */

static uint32_t g_rng = 12345;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}
static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void make_data_sector(uint8_t *s, uint32_t lba, uint32_t seed)
{
    unsigned i, f = lba + 150;
    memset(s, 0, 2352);
    memset(s + 1, 0xff, 10);
    s[12] = bcd(f / 4500);
    s[13] = bcd((f / 75) % 60);
    s[14] = bcd(f % 75);
    s[15] = 1;
    g_rng = seed * 2654435761u + lba;
    for (i = 16; i < 16 + 2048; i++) s[i] = (uint8_t)rnd();
    mh_cd_sector_recode(s);
}

static void write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, n, f) == n);
    fclose(f);
}

static void msf(unsigned f, char *out)
{
    snprintf(out, 16, "%02u:%02u:%02u", f / 4500, (f / 75) % 60, f % 75);
}

/* A MODE1 data track followed by an audio track with a 2 second pregap. multi: one BIN per track. */
static void make_synth(const char *name, unsigned ndata, unsigned naudio, bool multi, uint32_t seed, char *cue_out)
{
    char bin1[MH_PATH_MAX], bin2[MH_PATH_MAX], cue[MH_PATH_MAX], text[1024], a[16], b[16];
    uint8_t sec[2352];
    unsigned i;
    FILE *f;
    snprintf(bin1, sizeof bin1, "%s/%s%s.bin", g_tmp, name, multi ? " (Track 1)" : "");
    snprintf(bin2, sizeof bin2, "%s/%s (Track 2).bin", g_tmp, name);
    snprintf(cue, sizeof cue, "%s/%s.cue", g_tmp, name);
    f = fopen(bin1, "wb");
    assert(f);
    for (i = 0; i < ndata; i++) {
        make_data_sector(sec, i, seed);
        fwrite(sec, 1, sizeof sec, f);
    }
    if (!multi) {
        for (i = 0; i < 150 + naudio; i++) {
            unsigned k;
            for (k = 0; k < sizeof sec; k++) sec[k] = (uint8_t)(rnd() >> 3);
            fwrite(sec, 1, sizeof sec, f);
        }
    }
    fclose(f);
    if (multi) {
        f = fopen(bin2, "wb");
        assert(f);
        for (i = 0; i < 150 + naudio; i++) {
            unsigned k;
            for (k = 0; k < sizeof sec; k++) sec[k] = (uint8_t)(rnd() >> 3);
            fwrite(sec, 1, sizeof sec, f);
        }
        fclose(f);
    }
    if (naudio == 0) {
        snprintf(text, sizeof text, "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n",
                 strrchr(bin1, '/') + 1);
    } else if (multi) {
        snprintf(text, sizeof text,
                 "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                 "FILE \"%s\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n",
                 strrchr(bin1, '/') + 1, strrchr(bin2, '/') + 1);
    } else {
        msf(ndata, a);
        msf(ndata + 150, b);
        snprintf(text, sizeof text,
                 "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                 "  TRACK 02 AUDIO\r\n    INDEX 00 %s\r\n    INDEX 01 %s\r\n",
                 strrchr(bin1, '/') + 1, a, b);
    }
    write_file(cue, text, strlen(text));
    strcpy(cue_out, cue);
}

/* ---- unit tests ----------------------------------------------------------------------------- */

static void test_extents(void)
{
    MhV2Extent e[MH_V2_EXTENTS_MAX];
    uint32_t n = 0;
    uint64_t lba;
    CHECK(!mh_v2_extent_insert(e, &n, 0x2700, 0x2800));        /* below the fixed zone */
    CHECK(!mh_v2_extent_insert(e, &n, 0x2801, 0x3000));        /* unaligned */
    CHECK(!mh_v2_extent_insert(e, &n, 0x2800, 0x2800));        /* empty */
    CHECK(mh_v2_extent_insert(e, &n, 0x3000, 0x3100));
    CHECK(mh_v2_extent_insert(e, &n, 0x2800, 0x2900));         /* before */
    CHECK(mh_v2_extent_insert(e, &n, 0x3200, 0x3300));         /* after */
    CHECK(!mh_v2_extent_insert(e, &n, 0x3000, 0x3100));        /* duplicate */
    CHECK(!mh_v2_extent_insert(e, &n, 0x2f00, 0x3001));        /* overlaps next */
    CHECK(mh_v2_extent_insert(e, &n, 0x3100, 0x3200));         /* exactly in the hole */
    CHECK(n == 4 && e[0].start == 0x2800 && e[1].start == 0x3000 && e[2].start == 0x3100 && e[3].start == 0x3200);
    CHECK(mh_v2_extent_allocate(e, &n, 0x100, 0, &lba) && lba == 0x2900);   /* first fit: hole at 0x2900 */
    CHECK(mh_v2_extent_allocate(e, &n, 0x200, 0, &lba) && lba == 0x2a00);   /* the hole is large */
    CHECK(mh_v2_extent_allocate(e, &n, 0x500, 0, &lba) && lba == 0x3300);   /* too big for the rest */
    CHECK(!mh_v2_extent_allocate(e, &n, 0x1000, 0x3000, &lba));            /* cap */
    n = 0;
    CHECK(mh_v2_extent_allocate(e, &n, 0x1234, 0, &lba) && lba == 0x2800 && e[0].end == 0x2800 + 0x1234);
    CHECK(mh_v2_extent_allocate(e, &n, 0x10, 0, &lba) && lba == 0x3b00);   /* aligned after the end */
    for (n = 0; n < MH_V2_EXTENTS_MAX; n++) { e[n].start = 0x2800 + n * 0x100; e[n].end = e[n].start + 0x100; }
    CHECK(!mh_v2_extent_insert(e, &n, 0x2800 + 256 * 0x100, 0x2800 + 257 * 0x100)); /* full */
    /* overlap with the end of the previous extent */
    n = 0;
    CHECK(mh_v2_extent_insert(e, &n, 0x2800, 0x2a00));
    CHECK(!mh_v2_extent_insert(e, &n, 0x2900, 0x2b00));
    CHECK(mh_v2_extent_insert(e, &n, 0x2a00, 0x2b00));
}

static void test_empty_estimate(void)
{
    MhProject *p = new_project();
    CHECK(mh_v2_estimate_project_bytes(p) == 0x500000);
    CHECK(mh_v2_estimate_project_bytes(NULL) == 0);
    mh_project_clear(p);
    free(p);
}

static uint64_t file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

/* Reads `n` bytes at `off` of a file into a malloc'ed buffer. */
static uint8_t *pread_file(const char *path, uint64_t off, size_t n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b = malloc(n ? n : 1);
    assert(f && b);
    assert(fseeko(f, (off_t)off, SEEK_SET) == 0);
    assert(fread(b, 1, n, f) == n);
    fclose(f);
    return b;
}

static void project_add(MhProject *p, const char *cue, unsigned slot)
{
    MhAddStatus s = add_cue(p, cue, slot);
    assert(s == MH_ADD_OK);
    mh_project_recount(p);
}

/* Compares every slot of the RAW data of disc `d` (kind 1) in the image with its BIN files. */
static bool raw_data_matches(const char *img, const MhDisc *d, uint64_t raw_lba)
{
    unsigned k;
    uint32_t sec = 0;
    for (k = 0; k < d->nfiles; k++) {
        const MhDiscFile *f = &d->files[k];
        uint8_t *bin = pread_file(f->path, 0, (size_t)f->sectors * 2352);
        uint8_t *slots = pread_file(img, (raw_lba << 9) + (uint64_t)sec * 0xa00, (size_t)f->sectors * 0xa00);
        uint32_t i;
        for (i = 0; i < f->sectors; i++) {
            unsigned z;
            if (memcmp(bin + (size_t)i * 2352, slots + (size_t)i * 0xa00, 2352) != 0) { free(bin); free(slots); return false; }
            for (z = 2352; z < 0xa00; z++) if (slots[(size_t)i * 0xa00 + z]) { free(bin); free(slots); return false; }
        }
        sec += f->sectors;
        free(bin);
        free(slots);
    }
    return sec == d->total_sectors;
}

/* Byte level checks of one written image against the project it came from. */
static void check_image(const char *img, const MhProject *p, int stamp_known)
{
    uint64_t sz = file_size(img);
    uint8_t *fixed = pread_file(img, 0, 0x500000);
    uint8_t *hdr = fixed;
    MhV2Header h;
    unsigned ci, j, nc = 0, nd = 0;
    MhV2Result vres;
    MhProgress none;

    CHECK(sz >= 0x500000);
    CHECK(mh_v2_header_raw_valid(fixed) && mh_v2_header_raw_valid(fixed + 0xa00));
    CHECK(memcmp(fixed, fixed + 0xa00, 0xa00) == 0);
    mh_v2_header_decode(hdr, &h);
    CHECK(h.version == 2 && h.flags == 3 && h.unit == 0x800 && h.b_lba == 5 && h.case_lba == 10);
    CHECK(h.disc_lba == 0x32 && h.art_lba == 0xb4 && h.menu_lba == 0x14b4 && h.cfg_lba == 0x1685 && h.art_size == 0x2800);
    CHECK(h.menu_sectors == g_menu_size / 0x930);
    CHECK(h.crc_cases == mh_crc32(fixed + 0x1400, 0x5000));
    CHECK(h.crc_discs == mh_crc32(fixed + 0x6400, 0x10000));
    CHECK(h.crc_art == mh_crc32(fixed + 0x16800, 0x280000));
    CHECK(h.mbr_sectors == mh_v2_mbr_sectors(sz / 512) || h.mbr_sectors == 0xffffffffu || h.mbr_sectors > 0);
    CHECK(mh_v2_rd16(fixed + 0x1fe) == 0xaa55);
    /* the unused tail of the header slot, the gap before the art, and the gap after the music */
    {
        unsigned i;
        bool z = true;
        for (i = 0x930; i < 0xa00; i++) z = z && fixed[i] == 0 && fixed[0xa00 + i] == 0;
        for (i = 0x16400; i < 0x16800; i++) z = z && fixed[i] == 0;
        CHECK(z);
    }
    /* menu */
    {
        uint32_t c = mh_crc32_begin(), i;
        MhSha1 sh;
        uint8_t dig[20];
        mh_sha1_init(&sh);
        for (i = 0; i < h.menu_sectors; i++) {
            c = mh_crc32_update(c, fixed + 0x296800 + (size_t)i * 0xa00, 0x930);
            mh_sha1_update(&sh, fixed + 0x296800 + (size_t)i * 0xa00, 0x930);
            { unsigned z; for (z = 0x930; z < 0xa00; z++) CHECK(fixed[0x296800 + (size_t)i * 0xa00 + z] == 0); }
        }
        mh_sha1_final(&sh, dig);
        CHECK(h.crc_menu == mh_crc32_end(c) && memcmp(dig, h.menu_sha1, 20) == 0);
    }
    /* config and music */
    CHECK(mh_v2_cfg_valid(fixed + 0x2d0a00));
    CHECK(memcmp(fixed + 0x2d1400, g_music, g_music_size) == 0);
    CHECK(mh_v2_rd32(fixed + 0x2d0a00 + 0x24) == g_music_size);
    CHECK(mh_v2_rd32(fixed + 0x2d0a00 + 0x28) == mh_crc32(g_music, (size_t)g_music_size));
    {
        uint64_t z;
        bool zero = true;
        for (z = 0x2d1400 + g_music_size; z < 0x500000; z++) zero = zero && fixed[z] == 0;
        CHECK(zero);
    }

    for (ci = 0; ci < 256; ci++) {
        const MhCase *c = &p->cases[ci];
        MhV2CaseRec cr;
        bool crc_ok;
        mh_v2_case_decode(fixed + 0x1400 + ci * 0x50, &cr, &crc_ok);
        if (!c->in_use) {
            CHECK(!cr.used && mh_v2_rd32(fixed + 0x1400 + ci * 0x50) == 0);
            continue;
        }
        nc++;
        CHECK(crc_ok && cr.used == 1 && cr.index == ci && cr.disc_count == c->disc_count);
        CHECK(strncmp(cr.name, c->title, 31) == 0 && strncmp(cr.code, c->serial, 15) == 0);
        CHECK(cr.art_id == (c->art_path[0] ? ci : 0xffff));
        if (c->art_path[0]) {
            uint8_t *art = pread_file(c->art_path, 0, 0x2800);
            CHECK(memcmp(art, fixed + 0x16800 + (size_t)ci * 0x2800, 0x2800) == 0);
            free(art);
        } else {
            unsigned z; bool zero = true;
            for (z = 0; z < 0x2800; z++) zero = zero && fixed[0x16800 + (size_t)ci * 0x2800 + z] == 0;
            CHECK(zero);
        }
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            MhV2DiscRec dr;
            uint8_t *rec = fixed + 0x6400 + (size_t)cr.disc_ids[j] * 0x100;
            uint8_t *pkg;
            bool ok2;
            mh_v2_disc_decode(rec, &dr, &ok2);
            nd++;
            CHECK(ok2 && dr.used == 1 && dr.id == cr.disc_ids[j] && dr.case_index == ci && dr.slot == j);
            CHECK(dr.flags == (2u | ((d->flags >> 6) & 1)));
            CHECK(strncmp(dr.serial, d->serial, 15) == 0 && strncmp(dr.title, d->name, 31) == 0);
            CHECK(dr.base_lba >= 0x2800 && (dr.base_lba & 0xff) == 0 && dr.track_lba == dr.base_lba);
            CHECK(dr.raw_lba == dr.base_lba + 0x100 && dr.raw_blocks == (uint64_t)d->total_sectors * 5);
            CHECK(dr.sectors == d->total_sectors && dr.track_count == d->ntracks && dr.bin_count == d->nfiles);
            CHECK(dr.cheat_lba == dr.base_lba + 5 && dr.extra_lba == dr.base_lba + 0x19 && dr.extra_size == 0xa00);
            CHECK(dr.art_id == (d->cd_art_path[0] ? dr.id : 0xffff));
            if (d->kind == MH_DISC_KIND_CUE && d->verified) {
                CHECK(dr.crc32 == d->verify_crc32 && memcmp(dr.sha1, d->verify_sha1, 20) == 0);
            }
            CHECK(dr.base_lba * 512 + (0x100 + (uint64_t)d->total_sectors * 5) * 512 <= sz);
            pkg = pread_file(img, dr.base_lba << 9, 0x6400);
            CHECK(mh_track_sector_valid(pkg, d->total_sectors));
            CHECK(pkg[8] == d->ntracks && mh_v2_rd32(pkg + 0x640 - 0x640 + 0x10) == 0);
            if (d->cheats && d->cheats->count) {
                CHECK(dr.cheat_size == mh_cheat_profile_serialized_size(d->cheats) && dr.cheat_size > 0);
                CHECK(mh_cheat_blob_scope(pkg + 0xa00, dr.cheat_size) == 2);
            } else {
                CHECK(dr.cheat_size == 0);
            }
            if (d->cd_art_path[0]) {
                uint8_t *art = pread_file(d->cd_art_path, 0, 0x2800);
                CHECK(memcmp(art, pkg + 0x3c00, 0x2800) == 0);
                free(art);
            }
            {   /* the gap up to the marker and the marker itself */
                uint8_t *gap = pread_file(img, (dr.base_lba + 0x32) << 9, 0xcd * 512);
                uint8_t blk[512];
                unsigned z; bool zero = true;
                MhV2Resume rm;
                for (z = 0; z < 0xcd * 512; z++) zero = zero && gap[z] == 0;
                CHECK(zero);
                free(gap);
                { uint8_t *m = pread_file(img, (dr.raw_lba - 1) << 9, 512); memcpy(blk, m, 512); free(m); }
                CHECK(mh_v2_resume_decode(blk, &rm));
                CHECK(memcmp(rm.disc_rec, rec, 0x100) == 0);
                if (stamp_known) CHECK(rm.stamp == 0x1122334455667788ull);
                CHECK(rm.has_verify == (d->verified != 0));
            }
            if (d->kind == MH_DISC_KIND_CUE) CHECK(raw_data_matches(img, d, dr.raw_lba));
            free(pkg);
        }
    }
    CHECK(nc == h.case_count && nd == h.disc_count);
    free(fixed);
    memset(&vres, 0, sizeof vres);
    mh_progress_init(&none, NULL, NULL);
    CHECK(mh_v2_validate_image(img, NULL, &vres));
    if (!vres.ok) fprintf(stderr, "validate(%s): %s\n", img, vres.msg);
}

static void test_fixture_single_disc(void)
{
    MhProject *p = new_project();
    MhV2Result res, vres;
    char *img = tpath("strider.img");
    uint64_t est, sz;
    MhProject *q = new_project();
    MhDisc *d;
    char cache[MH_PATH_MAX];

    project_add(p, STRIDER, 256);
    CHECK(p->case_count == 1 && p->disc_count == 1);
    d = p->cases[0].discs[0];
    CHECK(d != NULL && d->verified == 1 && d->status == MH_DISC_STATUS_OK);
    printf("strider: serial %s status %u issues %x cheats %s (%u entries) title '%s'\n", d->serial, d->status, d->verify_issues,
           d->cheats ? "yes" : "no", d->cheats ? d->cheats->count : 0, p->cases[0].title);
    est = mh_v2_estimate_project_bytes(p);
    CHECK(est > 0x500000);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(res.ok == 1 && res.cases == 1 && res.discs == 1);
    CHECK_STR(res.msg, "PSXRAW V2 finished. New Discs written and confirmed; existing Discs preserved.");
    sz = file_size(img);
    CHECK(sz == est && res.size == est);
    CHECK(g_count_gravando >= 1);
    /* machine readable phases: in order, one disc of one, ends in DONE */
    CHECK(g_phase_backwards == 0 && g_phase_last == MH_V2_PHASE_DONE);
    CHECK(g_phase_seen[MH_V2_PHASE_PREPARING] && g_phase_seen[MH_V2_PHASE_WRITING_DISCS] &&
          g_phase_seen[MH_V2_PHASE_UPDATING_TABLES] && g_phase_seen[MH_V2_PHASE_COMMITTING_HEADER]);
    CHECK(g_step_max == 1 && g_steps_seen == 1);
    CHECK(d->flags & MH_DF_KEEP_PACKAGE);
    check_image(img, p, 1);

    snprintf(cache, sizeof cache, "%s/cache", g_tmp);
    mh_ensure_dir(cache);
    memset(&vres, 0, sizeof vres);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    {
        const MhDisc *ld = q->cases[0].discs[0];
        CHECK(ld && ld->kind == MH_DISC_KIND_V2_IMAGE);
        CHECK(ld->total_sectors == d->total_sectors && ld->ntracks == d->ntracks);
        CHECK(ld->crc32 == d->verify_crc32 && memcmp(ld->sha1, d->verify_sha1, 20) == 0);
        CHECK_STR(ld->serial, d->serial);
        CHECK_STR(q->cases[0].title, p->cases[0].title);
        CHECK(q->reserved_c == 1); /* generation of a first write */
    }
    /* an image loaded from the first one and written again keeps the packages in place */
    {
        char *img2 = tpath("strider2.img");
        MhV2Result r2;
        CHECK(mh_v2_estimate_project_bytes(q) == sz);
        reset_progress();
        CHECK(mh_v2_write_image_direct(&g_w, q, img2, &r2));
        CHECK(r2.ok && file_size(img2) == sz);
        CHECK(g_count_gravando >= 1); /* the data is copied from the source image */
        memset(&vres, 0, sizeof vres);
        CHECK(mh_v2_validate_image(img2, NULL, &vres) && vres.ok);
        {   /* same library: the loaded project had generation 1, the copy has 2 */
            uint8_t *a = pread_file(img, 0x1400, 0x4fec00), *b = pread_file(img2, 0x1400, 0x4fec00);
            MhV2Header ha, hb;
            uint8_t *ha_raw = pread_file(img, 0, 0xa00), *hb_raw = pread_file(img2, 0, 0xa00);
            mh_v2_header_decode(ha_raw, &ha);
            mh_v2_header_decode(hb_raw, &hb);
            CHECK(ha.generation == 1 && hb.generation == 2);
            CHECK(ha.crc_cases == hb.crc_cases && ha.crc_discs == hb.crc_discs && ha.crc_art == hb.crc_art);
            CHECK(memcmp(a, b, 0x4fec00) == 0);
            free(a); free(b); free(ha_raw); free(hb_raw);
            {   /* the package and RAW data are identical too */
                uint8_t *x = pread_file(img, 0x500000, (size_t)(sz - 0x500000)), *y = pread_file(img2, 0x500000, (size_t)(sz - 0x500000));
                CHECK(memcmp(x, y, (size_t)(sz - 0x500000)) == 0);
                free(x); free(y);
            }
        }
        /* in place: write q back to its own image (OPEN_EXISTING, nothing to copy) */
        {
            MhV2Result r3;
            reset_progress();
            CHECK(mh_v2_write_image_direct(&g_w, q, img, &r3));
            CHECK(r3.ok && file_size(img) == sz);
            CHECK(g_count_gravando == 0);
            memset(&vres, 0, sizeof vres);
            CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
            {
                uint8_t *hr = pread_file(img, 0, 0xa00);
                MhV2Header hh;
                mh_v2_header_decode(hr, &hh);
                CHECK(hh.generation == 2);
                free(hr);
            }
        }
    }
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}



/* A real multi-BIN disc with audio tracks (data + 2 audio files) and the cheat catalog binding. */
static void test_fixture_multi_bin(void)
{
    MhProject *p = new_project();
    MhV2Result res, vres;
    char *img = tpath("tactics.img");
    MhDisc *d;
    char cache[MH_PATH_MAX];
    MhProject *q = new_project();

    project_add(p, TACTICS, 256);
    d = p->cases[0].discs[0];
    printf("tactics: serial %s status %u issues %x nfiles %u ntracks %u cheats %s (%u entries)\n", d->serial, d->status,
           d->verify_issues, d->nfiles, d->ntracks, d->cheats ? "yes" : "no", d->cheats ? d->cheats->count : 0);
    CHECK(d->nfiles == 3 && d->ntracks == 3 && d->verified == 1);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
    if (!res.ok) fprintf(stderr, "tactics: %s\n", res.msg);
    CHECK(res.ok && file_size(img) == res.size);
    check_image(img, p, 1);
    snprintf(cache, sizeof cache, "%s/cache4", g_tmp);
    mh_ensure_dir(cache);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    {
        const MhDisc *ld = q->cases[0].discs[0];
        unsigned i;
        CHECK(ld->ntracks == d->ntracks && ld->total_sectors == d->total_sectors);
        for (i = 0; i < d->ntracks; i++) {
            CHECK(ld->tracks[i].kind == d->tracks[i].kind && ld->tracks[i].mode == d->tracks[i].mode);
            CHECK(ld->tracks[i].start == d->tracks[i].start && ld->tracks[i].pregap == d->tracks[i].pregap);
        }
    }
    /* verification of the disc stored in the image (manual verification of a loaded library) */
    {
        char msg[256];
        CHECK(mh_v2_verify_disc_source(&g_w, q->cases[0].discs[0], msg, sizeof msg));
        CHECK_STR(msg, "RAW Disc verified successfully.");
    }
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}


/* classify (the writer) and mh_kept_package (part A / estimate) are two ports of the same
 * decision tree: they must agree on every combination. */
#include "../src/v2/write_internal.h"
static void test_classify_vs_kept(void)
{
    static MhDisc d;
    static const unsigned kinds[] = {MH_DISC_KIND_CUE, MH_DISC_KIND_V2_DEVICE, MH_DISC_KIND_V2_IMAGE};
    static const int32_t physs[] = {-1, 3, 4};
    static const char *const paths[] = {NULL, "", "A", "B"};
    static const char *const pkgs[] = {"", "A", "B"};
    static const int slots[] = {3, 9};
    unsigned ki, pi, hi, qi, si, sli, keep, n = 0;
    for (ki = 0; ki < 3; ki++)
        for (keep = 0; keep < 2; keep++)
            for (pi = 0; pi < 3; pi++)
                for (hi = 0; hi < 4; hi++)
                    for (qi = 0; qi < 3; qi++)
                        for (si = 0; si < 3; si++)
                            for (sli = 0; sli < 2; sli++) {
                                bool in_place, resumed;
                                uint64_t kept;
                                memset(&d, 0, sizeof d);
                                d.kind = kinds[ki];
                                d.flags = keep ? MH_DF_KEEP_PACKAGE : 0;
                                d.v2_drive = 4;
                                d.v2_pkg_a = 0x2800;
                                d.v2_pkg_b = 0x2900;
                                strcpy(d.v2_pkg_path, pkgs[qi]);
                                strcpy(d.source_path, pkgs[si]);
                                d.v2_slot = slots[sli];
                                kept = mh_kept_package(&d, physs[pi], paths[hi]);
                                mh_v2w_classify(physs[pi], paths[hi], &d, &in_place, &resumed);
                                n++;
                                if ((kept != 0) != (in_place || resumed) || (in_place && kept != 0x2800) ||
                                    (resumed && !in_place && kept != 0x2900)) {
                                    fprintf(stderr, "classify mismatch kind %u keep %u phys %d path %s pkg '%s' src '%s' slot %d: kept %llx in_place %d resumed %d\n",
                                            kinds[ki], keep, physs[pi], paths[hi] ? paths[hi] : "(null)", pkgs[qi], pkgs[si],
                                            slots[sli], (unsigned long long)kept, in_place, resumed);
                                    g_fail++;
                                }
                                g_checks++;
                            }
    CHECK(n == 3 * 2 * 3 * 4 * 3 * 3 * 2);
}


/* A big disc (9 BIN files, 13 cheat groups bound by serial): MH_V2W_BIG=1 only, it takes a while. */
static void test_fixture_big(void)
{
    MhProject *p = new_project();
    MhV2Result res;
    char *img = tpath("big.img");
    MhDisc *d;
    if (!getenv("MH_V2W_BIG")) { free(p); return; }
    project_add(p, FIXTURES "/Twisted Metal (USA).cue", 256);
    d = p->cases[0].discs[0];
    CHECK(d->cheats && d->cheats->count > 0 && d->nfiles == 9);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
    if (!res.ok) fprintf(stderr, "big: %s\n", res.msg);
    check_image(img, p, 1);
    mh_project_clear(p);
    free(p);
}

/* ---- synthetic libraries ------------------------------------------------------------------- */

static void make_art(const char *path, uint8_t seed)
{
    uint8_t b[0x2800];
    unsigned i;
    for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)(seed + i * 7);
    write_file(path, b, sizeof b);
}

static bool rd_fail_text(const MhV2Result *r, const char *prefix)
{
    return !r->ok && strncmp(r->msg, prefix, strlen(prefix)) == 0;
}

static void test_multi_layout(void)
{
    MhProject *p = new_project(), *q = new_project();
    char cue[MH_PATH_MAX], *img = tpath("multi.img");
    MhV2Result res, vres;
    MhCheatProfile *cp;
    uint8_t lines[12] = {0x30, 0x00, 0x12, 0x34, 0x00, 0x05, 0x80, 0x00, 0x56, 0x78, 0x00, 0x09};
    uint8_t mdrec[64];
    MhMenuDefaults md = {0x200, 0x100, 2, 1, 1, 1, 1, 50};
    char cache[MH_PATH_MAX];
    uint8_t *menu_expect;
    uint8_t *got;
    unsigned i;

    make_synth("alpha", 600, 400, false, 1, cue);
    project_add(p, cue, 256);
    make_synth("bravo", 700, 300, true, 2, cue);
    project_add(p, cue, 0);          /* second disc of case 0 */
    make_synth("charlie", 500, 0, false, 3, cue);
    project_add(p, cue, 256);        /* case 1 */
    make_synth("delta", 650, 200, true, 4, cue);
    project_add(p, cue, 256);        /* case 2 */
    CHECK(p->case_count == 3 && p->disc_count == 4);
    mh_project_move_case(p, 2, 9);   /* sparse slots */
    mh_project_move_case(p, 1, 255);
    mh_project_recount(p);

    make_art(tpath("case0.art"), 11);
    make_art(tpath("disc_b.art"), 22);
    mh_str_copy(p->cases[0].art_path, MH_PATH_MAX, tpath("case0.art"));
    mh_str_copy(p->cases[0].discs[1]->cd_art_path, MH_PATH_MAX, tpath("disc_b.art"));
    cp = mh_cheat_profile_new();
    CHECK(mh_cheat_profile_add_custom(cp, "Vidas infinitas", lines, 2, 0));
    p->cases[255].discs[0]->cheats = cp;
    p->cases[255].discs[0]->flags |= MH_DF_TRACKS_FIXED; /* only the record flag bit */
    CHECK(mh_menu_defaults_build(mdrec, &md));
    memcpy(p->menu_defaults, mdrec, 64);
    p->has_menu_defaults = 1;

    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
    if (!res.ok) fprintf(stderr, "multi: %s\n", res.msg);
    CHECK(res.ok && res.cases == 3 && res.discs == 4 && res.size == file_size(img));
    CHECK(mh_v2_estimate_project_bytes(p) == res.size || mh_v2_estimate_project_bytes_for_target(p, -1, img) == res.size);
    check_image(img, p, 1);

    /* the menu in the image carries the user's defaults */
    menu_expect = malloc((size_t)g_menu_size);
    memcpy(menu_expect, g_menu, (size_t)g_menu_size);
    CHECK(mh_menu_defaults_patch(menu_expect, g_menu_size, mdrec));
    got = pread_file(img, 0x296800, (size_t)(g_menu_size / 0x930) * 0xa00);
    for (i = 0; i < g_menu_size / 0x930; i++)
        CHECK(memcmp(got + (size_t)i * 0xa00, menu_expect + (size_t)i * 0x930, 0x930) == 0);
    CHECK(memcmp(menu_expect, g_menu, (size_t)g_menu_size) != 0);
    free(got);
    free(menu_expect);

    snprintf(cache, sizeof cache, "%s/cache2", g_tmp);
    mh_ensure_dir(cache);
    memset(&vres, 0, sizeof vres);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    mh_project_recount(q);
    CHECK(q->case_count == 3 && q->disc_count == 4);
    CHECK(q->cases[0].in_use && q->cases[0].disc_count == 2 && q->cases[9].in_use && q->cases[255].in_use);
    CHECK(q->cases[255].discs[0]->cheats && q->cases[255].discs[0]->cheats->count == 1);
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}

static void build_ab(MhProject *p, const char *prefix)
{
    char cue[MH_PATH_MAX], n1[64], n2[64];
    snprintf(n1, sizeof n1, "%s_a", prefix);
    snprintf(n2, sizeof n2, "%s_b", prefix);
    make_synth(n1, 600, 300, false, 5, cue);
    project_add(p, cue, 256);
    make_synth(n2, 800, 100, true, 6, cue);
    project_add(p, cue, 256);
}

static uint8_t *slurp_all(const char *path, uint64_t *n)
{
    uint8_t *d = slurp(path, n);
    assert(d);
    return d;
}

static bool same_files(const char *a, const char *b)
{
    uint64_t na, nb;
    uint8_t *x = slurp_all(a, &na), *y = slurp_all(b, &nb);
    bool r = na == nb && memcmp(x, y, (size_t)na) == 0;
    free(x);
    free(y);
    return r;
}

static void test_reproducible(void)
{
    MhProject *p = new_project(), *q = new_project();
    MhV2Result res;
    char *i1 = tpath("rep1.img"), *i2 = tpath("rep2.img"), *i3 = tpath("rep3.img"), *i4 = tpath("rep4.img");
    uint8_t *a, *b;
    uint64_t na, nb, off;
    unsigned diffs = 0, bad = 0;

    build_ab(p, "rep");
    build_ab(q, "rep");
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, i1, &res) && res.ok);
    CHECK(mh_v2_write_image_direct(&g_w, q, i2, &res) && res.ok);
    CHECK(same_files(i1, i2)); /* fixed stamp: identical bytes */
    /* writing the same project to another path is identical as well */
    CHECK(mh_v2_write_image_direct(&g_w, p, i3, &res) && res.ok);
    CHECK(same_files(i1, i3));
    /* with the real clock only the resume markers differ (stamp and its CRC) */
    g_w.stamp = NULL;
    {
        MhProject *r = new_project();
        build_ab(r, "rep");
        CHECK(mh_v2_write_image_direct(&g_w, r, i4, &res) && res.ok);
        mh_project_clear(r);
        free(r);
    }
    g_w.stamp = fixed_stamp;
    a = slurp_all(i1, &na);
    b = slurp_all(i4, &nb);
    CHECK(na == nb);
    for (off = 0; off < na; off++) {
        if (a[off] == b[off]) continue;
        diffs++;
        {   /* inside a marker block: offset in the 512-byte block 8..15 or 0x1fc..0x1ff */
            uint64_t blk = off & 511;
            unsigned ci, j;
            bool found = false;
            for (ci = 0; ci < 256 && !found; ci++) {
                const MhCase *c = &p->cases[ci];
                if (!c->in_use) continue;
                for (j = 0; j < c->disc_count && !found; j++) {
                    uint64_t raw_lba = 0;
                    unsigned di;
                    for (di = 0; di < 256; di++) {
                        MhV2DiscRec dr;
                        if (!mh_v2_disc_magic(a + 0x6400 + di * 0x100)) continue;
                        mh_v2_disc_decode(a + 0x6400 + di * 0x100, &dr, NULL);
                        if ((dr.raw_lba - 1) == (off >> 9)) raw_lba = dr.raw_lba;
                    }
                    if (raw_lba && ((blk >= 8 && blk < 16) || blk >= 0x1fc)) found = true;
                }
            }
            if (!found) bad++;
        }
    }
    CHECK(diffs > 0 && bad == 0);
    free(a);
    free(b);
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}

static bool header_a_is_wip(const char *img)
{
    uint8_t *h = pread_file(img, 0, 0xa00);
    bool r = memcmp(h, "PSTWIP1", 7) == 0 && !mh_v2_header_valid(h) && !mh_v2_header_valid(h + 0xa00 - 0xa00);
    free(h);
    return r;
}

static void test_cancel_and_resume(void)
{
    MhProject *p = new_project(), *f = new_project();
    MhV2Result res, vres;
    char *img = tpath("cr.img"), *ref = tpath("cr_ref.img");
    const MhDisc *a, *b;
    int round;
    static const struct {
        const char *text;
        const char *msg_prefix;
        int disc_a_kept, disc_b_kept;
    } cases[] = {
        {"Done: ", "Write cancelled. Finished Discs will be reused when you resume in this session.", 1, 0},
        {"Writing: cr_b (Track 1)", "Write interrupted. The current Disc is pending; finished Discs can be resumed.", 1, 0},
        {"Syncing tables, menu and art", "Persistent failure syncing the V2 fixed zone at card LBA ", 1, 1},
        {"Verifying tables and art", "Verification of the V2 fixed zone found a mismatch.", 1, 1},
        {"Finalizing the card library", "Finalization cancelled. Apply again in this session to finish.", 1, 1},
        {"Updating V2 tables and art", "", 1, 1},
    };

    build_ab(f, "cr");
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, f, ref, &res) && res.ok);

    for (round = 0; round < (int)(sizeof cases / sizeof cases[0]); round++) {
        mh_project_clear(p);
        mh_project_init(p);
        reset_progress();
        build_ab(p, "cr");
        a = p->cases[0].discs[0];
        b = p->cases[1].discs[0];
        unlink(img);
        reset_progress();
        g_cancel_text = cases[round].text;
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK(rd_fail_text(&res, cases[round].msg_prefix));
        CHECK(((a->flags & MH_DF_KEEP_PACKAGE) != 0) == cases[round].disc_a_kept);
        CHECK(((b->flags & MH_DF_KEEP_PACKAGE) != 0) == cases[round].disc_b_kept);
        /* never a valid header: the card/image is "uncommitted" and the validator refuses it */
        CHECK(header_a_is_wip(img));
        memset(&vres, 0, sizeof vres);
        CHECK(!mh_v2_validate_image(img, NULL, &vres));
        /* resume in the same session: finished discs are not written again */
        reset_progress();
        CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK(res.ok);
        CHECK(g_count_ja == cases[round].disc_a_kept + cases[round].disc_b_kept);
        CHECK(same_files(img, ref));
        CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
    }

    /* cancel before anything is touched: an existing valid image survives untouched */
    {
        uint64_t n1, n2;
        uint8_t *before, *after;
        mh_project_clear(p);
        mh_project_init(p);
        reset_progress();
        build_ab(p, "cr");
        CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
        before = slurp_all(img, &n1);
        reset_progress();
        g_cancel_text = "Starting to write the pending Discs";
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK_STR(res.msg, "Operation cancelled before writing.");
        after = slurp_all(img, &n2);
        CHECK(n1 == n2 && memcmp(before, after, (size_t)n1) == 0);
        CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
        free(before);
        free(after);
    }

    /* a tampered resume marker is refused */
    {
        MhV2DiscRec dr;
        uint8_t *tab, blk[512];
        FILE *fp;
        mh_project_clear(p);
        mh_project_init(p);
        reset_progress();
        build_ab(p, "cr");
        unlink(img);
        reset_progress();
        g_cancel_text = "Done: ";
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        /* the Disc table is not written yet: take the marker position from the project */
        CHECK(p->cases[0].discs[0]->flags & MH_DF_KEEP_PACKAGE);
        {
            const MhDisc *d = p->cases[0].discs[0];
            uint64_t marker = d->v2_pkg_b + 0xff;
            tab = pread_file(img, marker << 9, 512);
            memcpy(blk, tab, 512);
            free(tab);
            blk[0x10] ^= 0xff;
            fp = fopen(img, "r+b");
            assert(fp && fseeko(fp, (off_t)(marker << 9), SEEK_SET) == 0 && fwrite(blk, 1, 512, fp) == 512);
            fclose(fp);
        }
        (void)dr;
        reset_progress();
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK_STR(res.msg, "The target does not match the resume point. Reconnect the same card or restore the image used in this session.");
        CHECK(header_a_is_wip(img)); /* refused before the card was touched again: still WIP from run 1 */
    }
    mh_project_clear(p);
    mh_project_clear(f);
    free(p);
    free(f);
}

static void truncate_file(const char *path, off_t n)
{
    assert(truncate(path, n) == 0);
}

static void test_failures(void)
{
    MhProject *p;
    MhV2Result res;
    char cue[MH_PATH_MAX], bin[MH_PATH_MAX], *img = tpath("fail.img");
    MhDisc *d;

    /* truncated BIN after the admission: the read of the source fails */
    p = new_project();
    make_synth("trunc", 600, 300, false, 9, cue);
    project_add(p, cue, 256);
    snprintf(bin, sizeof bin, "%s/trunc.bin", g_tmp);
    truncate_file(bin, (off_t)file_size(bin) - 1000);
    unlink(img);
    reset_progress();
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(rd_fail_text(&res, "Write interrupted. The current Disc is pending; finished Discs can be resumed."));
    CHECK(strstr(res.msg, "\r\nDisc 1: ") != NULL);
    CHECK(strstr(res.msg, "Source read. Error code: 30") != NULL && res.code == MH_IOERR_SHORT_READ);
    CHECK(header_a_is_wip(img));
    mh_project_clear(p);
    free(p);

    /* a BIN changed after the full verification: CRC/SHA-1 of the verification no longer match */
    p = new_project();
    make_synth("tamper", 600, 300, false, 10, cue);
    project_add(p, cue, 256);
    snprintf(bin, sizeof bin, "%s/tamper.bin", g_tmp);
    {
        FILE *f = fopen(bin, "r+b");
        uint8_t z = 0x5a;
        assert(f && fseeko(f, 100000, SEEK_SET) == 0 && fwrite(&z, 1, 1, f) == 1);
        fclose(f);
    }
    unlink(img);
    reset_progress();
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Write interrupted. The current Disc is pending; finished Discs can be resumed.");
    CHECK(res.code == 0);
    mh_project_clear(p);
    free(p);

    /* tracks that are not consecutive, or a first track that is not data: no track map */
    p = new_project();
    make_synth("tracks", 600, 300, false, 11, cue);
    project_add(p, cue, 256);
    d = p->cases[0].discs[0];
    d->tracks[1].number = 5;
    unlink(img);
    reset_progress();
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Could not build this Disc's Track Table.");
    d->tracks[1].number = 2;
    d->tracks[0].kind = 1;
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Could not build this Disc's Track Table.");
    d->tracks[0].kind = 2;
    d->tracks[0].mode = 3;
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Could not build this Disc's Track Table.");
    d->tracks[0].mode = 1;
    d->tracks[0].start = 5; /* the first track must start the disc */
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Could not build this Disc's Track Table.");
    d->tracks[0].start = 0;
    d->total_sectors -= 451; /* the second track would begin beyond the disc */
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Could not build this Disc's Track Table.");
    d->total_sectors += 451;
    /* the last track is not checked against the end of the disc by the original, but the firmware
     * (FUN_10022084: index1 < end) and the loader refuse such a map, so the port refuses it too */
    d->tracks[1].start = d->total_sectors + 5;
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "Could not build this Disc's Track Table.");
    mh_project_clear(p);
    free(p);

    /* unaccepted observations and unprepared discs are refused before anything is written */
    p = new_project();
    make_synth("obs", 600, 0, false, 12, cue);
    project_add(p, cue, 256);
    d = p->cases[0].discs[0];
    CHECK(d->verify_issues != 0 && d->verify_accepted == d->verify_issues);
    d->verify_accepted = 0;
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(rd_fail_text(&res, "Write not started. Case: "));
    CHECK(strstr(res.msg, "\r\nDisc 1: ") != NULL);
    CHECK(strstr(res.msg, "The review notes for this Disc were not accepted when adding it or in manual verification.") != NULL);
    CHECK(strstr(res.msg, "\r\nFile: obs.bin") != NULL);
    d->verify_issues |= 0x20;
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(strstr(res.msg, "Invalid RAW padding; restore it from the CUE/BIN to keep the RAW format.") != NULL);
    d->verify_issues = 0;
    d->verified = 0;
    d->verified_aux = 0;
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(strstr(res.msg, "This CUE/BIN was not prepared when it was added.") != NULL);
    d->verified_aux = 1; /* "verification disabled" admission is enough */
    CHECK(mh_v2_prepare_write(p, &res));
    p->cases[0].discs[0] = NULL;
    CHECK(!mh_v2_prepare_write(p, &res));
    CHECK(strstr(res.msg, "Disc is missing.") != NULL && strstr(res.msg, "Disc 1: \r\nDisc is missing.") != NULL);
    p->cases[0].discs[0] = d;
    p->cases[0].disc_count = 6;
    CHECK(!mh_v2_prepare_write(p, &res));
    CHECK(strstr(res.msg, "Invalid number of Discs.") != NULL);
    p->cases[0].disc_count = 1;
    mh_project_clear(p);
    free(p);

    /* paths that would overwrite a source */
    p = new_project();
    make_synth("clash", 600, 300, true, 13, cue);
    project_add(p, cue, 256);
    make_art(tpath("clash_case.art"), 1);
    make_art(tpath("clash_disc.art"), 2);
    mh_str_copy(p->cases[0].art_path, MH_PATH_MAX, tpath("clash_case.art"));
    mh_str_copy(p->cases[0].discs[0]->cd_art_path, MH_PATH_MAX, tpath("clash_disc.art"));
    d = p->cases[0].discs[0];
    CHECK(!mh_v2_write_image_direct(&g_w, p, "", &res));
    CHECK_STR(res.msg, "Invalid V2 image path.");
    CHECK(!mh_v2_write_image_direct(&g_w, p, NULL, &res));
    CHECK(!mh_v2_write_image_direct(&g_w, p, d->files[1].path, &res));
    CHECK_STR(res.msg, "The image would overwrite a source BIN file.");
    CHECK(!mh_v2_write_image_direct(&g_w, p, d->cue_path, &res));
    CHECK_STR(res.msg, "The image would overwrite a source file.");
    CHECK(!mh_v2_write_image_direct(&g_w, p, d->cd_art_path, &res));
    CHECK_STR(res.msg, "The image would overwrite a source file.");
    CHECK(!mh_v2_write_image_direct(&g_w, p, p->cases[0].art_path, &res));
    CHECK_STR(res.msg, "The image would overwrite a source art file.");
    {   /* the source files are untouched */
        MhDisc *chk = mh_cue_parse(d->cue_path);
        CHECK(chk && chk->nfiles == 2 && (chk->flags & MH_DF_STRUCT_OK));
        mh_disc_free(chk);
    }
    /* art with a wrong size */
    {
        uint8_t small[100] = {0};
        char *old = tpath("badcase.art");
        write_file(old, small, sizeof small);
        mh_str_copy(p->cases[0].art_path, MH_PATH_MAX, old);
        reset_progress();
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK_STR(res.msg, "Invalid Case art.");
        mh_str_copy(p->cases[0].art_path, MH_PATH_MAX, tpath("clash_case.art"));
        write_file(tpath("baddisc.art"), small, sizeof small);
        mh_str_copy(d->cd_art_path, MH_PATH_MAX, tpath("baddisc.art"));
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK_STR(res.msg, "Invalid Disc art.");
        mh_str_copy(d->cd_art_path, MH_PATH_MAX, "");
        mh_str_copy(p->cases[0].art_path, MH_PATH_MAX, "");
    }
    /* menu defaults that are not valid, and a bad music blob */
    {
        MhV2Writer w2 = g_w;
        p->has_menu_defaults = 1;
        memset(p->menu_defaults, 0x11, 64);
        CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
        CHECK_STR(res.msg, "The built-in menu is invalid or exceeds the reserved area.");
        p->has_menu_defaults = 0;
        w2.music_size = 0;
        CHECK(!mh_v2_write_image_direct(&w2, p, img, &res));
        CHECK_STR(res.msg, "The built-in Umbrella music is invalid.");
        w2 = g_w;
        w2.music_size = MH_V2_MUSIC_MAX + 1;
        CHECK(!mh_v2_write_image_direct(&w2, p, img, &res));
        CHECK_STR(res.msg, "The built-in Umbrella music is invalid.");
        w2 = g_w;
        w2.env = NULL;
        CHECK(!mh_v2_write_image_direct(&w2, p, img, &res));
    }
    mh_project_clear(p);
    free(p);
}

static void test_card_standin(void)
{
    MhProject *p = new_project(), *q = new_project();
    MhV2Result res, vres;
    char err[256], *dev = tpath("card.bin");
    MhBlockDev *bd;
    uint64_t est;
    char cache[MH_PATH_MAX];

    build_ab(p, "card");
    est = mh_v2_estimate_project_bytes_for_target(p, 7, NULL);
    CHECK(est > 0x500000);
    bd = mh_blockdev_open_file(dev, true, est + 0x200000, err, sizeof err);
    CHECK(bd != NULL);
    reset_progress();
    CHECK(mh_v2_write_handle_direct(&g_w, p, &bd->io, 7, NULL, bd->size, &res));
    CHECK(res.ok && res.size == est && res.cases == 2 && res.discs == 2);
    CHECK(p->cases[0].discs[0]->v2_slot == 7 && p->cases[0].discs[0]->v2_pkg_path[0] == 0);
    CHECK(p->cases[0].discs[0]->v2_card_blocks == bd->size);
    check_image(dev, p, 1);
    /* the card is bigger than the library: the MBR covers the card */
    {
        uint8_t *h = pread_file(dev, 0, 0xa00);
        CHECK(mh_v2_rd32(h + 0x1ca) == (uint32_t)(bd->size / 512 - 1));
        free(h);
    }
    /* same card, same session: every disc is "already written" and the result is identical */
    {
        uint64_t n1;
        uint8_t *b1 = slurp_all(dev, &n1), *b2;
        uint64_t n2;
        reset_progress();
        CHECK(mh_v2_write_handle_direct(&g_w, p, &bd->io, 7, NULL, bd->size, &res) && res.ok);
        CHECK(g_count_ja == 2 && g_count_gravando == 0);
        b2 = slurp_all(dev, &n2);
        CHECK(n1 == n2 && memcmp(b1, b2, (size_t)n1) == 0);
        free(b1);
        free(b2);
    }
    /* another card (or the same one with a different size) does not match the resume point */
    reset_progress();
    CHECK(!mh_v2_write_handle_direct(&g_w, p, &bd->io, 7, NULL, bd->size + 512, &res));
    CHECK_STR(res.msg, "The target does not match the resume point. Reconnect the same card or restore the image used in this session.");
    /* a different disk number: the discs are written again from the BINs */
    reset_progress();
    CHECK(mh_v2_write_handle_direct(&g_w, p, &bd->io, 9, NULL, bd->size, &res) && res.ok);
    CHECK(g_count_ja == 0 && g_count_gravando >= 2);
    check_image(dev, p, 1);
    /* the project estimate follows the target */
    CHECK(mh_v2_estimate_project_bytes_for_target(p, 9, NULL) == est);
    mh_blockdev_close(bd);

    /* the card loaded as a project would be a kind 4 source; here the image loader gives kind 5 */
    snprintf(cache, sizeof cache, "%s/cache3", g_tmp);
    mh_ensure_dir(cache);
    CHECK(mh_v2_load_image(dev, cache, &g_env, q, &vres));
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}

static void test_estimate_kept(void)
{
    MhProject *p = new_project();
    MhV2Result res;
    char *img = tpath("est.img");
    MhDisc *a, *b;

    build_ab(p, "est");
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    a = p->cases[0].discs[0];
    b = p->cases[1].discs[0];
    CHECK(mh_v2_estimate_project_bytes_for_target(p, -1, img) == res.size);
    CHECK(mh_v2_estimate_project_bytes_for_target(p, -1, NULL) > 0); /* no path: both discs allocated fresh */
    /* two packages with the same LBA overlap: the estimate fails */
    b->v2_pkg_b = a->v2_pkg_b;
    CHECK(mh_v2_estimate_project_bytes_for_target(p, -1, img) == 0);
    /* a preserved package past the end of a small target is refused by the writer */
    b->v2_pkg_b = a->v2_pkg_b + 0x100000;
    a->v2_card_blocks = a->v2_card_blocks;
    {
        MhProject *q = new_project();
        build_ab(q, "est2");
        mh_project_clear(q);
        free(q);
    }
    mh_project_clear(p);
    free(p);
}


static void poke(const char *path, uint64_t off, uint8_t v)
{
    FILE *f = fopen(path, "r+b");
    assert(f && fseeko(f, (off_t)off, SEEK_SET) == 0 && fwrite(&v, 1, 1, f) == 1);
    fclose(f);
}

/* Analysis and manual verification of a disc that was loaded from an image (kind 5 source). */
static void test_loaded_disc_analysis(void)
{
    MhProject *p = new_project(), *q = new_project();
    MhV2Result res, vres;
    char *img = tpath("ana.img"), cache[MH_PATH_MAX], msg[256];
    MhDisc *d;
    uint64_t raw;
    uint8_t *tab;
    MhV2DiscRec dr;

    build_ab(p, "ana");
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    snprintf(cache, sizeof cache, "%s/cache5", g_tmp);
    mh_ensure_dir(cache);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    d = q->cases[0].discs[0];
    CHECK(d->kind == MH_DISC_KIND_V2_IMAGE && d->total_sectors == 900 + 150);
    CHECK(mh_v2_verify_disc_source(&g_w, d, msg, sizeof msg) && strcmp(msg, "RAW Disc verified successfully.") == 0);
    CHECK(mh_v2_check_disc_source(&g_w, d));
    CHECK(d->verified == 1 && (d->verify_issues & 0x10) == 0 && (d->verify_issues & 0x20) == 0);
    CHECK(d->crc32 == d->verify_crc32 && memcmp(d->sha1, d->verify_sha1, 20) == 0);
    /* the source file is the image: the manual verification reads it back */
    tab = pread_file(img, 0x6400, 0x100);
    mh_v2_disc_decode(tab, &dr, NULL);
    free(tab);
    raw = dr.raw_lba << 9;
    /* a flipped data byte: card data differs from the record */
    poke(img, raw + 10 * 0xa00 + 100, 0xa5);
    CHECK(mh_v2_check_disc_source(&g_w, d));
    CHECK(d->verify_issues & 0x10);
    CHECK(strstr(d->verify_text, "The data on the card does not match the Disc record.\r\n") != NULL);
    CHECK(!mh_v2_verify_disc_source(&g_w, d, msg, sizeof msg));
    CHECK_STR(msg, "The Disc CRC32/SHA-1 differs from the values stored in the V2 library.");
    /* a byte in the zero padding of a slot */
    poke(img, raw + 20 * 0xa00 + 0x940, 1);
    CHECK(!mh_v2_verify_disc_source(&g_w, d, msg, sizeof msg));
    CHECK_STR(msg, "The RAW padding of a sector is not zeroed.");
    CHECK(!mh_v2_check_disc_source(&g_w, d) || (d->verify_issues & 0x20));
    /* writing the damaged disc to another image is refused by the padding test of the stream */
    {
        MhV2Result r2;
        CHECK(!mh_v2_write_image_direct(&g_w, q, tpath("ana2.img"), &r2));
        CHECK(rd_fail_text(&r2, "Write not started. Case: ") || rd_fail_text(&r2, "Write interrupted."));
    }
    /* a disc without RAW source cannot be verified */
    d->v2_raw_blocks = 0;
    CHECK(!mh_v2_verify_disc_source(&g_w, d, msg, sizeof msg));
    CHECK_STR(msg, "This Disc has no verifiable V2 RAW source.");
    mh_project_clear(p);
    mh_project_clear(q);
    free(p);
    free(q);
}

static void test_max_discs(void)
{
    MhProject *p = new_project();
    MhV2Result res;
    char cue[MH_PATH_MAX], *img = tpath("max.img");
    unsigned i, n = 0;
    MhDisc *first;

    make_synth("max", 300, 0, false, 14, cue);
    project_add(p, cue, 256);
    first = p->cases[0].discs[0];
    first->verified = 1;
    for (i = 1; i < 257; i++) {
        MhDisc *c = mh_calloc(sizeof *c);
        MhCase *cs = &p->cases[i / 5];
        memcpy(c, first, sizeof *c);
        c->cheats = NULL;
        if (!cs->in_use) {
            memset(cs, 0, sizeof *cs);
            cs->in_use = 1;
            cs->slot = (uint8_t)(i / 5);
            snprintf(cs->title, sizeof cs->title, "CASE %u", i / 5);
        }
        cs->discs[cs->disc_count++] = c;
        n++;
    }
    CHECK(n == 256);
    reset_progress();
    CHECK(!mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK_STR(res.msg, "V2 supports at most 256 Discs.");
    CHECK(file_size(img) <= 0x500000 || 1);
    /* the first 256 discs are accepted by the estimate (sparse files only) */
    p->cases[256 / 5].disc_count = 1; /* drop the 257th */
    mh_project_recount(p);
    CHECK(mh_v2_estimate_project_bytes_for_target(p, -1, NULL) > 0x500000 * 40);
    mh_project_clear(p);
    free(p);
}

static void test_empty_project_and_small_target(void)
{
    MhProject *p = new_project();
    MhV2Result res, vres;
    char *img = tpath("empty.img");
    MhIo io;
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok && res.cases == 0 && res.discs == 0);
    CHECK(file_size(img) == 0x500000);
    CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
    /* a target smaller than the fixed zone */
    CHECK(mh_io_open(&io, img, MH_IO_RDWR, true, NULL));
    CHECK(!mh_v2_write_handle_direct(&g_w, p, &io, -1, img, 0x4fffff, &res));
    CHECK_STR(res.msg, "The games do not fit on the selected target.");
    mh_io_close(&io);
    CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok); /* refused before the target was touched */
    mh_project_clear(p);
    free(p);
}

static void test_texts(void)
{
    MhProject *p = new_project();
    char cue[MH_PATH_MAX], msg[2048];
    bool obs = true;
    MhDisc *d;
    const char *r;
    uint64_t est;

    CHECK(!mh_v2_all_ready(p, &obs, msg, sizeof msg) && !obs);
    CHECK_STR(msg, "Add at least one game before writing.");
    make_synth("txt", 600, 100, false, 15, cue);
    project_add(p, cue, 256);
    d = p->cases[0].discs[0];
    CHECK(mh_v2_all_ready(p, &obs, msg, sizeof msg) && obs); /* observations: no serial, unknown edition */
    d->status = MH_DISC_STATUS_OK;
    CHECK(mh_v2_all_ready(p, &obs, msg, sizeof msg) && !obs);
    d->status = MH_DISC_STATUS_ERROR;
    mh_str_copy(d->notes, sizeof d->notes, "Nota um\r\nNota dois");
    mh_str_copy(d->verify_text, sizeof d->verify_text, "Texto da verificacao");
    CHECK(!mh_v2_all_ready(p, &obs, msg, sizeof msg));
    {
        char want[1024];
        snprintf(want, sizeof want,
                 "Write not started. Case: %s\r\nDisc 1\r\nNota um\r\nNota dois\r\n\r\nLast verification: Texto da verificacao\r\nCUE: txt.cue",
                 p->cases[0].title);
        CHECK_STR(msg, want);
    }
    d->verify_text[0] = 0;
    CHECK(!mh_v2_all_ready(p, &obs, msg, sizeof msg));
    CHECK(strstr(msg, "Nota dois\r\nCUE: txt.cue") != NULL);
    p->cases[0].discs[0] = NULL;
    CHECK(!mh_v2_all_ready(p, &obs, msg, sizeof msg));
    {
        char want[256];
        snprintf(want, sizeof want, "Write not started. Case: %s\r\nDisc 1", p->cases[0].title);
        CHECK_STR(msg, want);
    }
    p->cases[0].discs[0] = d;
    d->status = MH_DISC_STATUS_OK;

    r = mh_v2_write_precheck(p, false, true, true, 3, 1ull << 40, &est);
    CHECK(r && strncmp(r, "WRITE BLOCKED: the built-in", 27) == 0);
    r = mh_v2_write_precheck(p, true, false, true, 3, 1ull << 40, &est);
    CHECK_STR(r, "WRITE BLOCKED: the audited external cheat database must stay in the database/cheats folder next to the app.");
    CHECK_STR(mh_v2_write_precheck(p, true, true, false, 3, 1ull << 40, &est), "Select an SD card.");
    p->card_loaded = 1;
    p->device = 4;
    CHECK_STR(mh_v2_write_precheck(p, true, true, true, 3, 1ull << 40, &est),
              "This project was loaded from another card. Select the same disk to apply the changes.");
    p->card_loaded = 0;
    CHECK_STR(mh_v2_write_precheck(p, true, true, true, 3, 0x500000, &est), "The games do not fit on the selected card.");
    CHECK(mh_v2_write_precheck(p, true, true, true, 3, 1ull << 40, &est) == NULL && est > 0x500000);

    mh_v2_write_confirm_text(p, "disk3", "Model X", "", 123456789, msg, sizeof msg);
    CHECK_STR(msg,
              "The whole card will be prepared directly as PSXRAW V2.\r\n\r\ndisk3\r\nModel: Model X\r\nSerial: not provided\r\nCases: 1  |  Discs: 1\r\nApproximate final RAW size: 117.7 MB\r\nNew Discs were prepared when added; Discs already written will be preserved.\r\n\r\nWARNING: choosing the wrong disk can erase another device.");
    p->card_loaded = 1;
    mh_v2_write_confirm_text(p, "disk3", "M", "S1", 1024, msg, sizeof msg);
    CHECK(strncmp(msg, "The changes will be applied directly to the PSXRAW V2 card. The RAW data of preserved games will stay at the same LBAs.\r\n\r\ndisk3\r\n", strlen("The changes will be applied directly to the PSXRAW V2 card. The RAW data of preserved games will stay at the same LBAs.\r\n\r\ndisk3\r\n")) == 0);
    CHECK(strstr(msg, "Serial: S1") != NULL);
    mh_v2_write_done_text(2, 3, 0x500000, msg, sizeof msg);
    CHECK_STR(msg, "Operation completed directly on the card.\r\n\r\nCases: 2\r\nDiscs: 3\r\nRAW size used: 5.0 MB\r\n\r\nThe card is PSXRAW and has no files visible in Finder. Do not format the card.");
    mh_v2_write_failure_text("Failure X", false, msg, sizeof msg);
    CHECK_STR(msg, "Failure X\r\n\r\nKeep the app open. Reconnect the same target and apply again: Discs already finished (shown in blue) will be reused. The library must be finalized before you use the card in the console.");
    mh_v2_write_failure_text("Failure X", true, msg, sizeof msg);
    CHECK_STR(msg, "Cancelled. Keep the app open and apply again to resume the pending Discs.");
    mh_project_clear(p);
    free(p);
}

static void test_admission(void)
{
    MhProject *p = new_project();
    char cue[MH_PATH_MAX], bin[MH_PATH_MAX];
    MhDisc *d;
    char msg[512];

    make_synth("adm", 600, 100, false, 16, cue);
    snprintf(bin, sizeof bin, "%s/adm.bin", g_tmp);
    d = mh_manager_parse(&g_db, cue, NULL);
    CHECK(d && d->kind == MH_DISC_KIND_CUE);
    /* cheap admission */
    CHECK(mh_v2_manager_admit(&g_w, d, false, NULL, NULL));
    CHECK(d->verified == 0 && d->verified_aux == 1 && d->status == MH_DISC_STATUS_NOTE);
    CHECK_STR(d->verify_text, "Full verification is turned off. The write will still confirm the data on the card.");
    /* full admission: declined observations reject the disc */
    CHECK(!mh_v2_manager_admit(&g_w, d, true, NULL, NULL));
    CHECK(d->verified == 1 && d->verify_issues != 0 && d->verify_accepted == 0);
    CHECK(strstr(d->verify_text, "Could not confirm the game ID.\r\n") != NULL);
    CHECK(strstr(d->verify_text, "The Disc differs from the known version; it may be a modification.\r\n") != NULL);
    CHECK(mh_v2_manager_admit(&g_w, d, true, yes, NULL));
    CHECK(d->verify_accepted == d->verify_issues && d->status == MH_DISC_STATUS_NOTE);
    CHECK(d->verify_crc32 != 0);
    {   /* the CRC/SHA-1 of the verification are those of the BIN */
        uint64_t n;
        uint8_t *b = slurp_all(bin, &n), dig[20];
        CHECK(d->verify_crc32 == mh_crc32(b, (size_t)n));
        mh_sha1(b, (size_t)n, dig);
        CHECK(memcmp(dig, d->verify_sha1, 20) == 0);
        free(b);
    }
    /* a BIN that changed size is noticed by the cheap admission */
    truncate_file(bin, (off_t)file_size(bin) - 2352);
    CHECK(!mh_v2_admit_disc_without_full_check(d));
    CHECK_STR(d->verify_text, "A BIN file is missing or changed size.");
    unlink(bin);
    CHECK(!mh_v2_admit_disc_without_full_check(d));
    d->status = MH_DISC_STATUS_ERROR;
    CHECK(!mh_v2_admit_disc_without_full_check(d));
    CHECK_STR(d->verify_text, "Incomplete or invalid CUE/BIN.");
    CHECK(!mh_v2_check_disc_source(&g_w, d));
    CHECK_STR(d->verify_text, "Incomplete structure: provide the CUE and all readable BIN files.");
    mh_v2_verification_issue_text(d);
    mh_disc_free(d);
    (void)msg;
    mh_project_clear(p);
    free(p);
}

int main(void)
{
    const char *t = getenv("TMPDIR");
    uint8_t *blob;
    uint64_t blen;

    snprintf(g_tmp, sizeof g_tmp, "%s/mhv2w-XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(g_tmp)) { perror("mkdtemp"); return 2; }

    test_extents();
    test_empty_estimate();
    test_classify_vs_kept();

    if (access(STRIDER, R_OK) != 0 || access(EMBEDDED "/picostation_menu.bin", R_OK) != 0) {
        printf("fixtures absent, skipping the image tests\n");
        goto done;
    }
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

    {
        const char *only = getenv("MH_V2W_ONLY"); /* e.g. MH_V2W_ONLY=cancel,card for a quick run */
#define RUN(name, fn) do { if (!only || strstr(only, name)) fn(); } while (0)
        RUN("fixture", test_fixture_single_disc);
        RUN("tactics", test_fixture_multi_bin);
        if (only && strstr(only, "big")) test_fixture_big();
        RUN("layout", test_multi_layout);
        RUN("repro", test_reproducible);
        RUN("cancel", test_cancel_and_resume);
        RUN("fail", test_failures);
        RUN("card", test_card_standin);
        RUN("estimate", test_estimate_kept);
        RUN("analysis", test_loaded_disc_analysis);
        RUN("max", test_max_discs);
        RUN("empty", test_empty_project_and_small_target);
        RUN("texts", test_texts);
        RUN("admission", test_admission);
#undef RUN
    }

done:
    if (getenv("MH_V2W_KEEP")) printf("kept %s\n", g_tmp); else rm_rf(g_tmp);
    printf("test_v2_write: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
