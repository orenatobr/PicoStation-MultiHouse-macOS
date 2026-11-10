/* MHMainController.h: the main window (orig: main_proc and everything it calls). */
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
/* WM_DROPFILES onto the lists, or Finder "Open With"/Dock drops (case list rules). */
- (void)importPaths:(NSArray<NSString *> *)paths;
/* Menu bar actions. */
- (void)showAbout;

/* Test and demo hooks (launch arguments of AppDelegate). */
- (void)testCommand:(NSString *)cmd;
@end

#endif
