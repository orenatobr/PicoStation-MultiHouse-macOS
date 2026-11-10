/* mh/str.h: string helpers and the growable string builder ("wbuf").
 *
 * All text is UTF-8 `char *`. The original had two parallel families, a_* (ASCII bytes) and
 * w_* (UTF-16). Both collapse here into mh_str_*: case folding stays ASCII-only exactly like
 * the original (only A-Z/a-z are folded, bytes >= 0x80 compare by value). NULL is accepted
 * wherever the original accepted it and behaves as the empty string. */
#ifndef MH_STR_H
#define MH_STR_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* orig: m_strlen, w_strlen, a_* length. NULL gives 0. */
size_t mh_strlen(const char *s);

/* orig: a_copy / w_copy. dst may be NULL or n == 0 (then nothing happens). Copies at most
 * n-1 bytes and always NUL terminates. A UTF-8 sequence cut by the limit is dropped whole. */
void mh_str_copy(char *dst, size_t n, const char *src);
/* orig: a_cat / w_cat. Appends src to the NUL terminated string in dst (capacity n). If dst
 * is already full (strlen >= n) nothing is changed. */
void mh_str_cat(char *dst, size_t n, const char *src);
/* orig: w_cmp. Byte compare returning -1/0/1 (the original returned exactly these). */
int mh_str_cmp(const char *a, const char *b);
/* orig: a_icmp / w_icmp. ASCII case-insensitive compare returning -1/0/1.
 * Deviation: a_icmp compared signed chars; here bytes >= 0x80 compare as unsigned (identical
 * for ASCII, which is all its callers pass). */
int mh_str_icmp(const char *a, const char *b);
/* orig: a_starts_i / w_starts_i. NULL s or NULL prefix gives false; empty prefix gives true. */
bool mh_str_starts_i(const char *s, const char *prefix);
/* orig: w_ends_i. NULL suffix or "" gives true. */
bool mh_str_ends_i(const char *s, const char *suffix);
/* orig: a_upper. In place, ASCII only. */
void mh_str_upper(char *s);
/* orig: ascii_find_i. Index of the first ASCII-case-insensitive match of needle in hay, or -1
 * (also -1 for an empty needle or needle longer than hay). */
int mh_str_find_i(const char *hay, const char *needle);

/* UTF-8 decoding: returns the next code point and advances *s. Invalid or truncated bytes
 * yield U+FFFD and consume one byte. At the NUL it returns 0 without advancing. */
uint32_t mh_utf8_next(const char **s);

/* orig: ascii_from_w. Copies text into an ASCII buffer: every UTF-16 code unit above 0x7F
 * becomes '?', so a BMP character gives one '?' and a character outside the BMP gives two
 * (surrogate pair). Truncates at n-1 and NUL terminates. */
void mh_ascii_from_utf8(const char *src, char *dst, size_t n);
/* orig: wide_from_ascii. The original widened bytes one to one (Latin-1). Here the bytes of
 * src are taken as ISO-8859-1 and written as UTF-8 (identical for pure ASCII). Truncates at
 * n-1 bytes of output, never inside a sequence. */
void mh_utf8_from_latin1(const char *src, char *dst, size_t n);

/* orig: bytes_to_hex. Upper-case hex of src[0..len). Writes as many whole bytes as fit in
 * n-1 characters and NUL terminates (n == 0: nothing is written). */
void mh_bytes_to_hex(const uint8_t *src, size_t len, char *dst, size_t n);
void mh_bytes_to_hex_lower(const uint8_t *src, size_t len, char *dst, size_t n);
/* Parses exactly 2*out_len hex digits (either case) into out. False on any other length or
 * non-hex character. Not in the original as a function; the database needs it. */
bool mh_hex_decode(const char *hex, uint8_t *out, size_t out_len);

/* orig: clean_ascii. Reads src as ASCII (bytes outside 0x20..0x7E are dropped, not replaced),
 * drops leading blanks, collapses runs of spaces to one, strips trailing blanks. Output is
 * at most n-1 characters. */
void mh_clean_ascii(const char *src, char *dst, size_t n);
/* orig: normalize_ascii_title (UTF-16 in, ASCII out). Builds the key used to compare titles:
 * A-Z, a-z (upper-cased), 0-9, ' - & . survive; Latin-1 letters (U+00C0..U+00FF) are mapped
 * to their base letter (AE, eth, thorn, sharp s and the math signs become separators);
 * everything else is a single collapsed space; no leading or trailing space. */
void mh_normalize_title(const char *src_utf8, char *dst, size_t n);
/* orig: normalize_serial. In place. Keeps A-Z (lower-case is upper-cased) 0-9 and '-', turns
 * '_' into '-', drops everything else (including '.') and keeps at most 15 characters, so the
 * buffer must hold 16 bytes. */
void mh_normalize_serial(char *s);
/* orig: trim_line. In place. Removes leading spaces and tabs and trailing spaces, tabs, CR
 * and LF. (Other control characters below 0x20 are NOT trimmed, as in the original.) */
void mh_trim_line(char *s);

/* ---- wbuf: growable string builder (orig: wbuf_*) -------------------------------------------
 * Zero-initialise or call mh_wbuf_init. data is NULL until something is added; use
 * mh_wbuf_cstr() to always get a valid C string. Every adder returns false only when memory
 * is exhausted. The buffer keeps a NUL after len. */
typedef struct MhWbuf {
    char *data;
    size_t len;
    size_t cap;
} MhWbuf;

void mh_wbuf_init(MhWbuf *b);
void mh_wbuf_free(MhWbuf *b); /* frees and re-initialises */
bool mh_wbuf_reserve(MhWbuf *b, size_t extra); /* room for len+extra+1 bytes */
bool mh_wbuf_putc(MhWbuf *b, char c);
bool mh_wbuf_puts(MhWbuf *b, const char *s); /* NULL or "" is a no-op that succeeds */
bool mh_wbuf_putn(MhWbuf *b, const char *s, size_t n);
bool mh_wbuf_u32(MhWbuf *b, uint32_t v);      /* decimal */
bool mh_wbuf_u64(MhWbuf *b, uint64_t v);      /* decimal */
bool mh_wbuf_hex32(MhWbuf *b, uint32_t v);    /* always 8 upper-case digits */
/* orig wbuf_size: "<int>[.<d>] <unit>" using binary units B, KB, MB, GB (1024 based), one
 * truncated decimal for KB and above, e.g. 1536 -> "1.5 KB", 1000 -> "1000 B", 2^30 -> "1.0 GB". */
bool mh_wbuf_size(MhWbuf *b, uint64_t bytes);
bool mh_wbuf_printf(MhWbuf *b, const char *fmt, ...) /* addition, not in the original */
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
const char *mh_wbuf_cstr(const MhWbuf *b);

#ifdef __cplusplus
}
#endif
#endif
