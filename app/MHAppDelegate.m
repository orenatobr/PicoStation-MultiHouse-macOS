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
    NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua]; /* the original is light themed */
    [self buildMenus];
    [MHApp.shared startup];
    _main = [[MHMainController alloc] init];
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
                if (b.count == 2 && [b[0] isEqualToString:@"Sim"]) return yes ? 0 : 1;
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

- (void)buildMenus {
    NSMenu *bar = [[NSMenu alloc] init];
    NSString *name = MH_TITLE;
    NSMenuItem *appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *app = [[NSMenu alloc] initWithTitle:name];
    [app addItemWithTitle:@"Sobre" action:@selector(showAbout:) keyEquivalent:@""].target = self;
    [app addItem:NSMenuItem.separatorItem];
    [app addItemWithTitle:@"Esconder" action:@selector(hide:) keyEquivalent:@"h"];
    [app addItem:NSMenuItem.separatorItem];
    [app addItemWithTitle:@"Sair" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;

    NSMenuItem *editItem = [bar addItemWithTitle:@"Editar" action:nil keyEquivalent:@""];
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Editar"];
    [edit addItemWithTitle:@"Recortar" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copiar" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Colar" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Selecionar tudo" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem.submenu = edit;

    NSMenuItem *winItem = [bar addItemWithTitle:@"Janela" action:nil keyEquivalent:@""];
    NSMenu *win = [[NSMenu alloc] initWithTitle:@"Janela"];
    [win addItemWithTitle:@"Minimizar" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [win addItemWithTitle:@"Ampliar" action:@selector(performZoom:) keyEquivalent:@""];
    winItem.submenu = win;
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = win;
}

- (void)showAbout:(id)s { [_main showAbout]; }

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)a {
    return (!_main || [_main shouldQuit]) ? NSTerminateNow : NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }

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
