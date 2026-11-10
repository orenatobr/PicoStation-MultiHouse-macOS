/* The analysis pass of the disc verification (mh_v2_stream_disc, MH_V2_STREAM_ANALYZE) on synthetic discs.
 * It runs the CRC-32, the whole-disc SHA-1, the per-track SHA-1 and the sector checks on several threads,
 * reading a chunk ahead; the results must be those of a plain sequential pass over the same bytes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/v2write.h"
#include "v2_stubs.inc"

static int g_fail, g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            g_fail++;                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                         \
    } while (0)

#define SECTOR 2352u
#define CHUNK 2048u /* sectors per read of the analysis */

static char g_tmp[MH_PATH_MAX];
static MhDb g_db;

static uint32_t g_rng = 12345;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}
static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* A valid MODE1 sector with random user data and correct EDC/ECC. */
static void make_data_sector(uint8_t *s, uint32_t lba)
{
    unsigned i, f = lba + 150;
    memset(s, 0, SECTOR);
    memset(s + 1, 0xff, 10);
    s[12] = bcd(f / 4500);
    s[13] = bcd((f / 75) % 60);
    s[14] = bcd(f % 75);
    s[15] = 1;
    for (i = 16; i < 16 + 2048; i++) s[i] = (uint8_t)rnd();
    mh_cd_sector_recode(s);
}

static void msf(unsigned f, char *out) { snprintf(out, 16, "%02u:%02u:%02u", f / 4500, (f / 75) % 60, f % 75); }

typedef struct Synth {
    char cue[MH_PATH_MAX];
    char bin[2][MH_PATH_MAX];
    unsigned nbins;
} Synth;

/* A MODE1 track of ndata sectors and an audio track with a 2 second pregap, in one BIN or in two.
 * bad_edc / bad_format are data sectors to damage (-1 for none). */
static void make_synth(Synth *out, const char *name, unsigned ndata, unsigned naudio, bool multi, long bad_edc,
                       long bad_format)
{
    char text[1024], a[16], b[16];
    uint8_t sec[SECTOR];
    unsigned i, k;
    FILE *f;
    out->nbins = multi ? 2 : 1;
    snprintf(out->bin[0], MH_PATH_MAX, "%s/%s%s.bin", g_tmp, name, multi ? " (Track 1)" : "");
    snprintf(out->bin[1], MH_PATH_MAX, "%s/%s (Track 2).bin", g_tmp, name);
    snprintf(out->cue, MH_PATH_MAX, "%s/%s.cue", g_tmp, name);
    f = fopen(out->bin[0], "wb");
    assert(f);
    for (i = 0; i < ndata; i++) {
        make_data_sector(sec, i);
        if ((long)i == bad_edc) sec[100] ^= 0x5a; /* user data changed, EDC and ECC now wrong */
        if ((long)i == bad_format) sec[0] = 1;    /* the sync pattern is broken */
        fwrite(sec, 1, SECTOR, f);
    }
    if (multi) {
        fclose(f);
        f = fopen(out->bin[1], "wb");
        assert(f);
    }
    for (i = 0; i < 150 + naudio; i++) {
        for (k = 0; k < SECTOR; k++) sec[k] = (uint8_t)(rnd() >> 3);
        fwrite(sec, 1, SECTOR, f);
    }
    fclose(f);
    if (multi) {
        snprintf(text, sizeof text,
                 "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                 "FILE \"%s\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n",
                 strrchr(out->bin[0], '/') + 1, strrchr(out->bin[1], '/') + 1);
    } else {
        msf(ndata, a);
        msf(ndata + 150, b);
        snprintf(text, sizeof text,
                 "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                 "  TRACK 02 AUDIO\r\n    INDEX 00 %s\r\n    INDEX 01 %s\r\n",
                 strrchr(out->bin[0], '/') + 1, a, b);
    }
    f = fopen(out->cue, "wb");
    assert(f);
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

/* CRC-32 and SHA-1 of the BINs in order, computed in one go as the reference. */
static void reference(const Synth *s, uint32_t *crc, uint8_t sha[20])
{
    uint64_t total = 0, size;
    uint8_t *all = NULL, *d;
    unsigned k;
    for (k = 0; k < s->nbins; k++) {
        CHECK(mh_read_entire_file(s->bin[k], &d, &size));
        all = realloc(all, total + size);
        memcpy(all + total, d, size);
        total += size;
        mh_free(d);
    }
    *crc = mh_crc32(all, total);
    mh_sha1(all, total, sha);
    free(all);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Adds the CUE and analyses its disc. Returns the verify_issues bits; checks the hashes. */
static uint32_t analyse(const Synth *s, unsigned ndata)
{
    MhProject *p = mh_calloc(sizeof *p);
    MhCueHooks h = {NULL, NULL, NULL, NULL, NULL};
    MhAddResult r;
    char err[256];
    uint8_t rec[MH_V2_DISC_SIZE], sha[20], ref_sha[20];
    uint32_t ref_crc, issues = 0xffffffffu;
    MhDisc *d;
    uint64_t done = 0;

    mh_project_init(p);
    if (mh_project_add_cue(p, &g_db, s->cue, 0, &h, &r, err, sizeof err) != MH_ADD_OK) {
        fprintf(stderr, "add_cue failed: %s\n", err);
        CHECK(false);
        mh_free(p);
        return issues;
    }
    d = r.cas->discs[r.disc_index];
    d->verify_issues = 0;
    memset(rec, 0, sizeof rec);
    CHECK(mh_v2_stream_disc(NULL, NULL, d, rec, &done, (uint64_t)d->total_sectors * MH_V2_SLOT, MH_V2_STREAM_ANALYZE));
    reference(s, &ref_crc, ref_sha);
    memcpy(sha, rec + 0x80, 20);
    CHECK(rd32(rec + 0x7c) == ref_crc);
    CHECK(memcmp(sha, ref_sha, 20) == 0);
    CHECK(done == (uint64_t)d->total_sectors * MH_V2_SLOT);
    (void)ndata;
    issues = d->verify_issues;
    mh_project_clear(p);
    mh_free(p);
    return issues;
}

int main(void)
{
    const char *t = getenv("TMPDIR");
    Synth s;
    /* more than two chunks, with the damaged sectors on the edges of chunks and of the parallel slices */
    static const long edges[] = {0, 1, 63, 64, CHUNK - 1, CHUNK, CHUNK + 1, 2 * CHUNK - 1, 2 * CHUNK, 5000 + 77};
    unsigned i, nedges = sizeof edges / sizeof edges[0];

    snprintf(g_tmp, sizeof g_tmp, "%s/mhv2an-XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(g_tmp)) { perror("mkdtemp"); return 2; }
    memset(&g_db, 0, sizeof g_db);

    for (i = 0; i < 2; i++) { /* one BIN, then one BIN per track */
        bool multi = i == 1;
        unsigned k;
        make_synth(&s, "clean", 5100, 600, multi, -1, -1);
        CHECK(analyse(&s, 5100) == 0);
        for (k = 0; k < nedges; k++) {
            make_synth(&s, "edc", 5100, 600, multi, edges[k], -1);
            CHECK((analyse(&s, 5100) & 0xc0) == 0xc0); /* EDC and ECC of that sector */
            make_synth(&s, "fmt", 5100, 600, multi, -1, edges[k]);
            CHECK((analyse(&s, 5100) & 8) == 8);       /* the format of that sector */
        }
        make_synth(&s, "both", 5100, 600, multi, 3000, 4000);
        CHECK((analyse(&s, 5100) & 0xc8) == 0xc8);
    }
    {   /* a short disc: one partial chunk */
        make_synth(&s, "short", 10, 3, false, -1, -1);
        CHECK(analyse(&s, 10) == 0);
    }
    {
        char cmd[MH_PATH_MAX + 16];
        snprintf(cmd, sizeof cmd, "rm -rf \"%s\"", g_tmp);
        if (system(cmd)) fprintf(stderr, "could not remove %s\n", g_tmp);
    }
    printf("test_v2_analyze: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
