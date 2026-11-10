#import <Cocoa/Cocoa.h>
#import "MHAppDelegate.h"
#import "MHCrashGuard.h"

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        MHCrashGuardInstall();
        NSApplication *app = MHCrashApplication.sharedApplication;
        MHAppDelegate *delegate = [[MHAppDelegate alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        /* Offer the previous crash log once the first window is up. */
        [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidFinishLaunchingNotification object:nil queue:nil
                                                    usingBlock:^(NSNotification *n) {
            dispatch_async(dispatch_get_main_queue(), ^{ MHCrashGuardOfferLog(); });
        }];
        [app run];
    }
    return 0;
}
