/* MHDialogs.h: the sheets of the main window: write confirmation, write progress and result,
 * menu settings. All of them attach to the main window as sheets. */
#ifndef MH_APP_DIALOGS_H
#define MH_APP_DIALOGS_H

#import <Cocoa/Cocoa.h>
#include "mh/menu.h"

/* What the write confirmation sheet shows. */
@interface MHWriteSummary : NSObject
@property(copy) NSString *bsd;          /* "disk8" */
@property(copy) NSString *model;        /* card model */
@property(copy) NSString *serial;
@property uint64_t cardSize;            /* bytes */
@property uint64_t estimate;            /* approximate final RAW size, bytes */
@property unsigned cases, discs;
@property BOOL editingCard;             /* a V2 card is loaded: apply changes instead of erasing */
@property(copy) NSString *details;      /* full core text (logged by the test hook) */
@end

/* Sheet: "Erase and write to <card>?" with the explicit raw-write authorization checkbox. YES
 * when "Erase and Write" (or "Write Changes") was pressed, only possible after ticking it. */
BOOL MHConfirmWrite(NSWindow *parent, MHWriteSummary *summary);

/* Sheet "Menu Settings". rec is the 64 byte record, edited in place; YES when Save produced a
 * valid sealed record. */
BOOL MHMenuDefaultsDialog(NSWindow *parent, uint8_t rec[MH_MENU_DEFAULTS_SIZE]);

/* Sheet with selectable text, "Copy" and "Close" (technical details). Returns when closed. */
void MHShowDetails(NSWindow *parent, NSString *title, NSString *text);

/* Human readable title of a MhV2Phase value ("Writing discs"). */
NSString *MHPhaseTitle(int phase);

/* Progress sheet of a long write. Shown with +present...; update from the main thread. */
@interface MHProgressSheet : NSObject
@property(copy) void (^onCancel)(void);
+ (instancetype)presentOn:(NSWindow *)parent title:(NSString *)title;
- (void)updatePhase:(int)phase step:(int)step steps:(int)steps fraction:(double)fraction
               text:(NSString *)text cancelEnabled:(BOOL)cancelEnabled cancelling:(BOOL)cancelling;
/* Result states. completion runs after the sheet is closed (immediately under the test hook). */
- (void)finishSuccessTitle:(NSString *)title summary:(NSString *)summary completion:(dispatch_block_t)completion;
- (void)finishFailureTitle:(NSString *)title text:(NSString *)text completion:(dispatch_block_t)completion;
- (void)dismiss;
@end

#endif
