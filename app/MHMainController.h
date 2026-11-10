/* MHMainController.h: the main window (orig: main_proc and everything it calls).
 * It builds the split view window (sidebar, content, inspector, bottom bar), owns the worker
 * jobs and exposes the commands the menu bar and toolbar target. */
#ifndef MH_APP_MAIN_CONTROLLER_H
#define MH_APP_MAIN_CONTROLLER_H

#import <Cocoa/Cocoa.h>

@interface MHMainController : NSObject
@property(readonly) NSWindow *window;

- (void)showWindow;
/* End of app_main: startup alerts, first refresh, device scan. */
- (void)start;
- (void)writeShot:(NSString *)path;
/* WM_CLOSE: YES when the application may quit now. */
- (BOOL)shouldQuit;
/* Finder drops and "Open With": folders and .cue files, case list rules. */
- (void)importPaths:(NSArray<NSString *> *)paths;
/* Applies the stored sidebar/inspector visibility and the main window frame. */
- (void)restoreState;

/* ---- commands (menu bar, toolbar, context menus, empty state) ---- */
- (void)showAbout;
- (void)addGame:(id)sender;
- (void)addDisc:(id)sender;
- (void)imageOpen:(id)sender;
- (void)imageSaveAs:(id)sender;
- (void)imageApply:(id)sender;
- (void)imageNew:(id)sender;
- (void)cmdDefaults:(id)sender;            /* Menu Settings... */
- (void)refreshSDAction:(id)sender;        /* Refresh Cards */
- (void)cmdWrite:(id)sender;               /* Create V2 Card / Apply Changes / Cancel */
- (void)caseArt:(id)sender;
- (void)discArt:(id)sender;
- (void)cmdCheatsAction:(id)sender;
- (void)moveItemUp:(id)sender;
- (void)moveItemDown:(id)sender;
- (void)removeItem:(id)sender;
- (void)renameItem:(id)sender;
- (void)verifyDiscAction:(id)sender;
- (void)recoverSerialAction:(id)sender;
- (void)recodeAction:(id)sender;
- (void)detailsAction:(id)sender;
- (void)toggleVerify:(id)sender;
- (void)toggleSidebarAction:(id)sender;
- (void)toggleInspectorAction:(id)sender;

/* Test and demo hooks (launch arguments of AppDelegate). */
- (void)testCommand:(NSString *)cmd;
@end

#endif
