/* How many bytes a project occupies on a card or image (orig: v2_estimate_project_bytes*). */
#include "write_internal.h"

/* orig: v2_estimate_project_bytes_for_target @0x140035dd0 (+ constprop.0 @0x1400361a0, the
 * same function with phys_id == -1 and unrolled five times). The two passes: preserved
 * packages first (they keep their LBA), then every other disc is allocated first fit. */
uint64_t mh_v2_estimate_project_bytes_for_target(const MhProject *p, int32_t phys_id,
                                                 const char *path)
{
    MhV2Extent ext[MH_V2_EXTENTS_MAX];
    uint32_t count = 0;
    unsigned ci, j;
    if (!p) return 0;
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *c = &p->cases[ci];
        if (!c->in_use || c->disc_count == 0) continue;
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            uint64_t lba;
            if (!d) return 0;
            lba = mh_kept_package(d, phys_id, path);
            if (lba != 0 && !mh_v2_extent_insert(ext, &count, lba, lba + MH_V2_PKG_BLOCKS(d->total_sectors)))
                return 0;
        }
    }
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *c = &p->cases[ci];
        if (!c->in_use || c->disc_count == 0) continue;
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            uint64_t unused;
            if (mh_kept_package(d, phys_id, path) != 0) continue;
            if (!mh_v2_extent_allocate(ext, &count, MH_V2_PKG_BLOCKS(d->total_sectors), 0, &unused))
                return 0;
        }
    }
    return count == 0 ? MH_V2_FIXED_ZONE : ext[count - 1].end << 9;
}

/* orig: v2_estimate_project_bytes @0x140035d80 */
uint64_t mh_v2_estimate_project_bytes(const MhProject *p)
{
    if (!p) return 0;
    if (p->card_loaded) return mh_v2_estimate_project_bytes_for_target(p, p->device, NULL);
    if (!p->image_loaded || p->image_path[0] == 0)
        return mh_v2_estimate_project_bytes_for_target(p, -1, NULL);
    return mh_v2_estimate_project_bytes_for_target(p, -1, p->image_path);
}
