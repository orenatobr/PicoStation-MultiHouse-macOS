/* String helpers and the MhWbuf builder. */
#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* orig: m_strlen @0x14001e3f0, w_strlen @0x14003e0f0 */
size_t mh_strlen(const char *s) { return s ? strlen(s) : 0; }

/* orig: a_copy @0x1400010f0, w_copy @0x14003ddf0 */
void mh_str_copy(char *dst, size_t n, const char *src)
{
    size_t len, k;
    if (!dst || n == 0) return;
    len = src ? strlen(src) : 0;
    k = len < n - 1 ? len : n - 1;
    if (k < len) k = mh_utf8_whole_prefix(src, k);
    if (k) memmove(dst, src, k);
    dst[k] = 0;
}

/* orig: a_cat @0x140001060, w_cat @0x14003dce0 */
void mh_str_cat(char *dst, size_t n, const char *src)
{
    size_t have, len, k;
    if (!dst || n == 0) return;
    have = strnlen(dst, n);
    if (have >= n) return;
    len = src ? strlen(src) : 0;
    k = len < n - 1 - have ? len : n - 1 - have;
    if (k < len) k = mh_utf8_whole_prefix(src, k);
    if (k) memmove(dst + have, src, k);
    dst[have + k] = 0;
}

/* orig: w_cmp @0x14003dd90 */
int mh_str_cmp(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)(a ? a : "");
    const unsigned char *y = (const unsigned char *)(b ? b : "");
    for (;; x++, y++) {
        if (*x != *y) return *x < *y ? -1 : 1;
        if (!*x) return 0;
    }
}

/* orig: a_icmp @0x140001150, w_icmp @0x14003dfb0 */
int mh_str_icmp(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)(a ? a : "");
    const unsigned char *y = (const unsigned char *)(b ? b : "");
    for (;; x++, y++) {
        int p = mh_ascii_lower(*x), q = mh_ascii_lower(*y);
        if (p != q) return p < q ? -1 : 1;
        if (!p) return 0;
    }
}

/* orig: a_starts_i @0x140001220, w_starts_i @0x14003e080 */
bool mh_str_starts_i(const char *s, const char *prefix)
{
    if (!s || !prefix) return false;
    for (; *prefix; s++, prefix++)
        if (mh_ascii_lower((unsigned char)*s) != mh_ascii_lower((unsigned char)*prefix)) return false;
    return true;
}

/* orig: w_ends_i @0x14003de60 */
bool mh_str_ends_i(const char *s, const char *suffix)
{
    size_t a = mh_strlen(s), b = mh_strlen(suffix);
    if (b > a) return false;
    return mh_str_icmp(s ? s + (a - b) : "", suffix) == 0;
}

/* orig: a_upper @0x140001290 */
void mh_str_upper(char *s)
{
    if (!s) return;
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

/* orig: ascii_find_i @0x140002820 */
int mh_str_find_i(const char *hay, const char *needle)
{
    size_t h = mh_strlen(hay), n = mh_strlen(needle), i, j;
    if (n == 0 || n > h) return -1;
    for (i = 0; i + n <= h; i++) {
        for (j = 0; j < n; j++)
            if (mh_ascii_lower((unsigned char)hay[i + j]) != mh_ascii_lower((unsigned char)needle[j])) break;
        if (j == n) return (int)i;
    }
    return -1;
}

uint32_t mh_utf8_next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    uint32_t c = s[0], cp;
    int extra, i;
    if (c == 0) return 0;
    if (c < 0x80) {
        *sp += 1;
        return c;
    }
    if (c >= 0xC2 && c <= 0xDF) { extra = 1; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { extra = 2; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { extra = 3; cp = c & 0x07; }
    else { *sp += 1; return 0xFFFD; }
    for (i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *sp += 1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if ((extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
        (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))) {
        *sp += 1;
        return 0xFFFD;
    }
    *sp += extra + 1;
    return cp;
}

/* orig: ascii_from_w @0x1400028c0 */
void mh_ascii_from_utf8(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    uint32_t cp;
    if (!dst || n == 0) return;
    if (src) {
        while (o + 1 < n && (cp = mh_utf8_next(&src)) != 0) {
            if (cp < 0x80) {
                dst[o++] = (char)cp;
            } else {
                dst[o++] = '?';
                if (cp >= 0x10000 && o + 1 < n) dst[o++] = '?'; /* surrogate pair = 2 units */
            }
        }
    }
    dst[o] = 0;
}

/* orig: wide_from_ascii @0x14003ea60 */
void mh_utf8_from_latin1(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    if (!dst || n == 0) return;
    for (; src && *src; src++) {
        unsigned char c = (unsigned char)*src;
        if (c < 0x80) {
            if (o + 1 >= n) break;
            dst[o++] = (char)c;
        } else {
            if (o + 2 >= n) break;
            dst[o++] = (char)(0xC0 | (c >> 6));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    dst[o] = 0;
}

static void hex_out(const uint8_t *src, size_t len, char *dst, size_t n, const char *digits)
{
    size_t i = 0;
    if (!dst || n == 0) return;
    if (src)
        for (; i < len && 2 * i + 2 < n; i++) {
            dst[2 * i] = digits[src[i] >> 4];
            dst[2 * i + 1] = digits[src[i] & 15];
        }
    dst[2 * i] = 0;
}

/* orig: bytes_to_hex @0x140002f50 */
void mh_bytes_to_hex(const uint8_t *src, size_t len, char *dst, size_t n)
{
    hex_out(src, len, dst, n, "0123456789ABCDEF");
}

void mh_bytes_to_hex_lower(const uint8_t *src, size_t len, char *dst, size_t n)
{
    hex_out(src, len, dst, n, "0123456789abcdef");
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool mh_hex_decode(const char *hex, uint8_t *out, size_t out_len)
{
    size_t i;
    if (!hex || strlen(hex) != 2 * out_len) return false;
    for (i = 0; i < out_len; i++) {
        int h = hexval((unsigned char)hex[2 * i]), l = hexval((unsigned char)hex[2 * i + 1]);
        if (h < 0 || l < 0) return false;
        out[i] = (uint8_t)(h << 4 | l);
    }
    return true;
}

/* orig: clean_ascii @0x140010c70 */
void mh_clean_ascii(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    bool prev_space = false;
    if (!dst || n == 0) return;
    for (; src && *src && o + 1 < n; src++) {
        unsigned char c = (unsigned char)*src;
        if (c < 0x20 || c > 0x7E) continue; /* dropped, state unchanged */
        if (c != ' ') {
            dst[o++] = (char)c;
            prev_space = false;
        } else {
            if (o != 0 && !prev_space) dst[o++] = ' ';
            prev_space = true;
        }
    }
    while (o > 0 && dst[o - 1] == ' ') o--;
    dst[o] = 0;
}

/* orig: normalize_ascii_title @0x140024e50 (table: 64 bytes at 0x140f1a800) */
void mh_normalize_title(const char *src, char *dst, size_t n)
{
    static const char fold[65] =
        "AAAAAA CEEEEIIII NOOOOO  UUUUY  AAAAAA CEEEEIIII NOOOOO  UUUUY Y";
    size_t o = 0;
    bool prev_sep = false;
    uint32_t cp;
    if (!dst || n == 0) return;
    if (src) {
        while (o + 1 < n && (cp = mh_utf8_next(&src)) != 0) {
            unsigned b = 0x20; /* anything that is not kept becomes a separator */
            bool keep = false;
            if (cp < 0x80) b = cp;
            else if (cp >= 0xC0 && cp < 0x100) b = (unsigned char)fold[cp - 0xC0];
            if (b >= 'a' && b <= 'z') { b -= 32; keep = true; }
            else if ((b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '\'' || b == '-' ||
                     (b & 0xF7) == 0x26) keep = true;
            if (keep) {
                dst[o++] = (char)b;
                prev_sep = false;
            } else if (!prev_sep && o != 0) {
                dst[o++] = ' ';
                prev_sep = true;
            }
        }
    }
    while (o > 0 && dst[o - 1] == ' ') o--;
    dst[o] = 0;
}

/* Port only: a "(En,Fr,De)" language list. src is just after the "(". Returns the text after the ")", or NULL. */
static const char *lang_group_end(const char *src)
{
    for (;;) {
        if (!isalpha((unsigned char)src[0]) || !isalpha((unsigned char)src[1])) return NULL;
        src += 2;
        if (*src == ')') return src + 1;
        if (*src++ != ',') return NULL;
    }
}

/* Port only, see str.h. */
void mh_card_title(const char *src, char *dst, size_t n)
{
    static const char fold[65] =
        "AAAAAA CEEEEIIII NOOOOO  UUUUY  AAAAAA CEEEEIIII NOOOOO  UUUUY Y";
    size_t o = 0;
    bool prev_sep = false;
    uint32_t cp;
    const char *e;
    if (!dst || n == 0) return;
    while (src && o + 1 < n && (cp = mh_utf8_next(&src)) != 0) {
        unsigned char b = ' '; /* anything that is not kept becomes a separator */
        if (cp == '(' && (e = lang_group_end(src)) != NULL) src = e;
        else if (cp > 0x20 && cp < 0x7F) b = (unsigned char)cp;
        else if (cp >= 0xC0 && cp < 0x100) {
            b = (unsigned char)fold[cp - 0xC0];
            if (cp >= 0xE0 && b != ' ') b += 32;
        }
        if (b != ' ') {
            dst[o++] = (char)b;
            prev_sep = false;
        } else if (!prev_sep && o != 0) {
            dst[o++] = ' ';
            prev_sep = true;
        }
    }
    while (o > 0 && dst[o - 1] == ' ') o--;
    dst[o] = 0;
}

/* Port only: what comes before the first "(" is the game name; the prefix of the card title is the card
 * title of the prefix because the conversion is character by character. */
size_t mh_title_base_len(const char *src)
{
    const char *p = src ? strchr(src, '(') : NULL;
    char base[0x100], norm[0x60];
    size_t k;
    if (!p) return 0;
    k = (size_t)(p - src);
    if (k >= sizeof base) k = sizeof base - 1;
    memcpy(base, src, k);
    base[k] = 0;
    mh_card_title(base, norm, sizeof norm);
    return strlen(norm);
}

/* orig: normalize_serial @0x140024f30. The original collected up to 31 kept characters in a
 * scratch buffer and copied back the first 15. */
void mh_normalize_serial(char *s)
{
    char tmp[32];
    size_t o = 0, i;
    if (!s) return;
    for (i = 0; s[i] && o < 31; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '_') tmp[o++] = '-';
        else if (c >= 'a' && c <= 'z') tmp[o++] = (char)(c - 32);
        else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') tmp[o++] = (char)c;
    }
    if (o > 15) o = 15;
    memcpy(s, tmp, o);
    s[o] = 0;
}

/* orig: trim_line @0x140032420 */
void mh_trim_line(char *s)
{
    size_t skip = 0, len;
    if (!s) return;
    while (s[skip] == ' ' || s[skip] == '\t') skip++;
    len = strlen(s + skip);
    if (skip) memmove(s, s + skip, len + 1);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = 0;
}

/* ---- wbuf ---------------------------------------------------------------------------------- */

/* orig: wbuf_init @0x14003e290 */
void mh_wbuf_init(MhWbuf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* orig: wbuf_free @0x14003e110 */
void mh_wbuf_free(MhWbuf *b)
{
    free(b->data);
    mh_wbuf_init(b);
}

/* orig: wbuf_reserve @0x14003e420. Growth: start at 128, double up to 32768, then +32768. */
bool mh_wbuf_reserve(MhWbuf *b, size_t extra)
{
    size_t need = b->len + 1 + extra, cap = b->cap;
    char *p;
    if (cap >= need) return true;
    if (cap == 0) cap = 0x80;
    while (cap < need) cap = cap >= 0x8000 ? cap + 0x8000 : cap * 2;
    p = b->data ? realloc(b->data, cap) : malloc(cap);
    if (!p) return false;
    b->data = p;
    b->cap = cap;
    if (b->len == 0) p[0] = 0;
    return true;
}

bool mh_wbuf_putn(MhWbuf *b, const char *s, size_t n)
{
    if (!mh_wbuf_reserve(b, n)) return false;
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
    return true;
}

/* orig: wbuf_putc @0x14003e2a0 */
bool mh_wbuf_putc(MhWbuf *b, char c) { return mh_wbuf_putn(b, &c, 1); }

/* orig: wbuf_puts @0x14003e390 */
bool mh_wbuf_puts(MhWbuf *b, const char *s) { return mh_wbuf_putn(b, s ? s : "", mh_strlen(s)); }

/* orig: wbuf_u64 @0x14003e710, wbuf_u32 @0x14003e700 */
bool mh_wbuf_u64(MhWbuf *b, uint64_t v)
{
    char t[24];
    int i = (int)sizeof t;
    do {
        t[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    return mh_wbuf_putn(b, t + i, sizeof t - (size_t)i);
}
bool mh_wbuf_u32(MhWbuf *b, uint32_t v) { return mh_wbuf_u64(b, v); }

/* orig: wbuf_hex32 @0x14003e150 */
bool mh_wbuf_hex32(MhWbuf *b, uint32_t v)
{
    char t[8];
    int i;
    for (i = 0; i < 8; i++) t[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15];
    return mh_wbuf_putn(b, t, 8);
}

/* orig: wbuf_size @0x14003e4f0 (suffixes " GB", " MB", " KB", " B" at 0x140f13918..) */
bool mh_wbuf_size(MhWbuf *b, uint64_t bytes)
{
    uint64_t unit;
    const char *sfx;
    if (bytes >= (1ull << 30)) { unit = 1ull << 30; sfx = " GB"; }
    else if (bytes >= (1ull << 20)) { unit = 1ull << 20; sfx = " MB"; }
    else if (bytes >= 1024) { unit = 1024; sfx = " KB"; }
    else { unit = 1; sfx = " B"; }
    if (!mh_wbuf_u64(b, bytes / unit)) return false;
    if (unit != 1) {
        if (!mh_wbuf_putc(b, '.')) return false;
        if (!mh_wbuf_u64(b, (bytes % unit) * 10 / unit)) return false;
    }
    return mh_wbuf_puts(b, sfx);
}

bool mh_wbuf_printf(MhWbuf *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[256];
    int r;
    va_start(ap, fmt);
    r = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (r < 0) return false;
    if ((size_t)r < sizeof tmp) return mh_wbuf_putn(b, tmp, (size_t)r);
    {
        char *big = malloc((size_t)r + 1);
        bool ok;
        if (!big) return false;
        va_start(ap, fmt);
        vsnprintf(big, (size_t)r + 1, fmt, ap);
        va_end(ap);
        ok = mh_wbuf_putn(b, big, (size_t)r);
        free(big);
        return ok;
    }
}

const char *mh_wbuf_cstr(const MhWbuf *b) { return b->data ? b->data : ""; }
