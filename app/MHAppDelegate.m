#import "MHAppDelegate.h"
#import "MHApp.h"
#import "MHMainController.h"
#include "mh/disc.h"

/* Launch arguments for development and screenshots (not part of the original):
 *   --add <folder|cue>     import like a Finder drop on the case list (repeatable)
 *   --cmd "<command>"      test command (see MHMainController testCommand:), repeatable, in order;
 *                          "wait-idle" waits for the background work, "sleep <s>" pauses
 *   --auto-answer yes|no   answer every confirmation box without showing it
 *   --quit                 quit when all commands ran */
@implementation MHAppDelegate {
    MHMainController *_main;
    NSMutableArray<NSString *> *_cmds;
    NSMutableArray<NSString *> *_adds;
    BOOL _quitAfter;
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    [MHApp.shared startup];
    _main = [[MHMainController alloc] init];
    [self buildMenus];
    [_main showWindow];
    [_main start];
    [NSApp activateIgnoringOtherApps:YES];

    _cmds = [NSMutableArray array];
    _adds = [NSMutableArray array];
    NSArray *args = NSProcessInfo.processInfo.arguments;
    for (NSUInteger i = 1; i < args.count; i++) {
        NSString *a = args[i];
        if ([a isEqualToString:@"--add"] && i + 1 < args.count) [_adds addObject:args[++i]];
        else if ([a isEqualToString:@"--cmd"] && i + 1 < args.count) [_cmds addObject:args[++i]];
        else if ([a isEqualToString:@"--quit"]) _quitAfter = YES;
        else if ([a isEqualToString:@"--auto-answer"] && i + 1 < args.count) {
            BOOL yes = [args[++i] isEqualToString:@"yes"];
            MHAlertHook = ^NSInteger(NSString *title, NSString *text, NSArray<NSString *> *b) {
                fprintf(stderr, "[alert] %s: %s\n", title.UTF8String, text.UTF8String);
                if (b.count == 2) return yes ? 0 : 1; /* confirmations: [confirm, cancel] */
                return 0;
            };
        }
    }
    if (_adds.count) {
        [_main importPaths:_adds];
        [_cmds insertObject:@"wait-idle" atIndex:0];
    }
    if (_cmds.count || _quitAfter) [self performSelector:@selector(runNextCommand) withObject:nil afterDelay:1.0];
}

- (void)runNextCommand {
    if (!_cmds.count) {
        if (_quitAfter) { _main = nil; exit(0); }
        return;
    }
    NSString *c = _cmds.firstObject;
    if ([c isEqualToString:@"wait-idle"]) {
        if (MHApp.shared.busy) { [self performSelector:@selector(runNextCommand) withObject:nil afterDelay:0.3]; return; }
    } else if ([c hasPrefix:@"sleep "]) {
        double s = [[c substringFromIndex:6] doubleValue];
        [_cmds removeObjectAtIndex:0];
        [self performSelector:@selector(runNextCommand) withObject:nil afterDelay:s];
        return;
    } else {
        [_main testCommand:c];
    }
    [_cmds removeObjectAtIndex:0];
    [self performSelector:@selector(runNextCommand) withObject:nil afterDelay:0.5];
}

#pragma mark - menu bar

static NSMenuItem *item(NSMenu *m, NSString *title, SEL a, NSString *key, NSEventModifierFlags mods, id target) {
    NSMenuItem *it = [m addItemWithTitle:title action:a keyEquivalent:key];
    it.keyEquivalentModifierMask = mods;
    it.target = target;
    return it;
}

static NSMenuItem *top(NSMenu *bar, NSString *title, NSMenu **out) {
    NSMenuItem *it = [bar addItemWithTitle:title action:nil keyEquivalent:@""];
    NSMenu *m = [[NSMenu alloc] initWithTitle:title];
    it.submenu = m;
    *out = m;
    return it;
}

static NSString *fkey(unichar c) { return [NSString stringWithCharacters:&c length:1]; }

- (void)buildMenus {
    const NSEventModifierFlags cmd = NSEventModifierFlagCommand, shift = NSEventModifierFlagShift, opt = NSEventModifierFlagOption;
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenu *m;

    /* App */
    top(bar, @"", &m);
    item(m, @"About PicoStation Multi House V3", @selector(showAbout:), @"", 0, self);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Hide PicoStation Multi House V3", @selector(hide:), @"h", cmd, nil);
    item(m, @"Hide Others", @selector(hideOtherApplications:), @"h", cmd | opt, nil);
    item(m, @"Show All", @selector(unhideAllApplications:), @"", 0, nil);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Quit PicoStation Multi House V3", @selector(terminate:), @"q", cmd, nil);

    /* File */
    top(bar, @"File", &m);
    item(m, @"Add Game…", @selector(addGame:), @"n", cmd, _main);
    item(m, @"Add Disc…", @selector(addDisc:), @"n", cmd | shift, _main);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Open Image…", @selector(imageOpen:), @"o", cmd, _main);
    item(m, @"Save Image As…", @selector(imageSaveAs:), @"s", cmd | shift, _main);
    item(m, @"Apply to Open Image", @selector(imageApply:), @"", 0, _main);
    item(m, @"New Project", @selector(imageNew:), @"", 0, _main);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Close", @selector(performClose:), @"w", cmd, nil);

    /* Edit */
    top(bar, @"Edit", &m);
    item(m, @"Cut", @selector(cut:), @"x", cmd, nil);
    item(m, @"Copy", @selector(copy:), @"c", cmd, nil);
    item(m, @"Paste", @selector(paste:), @"v", cmd, nil);
    item(m, @"Select All", @selector(selectAll:), @"a", cmd, nil);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Rename", @selector(renameItem:), fkey(NSF2FunctionKey), 0, _main);
    item(m, @"Remove…", @selector(removeItem:), fkey(NSBackspaceCharacter), cmd, _main);

    /* Library */
    top(bar, @"Library", &m);
    item(m, @"Move Up", @selector(moveItemUp:), fkey(NSUpArrowFunctionKey), cmd | opt, _main);
    item(m, @"Move Down", @selector(moveItemDown:), fkey(NSDownArrowFunctionKey), cmd | opt, _main);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Replace Case Art…", @selector(caseArt:), @"", 0, _main);
    item(m, @"Replace Disc Art…", @selector(discArt:), @"", 0, _main);
    item(m, @"Cheats…", @selector(cmdCheatsAction:), @"k", cmd | shift, _main);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Verify Disc", @selector(verifyDiscAction:), @"", 0, _main);
    item(m, @"Recover Serial…", @selector(recoverSerialAction:), @"", 0, _main);
    item(m, @"Create EDC/ECC Copy…", @selector(recodeAction:), @"", 0, _main);
    item(m, @"Technical Details…", @selector(detailsAction:), @"i", cmd | shift, _main);
    item(m, @"Verify on Add", @selector(toggleVerify:), @"", 0, _main);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Menu Settings…", @selector(cmdDefaults:), @"m", cmd | shift, _main);
    item(m, @"Refresh Cards", @selector(refreshSDAction:), @"r", cmd, _main);
    item(m, @"Create V2 Card", @selector(cmdWrite:), @"\r", cmd, _main);

    /* View */
    top(bar, @"View", &m);
    item(m, @"Hide Sidebar", @selector(toggleSidebarAction:), @"s", cmd | opt, _main);
    item(m, @"Hide Inspector", @selector(toggleInspectorAction:), @"i", cmd | opt, _main);
    [m addItem:NSMenuItem.separatorItem];
    item(m, @"Enter Full Screen", @selector(toggleFullScreen:), @"f", cmd | NSEventModifierFlagControl, nil);

    /* Window */
    NSMenu *win;
    top(bar, @"Window", &win);
    item(win, @"Minimize", @selector(performMiniaturize:), @"m", cmd, nil);
    item(win, @"Zoom", @selector(performZoom:), @"", 0, nil);
    [win addItem:NSMenuItem.separatorItem];
    item(win, @"Bring All to Front", @selector(arrangeInFront:), @"", 0, nil);

    /* Help */
    NSMenu *help;
    top(bar, @"Help", &help);
    item(help, @"PicoStation Multi House V3 on GitHub", @selector(openRepo:), @"", 0, self);

    NSApp.mainMenu = bar;
    NSApp.windowsMenu = win;
    NSApp.helpMenu = help;
}

- (void)showAbout:(id)s { [_main showAbout]; }
- (void)openRepo:(id)s {
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/orenatobr/PicoStation-MultiHouse-macOS"]];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)a {
    return (!_main || [_main shouldQuit]) ? NSTerminateNow : NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)a { return YES; }

- (void)applicationWillTerminate:(NSNotification *)n { /* orig: WM_DESTROY */
    MHApp *app = MHApp.shared;
    mh_project_clear(app.project);
    mh_db_free(app.db);
    mh_cheat_db_free(app.cheatDb);
    mh_art_shutdown(app.art);
}

/* Finder "Open With" and drops on the Dock icon behave like a drop on the case list. */
- (void)application:(NSApplication *)a openURLs:(NSArray<NSURL *> *)urls {
    NSMutableArray *p = [NSMutableArray array];
    for (NSURL *u in urls) if (u.isFileURL) [p addObject:u.path];
    [_main importPaths:p];
}
@end
