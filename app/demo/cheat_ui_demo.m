/* cheat_ui_demo.m: standalone harness for the cheat windows (make cheat-demo). Not part of the
 * product app. Usage: cheat-demo [shot-dir] [serial] [title] [light|dark] [full|empty]
 * With a shot-dir it drives the windows through a scripted session, writes PNGs, prints PASS/FAIL
 * checks of the resulting profile and exits. Without it, it opens the window interactively. */
#import <Cocoa/Cocoa.h>
#import "../cheat_ui.h"
#include "mh/cheat.h"

static NSString *gDir;
static int gFail;

static void Check(BOOL ok, NSString *what) {
    NSLog(@"%@ %@", ok ? @"PASS" : @"FAIL", what);
    if (!ok) gFail++;
}

/* Frame of the key cheat window plus whatever sheet is attached to it, drawn like on screen. */
static void Shot(NSString *name) {
    id<MHCheatUITestable> t = MHCheatUITestTarget(@"manager");
    NSWindow *w = t.testWindow;
    if (!w || !gDir) { NSLog(@"shot %@: no window", name); return; }
    NSRect fr = w.frame;
    CGFloat scale = 2; /* always 2x, whatever the screen (or no screen) says */
    NSBitmapImageRep *out = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)(fr.size.width * scale)
                                                                    pixelsHigh:(NSInteger)(fr.size.height * scale)
                                                                 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                                colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    out.size = fr.size;
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:out];
    NSWindow *layers[3] = {w, w.attachedSheet, w.attachedSheet.attachedSheet};
    for (int i = 0; i < 3; i++) {
        NSWindow *x = layers[i];
        if (!x) break;
        if (i > 0) {
            [[NSColor colorWithWhite:0 alpha:0.25] setFill];
            NSRectFillUsingOperation(NSMakeRect(0, 0, fr.size.width, fr.size.height), NSCompositingOperationSourceOver);
        }
        if (i > 0) {
            NSRect sr = NSMakeRect(x.frame.origin.x - fr.origin.x, x.frame.origin.y - fr.origin.y, x.frame.size.width, x.frame.size.height);
            [x.effectiveAppearance performAsCurrentDrawingAppearance:^{ [NSColor.windowBackgroundColor setFill]; NSRectFill(sr); }];
        }
        NSView *fv = x.contentView.superview;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)(fv.bounds.size.width * scale)
                                                                        pixelsHigh:(NSInteger)(fv.bounds.size.height * scale)
                                                                     bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                                    colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        rep.size = fv.bounds.size;
        [fv cacheDisplayInRect:fv.bounds toBitmapImageRep:rep];
        NSImage *img = [[NSImage alloc] initWithSize:fv.bounds.size];
        [img addRepresentation:rep];
        NSPoint o = NSMakePoint(x.frame.origin.x - fr.origin.x, x.frame.origin.y - fr.origin.y);
        [img drawAtPoint:o fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
    }
    [NSGraphicsContext restoreGraphicsState];
    NSData *png = [out representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    [png writeToFile:[gDir stringByAppendingPathComponent:[name stringByAppendingString:@".png"]] atomically:YES];
}

static void At(NSTimeInterval t, void (^b)(void)) {
    NSTimer *tm = [NSTimer timerWithTimeInterval:t repeats:NO block:^(NSTimer *x) { b(); }];
    [[NSRunLoop mainRunLoop] addTimer:tm forMode:NSRunLoopCommonModes];
}
static NSTimeInterval T;
static void Go(NSTimeInterval dt, void (^b)(void)) { T += dt; At(T, b); }
static void Act(NSString *kind, NSString *a, id arg) { [MHCheatUITestTarget(kind) testAction:a argument:arg]; }

static NSButton *FindButton(NSView *v, NSString *title) {
    if ([v isKindOfClass:NSButton.class] && [((NSButton *)v).title isEqualToString:title]) return (NSButton *)v;
    for (NSView *s in v.subviews) { NSButton *b = FindButton(s, title); if (b) return b; }
    return nil;
}
/* Press a button of the alert sheet that is on top of the manager window. */
static void ClickSheet(NSString *title) {
    NSWindow *w = MHCheatUITestTarget(@"manager").testWindow;
    while (w.attachedSheet) w = w.attachedSheet;
    NSButton *b = FindButton(w.contentView, title);
    NSLog(@"click [%@] %@", title, b ? @"ok" : @"NOT FOUND");
    [b performClick:nil];
}

static const MhCheatProfile *P(void) { return MHCheatUITestTarget(@"manager").testProfile; }
static NSString *EntryName(const MhCheatProfile *p, uint32_t i) { return [NSString stringWithUTF8String:p->entries[i].name]; }

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        gDir = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : nil;
        if (gDir) [[NSFileManager defaultManager] createDirectoryAtPath:gDir withIntermediateDirectories:YES attributes:nil error:nil];
        const char *serial = argc > 2 ? argv[2] : "SCUS-94221";
        NSString *mode = argc > 4 ? [NSString stringWithUTF8String:argv[4]] : @"light";
        BOOL empty = argc > 5 && strcmp(argv[5], "empty") == 0;
        NSApp.appearance = [NSAppearance appearanceNamed:[mode isEqualToString:@"dark"] ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
        NSString *blobPath = @"resources/embedded/cdx_cheats.bin";
        NSData *blob = [NSData dataWithContentsOfFile:blobPath];
        static MhCheatDb db;
        if (!blob || !mh_cheat_db_load(&db, blob.bytes, blob.length)) { NSLog(@"db load failed (%@)", blobPath); return 1; }
        MhCheatProfile *prof = NULL;
        char err[256] = "";
        if (!empty && !mh_cheat_profile_build_for_disc(&db, serial, &prof, err, sizeof err)) {
            /* serial not bound: pick the title by name like the library popup would */
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
        uint32_t n0 = prof ? prof->count : 0;
        NSLog(@"profile entries: %u", n0);
        for (uint32_t i = 0; prof && i < prof->count && i < 8; i++)
            NSLog(@"  [%u] %s flags=%02x lines=%u", i, prof->entries[i].name, prof->entries[i].flags, prof->entries[i].nlines);

        NSWindow *parent = [[NSWindow alloc] initWithContentRect:NSMakeRect(300, 300, 400, 200)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered defer:NO];
        parent.title = @"Parent";
        [parent makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        NSString *header = @"Final Fantasy Tactics  |  Serial: SCUS-94221\r\nLinked by serial. Use the button next to it to choose another profile.";
        if (empty) header = @"Unknown Disc  |  Serial: not set\nNo serial: you can edit a custom list, but the menu cannot bind it automatically.";

        if (gDir && !empty) {
            NSString *exportPath = [gDir stringByAppendingPathComponent:@"export.cht"];
            MHCheatUIPanelHook = ^NSURL *(BOOL save) { return [NSURL fileURLWithPath:exportPath]; };
            MHCheatUIAlertHook = ^NSInteger(NSString *t, NSString *x, NSArray *b) {
                NSLog(@"ALERT [%@] %@ %@", t, x, b);
                return 0;
            };
            NSString *longName = @"A VERY LONG CHEAT NAME THAT KEEPS GOING PAST THE END OF THE NAME COLUMN";
            __block uint32_t before = 0, exported = 0;
            __block NSString *moved = nil;
            Go(1.0, ^{ Shot(@"01_manager"); Act(@"manager", @"select", @[ @0 ]); });
            Go(0.6, ^{ Shot(@"02_selected"); Act(@"manager", @"toggle", nil); });
            Go(0.6, ^{ Shot(@"03_toggled"); Check(P()->entries[0].flags & MH_CHEAT_F_ENABLED, @"toggle enabled the first cheat"); Act(@"manager", @"toggle", nil); });
            Go(0.5, ^{ Check(!(P()->entries[0].flags & MH_CHEAT_F_ENABLED), @"toggle again disabled it"); Act(@"manager", @"toggle", nil); });
            Go(0.5, ^{ Act(@"manager", @"search", @"character 1"); });
            Go(0.6, ^{ Shot(@"04_search"); Act(@"manager", @"search", @"zzzzzz"); });
            Go(0.6, ^{ Shot(@"05_search_empty"); Act(@"manager", @"search", @""); before = P()->count; Act(@"manager", @"select", @[ @0 ]); Act(@"manager", @"remove", nil); Act(@"manager", @"choose", nil); });
            Go(0.9, ^{ Shot(@"06_library"); Act(@"catalog", @"select", @[ @0 ]); });
            Go(0.6, ^{ Shot(@"07_library_sel"); Act(@"catalog", @"add", nil); });
            Go(0.6, ^{ Shot(@"08_library_added"); Check(P()->count == before, @"library add put the removed group back"); Act(@"catalog", @"search", @"item"); });
            Go(0.6, ^{ Shot(@"09_library_search"); Act(@"catalog", @"back", nil); });
            Go(0.9, ^{ Shot(@"10_manager_after"); before = P()->count; Act(@"manager", @"new", nil); });
            Go(0.9, ^{ Shot(@"11_editor_new"); Act(@"editor", @"save", nil); });
            Go(0.5, ^{ Shot(@"12_editor_error_name"); Check(MHCheatUITestTarget(@"editor") != nil, @"editor stays open on an empty name"); Act(@"editor", @"name", longName); Act(@"editor", @"codes", @"8007A2B0 00ZZ\n"); });
            Go(0.5, ^{ Shot(@"13_editor_error_codes"); Act(@"editor", @"save", nil); });
            Go(0.5, ^{ Check(MHCheatUITestTarget(@"editor") != nil, @"editor stays open on invalid codes"); Act(@"editor", @"codes", @"8007A2B0 0063\n3007A2B1 00FF\n"); Act(@"editor", @"version", @0); });
            Go(0.5, ^{ Shot(@"14_editor_valid"); Act(@"editor", @"save", nil); });
            Go(0.9, ^{
                Shot(@"15_manager_new");
                Check(P()->count == before + 1, @"new cheat added");
                BOOL f = NO;
                for (uint32_t i = 0; i < P()->count; i++) if (strncmp(P()->entries[i].name, "A VERY LONG CHEAT NAME", 22) == 0 && P()->entries[i].nlines == 2 && (P()->entries[i].flags & MH_CHEAT_F_CUSTOM)) f = YES;
                Check(f, @"new cheat is custom with 2 lines");
                exported = P()->count;
            });
            /* real alert sheets from here on */
            Go(0.3, ^{ MHCheatUIAlertHook = nil; Act(@"manager", @"export", nil); });
            Go(0.8, ^{ Shot(@"16_alert_exported"); ClickSheet(@"OK"); });
            Go(0.8, ^{ Check([[NSFileManager defaultManager] fileExistsAtPath:[gDir stringByAppendingPathComponent:@"export.cht"]], @"export wrote the file"); Act(@"manager", @"select", @[ @0 ]); Act(@"manager", @"edit", nil); });
            Go(0.9, ^{ Shot(@"17_editor_edit"); Act(@"editor", @"cancel", nil); });
            Go(0.9, ^{ Act(@"manager", @"sort", nil); });
            Go(0.5, ^{
                Shot(@"18_sorted");
                BOOL sorted = YES;
                for (uint32_t i = 1; i < P()->count; i++) if (mh_str_icmp(P()->entries[i - 1].name, P()->entries[i].name) > 0) sorted = NO;
                Check(sorted, @"sort by name");
                before = P()->count;
                Act(@"manager", @"import", nil);
            });
            Go(0.8, ^{ Shot(@"19_alert_dialect"); ClickSheet(@"No"); });
            Go(0.8, ^{
                Shot(@"20_imported");
                Check(P()->count == before + exported, [NSString stringWithFormat:@"import added %u cheats (%u to %u)", exported, before, P()->count]);
                moved = EntryName(P(), 1);
                Act(@"manager", @"select", @[ @1 ]); Act(@"manager", @"down", nil);
            });
            Go(0.5, ^{
                Shot(@"21_moved_down");
                Check([EntryName(P(), 2) isEqualToString:moved], @"Move Down swapped with the next row");
                Act(@"manager", @"up", nil); Act(@"manager", @"up", nil);
            });
            Go(0.5, ^{
                Check([EntryName(P(), 0) isEqualToString:moved], @"Move Up twice reached the top");
                Act(@"manager", @"drag", @[ @0, @5 ]);
            });
            Go(0.5, ^{
                Shot(@"22_dragged");
                Check([EntryName(P(), 5) isEqualToString:moved], @"drag moved the entry to row 5");
                before = P()->count;
                Act(@"manager", @"select", @[ @0 ]); Act(@"manager", @"remove", nil);
            });
            Go(0.8, ^{ Shot(@"23_removed"); Check(P()->count == before - 1, @"delete removed one cheat"); });
            /* fill the library until the budget refuses, to see the error state */
            Go(0.3, ^{ MHCheatUIAlertHook = ^NSInteger(NSString *t, NSString *x, NSArray *b) { NSLog(@"ALERT [%@] %@", t, x); return 0; }; Act(@"manager", @"choose", nil); });
            Go(0.9, ^{
                for (int i = 0; i < 400; i++) { Act(@"catalog", @"select", @[ @(i % 60) ]); Act(@"catalog", @"add", nil); }
                Shot(@"24_library_full");
                Act(@"catalog", @"back", nil);
            });
            Go(0.9, ^{ Shot(@"25_manager_full"); Act(@"manager", @"search", @""); Act(@"manager", @"accept", nil); });
            Go(4.0, ^{ NSLog(@"timeout"); exit(2); });
        } else if (gDir && empty) {
            Go(1.0, ^{ Shot(@"e1_empty"); Act(@"manager", @"search", @"xyz"); });
            Go(0.6, ^{ Shot(@"e2_empty_search"); Act(@"manager", @"search", @""); Act(@"manager", @"new", nil); });
            Go(0.9, ^{ Act(@"editor", @"name", @"Infinite HP"); Act(@"editor", @"codes", @"8007A2B0 0063\n"); Act(@"editor", @"version", @0); Act(@"editor", @"save", nil); });
            Go(0.9, ^{ Shot(@"e3_one"); Act(@"manager", @"accept", nil); });
            Go(3.0, ^{ NSLog(@"timeout"); exit(2); });
        }
        uint32_t oldCount = n0;
        BOOL ok = MHCheatEditProfile(parent, &db, header, serial, MH_CHEAT_UI_SCOPE_DISC, &prof);
        NSLog(@"result applied=%d entries=%u enabled=%d", ok, prof ? prof->count : 0, prof ? mh_cheat_profile_enabled_count(prof) : 0);
        if (gDir) {
            Check(ok, @"apply returned YES");
            char verr[256] = "";
            Check(mh_cheat_profile_validate_hardware(prof, verr, sizeof verr), [NSString stringWithFormat:@"profile validates (%s)", verr]);
            Check(mh_cheat_profile_serialized_size(prof) <= MH_CHEAT_BUDGET_DISC, @"profile fits in the disc budget");
            if (empty) {
                Check(prof && prof->count == 1 && strcmp(prof->entries[0].name, "INFINITE HP") == 0, @"empty run: one custom cheat INFINITE HP");
                Check(prof && prof->entries[0].nlines == 1, @"custom cheat has 1 line");
            } else {
                BOOL found = NO, longFound = NO;
                for (uint32_t i = 0; prof && i < prof->count; i++) {
                    if (strncmp(prof->entries[i].name, "A VERY LONG CHEAT NAME", 22) == 0) { longFound = YES; found = prof->entries[i].nlines == 2; }
                }
                Check(longFound && found, @"new custom cheat saved with 2 lines and long name");
                Check(prof && prof->count > oldCount, [NSString stringWithFormat:@"count grew from %u to %u", oldCount, prof ? prof->count : 0]);
                (void)EntryName;
            }
            NSLog(@"%d failure(s)", gFail);
            return gFail ? 1 : 0;
        }
        return ok ? 0 : 1;
    }
}
