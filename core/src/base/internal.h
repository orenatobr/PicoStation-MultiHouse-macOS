/* Private helpers of the base module. */
#ifndef MH_BASE_INTERNAL_H
#define MH_BASE_INTERNAL_H

#include "mh/base.h"

/* If s[0..len) ends in the middle of a UTF-8 sequence, returns the length without that
 * partial sequence; otherwise len. */
static inline size_t mh_utf8_whole_prefix(const char *s, size_t len)
{
    size_t j = len;
    if (len == 0) return 0;
    while (j > 0 && len - j < 4 && ((unsigned char)s[j - 1] & 0xC0) == 0x80) j--;
    if (j == 0) return len;
    {
        unsigned char lead = (unsigned char)s[j - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (lead >= 0xC0 && len - (j - 1) < need) return j - 1;
    }
    return len;
}

static inline int mh_ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static inline bool mh_is_sep(char c) { return c == '/' || c == '\\'; }

#endif
