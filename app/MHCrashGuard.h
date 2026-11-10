#import <Foundation/Foundation.h>

/* Last-resort crash logging. MHCrashGuardInstall() is called first thing in main(): it installs an
 * uncaught-exception handler and fatal-signal handlers that write the exception, the stack and the
 * recent breadcrumbs to ~/Library/Logs/PicoStation Multi House/crash-<date>.log, then lets the
 * process die as before (so the system crash report is still produced).
 * MHCrashGuardOfferLog() runs after launch: when a log newer than the last acknowledged one exists
 * it offers "Reveal Log" once (stderr only in test mode). */
void MHCrashGuardInstall(void);

/* Use [MHCrashApplication sharedApplication] in main() instead of NSApplication. */
#import <Cocoa/Cocoa.h>
@interface MHCrashApplication : NSApplication
@end
void MHCrashGuardOfferLog(void);

/* Async-signal-safe: remembers the last 16 short notes (command, file, action) for the crash log. */
void MHBreadcrumb(const char *note);
