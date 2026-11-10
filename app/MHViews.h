/* MHViews.h: the custom views of the main window.
 *   MHRootView     flipped content view (top-left origin like the Win32 client area)
 *   MHRow          one cached list row (the lists never read the project while a worker runs)
 *   MHListTable    list with the original mouse/keyboard behaviour (orig: SysListView32 in report mode)
 *   MHPreviewView  orig: PSTPreview window class (preview_proc)
 *   MHGhostPanel   orig: PSTDragGhost (drag_ghost_proc)
 *   MHDropLine     orig: PSTDropLine (drop_line_proc) */
#ifndef MH_APP_VIEWS_H
#define MH_APP_VIEWS_H

#import <Cocoa/Cocoa.h>

/* Win32 COLOR_BTNFACE / COLOR_WINDOW of the light theme the original used. */
NSColor *MHColorFace(void);

@interface MHRootView : NSView
@end

@interface MHRow : NSObject
@property(copy) NSArray<NSString *> *cells; /* column texts */
@property BOOL blue;                        /* already written disc (orig: 0xb42100) */
@property BOOL obs;                         /* status column reads "OBS" (red) */
@property NSInteger param;                  /* case slot / disc index (LVITEM lParam) */
@end

@class MHListTable;
@protocol MHListOwner <NSObject>
/* The press moved far enough to start a drag (orig: LVN_BEGINDRAG). Returns after the mouse is
 * released. */
- (void)list:(MHListTable *)t beginDragRow:(NSInteger)row event:(NSEvent *)e;
- (void)list:(MHListTable *)t doubleClickRow:(NSInteger)row;
- (void)list:(MHListTable *)t keyDown:(unichar)key keyCode:(unsigned short)code;
@end

@interface MHListTable : NSTableView
@property(weak) id<MHListOwner> owner;
@property(nonatomic) int dropRow; /* orig: LVIS_DROPHILITED item, -1 none */
@end

@interface MHPreviewView : NSView
@property(copy) NSString *caption; /* "CASE" / "DISCO" */
/* Takes ownership of a malloc'ed w*h BGRA bitmap (or NULL to clear). */
- (void)setBitmap:(uint8_t *)bgra width:(uint32_t)w height:(uint32_t)h;
@end

@interface MHGhostPanel : NSPanel
- (instancetype)init;
- (void)setText:(NSString *)text;
- (void)moveNearCursor;
@end

@interface MHDropLine : NSView
@end

#endif
