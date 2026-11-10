/* MHBridge.h: the few core operations the UI needs that live in the v2 / manager modules.
 *
 * Each function is a thin adapter around one original function (named in the comment). They are
 * bound to the real core API in MHBridge.m; until a module lands the adapter reports
 * "not available" instead of crashing. All of them may be called from a worker thread. */
#ifndef MH_APP_BRIDGE_H
#define MH_APP_BRIDGE_H

#include "mh/disc.h"
#include "mh/blockdev.h"
#include "mh/cheat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result block of a finished write (orig: the 0x430 byte result of v2_prepare_write). */
typedef struct MhbWriteInfo {
    uint16_t cases;
    uint16_t discs;
    uint64_t raw_bytes;
    char msg[MH_ERR_MSG_MAX]; /* orig: wide message at +0x30 */
} MhbWriteInfo;

/* orig: manager_admit(disc, verify, progress_cb, ctx, keep_disc_with_observations, ...).
 * True when the disc was accepted; on false the reason is in d->verify_text / d->verified. */
bool mhb_manager_admit(MhDisc *d, bool verify, MhProgress *prog);

/* orig: manager_recode_copy. Writes a CUE/BIN copy with EDC/ECC recomputed. */
bool mhb_recode_copy(const MhDisc *d, const char *dst_cue, MhProgress *prog, char *err, size_t n);

/* orig: v2_estimate_project_bytes_for_target(project, drive, 0, 0x10). */
uint64_t mhb_estimate_bytes(MhProject *p, int drive);
/* orig: v2_prepare_write(project, 0, 0, &info). False with info->msg when a disc is not ready. */
bool mhb_prepare_write(MhProject *p, MhbWriteInfo *info);
/* orig: sd_write_project_direct(disk, project, resume=1, progress_cb, ...). */
bool mhb_write_sd(const MhDiskInfo *disk, MhProject *p, MhProgress *prog, MhbWriteInfo *info);
/* orig: v2_write_image_direct(project, path, 1, progress_cb, 0, &info). */
bool mhb_write_image(MhProject *p, const char *path, MhProgress *prog, MhbWriteInfo *info);
/* orig: v2_load_physical / v2_load_image. msg receives the load note (may be empty) or the
 * failure reason. */
bool mhb_load_physical(const MhDiskInfo *disk, const char *cache_dir, MhProject *p, char *msg,
                       size_t n);
bool mhb_load_image(const char *path, const char *cache_dir, MhProject *p, char *msg, size_t n);
/* orig: hydrate_loaded_project. */
void mhb_hydrate(MhProject *p, const MhDb *db, const MhCheatDb *cheats);
/* orig: auto_attach_cheats. */
void mhb_attach_cheats(MhDisc *d, const MhCheatDb *cheats);

#ifdef __cplusplus
}
#endif
#endif
