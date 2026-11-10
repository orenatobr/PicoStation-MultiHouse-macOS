/* test_art_image: the ImageIO loader (platform/mac) feeding the art generator, and PNG previews
 * of decoded .art files. The loader source is included on purpose: the Makefile only builds
 * the core/src module directories, and the file is plain C over CoreGraphics. */
#include "../../platform/mac/mh_image_mac.m"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mh/art.h"
#include "mh/base.h"

#define PREVIEW_DIR "/private/tmp/claude-501/-Volumes-Renato-PicoStation-BR/cdfb4acc-884d-4bb8-b7b1-d9c0a797961e/scratchpad/art_preview"
#define TEMPLATE "resources/embedded/case_template.bgra"
#define DB "/Users/renato.pereira/Downloads/PICOSTATION BR/Managers/database/art/default/"
#define COVER "resources/AppIcon-256.png"

static void save_x(const char *name, const uint8_t *bgra, int w, int h, int scale) {
    char path[MH_PATH_MAX];
    mh_path_join(PREVIEW_DIR, name, path, sizeof path);
    uint8_t *big = malloc((size_t)w * h * scale * scale * 4);
    for (int y = 0; y < h * scale; y++)
        for (int x = 0; x < w * scale; x++) memcpy(big + ((size_t)y * w * scale + x) * 4, bgra + ((size_t)(y / scale) * w + x / scale) * 4, 4);
    assert(mh_image_save_png(path, big, w * scale, h * scale));
    free(big);
}

/* flatten onto a checkerboard so transparency is visible */
static void checker(uint8_t *bgra, int n) {
    for (int i = 0; i < n; i++) {
        int bg = (((i % 96) / 8 + (i / 96) / 8) & 1) ? 0xd0 : 0x90;
        uint8_t *p = bgra + (size_t)i * 4;
        for (int c = 0; c < 3; c++) p[c] = (uint8_t)((p[c] * p[3] + bg * (255 - p[3])) / 255);
        p[3] = 255;
    }
}

static void preview_art(const char *file, const char *out) {
    uint8_t *b;
    uint32_t w, h;
    if (!mh_art_decode(file, &b, &w, &h)) return;
    checker(b, 96 * 96);
    save_x(out, b, 96, 96, 4);
    free(b);
}

int main(void) {
    if (!mh_ensure_dir(PREVIEW_DIR)) { puts("skip: no preview dir"); return 0; }
    uint8_t *img;
    int w, h;
    if (!mh_file_exists(COVER)) { puts("skip: run from the repo root"); return 0; }
    assert(mh_image_load_imageio(COVER, &img, &w, &h) && w == 256 && h == 256);
    assert(!mh_image_load_imageio("/nonexistent/none.png", &img, &w, &h));
    assert(!mh_image_load_imageio(TEMPLATE, &img, &w, &h)); /* not an image */

    /* save + load round trip, including straight alpha */
    uint8_t px[4 * 4] = {10, 20, 30, 255, 200, 100, 50, 128, 0, 0, 0, 0, 255, 255, 255, 255};
    char tmp[] = "/tmp/mh_art_img_XXXXXX";
    assert(mkdtemp(tmp));
    char png[MH_PATH_MAX];
    mh_path_join(tmp, "t.png", png, sizeof png);
    assert(mh_image_save_png(png, px, 2, 2));
    uint8_t *r;
    assert(mh_image_load_imageio(png, &r, &w, &h) && w == 2 && h == 2);
    assert(r[0] == 10 && r[1] == 20 && r[2] == 30 && r[3] == 255);
    assert(abs(r[4] - 200) <= 2 && abs(r[5] - 100) <= 2 && abs(r[6] - 50) <= 2 && r[7] == 128);
    assert(r[11] == 0 && r[12] == 255 && r[15] == 255);
    free(r);

    MhArt a;
    assert(mh_art_init(&a, mh_image_load_imageio));
    assert(mh_art_load_case_template(&a, TEMPLATE));
    char err[256], dst[MH_PATH_MAX];
    const struct { MhArtKind k; const char *n; } kinds[] = {{MH_ART_PLAIN, "plain"}, {MH_ART_CASE, "case"}, {MH_ART_DISC, "disc"}};
    for (int i = 0; i < 3; i++) {
        char nm[32];
        snprintf(nm, sizeof nm, "gen_%s.art", kinds[i].n);
        mh_path_join(tmp, nm, dst, sizeof dst);
        assert(mh_art_generate(&a, kinds[i].k, COVER, dst, err, sizeof err));
        snprintf(nm, sizeof nm, "gen_%s.png", kinds[i].n);
        preview_art(dst, nm);
    }
    assert(!mh_art_generate(&a, MH_ART_PLAIN, "/nonexistent/none.png", dst, err, sizeof err));
    assert(!strcmp(err, "Could not load the image. Supported formats: PNG, JPG, BMP, GIF and TIFF."));

    /* the full 600x600 case render, and the template itself */
    uint8_t *c = mh_art_make_case600(a.case_template, a.case_template_size, img, 256, 256);
    assert(c);
    assert(mh_image_save_png(PREVIEW_DIR "/case600.png", c, 600, 600));
    free(c);
    assert(mh_image_save_png(PREVIEW_DIR "/template600.png", a.case_template, 600, 600));

    /* some real database art */
    const char *samples[] = {"SCUS-94467-CASE.art", "SCUS-94467-CD.art", "SLUS-00594-CASE.art", "SLUS-00594-CD.art", "SCUS-94163-CASE.art", "SCUS-94163-CD.art"};
    for (size_t i = 0; i < sizeof samples / sizeof *samples; i++) {
        char f[MH_PATH_MAX], o[64];
        snprintf(f, sizeof f, DB "%s", samples[i]);
        snprintf(o, sizeof o, "db_%s.png", samples[i]);
        preview_art(f, o);
    }
    free(img);
    mh_art_shutdown(&a);
    char cmd[MH_PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    assert(system(cmd) == 0);
    puts("test_art_image: ok");
    return 0;
}
