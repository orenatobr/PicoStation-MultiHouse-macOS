/* Extent list of the disc packages (orig: extent_insert, extent_allocate). */
#include "write_internal.h"

/* orig: extent_insert @0x1400199a0 */
bool mh_v2_extent_insert(MhV2Extent *ext, uint32_t *count, uint64_t start, uint64_t end)
{
    uint32_t n = *count, i = 0;
    if (n >= MH_V2_EXTENTS_MAX) return false;
    if (start < MH_V2_FIXED_BLOCKS || end <= start) return false;
    if ((start & (MH_V2_PKG_ALIGN - 1)) != 0) return false;
    if (n != 0) {
        while (i < n && ext[i].start < start) i++;
        if (i == 0) {
            if (ext[0].start < end) return false;
        } else {
            if (start < ext[i - 1].end) return false;
            if (i < n && ext[i].start < end) return false;
        }
        memmove(&ext[i + 1], &ext[i], (size_t)(n - i) * sizeof ext[0]);
    }
    ext[i].start = start;
    ext[i].end = end;
    *count = n + 1;
    return true;
}

/* orig: extent_allocate @0x1400198e0 */
bool mh_v2_extent_allocate(MhV2Extent *ext, uint32_t *count, uint64_t size, uint64_t cap,
                           uint64_t *out)
{
    uint64_t cand = MH_V2_FIXED_BLOCKS;
    uint32_t i;
    if (*count != 0) {
        for (i = 0; i < *count; i++) {
            cand = (cand + 0xff) & ~(uint64_t)0xff;
            if (cand <= ~size && size + cand <= ext[i].start) break;
            if (cand < ext[i].end) cand = ext[i].end;
        }
        cand = (cand + 0xff) & ~(uint64_t)0xff;
        if (~size < cand) return false;
    }
    if ((cap == 0 || cand + size <= cap) && mh_v2_extent_insert(ext, count, cand, cand + size)) {
        *out = cand;
        return true;
    }
    return false;
}
