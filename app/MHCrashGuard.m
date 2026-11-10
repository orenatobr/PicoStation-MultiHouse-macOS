#import "MHCrashGuard.h"
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define CRUMBS 16
#define CRUMB_LEN 160

static char g_dir[1024], g_path[1100];       /* log dir and this run's log file, built at install time */
static char g_head[512];                     /* "version / macOS / pid" line, built at install time */
static char g_crumb[CRUMBS][CRUMB_LEN];
static volatile sig_atomic_t g_crumbN;
static volatile sig_atomic_t g_inCrash;

void MHBreadcrumb(const char *note) {
    if (!note) return;
    int i = (g_crumbN++) % CRUMBS; /* ponytail: no lock, two racing writers may mix one note */
    strlcpy(g_crumb[i], note, CRUMB_LEN);
}

static void put(int fd, const char *s) { if (s) (void)!write(fd, s, strlen(s)); }

static void dump_crumbs(int fd) {
    put(fd, "\nBreadcrumbs (oldest first):\n");
    int n = g_crumbN;
    for (int k = n > CRUMBS ? n - CRUMBS : 0; k < n; k++) { put(fd, "  "); put(fd, g_crumb[k % CRUMBS]); put(fd, "\n"); }
}

static int open_log(void) { return open(g_path, O_WRONLY | O_CREAT | O_APPEND, 0644); }

static void signal_handler(int sig) {
    if (g_inCrash) _exit(128 + sig);
    g_inCrash = 1;
    int fd = open_log();
    if (fd >= 0) {
        char d[] = { (char)('0' + sig / 10), (char)('0' + sig % 10), '\n', 0 };
        put(fd, "FATAL SIGNAL ");
        put(fd, d);
        put(fd, g_head);
        void *bt[64];
        int n = backtrace(bt, 64);
        put(fd, "\nStack:\n");
        backtrace_symbols_fd(bt, n, fd);
        dump_crumbs(fd);
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig); /* the default action still produces the system crash report */
}

static void exception_handler(NSException *e) {
    g_inCrash = 1;
    int fd = open_log();
    if (fd < 0) return;
    put(fd, "UNCAUGHT EXCEPTION\n");
    put(fd, g_head);
    NSMutableString *s = [NSMutableString stringWithFormat:@"\nName: %@\nReason: %@\nUserInfo: %@\n\nStack:\n", e.name, e.reason, e.userInfo];
    for (NSString *f in e.callStackSymbols) [s appendFormat:@"%@\n", f];
    put(fd, s.UTF8String);
    dump_crumbs(fd);
    close(fd);
    /* returning lets the runtime abort as before */
}

/* AppKit catches exceptions raised inside event handling and layout, reports them to -reportException:
 * and then traps (SIGTRAP) without calling the uncaught-exception handler. This hook sees the
 * exception before the trap, so the log gets the name and reason, not just the signal. */
@implementation MHCrashApplication
- (void)reportException:(NSException *)e {
    exception_handler(e);
    [super reportException:e];
    abort(); /* same outcome as AppKit's own crash-on-exception, now with the reason in the log */
}
@end

void MHCrashGuardInstall(void) {
    @autoreleasepool {
        NSString *lib = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES).firstObject;
        NSString *dir = [lib stringByAppendingPathComponent:@"Logs/PicoStation Multi House"];
        [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        NSDateFormatter *f = [[NSDateFormatter alloc] init];
        f.dateFormat = @"yyyy-MM-dd-HHmmss";
        f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"crash-%@-%d.log", [f stringFromDate:NSDate.date], getpid()]];
        strlcpy(g_dir, dir.fileSystemRepresentation, sizeof g_dir);
        strlcpy(g_path, path.fileSystemRepresentation, sizeof g_path);
        NSString *ver = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"?";
        snprintf(g_head, sizeof g_head, "Version %s, macOS %s, pid %d\n", ver.UTF8String,
                 NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String, getpid());
        void *warm[4];
        backtrace(warm, 4); /* loads the unwinder now, not inside a crashing process */
        NSSetUncaughtExceptionHandler(exception_handler);
        /* Without this AppKit traps on the exception before any hook sees it (macOS 26+). */
        [NSUserDefaults.standardUserDefaults registerDefaults:@{ @"NSApplicationCrashOnExceptions" : @NO }];
        /* Layout and drawing exceptions still go straight to AppKit's private +_crashOnException:, so log
         * there too. Best effort: skipped silently when the selector is not there. */
        Method m = class_getClassMethod(NSApplication.class, NSSelectorFromString(@"_crashOnException:"));
        if (m) {
            IMP orig = method_getImplementation(m);
            method_setImplementation(m, imp_implementationWithBlock(^(id self_, NSException *e) {
                exception_handler(e);
                ((void (*)(id, SEL, NSException *))orig)(self_, NSSelectorFromString(@"_crashOnException:"), e);
            }));
        }
        static char altstack[64 * 1024];
        stack_t ss = { .ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0 };
        sigaltstack(&ss, NULL);
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = signal_handler;
        sa.sa_flags = SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP };
        for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
    }
}

void MHCrashGuardOfferLog(void) {
    if (!g_dir[0]) return;
    NSString *dir = @(g_dir), *mine = @(g_path).lastPathComponent;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *seen = [dir stringByAppendingPathComponent:@".last-seen"];
    NSDate *newestAt = [fm attributesOfItemAtPath:seen error:nil].fileModificationDate ?: NSDate.distantPast;
    NSString *newest = nil;
    for (NSString *n in [fm contentsOfDirectoryAtPath:dir error:nil]) {
        if (![n hasPrefix:@"crash-"] || ![n hasSuffix:@".log"] || [n isEqualToString:mine]) continue;
        NSDate *d = [fm attributesOfItemAtPath:[dir stringByAppendingPathComponent:n] error:nil].fileModificationDate;
        if (d && [d compare:newestAt] == NSOrderedDescending) { newest = n; newestAt = d; }
    }
    if (!newest) return;
    NSString *log = [dir stringByAppendingPathComponent:newest];
    [fm createFileAtPath:seen contents:nil attributes:nil]; /* ask once per crash */
    [fm setAttributes:@{ NSFileModificationDate : NSDate.date } ofItemAtPath:seen error:nil];
    if (getenv("MH_NO_ZOOM")) { fprintf(stderr, "[crash-guard] previous crash log: %s\n", log.UTF8String); return; }
    NSAlert *a = [[NSAlert alloc] init];
    a.alertStyle = NSAlertStyleWarning;
    a.messageText = @"PicoStation Multi House closed unexpectedly last time";
    a.informativeText = @"A crash log was saved. You can reveal it in Finder and attach it to a bug report.";
    [a addButtonWithTitle:@"Reveal Log"];
    [a addButtonWithTitle:@"Dismiss"];
    if ([a runModal] == NSAlertFirstButtonReturn)
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:log] ]];
}
