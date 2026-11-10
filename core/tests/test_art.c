/* test_art: .art codec against real files, synthetic checks of every building block. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mh/art.h"
#include "mh/base.h"

#define DB_DIR "/Users/renato.pereira/Downloads/PICOSTATION BR/Managers/database/art/default"
#define LIB_DIR "/Volumes/Renato/PicoStation BR"
#define TEMPLATE "resources/embedded/case_template.bgra"

static int checked;

/* Every real .art (the shipped database art uses an adaptive palette, not the Manager's
 * 3-3-2 one, so bytes cannot be reproduced) must decode, and the transparency mask must
 * survive a decode + encode cycle. */
static int padded_zero;
static void roundtrip_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { printf("skip: %s absent\n", dir); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 5 || e->d_name[0] == '.' || strcmp(e->d_name + n - 4, ".art")) continue;
        char path[MH_PATH_MAX];
        mh_path_join(dir, e->d_name, path, sizeof path);
        uint8_t *bgra;
        uint32_t w, h;
        assert(mh_art_decode(path, &bgra, &w, &h));
        assert(w == 96 && h == 96);
        uint8_t again[MH_ART_FILE_SIZE], *b2;
        mh_art_encode(bgra, again);
        assert(mh_art_decode_mem(again, sizeof again, &b2, &w, &h));
        for (size_t i = 0; i < MH_ART_PIXELS; i++) assert((bgra[i * 4 + 3] == 0) == (b2[i * 4 + 3] == 0));
        uint8_t *file;
        uint64_t sz;
        assert(mh_read_entire_file(path, &file, &sz) && sz == MH_ART_FILE_SIZE);
        size_t z = 0x2607;
        while (z < sz && file[z] == 0) z++;
        padded_zero += z == sz;
        free(file);
        free(b2);
        free(bgra);
        checked++;
    }
    closedir(d);
}

/* a file holding every palette index decodes and re-encodes to itself (the quantiser is the
 * exact inverse of the palette) */
static void test_palette_inverse(void) {
    uint8_t ref[MH_ART_BGRA_SIZE], art[MH_ART_FILE_SIZE], art2[MH_ART_FILE_SIZE];
    for (size_t i = 0; i < MH_ART_PIXELS; i++) {
        ref[i * 4] = (uint8_t)(i * 3); ref[i * 4 + 1] = (uint8_t)(i * 5); ref[i * 4 + 2] = (uint8_t)(i * 11); ref[i * 4 + 3] = 255;
    }
    mh_art_encode(ref, art);
    uint8_t *bgra;
    uint32_t w, h;
    assert(mh_art_decode_mem(art, sizeof art, &bgra, &w, &h));
    mh_art_encode(bgra, art2);
    assert(!memcmp(art, art2, sizeof art));
    free(bgra);
}

static void test_decode_rules(void) {
    uint8_t buf[MH_ART_FILE_SIZE] = {0};
    uint8_t px[MH_ART_BGRA_SIZE];
    for (size_t i = 0; i < sizeof px; i += 4) { px[i] = 10; px[i + 1] = 200; px[i + 2] = 90; px[i + 3] = 255; }
    mh_art_encode(px, buf);
    uint8_t *o;
    uint32_t w, h;
    assert(mh_art_decode_mem(buf, sizeof buf, &o, &w, &h));
    free(o);
    uint8_t bad[MH_ART_FILE_SIZE];
    memcpy(bad, buf, sizeof bad); bad[0] = 1;
    assert(!mh_art_decode_mem(bad, sizeof bad, &o, &w, &h));
    memcpy(bad, buf, sizeof bad); bad[1] = 0;
    assert(!mh_art_decode_mem(bad, sizeof bad, &o, &w, &h));
    memcpy(bad, buf, sizeof bad); bad[3] ^= 1;
    assert(!mh_art_decode_mem(bad, sizeof bad, &o, &w, &h));
    assert(!mh_art_decode_mem(buf, 0x2400 + 0x206, &o, &w, &h)); /* too short for the palette */
    assert(mh_art_decode_mem(buf, 0x2400 + 0x207, &o, &w, &h));
    free(o);
    assert(!mh_art_decode_mem(buf, 0x206, &o, &w, &h));
}

static void test_encode_rules(void) {
    uint8_t px[MH_ART_BGRA_SIZE] = {0}, out[MH_ART_FILE_SIZE];
    /* pixel 0: transparent (alpha 0x1f), 1: black opaque, 2: white, 3: R=0xE0 only, 4: B=0xC0 */
    memcpy(px + 0, (uint8_t[]){0xff, 0xff, 0xff, 0x1f}, 4);
    memcpy(px + 4, (uint8_t[]){0, 0, 0, 0x20}, 4);
    memcpy(px + 8, (uint8_t[]){0xff, 0xff, 0xff, 0xff}, 4);
    memcpy(px + 12, (uint8_t[]){0, 0, 0xe0, 0xff}, 4);
    memcpy(px + 16, (uint8_t[]){0xc0, 0, 0, 0xff}, 4);
    mh_art_encode(px, out);
    assert(!memcmp(out, "\x02\x60\x60\x00\x24", 5));
    assert(out[5] == 0 && out[6] == 1 && out[7] == 0xff && out[8] == 0xe0 && out[9] == 3);
    assert(out[0x2405] == 0x00 && out[0x2406] == 0x02);
    assert(out[0x2407] == 0 && out[0x2408] == 0);               /* entry 0 transparent */
    assert(out[0x2407 + 2] == 0x00 && out[0x2407 + 3] == 0x28);  /* entry 1: black is stored as index 1, a dim blue */
    assert(out[0x2407 + 0x1fe] == 0xff && out[0x2407 + 0x1ff] == 0x7f); /* entry 255: white */
    for (size_t i = 0x2607; i < MH_ART_FILE_SIZE; i++) assert(out[i] == 0);
}

static void test_resize(void) {
    uint8_t src[MH_ART_BGRA_SIZE], dst[MH_ART_BGRA_SIZE];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i * 7 + (i >> 8));
    mh_art_resize96(src, 96, 96, dst);
    assert(!memcmp(src, dst, sizeof src)); /* identity at the same size */
    uint8_t flat[200 * 50 * 4];
    for (size_t i = 0; i < sizeof flat; i++) flat[i] = (uint8_t)(i & 3 ? 77 : 200);
    mh_art_resize96(flat, 200, 50, dst);
    for (size_t i = 0; i < sizeof dst; i++) assert(abs(dst[i] - (i & 3 ? 77 : 200)) <= 1);
    uint8_t one[4] = {1, 2, 3, 4};
    mh_art_resize96(one, 1, 1, dst);
    for (size_t i = 0; i < sizeof dst; i++) assert(dst[i] == one[i & 3]);
    /* horizontal ramp 0..255 over 256 pixels: corners exact, monotone */
    uint8_t ramp[256 * 4];
    for (int x = 0; x < 256; x++) { ramp[x * 4] = ramp[x * 4 + 1] = ramp[x * 4 + 2] = (uint8_t)x; ramp[x * 4 + 3] = 255; }
    mh_art_resize96(ramp, 256, 1, dst);
    assert(dst[0] == 0 && dst[95 * 4] == 255);
    for (int x = 1; x < 96; x++) assert(dst[x * 4] >= dst[(x - 1) * 4]);
}

static void test_disc(void) {
    uint8_t src[120 * 100 * 4], out[MH_ART_BGRA_SIZE];
    for (size_t i = 0; i < sizeof src; i += 4) { src[i] = 40; src[i + 1] = 120; src[i + 2] = 220; src[i + 3] = 255; }
    assert(mh_art_make_disc96(src, 120, 100, out));
#define PX(x, y) (out + ((y) * 96 + (x)) * 4)
    assert(PX(0, 0)[3] == 0 && PX(95, 95)[3] == 0 && PX(0, 95)[3] == 0);   /* corners */
    assert(PX(47, 47)[3] == 0 && PX(48, 48)[3] == 0);                       /* hub hole */
    assert(PX(40, 40)[3] == 0xdc);                                           /* hub ring: blended */
    assert(PX(40, 40)[0] == (40 + 0xbe) >> 1 && PX(40, 40)[1] == (120 + 0xc3) >> 1 && PX(40, 40)[2] == (220 + 200) >> 1);
    assert(PX(20, 48)[3] == 0xff && PX(20, 48)[0] == 40 && PX(20, 48)[2] == 220); /* picture */
    assert(PX(1, 48)[3] == 0xff && PX(1, 48)[0] == 8 && PX(1, 48)[1] == 8 && PX(1, 48)[2] == 8); /* rim */
    assert(PX(0, 48)[3] == 0);                                               /* outside the rim */
}

static uint8_t *load_template(size_t *n) {
    uint8_t *t;
    uint64_t sz;
    if (!mh_read_entire_file(TEMPLATE, &t, &sz)) return NULL;
    *n = (size_t)sz;
    return t;
}

static void test_case(void) {
    size_t tn;
    uint8_t *tpl = load_template(&tn);
    if (!tpl) { printf("skip: %s absent (run from the repo root)\n", TEMPLATE); return; }
    assert(tn == MH_CASE_TEMPLATE_SIZE);
    uint8_t cover[64 * 80 * 4];
    for (size_t i = 0; i < sizeof cover; i += 4) { cover[i] = 30; cover[i + 1] = 140; cover[i + 2] = 250; cover[i + 3] = 255; }
    uint8_t *c = mh_art_make_case600(tpl, tn, cover, 64, 80);
    assert(c);
    assert(!memcmp(c, tpl, 4 * 600 * 10)); /* top rows are outside the quad: template kept */
    const uint8_t *mid = c + (300 * 600 + 330) * 4;
    assert(mid[3] == 0xff && mid[0] == 30 && mid[1] == 140 && mid[2] == 250);
    assert(memcmp(c, tpl, MH_CASE_TEMPLATE_SIZE) != 0);
    assert(!mh_art_make_case600(tpl, tn - 1, cover, 64, 80)); /* template too small */
    free(c);
    free(tpl);
}

/* ---- generate with a fake loader ---------------------------------------------------------- */

static int fake_load(const char *path, uint8_t **bgra, int *w, int *h) {
    if (strstr(path, "missing")) return 0;
    int W = 150, H = 200;
    uint8_t *p = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *q = p + ((size_t)y * W + x) * 4;
            q[0] = (uint8_t)x; q[1] = (uint8_t)y; q[2] = (uint8_t)(x + y); q[3] = 255;
        }
    *bgra = p; *w = W; *h = H;
    return 1;
}

static void test_generate(void) {
    char tmpdir[] = "/tmp/mh_art_test_XXXXXX";
    assert(mkdtemp(tmpdir));
    char dst[MH_PATH_MAX], err[256];
    mh_path_join(tmpdir, "x.art", dst, sizeof dst);
    MhArt a;
    assert(!mh_art_init(&a, NULL));
    assert(mh_art_init(&a, fake_load));
    assert(!mh_art_generate_plain(&a, "missing.png", dst, err, sizeof err));
    assert(!strcmp(err, "Could not load the image. Supported formats: PNG, JPG, BMP, GIF and TIFF."));
    assert(mh_art_generate_plain(&a, "a.png", dst, err, sizeof err) && err[0] == 0);
    uint8_t *f; uint64_t n;
    assert(mh_read_entire_file(dst, &f, &n) && n == MH_ART_FILE_SIZE);
    uint8_t *bg; uint32_t w, h;
    assert(mh_art_decode_mem(f, n, &bg, &w, &h) && w == 96);
    free(bg); free(f);
    assert(mh_art_generate_disc(&a, "a.png", dst, err, sizeof err));
    assert(mh_read_entire_file(dst, &f, &n));
    assert(mh_art_decode_mem(f, n, &bg, &w, &h));
    assert(bg[3] == 0 && bg[(48 * 96 + 20) * 4 + 3] == 255);
    free(bg); free(f);
    /* case without a template fails with the original text */
    assert(!mh_art_generate_case(&a, "a.png", dst, err, sizeof err));
    assert(!strcmp(err, "Failed to compose the image on the default Case."));
    if (mh_art_load_case_template(&a, TEMPLATE)) {
        assert(mh_art_generate_case(&a, "a.png", dst, err, sizeof err));
        assert(mh_read_entire_file(dst, &f, &n) && n == MH_ART_FILE_SIZE);
        free(f);
    }
    /* unwritable destination */
    assert(!mh_art_generate_plain(&a, "a.png", "/nonexistent_dir_x/y.art", err, sizeof err));
    assert(!strcmp(err, "Failed to write the ART file."));
    /* import: .art copy rules */
    char src[MH_PATH_MAX], dst2[MH_PATH_MAX];
    mh_path_join(tmpdir, "in.ART", src, sizeof src);
    mh_path_join(tmpdir, "out.art", dst2, sizeof dst2);
    assert(mh_write_entire_file(src, "short", 5));
    assert(!mh_art_import(&a, false, src, dst2, err, sizeof err));
    assert(!strcmp(err, "The ART file must be exactly 10,240 bytes."));
    uint8_t full[MH_ART_FILE_SIZE] = {7};
    assert(mh_write_entire_file(src, full, sizeof full));
    assert(mh_art_import(&a, true, src, dst2, err, sizeof err));
    assert(mh_file_exists(dst2));
    assert(mh_art_import(&a, false, "a.png", dst2, err, sizeof err)); /* generated via the loader */
    char nm[64];
    mh_art_custom_name(false, "SLUS-00001", 0, nm, sizeof nm);
    assert(!strcmp(nm, "SLUS-00001-CD.art"));
    mh_art_custom_name(true, NULL, 3, nm, sizeof nm);
    assert(!strcmp(nm, "CASE-3.art"));
    mh_art_shutdown(&a);
    char cmd[MH_PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmpdir);
    assert(system(cmd) == 0);
}

int main(void) {
    roundtrip_dir(DB_DIR);
    roundtrip_dir(LIB_DIR);
    printf("decoded %d real .art files (%d with a zero tail)\n", checked, padded_zero);
    test_palette_inverse();
    test_decode_rules();
    test_encode_rules();
    test_resize();
    test_disc();
    test_case();
    test_generate();
    puts("test_art: ok");
    return 0;
}
