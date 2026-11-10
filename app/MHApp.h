/* MHApp.h: application state (orig: the big `g` block and its DAT_ globals) and the message
 * boxes. Everything here is main-thread only unless stated. */
#ifndef MH_APP_STATE_H
#define MH_APP_STATE_H

#import <Cocoa/Cocoa.h>
#include "mh/disc.h"
#include "mh/art.h"
#include "mh/blockdev.h"
#include "mh/menu.h"
#include "mh/cheat.h"

#define MH_TITLE @"PicoStation Multi House V3"

typedef NS_ENUM(NSInteger, MHIcon) { MHIconError, MHIconInfo, MHIconWarning };

@interface MHApp : NSObject
+ (MHApp *)shared;

@property(readonly) MhProject *project; /* orig: DAT_140e7d490 */
@property(readonly) MhDb *db;           /* orig: DAT_140e7c3b8 */
@property(readonly) MhCheatDb *cheatDb; /* orig: DAT_140e7cc40 */
@property(readonly) MhArt *art;
@property(readonly) BOOL dbOK;          /* all four embedded blobs validated */
@property(readonly) BOOL cheatOK;       /* orig: DAT_140e7cc74 */
@property(readonly, copy) NSString *dataDir;     /* orig: DAT_140e7b3b8, folder of the exe */
@property(readonly, copy) NSString *embeddedDir; /* resources/embedded */

/* selection (orig: DAT_140f04e38 / DAT_140f04e3c), -1 = none */
@property int curCase;
@property int curDisc;

/* flags (orig: DAT_140e7b3a0 busy, 3a8 loading card, 3ac verify on add, 3b0 cancel requested,
 * 3b4 cannot cancel any more) */
@property BOOL busy;
@property BOOL loading;
@property BOOL verifyOnAdd;
@property BOOL cancelRequested;
@property BOOL cannotCancel;

- (void)startup;
- (MhCase *)caseAt:(int)slot;   /* in-use case or NULL */
- (MhCase *)currentCase;
- (MhDisc *)currentDisc;
@end

/* ---- message boxes (orig: MessageBoxW). Safe from any thread: hop to the main thread. ---- */
void MHMessage(NSString *title, NSString *text, MHIcon icon);
/* orig: MessageBoxW(..., L"Confirmacao", MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2) == IDYES */
BOOL MHAsk(NSString *text);
/* Test hook: when set, every message box goes through it instead of NSAlert. buttons are the
 * captions; return the 0 based index of the pressed one (for ask boxes 0 = Sim, 1 = Nao). */
extern NSInteger (^MHAlertHook)(NSString *title, NSString *text, NSArray<NSString *> *buttons);
/* Runs a block on the main thread and waits (any thread). */
void MHMain(dispatch_block_t block);
/* UTF-8 C string to NSString (CRLF kept). */
NSString *MHStr(const char *s);
/* "\r\n" to "\n" for display in AppKit text. */
NSString *MHDisplay(NSString *s);

#endif
