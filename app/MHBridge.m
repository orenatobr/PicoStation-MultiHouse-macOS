#import "MHBridge.h"
#include <string.h>

/* ponytail: every adapter below is a placeholder until the matching core module (v2 writer and
 * loader, manager_admit, recode) is committed; each one is bound by replacing its body. */
#define MHB_NA "Recurso ainda nao disponivel nesta compilacao."

bool mhb_manager_admit(MhDisc *d, bool verify, MhProgress *prog) {
    (void)verify; (void)prog;
    if (d) { d->verified = 1; mh_err_set(d->verify_text, sizeof d->verify_text, "%s", ""); }
    return d != NULL;
}

bool mhb_recode_copy(const MhDisc *d, const char *dst_cue, MhProgress *prog, char *err, size_t n) {
    (void)d; (void)dst_cue; (void)prog;
    mh_err_set(err, n, "%s", MHB_NA);
    return false;
}

uint64_t mhb_estimate_bytes(MhProject *p, int drive) { (void)p; (void)drive; return 0; }

bool mhb_prepare_write(MhProject *p, MhbWriteInfo *info) { (void)p; (void)info; return true; }

bool mhb_write_sd(const MhDiskInfo *disk, MhProject *p, MhProgress *prog, MhbWriteInfo *info) {
    (void)disk; (void)p; (void)prog;
    mh_err_set(info->msg, sizeof info->msg, "%s", MHB_NA);
    return false;
}

bool mhb_write_image(MhProject *p, const char *path, MhProgress *prog, MhbWriteInfo *info) {
    (void)p; (void)path; (void)prog;
    mh_err_set(info->msg, sizeof info->msg, "%s", MHB_NA);
    return false;
}

bool mhb_load_physical(const MhDiskInfo *disk, const char *cache_dir, MhProject *p, char *msg,
                       size_t n) {
    (void)disk; (void)cache_dir; (void)p;
    mh_err_set(msg, n, "%s", MHB_NA);
    return false;
}

bool mhb_load_image(const char *path, const char *cache_dir, MhProject *p, char *msg, size_t n) {
    (void)path; (void)cache_dir; (void)p;
    mh_err_set(msg, n, "%s", MHB_NA);
    return false;
}

void mhb_hydrate(MhProject *p, const MhDb *db, const MhCheatDb *cheats) {
    (void)p; (void)db; (void)cheats;
}

void mhb_attach_cheats(MhDisc *d, const MhCheatDb *cheats) { (void)d; (void)cheats; }

#import "cheat_ui.h"
/* Weak fallback until app/cheat_ui.m (the cheat windows) is linked in. */
__attribute__((weak)) BOOL MHCheatEditProfile(NSWindow *parent, const MhCheatDb *db, NSString *headerText,
                                              const char *serial, uint8_t scope, MhCheatProfile **profile) {
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = @"Cheats do Disco";
    a.informativeText = @"O editor de cheats ainda nao esta disponivel nesta compilacao.";
    [a runModal];
    return NO;
}
__attribute__((weak)) NSInteger (^MHCheatUIAlertHook)(NSString *, NSString *, NSArray<NSString *> *) = nil;
__attribute__((weak)) NSURL *(^MHCheatUIPanelHook)(BOOL) = nil;
