/* art: .art decode/encode, resizing, disc mask, generate pipeline. See mh/art.h. */
#include "mh/art.h"

#include <stdlib.h>
#include <string.h>

#include "mh/path.h"
#include "mh/str.h"

#define ART_PAL_OFF 0x2407u
#define ART_MIN_FILE 0x207u

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* ---- context ------------------------------------------------------------------------------ */

/* orig: art_init @0x140002600 */
bool mh_art_init(MhArt *a, MhImageLoadFn load) {
    memset(a, 0, sizeof *a);
    a->load = load;
    return load != NULL;
}

/* orig: art_shutdown @0x1400027d0 */
void mh_art_shutdown(MhArt *a) {
    free(a->owned_template);
    memset(a, 0, sizeof *a);
}

void mh_art_set_case_template(MhArt *a, const uint8_t *data, size_t size) {
    free(a->owned_template);
    a->owned_template = NULL;
    a->case_template = data;
    a->case_template_size = size;
}

bool mh_art_load_case_template(MhArt *a, const char *path) {
    uint8_t *d;
    uint64_t n;
    if (!mh_read_entire_file(path, &d, &n)) return false;
    mh_art_set_case_template(a, d, (size_t)n);
    a->owned_template = d;
    return true;
}

/* ---- decode / encode ---------------------------------------------------------------------- */

/* orig: art_decode @0x1400023b0 (the file read is in mh_art_decode) */
bool mh_art_decode_mem(const uint8_t *d, size_t size, uint8_t **bgra, uint32_t *w, uint32_t *h) {
    if (size <= 0x206 || d[0] != 2 || d[1] == 0 || d[2] == 0) return false;
    uint32_t count = (uint32_t)d[1] * d[2];
    if (count != rd16(d + 3) || (size_t)count + ART_MIN_FILE > size) return false;
    uint8_t *out = malloc((size_t)count * 4);
    if (!out) return false;
    const uint8_t *pal = d + count + 7;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t c = rd16(pal + (size_t)d[5 + i] * 2);
        uint8_t *o = out + (size_t)i * 4;
        o[0] = (uint8_t)(((c >> 10 & 0x1f) * 0xff) / 0x1f); /* B */
        o[1] = (uint8_t)(((c >> 5 & 0x1f) * 0xff) / 0x1f);  /* G */
        o[2] = (uint8_t)(((c & 0x1f) * 0xff) / 0x1f);       /* R */
        o[3] = c != 0 ? 0xff : 0;
    }
    *bgra = out;
    *w = d[1];
    *h = d[2];
    return true;
}

bool mh_art_decode(const char *path, uint8_t **bgra, uint32_t *w, uint32_t *h) {
    uint8_t *d;
    uint64_t n;
    if (!mh_read_entire_file(path, &d, &n)) return false;
    bool ok = mh_art_decode_mem(d, (size_t)n, bgra, w, h);
    free(d);
    return ok;
}

/* orig: generate @0x14001b3d0, tail (palette and index conversion) */
void mh_art_encode(const uint8_t *bgra, uint8_t *out) {
    memset(out, 0, MH_ART_FILE_SIZE);
    out[0] = 2;
    out[1] = 0x60;
    out[2] = 0x60;
    out[3] = 0x00;
    out[4] = 0x24; /* count 0x2400, little endian */
    out[0x2405] = 0x00;
    out[0x2406] = 0x02; /* palette size 0x200 */
    for (uint32_t i = 1; i < 256; i++) {
        uint32_t r = (((i >> 5) * 0xff / 7) * 0x1f + 0x7f) / 0xff;
        uint32_t g = ((((i >> 2) & 7) * 0xff / 7) * 0x1f + 0x7f) / 0xff;
        uint32_t b = (((i & 3) * 0xff / 3) * 0x1f + 0x7f) / 0xff;
        uint32_t v = r | g << 5 | b << 10;
        if (v == 0) v = 0x421;
        out[ART_PAL_OFF + i * 2] = (uint8_t)v;
        out[ART_PAL_OFF + i * 2 + 1] = (uint8_t)(v >> 8);
    }
    for (uint32_t i = 0; i < MH_ART_PIXELS; i++) {
        const uint8_t *p = bgra + (size_t)i * 4;
        uint8_t idx = 0;
        if (p[3] >= 0x20) {
            idx = (uint8_t)((p[2] & 0xe0) | (p[1] >> 5) << 2 | p[0] >> 6);
            if (idx == 0) idx = 1;
        }
        out[5 + i] = idx;
    }
}

/* ---- resize / disc ------------------------------------------------------------------------ */

/* The original divided by 95 with a multiply-high trick; 16.16 position of sample n of 96. */
static void fix_pos(uint64_t x, uint32_t *idx, uint32_t *frac) {
    uint64_t q = x / 95;
    *frac = (uint32_t)(q & 0xffff);
    *idx = (uint32_t)((q >> 16) & 0xffff);
}

/* orig: generate, plain branch (and the 600 -> 96 reduction of the case branch) */
void mh_art_resize96(const uint8_t *src, uint32_t w, uint32_t h, uint8_t *out) {
    uint8_t *o = out;
    for (uint32_t row = 0; row < 96; row++) {
        uint32_t y0, fy;
        fix_pos((uint64_t)row * (h - 1) * 0x10000u, &y0, &fy);
        uint32_t y1 = y0 + 1 < h ? y0 + 1 : y0;
        for (uint32_t col = 0; col < 96; col++) {
            uint32_t x0, fx;
            fix_pos((uint64_t)col * (w - 1) * 0x10000u, &x0, &fx);
            uint32_t x1 = x0 + 1 < w ? x0 + 1 : x0;
            const uint8_t *p00 = src + ((size_t)y0 * w + x0) * 4;
            const uint8_t *p01 = src + ((size_t)y0 * w + x1) * 4;
            const uint8_t *p10 = src + ((size_t)y1 * w + x0) * 4;
            const uint8_t *p11 = src + ((size_t)y1 * w + x1) * 4;
            for (int c = 0; c < 4; c++) {
                uint32_t bot = (p11[c] * fx + (uint32_t)p10[c] * (0x10000u - fx)) >> 16;
                uint32_t top = (p01[c] * fx + (uint32_t)p00[c] * (0x10000u - fx)) >> 16;
                *o++ = (uint8_t)((bot * fy + top * (0x10000u - fy)) >> 16);
            }
        }
    }
}

/* orig: generate, disc branch */
bool mh_art_make_disc96(const uint8_t *src, uint32_t w, uint32_t h, uint8_t *out) {
    uint32_t s = w < h ? w : h;
    for (uint32_t row = 0; row < 96; row++) {
        uint32_t sy = ((h - s) >> 1) + (uint32_t)((uint64_t)row * (s - 1) / 95);
        for (uint32_t col = 0; col < 96; col++) {
            uint32_t sx = ((w - s) >> 1) + (uint32_t)((uint64_t)col * (s - 1) / 95);
            memcpy(out + ((size_t)row * 96 + col) * 4, src + ((size_t)sy * w + sx) * 4, 4);
        }
    }
    for (int row = 0; row < 96; row++) {
        for (int col = 0; col < 96; col++) {
            int dy = -95 + 2 * row, dx = -95 + 2 * col;
            uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
            uint8_t *p = out + ((size_t)row * 96 + col) * 4;
            if (d2 - 0x90u >= 0x22b2u || (d2 < 0x2111u && d2 <= 0x30fu && d2 < 0x121u)) {
                p[0] = p[1] = p[2] = p[3] = 0; /* outside the disc or the hub hole */
            } else if (d2 >= 0x2111u) {
                p[0] = p[1] = p[2] = 8; /* dark rim */
                p[3] = 0xff;
            } else if (d2 > 0x30fu) {
                p[3] = 0xff; /* picture kept */
            } else {
                p[0] = (uint8_t)((p[0] + 0xbe) >> 1); /* translucent hub ring */
                p[1] = (uint8_t)((p[1] + 0xc3) >> 1);
                p[2] = (uint8_t)((p[2] + 200) >> 1);
                p[3] = 0xdc;
            }
        }
    }
    return true;
}

/* ---- whole pipeline ----------------------------------------------------------------------- */

bool mh_art_build(const MhArt *a, MhArtKind kind, const uint8_t *src, uint32_t w, uint32_t h,
                  uint8_t *out, char *err, size_t err_n) {
    uint8_t px[MH_ART_BGRA_SIZE];
    if (kind == MH_ART_CASE) {
        uint8_t *c = a->case_template
                         ? mh_art_make_case600(a->case_template, a->case_template_size, src, w, h)
                         : NULL;
        if (!c) {
            mh_err_set(err, err_n, "Falha ao montar a imagem na case padrao.");
            return false;
        }
        mh_art_resize96(c, MH_CASE_TEMPLATE_DIM, MH_CASE_TEMPLATE_DIM, px);
        free(c);
    } else if (kind == MH_ART_DISC) {
        mh_art_make_disc96(src, w, h, px);
    } else {
        mh_art_resize96(src, w, h, px);
    }
    mh_art_encode(px, out);
    return true;
}

/* orig: generate @0x14001b3d0 */
bool mh_art_generate(const MhArt *a, MhArtKind kind, const char *src, const char *dst,
                     char *err, size_t err_n) {
    static const char kLoadFail[] = "Nao foi possivel carregar PNG, JPG, BMP, GIF ou TIFF pelo GDI+.";
    uint8_t *img = NULL;
    int w = 0, h = 0;
    if (err && err_n) err[0] = 0;
    if (!a->load || !a->load(src, &img, &w, &h) || !img || w < 1 || h < 1 ||
        (uint32_t)w > MH_ART_MAX_SRC_DIM || (uint32_t)h > MH_ART_MAX_SRC_DIM) {
        free(img);
        mh_err_set(err, err_n, "%s", kLoadFail);
        return false;
    }
    uint8_t *out = malloc(MH_ART_FILE_SIZE);
    if (!out) {
        free(img);
        mh_err_set(err, err_n, "Memoria insuficiente.");
        return false;
    }
    bool ok = mh_art_build(a, kind, img, (uint32_t)w, (uint32_t)h, out, err, err_n);
    if (ok && !mh_write_entire_file(dst, out, MH_ART_FILE_SIZE)) {
        mh_err_set(err, err_n, "Falha ao escrever o arquivo ART.");
        ok = false;
    }
    free(out);
    free(img);
    return ok;
}

/* ---- custom art --------------------------------------------------------------------------- */

/* orig: make_custom_art.isra.0 @0x1400223a0, file naming */
void mh_art_custom_name(bool is_case, const char *serial, unsigned case_no, char *out, size_t n) {
    char tmp[64];
    if (is_case) {
        MhWbuf b;
        mh_wbuf_init(&b);
        mh_wbuf_puts(&b, "CASE-");
        mh_wbuf_u32(&b, (uint8_t)case_no);
        mh_wbuf_puts(&b, ".art");
        mh_str_copy(out, n, mh_wbuf_cstr(&b));
        mh_wbuf_free(&b);
        return;
    }
    mh_str_copy(tmp, 48, serial ? serial : "");
    mh_str_copy(out, n, tmp);
    mh_str_cat(out, n, "-CD.art");
}

/* orig: make_custom_art.isra.0 @0x1400223a0, after the picker */
bool mh_art_import(const MhArt *a, bool is_case, const char *src, const char *dst, char *err,
                   size_t err_n) {
    if (mh_str_ends_i(src, ".art")) {
        uint64_t sz;
        if (!mh_file_size64(src, &sz) || sz != MH_ART_FILE_SIZE || !mh_copy_file_safe(src, dst)) {
            mh_err_set(err, err_n, "O arquivo ART precisa ter exatamente 10.240 bytes.");
            return false;
        }
        return true;
    }
    return mh_art_generate(a, is_case ? MH_ART_CASE : MH_ART_DISC, src, dst, err, err_n);
}
