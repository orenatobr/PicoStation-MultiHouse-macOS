/* art: make_case, the perspective projection of a cover onto the case template.
 *
 * The original solved an 8x8 linear system (Gauss-Jordan with partial pivoting) for the
 * homography that maps the cover rectangle (0,0) (w,0) (w,h) (0,h) onto the quad
 * (80,20) (590,31) (592,539) (81,583) of the 600x600 template, then for every template pixel
 * applied the inverse mapping and sampled the cover bilinearly. The floating point order of
 * operations is kept so results match the original bit for bit; contraction into fused
 * multiply-add would change them, so it is switched off. */
#pragma STDC FP_CONTRACT OFF

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mh/art.h"

static unsigned char clamp_round(double v) {
    if (!(v >= 0.0)) return 0;
    if (v > 255.0) return 0xff;
    return (unsigned char)(int)(v + 0.5);
}

/* orig: make_case.constprop.0 @0x140021400 */
uint8_t *mh_art_make_case600(const uint8_t *tpl, size_t tpl_size, const uint8_t *src, uint32_t sw,
                             uint32_t sh) {
    if (tpl_size < MH_CASE_TEMPLATE_SIZE) return NULL;
    uint8_t *dst = malloc(MH_CASE_TEMPLATE_SIZE);
    if (!dst) return NULL;
    memcpy(dst, tpl, MH_CASE_TEMPLATE_SIZE);

    const double W = (double)sw, H = (double)sh;
    /* u = (a x + b y + c) / (g x + h y + 1), v = (d x + e y + f) / (g x + h y + 1) */
    static const double U[4] = {80.0, 590.0, 592.0, 81.0};
    static const double V[4] = {20.0, 31.0, 539.0, 583.0};
    const double X[4] = {0.0, W, W, 0.0};
    const double Y[4] = {0.0, 0.0, H, H};
    double m[8][9];
    memset(m, 0, sizeof m);
    for (int k = 0; k < 4; k++) {
        double *r = m[2 * k], *s = m[2 * k + 1];
        r[0] = X[k]; r[1] = Y[k]; r[2] = 1.0; r[6] = -U[k] * X[k]; r[7] = -U[k] * Y[k]; r[8] = U[k];
        s[3] = X[k]; s[4] = Y[k]; s[5] = 1.0; s[6] = -V[k] * X[k]; s[7] = -V[k] * Y[k]; s[8] = V[k];
    }
    for (int i = 0; i < 8; i++) {
        int piv = i;
        double best = fabs(m[i][i]);
        for (int r = i + 1; r < 8; r++) {
            double c = fabs(m[r][i]);
            if (!(c <= best)) { piv = r; best = c; }
        }
        for (int c = 0; c < 9; c++) { double t = m[i][c]; m[i][c] = m[piv][c]; m[piv][c] = t; }
        double p = m[i][i];
        if (fabs(p) < 1e-12) { free(dst); return NULL; }
        for (int c = 0; c < 9; c++) m[i][c] = m[i][c] / p;
        for (int r = 0; r < 8; r++) {
            if (r == i) continue;
            double f = m[r][i];
            for (int c = 0; c < 9; c++) m[r][c] = m[r][c] - m[i][c] * f;
        }
    }
    const double a = m[0][8], b = m[1][8], c = m[2][8], d = m[3][8];
    const double e = m[4][8], f = m[5][8], g = m[6][8], h = m[7][8];
    const double t41 = e - f * h;
    const double t32 = d * h - g * e;
    double det = (a * t41 - (d - f * g) * b) + c * t32;
    if (fabs(det) < 1e-12) { free(dst); return NULL; }
    const double inv = 1.0 / det;

    for (int row = 0; row < 600; row++) {
        const double Yd = (double)row;
        uint8_t *o = dst + (size_t)row * 600 * 4;
        for (int col = 0; col < 600; col++, o += 4) {
            const double Xd = (double)col;
            double den = Xd * t32 * inv + Yd * (g * b - h * a) * inv + (a * e - b * d) * inv;
            if (den <= 1e-6) continue;
            double sx = (Xd * t41 * inv + (c * h - b) * inv * Yd + (b * f - c * e) * inv) / den;
            if (sx < 0.0) continue;
            double sy = (Xd * (f * g - d) * inv + (a - c * g) * inv * Yd + (c * d - a * f) * inv) / den;
            if (sy < 0.0) continue;
            if (W <= sx || H <= sy) continue;
            uint32_t x0 = (uint32_t)(int64_t)sx, y0 = (uint32_t)(int64_t)sy;
            uint32_t x1 = x0 + 1 < sw ? x0 + 1 : x0;
            uint32_t y1 = y0 + 1 < sh ? y0 + 1 : y0;
            double fx = sx - (double)x0, fy = sy - (double)y0;
            double ix = 1.0 - fx, iy = 1.0 - fy;
            const uint8_t *p00 = src + ((size_t)y0 * sw + x0) * 4;
            const uint8_t *p01 = src + ((size_t)y0 * sw + x1) * 4;
            const uint8_t *p10 = src + ((size_t)y1 * sw + x0) * 4;
            const uint8_t *p11 = src + ((size_t)y1 * sw + x1) * 4;
            for (int k = 0; k < 3; k++) {
                double v = (double)p10[k] * ix * fy + (double)p01[k] * fx * iy +
                           (double)p00[k] * ix * iy + (double)p11[k] * fx * fy;
                o[k] = clamp_round(v);
            }
            o[3] = 0xff;
        }
    }
    return dst;
}
