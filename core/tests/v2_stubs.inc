/* Stand-ins for the platform half of the disk layer (platform/mac is not linked in core tests).
 * The "device" is the regular file named by g_v2_dev_path; g_v2_identity_ok toggles the identity check. */
#include "mh/blockdev.h"
static const char *g_v2_dev_path;
static int g_v2_identity_ok = 1;

bool mh_disk_identity_matches(const MhDiskInfo *known, const MhDiskOpts *opts, MhDiskInfo *fresh)
{
    (void)opts;
    if (!g_v2_identity_ok) return false;
    *fresh = *known;
    return true;
}
MhBlockDev *mh_blockdev_open_read(const MhDiskInfo *known, const MhDiskOpts *opts, char *err, size_t errn)
{
    (void)known; (void)opts;
    return g_v2_dev_path ? mh_blockdev_open_file(g_v2_dev_path, false, 0, err, errn) : NULL;
}
MhBlockDev *mh_blockdev_open_write(const MhDiskInfo *known, const MhDiskOpts *opts, char *err, size_t errn)
{
    (void)known; (void)opts;
    return g_v2_dev_path ? mh_blockdev_open_file(g_v2_dev_path, true, 0, err, errn) : NULL;
}
