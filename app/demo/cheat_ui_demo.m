/* cheat_ui_demo.m: standalone harness for the cheat windows (make cheat-demo). Not part of the
 * product app. Usage: cheat-demo [shot-dir] ; with a shot-dir it drives the windows through a
 * scripted session, writes PNGs and exits. Without it, just opens the window interactively. */
#import <Cocoa/Cocoa.h>
#import "../cheat_ui.h"
#include "mh/cheat.h"

static NSString *gDir;

static void Shot(NSString *kind, NSString *name) {
    id<MHCheatUITestable> t = MHCheatUITestTarget(kind);
    NSWindow *w = t.testWindow;
    if (!w || !gDir) { NSLog(@"shot %@: no window", name); return; }
    NSView *fv = w.contentView.superview;
    NSBitmapImageRep *rep = [fv bitmapImageRepForCachingDisplayInRect:fv.bounds];
    [fv cacheDisplayInRect:fv.bounds toBitmapImageRep:rep];
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    [png writeToFile:[gDir stringByAppendingPathComponent:[name stringByAppendingString:@".png"]] atomically:YES];
    /* real screen capture of the window too (needs screen recording permission) */
    NSTask *tk = [[NSTask alloc] init];
    tk.launchPath = @"/usr/sbin/screencapture";
    tk.arguments = @[ @"-x", @"-o", [NSString stringWithFormat:@"-l%ld", (long)w.windowNumber],
                      [gDir stringByAppendingPathComponent:[name stringByAppendingString:@"_screen.png"]] ];
    [tk launch];
    [tk waitUntilExit];
}
static void At(NSTimeInterval t, void (^b)(void)) {
    NSTimer *tm = [NSTimer timerWithTimeInterval:t repeats:NO block:^(NSTimer *x) { b(); }];
    [[NSRunLoop mainRunLoop] addTimer:tm forMode:NSRunLoopCommonModes];
}
static void Act(NSString *kind, NSString *a, id arg) { [MHCheatUITestTarget(kind) testAction:a argument:arg]; }

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        gDir = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : nil;
        if (gDir) [[NSFileManager defaultManager] createDirectoryAtPath:gDir withIntermediateDirectories:YES attributes:nil error:nil];
        const char *serial = argc > 2 ? argv[2] : "SCUS-94221";
        NSString *blobPath = @"resources/embedded/cdx_cheats.bin";
        NSData *blob = [NSData dataWithContentsOfFile:blobPath];
        static MhCheatDb db;
        if (!blob || !mh_cheat_db_load(&db, blob.bytes, blob.length)) { NSLog(@"db load failed (%@)", blobPath); return 1; }
        MhCheatProfile *prof = NULL;
        char err[256] = "";
        if (!mh_cheat_profile_build_for_disc(&db, serial, &prof, err, sizeof err)) {
            /* serial not bound: pick the title by name like the "Escolher jogo" combo would */
            const char *want = argc > 3 ? argv[3] : "Final Fantasy Tactics";
            for (uint32_t t = 0; t < db.n_titles; t++) {
                char nm[768];
                if (mh_cheat_db_title(&db, t, nm, sizeof nm) && strstr(nm, want)) {
                    NSLog(@"title %u: %s", t, nm);
                    if (mh_cheat_profile_build_exact_catalog(&db, t, serial, &prof, err, sizeof err)) break;
                    NSLog(@"exact catalog: %s", err);
                }
            }
        }
        NSLog(@"profile entries: %u", prof ? prof->count : 0);

        NSWindow *parent = [[NSWindow alloc] initWithContentRect:NSMakeRect(300, 300, 400, 200)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered defer:NO];
        parent.title = @"Parent";
        [parent makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        if (gDir) {
            MHCheatUIAlertHook = ^NSInteger(NSString *t, NSString *x, NSArray *b) {
                NSLog(@"ALERT [%@] %@ %@", t, x, b);
                return [t isEqualToString:@"Versao dos Cheats"] ? 1 : 0;
            };
            NSString *tmp = [gDir stringByAppendingPathComponent:@"export.cht"];
            MHCheatUIPanelHook = ^NSURL *(BOOL save) { return [NSURL fileURLWithPath:tmp]; };
            At(1.0, ^{ Shot(@"manager", @"01_manager"); Act(@"manager", @"select", @[ @0 ]); });
            At(1.6, ^{ Shot(@"manager", @"02_selected"); Act(@"manager", @"search", @"vida"); });
            At(2.2, ^{ Shot(@"manager", @"03_search"); Act(@"manager", @"search", @""); Act(@"manager", @"select", @[ @1, @2 ]); Act(@"manager", @"toggle", nil); });
            At(2.8, ^{ Shot(@"manager", @"04_toggled"); Act(@"manager", @"choose", nil); });
            At(3.6, ^{ Shot(@"catalog", @"05_catalog"); Act(@"catalog", @"select", @[ @2 ]); });
            At(4.2, ^{ Shot(@"catalog", @"06_catalog_sel"); Act(@"catalog", @"add", nil); });
            At(4.8, ^{ Shot(@"catalog", @"07_catalog_added"); Act(@"catalog", @"search", @"item"); });
            At(5.4, ^{ Shot(@"catalog", @"08_catalog_search"); Act(@"catalog", @"back", nil); });
            At(6.0, ^{ Shot(@"manager", @"09_manager_after"); Act(@"manager", @"new", nil); });
            At(6.8, ^{ Shot(@"editor", @"10_editor_new"); Act(@"editor", @"name", @"Vida Infinita"); Act(@"editor", @"codes", @"8007A2B0 0063\n3007A2B1 00FF\n"); Act(@"editor", @"version", @0); });
            At(7.4, ^{ Shot(@"editor", @"11_editor_filled"); Act(@"editor", @"save", nil); });
            At(8.0, ^{ Shot(@"manager", @"12_manager_new"); Act(@"manager", @"export", nil); Act(@"manager", @"edit", nil); });
            At(8.8, ^{ Shot(@"editor", @"13_editor_edit"); Act(@"editor", @"cancel", nil); });
            At(9.4, ^{ Act(@"manager", @"sort", nil); });
            At(9.8, ^{ Shot(@"manager", @"14_sorted"); Act(@"manager", @"import", nil); });
            At(10.4, ^{ Shot(@"manager", @"15_imported"); Act(@"manager", @"drag", @[ @0, @5 ]); Act(@"manager", @"select", @[ @0 ]); Act(@"manager", @"remove", nil); });
            At(11.0, ^{ Shot(@"manager", @"16_removed"); Act(@"manager", @"accept", nil); });
            At(14.0, ^{ NSLog(@"timeout"); exit(2); });
        }
        BOOL ok = MHCheatEditProfile(parent, &db, @"Final Fantasy Tactics  |  Serial: SCUS-94221\nVinculo por serial. Use o botao ao lado para escolher outro perfil.", serial, MH_CHEAT_UI_SCOPE_DISC, &prof);
        NSLog(@"result applied=%d entries=%u enabled=%d", ok, prof ? prof->count : 0, prof ? mh_cheat_profile_enabled_count(prof) : 0);
        return ok ? 0 : 1;
    }
}
