/* MHViews.h: the custom views of the main window.
 *   MHLibRow           one cached list row (lists never read the project while a worker runs)
 *   MHListTable     NSTableView with the keyboard and context menu behaviour of the lists
 *   MHCaseCellView  sidebar row: art thumbnail, title, "N discs", status symbol
 *   MHDiscCellView  disc card content: disc art, title, serial pill, tracks, status chips
 *   MHDiscRowView   card background and selection of a disc row
 *   MHPillView      monospaced serial pill
 *   MHEmptyStateView, MHDropOverlay, MHBottomBar, MHPrimaryButton, MHDropRootView
 * Layout is done in code (no nibs). Colors, fonts and spacing come from MHTheme. */
#ifndef MH_APP_VIEWS_H
#define MH_APP_VIEWS_H

#import <Cocoa/Cocoa.h>
#import "MHTheme.h"

/* ---- row model ---- */
@interface MHLibRow : NSObject
@property NSInteger param;                  /* case slot / disc index (list item parameter) */
@property NSInteger position;               /* 1-based position in its list */
@property(copy) NSString *title;
@property(copy) NSString *serial;           /* discs only */
@property(copy) NSString *artPath;
@property NSUInteger count;                 /* discs in a case, tracks in a disc */
@property unsigned status;                  /* MH_DISC_STATUS_* (worst of the case) */
@property BOOL written;                     /* already on the card (all discs, for a case) */
@property BOOL verified;                    /* fully verified (all discs, for a case) */
@property(copy) NSArray<NSString *> *cells; /* legacy text columns, kept for the "report" test command */
- (MHStatusKind)statusKind;
/* Same row as spoken by VoiceOver: "Alundra, 1 disc, verified". */
- (NSString *)spokenDescriptionForDisc:(BOOL)disc;
@end

/* ---- tables ---- */
@class MHListTable;
@protocol MHListOwner <NSObject>
- (void)list:(MHListTable *)t keyDown:(unichar)key keyCode:(unsigned short)code;
- (NSMenu *)list:(MHListTable *)t menuForRow:(NSInteger)row;
@end

@interface MHListTable : NSTableView
@property(weak) id<MHListOwner> owner;
@end

@interface MHPillView : NSView
@property(nonatomic, copy) NSString *text;
@property(nonatomic) BOOL monospaced; /* serial vs "No serial" */
@end

@interface MHCaseCellView : NSTableCellView
@property(strong, readonly) MHThumbnailView *thumb;
@property(strong, readonly) NSTextField *caption;
@property(strong, readonly) MHStatusChip *chip;
- (void)configureWithRow:(MHLibRow *)r;
@end

@interface MHDiscCellView : NSTableCellView
@property(strong, readonly) MHThumbnailView *thumb;
@property(strong, readonly) NSTextField *caption;
@property(strong, readonly) MHPillView *pill;
@property(strong, readonly) MHStatusChip *chip;
@property(strong, readonly) MHStatusChip *writtenChip; /* compact, shown when written and status is not Written */
- (void)configureWithRow:(MHLibRow *)r;
@end

@interface MHDiscRowView : NSTableRowView
@end

/* ---- empty state ---- */
@interface MHEmptyStateView : NSView
@property(strong, readonly) NSButton *addGame, *addDisc;
@property(strong, readonly) NSTextField *titleLabel, *messageLabel;
- (void)setTarget:(id)target addGameAction:(SEL)g addDiscAction:(SEL)d;
/* Configures for "library empty" (YES) or "no case selected" (NO). */
- (void)setLibraryEmpty:(BOOL)empty;
@end

/* ---- drop highlight: dashed brand-blue border and "Drop to add" over the content area ---- */
@interface MHDropOverlay : NSView
@property(nonatomic, copy) NSString *detail; /* e.g. "Discs go to Alundra" */
- (void)setActive:(BOOL)on;
@end

/* ---- the big default button ---- */
@interface MHPrimaryButton : NSButton
@property(nonatomic, strong) NSColor *fillColor; /* default MHTheme.brandFill */
@end

/* ---- bottom bar (52 pt, material) ---- */
@interface MHBottomBar : NSVisualEffectView
@property(strong, readonly) NSPopUpButton *popCard;
@property(strong, readonly) NSButton *btnRefresh;
@property(strong, readonly) NSTextField *status;
@property(strong, readonly) NSProgressIndicator *progress;
@property(strong, readonly) MHPrimaryButton *btnWrite;
- (void)setStatusText:(NSString *)text;
- (void)setProgressVisible:(BOOL)on;
- (void)setWriteTitle:(NSString *)title;
@end

/* ---- root view of the window: accepts Finder drops and reports them ---- */
@protocol MHDropDelegate <NSObject>
- (NSDragOperation)dropOperationFor:(id<NSDraggingInfo>)info;
- (BOOL)performDrop:(id<NSDraggingInfo>)info;
- (void)dropEnded;
@end

@interface MHDropRootView : NSView
@property(weak) id<MHDropDelegate> dropDelegate;
@end

#endif
