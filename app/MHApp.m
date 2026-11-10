#import "MHApp.h"
#include "mh/base.h"
#include "mh/art.h"
#include "mh_image_mac.h"
#include "mh_disk_mac.h"

NSInteger (^MHAlertHook)(NSString *, NSString *, NSArray<NSString *> *) = nil;

void MHMain(dispatch_block_t block) {
    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

NSString *MHStr(const char *s) { return s ? ([NSString stringWithUTF8String:s] ?: @"") : @""; }

NSString *MHDisplay(NSString *s) {
    return [s stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
}

static NSInteger run_alert(NSString *title, NSString *text, NSArray<NSString *> *buttons,
                           NSAlertStyle style) {
    __block NSInteger r = 0;
    MHMain(^{
        if (MHAlertHook) { r = MHAlertHook(title, text, buttons); return; }
        NSAlert *a = [[NSAlert alloc] init];
        a.alertStyle = style;
        a.messageText = title;
        a.informativeText = MHDisplay(text);
        for (NSString *b in buttons) [a addButtonWithTitle:b];
        r = [a runModal] - NSAlertFirstButtonReturn;
    });
    return r;
}

void MHMessage(NSString *title, NSString *text, MHIcon icon) {
    NSAlertStyle st = icon == MHIconError ? NSAlertStyleCritical
                    : icon == MHIconWarning ? NSAlertStyleWarning : NSAlertStyleInformational;
    run_alert(title, text, @[ @"OK" ], st);
}

BOOL MHAsk(NSString *text) {
    /* Default answer is No (MB_DEFBUTTON2): it is the first button, so Return picks it. The
     * hook sees the buttons in logical order (Sim, Nao). */
    if (MHAlertHook) return MHAlertHook(@"Confirmacao", text, @[ @"Sim", @"Nao" ]) == 0;
    NSInteger r = run_alert(@"Confirmacao", text, @[ @"Nao", @"Sim" ], NSAlertStyleWarning);
    return r == 1;
}

@implementation MHApp {
    MhProject *_project;
    MhDb *_db;
    MhCheatDb *_cheat;
    MhArt *_art;
    MhV2Env *_env;
    MhV2Writer *_writer;
    uint8_t *_menu, *_music;
}

+ (MHApp *)shared {
    static MHApp *a;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ a = [[MHApp alloc] init]; });
    return a;
}

- (instancetype)init {
    if ((self = [super init])) {
        _project = calloc(1, sizeof(MhProject));
        _db = calloc(1, sizeof(MhDb));
        _cheat = calloc(1, sizeof(MhCheatDb));
        _art = calloc(1, sizeof(MhArt));
        _env = calloc(1, sizeof(MhV2Env));
        _writer = calloc(1, sizeof(MhV2Writer));
        _curCase = -1;
        _curDisc = -1;
        _verifyOnAdd = YES; /* orig: DAT_140e7b3ac = 1 */
    }
    return self;
}

- (MhProject *)project { return _project; }
- (MhDb *)db { return _db; }
- (MhCheatDb *)cheatDb { return _cheat; }
- (MhArt *)art { return _art; }
- (MhV2Env *)env { return _env; }
- (MhV2Writer *)writer { return _writer; }
- (MhDiskOpts)diskOpts {
    MhDiskOpts o = {.flags = mh_disk_flags_from_env(), .recover = mh_v2_recover_probe, .recover_user = _env};
    return o;
}

/* The original kept database/art, menu-defaults.dat and custom/art next to the exe. Search order:
 * $MH_DATA_DIR, the folder holding the .app, Contents/Resources, then (created if needed)
 * ~/Library/Application Support/PicoStation Multi House. A folder wins when it has "database". */
static NSString *find_data_dir(void) {
    NSFileManager *fm = NSFileManager.defaultManager;
    const char *env = getenv("MH_DATA_DIR");
    if (env && *env) return @(env);
    NSString *cands[] = {NSBundle.mainBundle.bundlePath.stringByDeletingLastPathComponent,
                         NSBundle.mainBundle.resourcePath ?: @""};
    for (int i = 0; i < 2; i++)
        if ([fm fileExistsAtPath:[cands[i] stringByAppendingPathComponent:@"database/art/default"]]) return cands[i];
    NSString *support = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
                             .firstObject stringByAppendingPathComponent:@"PicoStation Multi House"];
    [fm createDirectoryAtPath:[support stringByAppendingPathComponent:@"database/art/default"]
        withIntermediateDirectories:YES attributes:nil error:nil];
    return support;
}

static NSString *find_embedded_dir(void) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"embedded"];
    if ([fm fileExistsAtPath:res]) return res;
    /* running the bare binary from the build folder: look in the checkout */
    NSString *exe = NSBundle.mainBundle.executablePath;
    for (NSString *dir = exe.stringByDeletingLastPathComponent; dir.length > 1;
         dir = dir.stringByDeletingLastPathComponent) {
        NSString *c = [dir stringByAppendingPathComponent:@"resources/embedded"];
        if ([fm fileExistsAtPath:c]) return c;
    }
    return res;
}

/* orig: app_main, after the window classes: project_init, project_use_default_menu, db_load,
 * cheat_db_load, art_init. */
- (void)startup {
    _dataDir = find_data_dir();
    _embeddedDir = find_embedded_dir();
    mh_project_init(_project);
    char path[MH_PATH_MAX], rec[MH_MENU_DEFAULTS_SIZE];
    mh_path_join(_dataDir.fileSystemRepresentation, "menu-defaults.dat", path, sizeof path);
    if (mh_menu_defaults_load(path, (uint8_t *)rec)) mh_project_set_menu_defaults(_project, (uint8_t *)rec);

    _dbOK = mh_db_load_dir(_db, _embeddedDir.fileSystemRepresentation, _dataDir.fileSystemRepresentation);

    mh_path_join(_embeddedDir.fileSystemRepresentation, "cdx_cheats.bin", path, sizeof path);
    uint8_t *blob = NULL;
    uint64_t len = 0;
    _cheatOK = NO;
    if (mh_read_entire_file(path, &blob, &len)) {
        _cheatOK = mh_cheat_db_load(_cheat, blob, (size_t)len) != 0;
        mh_free(blob);
    }

    mh_path_join(_embeddedDir.fileSystemRepresentation, "picostation_menu.bin", path, sizeof path);
    uint64_t menuSize = 0;
    BOOL menuOK = mh_menu_embedded_load(path, &_menu, &menuSize);
    mh_path_join(_embeddedDir.fileSystemRepresentation, "umbrella_music.vag", path, sizeof path);
    uint64_t musicSize = 0;
    BOOL musicOK = mh_read_entire_file(path, &_music, &musicSize);
    if (!menuOK || !musicOK) _dbOK = NO; /* embedded data incomplete: same box as a failed database */
    _env->menu = _menu;
    _env->menu_size = menuSize;
    _env->db = _db;
    _env->cheats = _cheatOK ? _cheat : NULL;
    _writer->env = _env;
    _writer->music = _music;
    _writer->music_size = musicSize;

    if (mh_art_init(_art, mh_image_load_imageio)) {
        mh_path_join(_embeddedDir.fileSystemRepresentation, "case_template.bgra", path, sizeof path);
        mh_art_load_case_template(_art, path);
    }
}

- (MhCase *)caseAt:(int)slot {
    if (slot < 0 || slot >= MH_PROJECT_MAX_CASES) return NULL;
    MhCase *c = &_project->cases[slot];
    return c->in_use ? c : NULL;
}

- (MhCase *)currentCase { return [self caseAt:_curCase]; }

- (MhDisc *)currentDisc {
    MhCase *c = self.currentCase;
    if (!c || _curDisc < 0 || _curDisc >= c->disc_count) return NULL;
    return c->discs[_curDisc];
}
@end
