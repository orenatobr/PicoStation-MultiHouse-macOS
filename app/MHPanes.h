/* MHPanes.h: the three panes of the main window, each an NSViewController built in code.
 *   MHSidebarViewController    LIBRARY header, case list (source list), totals footer
 *   MHContentViewController    case header (editable title), disc cards, empty state, drop overlay
 *   MHInspectorViewController  art previews, names, Cheats, Details
 * They only build and lay out views. Data sources, delegates and actions are set by
 * MHMainController. */
#ifndef MH_APP_PANES_H
#define MH_APP_PANES_H

#import <Cocoa/Cocoa.h>
#import "MHViews.h"

@interface MHSidebarViewController : NSViewController
@property(strong, readonly) MHListTable *table;
@property(strong, readonly) NSScrollView *scroll;
- (void)setTotalsCases:(NSUInteger)cases discs:(NSUInteger)discs;
@end

typedef NS_ENUM(NSInteger, MHContentMode) {
    MHContentLibraryEmpty,  /* no case at all: big empty state */
    MHContentNoSelection,   /* cases exist, none selected */
    MHContentCase           /* header and disc cards */
};

@interface MHContentViewController : NSViewController
@property(strong, readonly) MHListTable *table;
@property(strong, readonly) NSScrollView *scroll;
@property(strong, readonly) NSTextField *fCase;     /* editable case title */
@property(strong, readonly) NSTextField *caption;   /* "1 disc" */
@property(strong, readonly) NSButton *chkVerify;    /* "Verify on add" */
@property(strong, readonly) MHEmptyStateView *empty;
@property(strong, readonly) MHDropOverlay *overlay;
@property(nonatomic) MHContentMode mode;
@end

@interface MHInspectorViewController : NSViewController
@property(strong, readonly) MHThumbnailView *pvCase, *pvDisc;
@property(strong, readonly) NSButton *btnCaseArt, *btnDiscArt, *btnCheats;
@property(strong, readonly) NSTextField *fCase, *fDisc;
@property(strong, readonly) NSTextView *details;
@end

#endif
