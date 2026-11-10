/* Regression tests from the disc audit (docs/audit/disc.md): differences against the original
 * found by reading the decompilation, and hostile input (mutated CUE sheets and ISO images,
 * oversized claims). Runs under ASan/UBSan with `make test-disc`. */
#include <time.h>
#include "disc_util.inc"

static char *read_all(const char *name, size_t *n)
{
    char p[MH_PATH_MAX];
    uint8_t *d = NULL;
    uint64_t sz = 0;
    tmp_path(name, p);
    if (!mh_read_entire_file(p, &d, &sz)) return NULL;
    *n = (size_t)sz;
    return (char *)d;
}

/* ============================================================== decode_cue / cue_rewrite_files */

static void test_empty_cue(void)
{
    char p[MH_PATH_MAX], q[MH_PATH_MAX], *t = NULL;
    MhDisc *d;
    put_file("empty.cue", "", 0);
    tmp_path("empty.cue", p);
    tmp_path("empty-out.cue", q);
    /* MultiByteToWideChar fails for 0 bytes: decode_cue fails, cue_parse says it could not decode */
    CHECK(!mh_cue_read_text(p, &t) && t == NULL);
    d = mh_cue_parse(p);
    CHECK(d->status == MH_DISC_STATUS_ERROR && d->flags == 0);
    CHECK_STR(d->notes, "Nao foi possivel abrir ou decodificar o CUE.");
    mh_disc_free(d);
    CHECK(mh_cue_count_files(p) == 0);
    CHECK(!mh_cue_rewrite_files(p, q, NULL, 0));
    /* a file with only line breaks is not empty */
    put_text("nl.cue", "\r\n");
    d = parse_tmp_cue("nl.cue");
    CHECK_STR(d->notes, "CUE sem FILE/TRACK validos.");
    mh_disc_free(d);
}

static void test_rewrite_trailing_line(void)
{
    char src[MH_PATH_MAX], dst[MH_PATH_MAX];
    size_t n = 0;
    char *out;
    const char *paths[1] = {NULL};
    tmp_path("tl.cue", src);
    tmp_path("tl-out.cue", dst);
    /* ends with CRLF: the original visits one more (empty) line and writes it with CRLF */
    put_text("tl.cue", "FILE \"a.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n");
    CHECK(mh_cue_rewrite_files(src, dst, paths, 1));
    out = read_all("tl-out.cue", &n);
    CHECK(out && n == strlen("FILE \"a.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n\r\n"));
    CHECK(out && memcmp(out, "FILE \"a.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n\r\n", n) == 0);
    free(out);
    /* no final line break: no extra line */
    put_text("tl.cue", "FILE \"a.bin\" BINARY\n  TRACK 01 MODE2/2352");
    CHECK(mh_cue_rewrite_files(src, dst, paths, 1));
    out = read_all("tl-out.cue", &n);
    CHECK(out && n == strlen("FILE \"a.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n"));
    CHECK(out && memcmp(out, "FILE \"a.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n", n) == 0);
    free(out);
    /* LF only and blank lines in the middle: the run of breaks collapses, the end adds one line */
    put_text("tl.cue", "REM x\n\n\nFILE \"a.bin\" BINARY\n");
    CHECK(mh_cue_rewrite_files(src, dst, paths, 1));
    out = read_all("tl-out.cue", &n);
    CHECK(out && n == strlen("REM x\r\nFILE \"a.bin\" BINARY\r\n\r\n"));
    CHECK(out && memcmp(out, "REM x\r\nFILE \"a.bin\" BINARY\r\n\r\n", n) == 0);
    free(out);
}

static void test_cue_size_cap(void)
{
    char p[MH_PATH_MAX], *t = NULL;
    MhDisc *d;
    put_zero_bin("huge.cue", (65ull << 20));
    tmp_path("huge.cue", p);
    CHECK(!mh_cue_read_text(p, &t) && t == NULL);
    d = mh_cue_parse(p);
    CHECK_STR(d->notes, "Nao foi possivel abrir ou decodificar o CUE.");
    mh_disc_free(d);
    unlink(p);
}

/* ======================================================================== hostile ISO sizes */

/* Two PS-X EXE files that each claim 100000 sectors of an image that is that large (sparse).
 * The original scanned both; the port stops at its scan budget (256 MiB) with an I/O error. */
static void test_scan_budget(void)
{
    char p[MH_PATH_MAX];
    MhPsSerial r;
    Iso iso;
    IsoFile f[2] = {{"A.EXE;1", NULL, 0, NULL}, {"B.EXE;1", NULL, 0, NULL}};
    uint8_t *e;
    uint32_t pos, big = 100000;
    uint8_t exe[2048] = {0};
    f[0].data = exe; f[0].size = 2048;
    f[1].data = exe; f[1].size = 2048;
    memcpy(exe, "PS-X EXE", 8);
    iso_build(&iso, f, 2, 2352, 0x18, 0, 0);
    /* rewrite the root directory so both files claim `big` sectors; the volume is large enough */
    e = iso_user(&iso, 17);
    memset(e, 0, 2048);
    pos = 0;
    pos += dirrec(e + pos, "\0", 1, 17, 2048, 1);
    pos += dirrec(e + pos, "\1", 1, 17, 2048, 1);
    pos += dirrec(e + pos, "A.EXE;1", 7, 19, big * 2048, 0);
    pos += dirrec(e + pos, "B.EXE;1", 7, 20, big * 2048, 0);
    {
        uint8_t *pvd = iso_user(&iso, 16);
        le32w(pvd + 80, big + 100);
        be32w(pvd + 84, big + 100);
    }
    iso_write(&iso, "budget.bin");
    tmp_path("budget.bin", p);
    CHECK(truncate(p, (off_t)(big + 100) * 2352) == 0);
    (void)mh_psserial_extract(&r, p);
    CHECK(r.status == MH_PSS_IO_ERROR);
    CHECK_STR(r.message, "Falha durante a leitura de executavel PS-X EXE.");
    /* one such file alone is still scanned completely */
    pos = 0;
    memset(e, 0, 2048);
    pos += dirrec(e + pos, "\0", 1, 17, 2048, 1);
    pos += dirrec(e + pos, "\1", 1, 17, 2048, 1);
    pos += dirrec(e + pos, "A.EXE;1", 7, 19, big * 2048, 0);
    iso_write(&iso, "budget1.bin");
    tmp_path("budget1.bin", p);
    CHECK(truncate(p, (off_t)(big + 100) * 2352) == 0);
    (void)mh_psserial_extract(&r, p);
    CHECK(r.status == MH_PSS_NOT_FOUND);
    iso_free(&iso);
}

/* ======================================================================== mutation fuzzing */

static uint32_t g_rs = 12345;
static uint32_t rnd(void)
{
    g_rs = g_rs * 1664525u + 1013904223u;
    return g_rs >> 8;
}

static void fuzz_cue(int iters)
{
    static const char base[] =
        "REM GENRE x\r\nFILE \"a.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n    PREGAP 00:02:00\r\n    FLAGS DCP\r\n    ISRC ABC\r\n    INDEX 00 00:10:00\r\n"
        "    INDEX 01 00:12:00\r\nFILE b.bin BINARY\r\n  TRACK 03 AUDIO\r\n    INDEX 01 00:00:00\r\n"
        "    POSTGAP 00:02:00\r\n";
    static const char *const tok[] = {"\"", "99999999999999", "FILE ", "TRACK 99 AUDIO\n", "\r\r\n",
                                      "\xff\xfe", "INDEX 01 ", "::", "00:00:99"};
    put_zero_bin("a.bin", 2352 * 2000);
    put_zero_bin("b.bin", 2352 * 100);
    for (int it = 0; it < iters; it++) {
        char buf[2048], p[MH_PATH_MAX];
        size_t n = sizeof base - 1;
        MhDisc *d;
        memcpy(buf, base, n);
        for (int k = 1 + (int)(rnd() % 6); k > 0; k--) {
            uint32_t op = rnd() % 5, pos = rnd() % (uint32_t)n;
            if (op == 0) buf[pos] = (char)rnd();
            else if (op == 1 && n < 2000) { memmove(buf + pos + 1, buf + pos, n - pos); buf[pos] = (char)rnd(); n++; }
            else if (op == 2 && n > 1) { memmove(buf + pos, buf + pos + 1, n - pos - 1); n--; }
            else if (op == 3) {
                const char *s = tok[rnd() % 9];
                size_t l = strlen(s);
                if (n + l < 2000) { memmove(buf + pos + l, buf + pos, n - pos); memcpy(buf + pos, s, l); n += l; }
            } else {
                size_t l = rnd() % 40;
                if (pos + l <= n && n > l) { memmove(buf + pos, buf + pos + l, n - pos - l); n -= l; }
            }
        }
        put_file("f.cue", buf, n);
        d = parse_tmp_cue("f.cue");
        CHECK(d != NULL && d->nfiles <= MH_DISC_MAX_FILES && d->ntracks <= MH_DISC_MAX_TRACKS);
        if (it % 16 == 0) {
            char o[MH_PATH_MAX], r[MH_PATH_MAX];
            const char *paths[2] = {"/x/y.bin", NULL};
            tmp_path("o.cue", o);
            tmp_path("r.cue", r);
            tmp_path("f.cue", p);
            if (d->flags & MH_DF_STRUCT_OK) (void)mh_cue_write_corrected(d, o);
            (void)mh_cue_count_files(p);
            (void)mh_cue_rewrite_files(p, r, paths, 2);
        }
        if (d->flags & MH_DF_STRUCT_OK) {
            /* a valid structure is always contiguous and ordered */
            for (unsigned i = 0; i < d->ntracks; i++) {
                CHECK(d->tracks[i].number == i + 1);
                CHECK(d->tracks[i].length > 0 && d->tracks[i].start + d->tracks[i].length <= d->total_sectors);
            }
        }
        mh_disc_free(d);
    }
}

static void fuzz_iso(int iters)
{
    Iso iso;
    uint8_t *exe = make_exe(4096, "SLUS_009.69"), *orig;
    IsoFile f[4] = {{"SYSTEM.CNF;1", "BOOT = cdrom:\\SLUS_009.69;1\r\nTCB = 4\r\n", 38, NULL},
                    {"SLUS_009.69;1", exe, 4096, NULL},
                    {"PSX.EXE;1", exe, 4096, NULL},
                    {"X.EXE;1", exe, 4096, "SUB"}};
    size_t total;
    iso_build(&iso, f, 4, 2352, 0x18, 2, 0);
    total = (size_t)iso.sectors * 2352;
    orig = malloc(total);
    memcpy(orig, iso.raw, total);
    for (int it = 0; it < iters; it++) {
        char p[MH_PATH_MAX];
        MhPsSerial r;
        memcpy(iso.raw, orig, total);
        for (int k = 1 + (int)(rnd() % 8); k > 0; k--) {
            size_t base = (size_t)(16 + rnd() % 4) * 2352 + 0x18;
            size_t pos = base + (rnd() % (rnd() % 2 ? 0x100 : 0x800));
            uint32_t op = rnd() % 3;
            if (op == 0) iso.raw[pos] = (uint8_t)rnd();
            else if (op == 1) iso.raw[pos] = 0xff;
            else iso.raw[pos] ^= (uint8_t)(1u << (rnd() % 8));
        }
        iso_write(&iso, "z.bin");
        tmp_path("z.bin", p);
        (void)mh_psserial_extract(&r, p);
        CHECK(r.cand_count <= 0xffff || r.cand_count > 0xffff); /* touch it: no crash is the check */
        if (it % 5 == 0) {
            size_t cut = rnd() % total;
            put_file("zt.bin", iso.raw, cut);
            tmp_path("zt.bin", p);
            (void)mh_psserial_extract(&r, p);
        }
        if (it % 5 == 1) {
            iso_write_raw_slots(&iso, "zr.bin", 3);
            tmp_path("zr.bin", p);
            (void)mh_psserial_extract_raw(&r, p, 3, iso.sectors);
        }
    }
    free(orig);
    free(exe);
    iso_free(&iso);
}

int main(void)
{
    tmp_init("audit");
    test_empty_cue();
    test_rewrite_trailing_line();
    test_cue_size_cap();
    test_scan_budget();
    fuzz_cue(400);
    fuzz_iso(200);
    rm_rf(g_tmp);
    printf("disc audit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail != 0;
}
