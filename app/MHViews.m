#import "MHViews.h"

NSColor *MHColorFace(void) { return [NSColor colorWithSRGBRed:240 / 255. green:240 / 255. blue:240 / 255. alpha:1]; }

@implementation MHRootView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)r {
    [MHColorFace() setFill];
    NSRectFill(r);
}
@end

@implementation MHRow
@end

@implementation MHListTable
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) _dropRow = -1;
    return self;
}
- (BOOL)acceptsFirstResponder { return self.isEnabled; }

- (void)setDropRow:(int)r {
    int old = _dropRow;
    _dropRow = r;
    if (old >= 0) [self setNeedsDisplayInRect:[self rectOfRow:old]];
    if (r >= 0) [self setNeedsDisplayInRect:[self rectOfRow:r]];
}

- (void)drawRow:(NSInteger)row clipRect:(NSRect)clip {
    [super drawRow:row clipRect:clip];
    if (row == _dropRow) {
        NSRect r = NSInsetRect([self rectOfRow:row], 1, 1);
        [[NSColor colorWithSRGBRed:0x23 / 255. green:0x73 / 255. blue:0xd2 / 255. alpha:0.22] setFill];
        NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
        [[NSColor colorWithSRGBRed:0x23 / 255. green:0x73 / 255. blue:0xd2 / 255. alpha:1] setStroke];
        NSFrameRect(r);
    }
}

- (void)mouseDown:(NSEvent *)e {
    if (!self.isEnabled) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    NSInteger row = [self rowAtPoint:p];
    [self.window makeFirstResponder:self];
    if (row < 0 || p.y < 0) { [super mouseDown:e]; return; }
    if (self.selectedRow != row)
        [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    if (e.clickCount == 2) { [self.owner list:self doubleClickRow:row]; return; }
    NSPoint start = e.locationInWindow;
    for (;;) {
        NSEvent *n = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        if (n.type == NSEventTypeLeftMouseUp) break;
        if (hypot(n.locationInWindow.x - start.x, n.locationInWindow.y - start.y) > 4) {
            [self.owner list:self beginDragRow:row event:n];
            break;
        }
    }
}

- (void)rightMouseDown:(NSEvent *)e {
    if (!self.isEnabled) return;
    NSInteger row = [self rowAtPoint:[self convertPoint:e.locationInWindow fromView:nil]];
    if (row < 0) return;
    [self.window makeFirstResponder:self];
    [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    [self.owner list:self doubleClickRow:row];
}

- (void)keyDown:(NSEvent *)e {
    NSString *c = e.charactersIgnoringModifiers;
    unichar ch = c.length ? [c characterAtIndex:0] : 0;
    /* Delete / forward delete (VK_DELETE) and F2 are the keys of the original. */
    if (e.keyCode == 51 || e.keyCode == 117 || e.keyCode == 120) {
        [self.owner list:self keyDown:ch keyCode:e.keyCode];
        return;
    }
    [super keyDown:e];
}
@end

@implementation MHPreviewView {
    uint8_t *_bgra;
    uint32_t _w, _h;
}
- (void)dealloc { free(_bgra); }
- (BOOL)isFlipped { return YES; }

- (void)setBitmap:(uint8_t *)bgra width:(uint32_t)w height:(uint32_t)h {
    free(_bgra);
    _bgra = bgra;
    _w = bgra ? w : 0;
    _h = bgra ? h : 0;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    [[NSColor whiteColor] setFill];
    NSRectFill(b);
    [[NSColor colorWithWhite:0.62 alpha:1] setStroke];
    NSFrameRect(b);
    if (_bgra && _w && _h) {
        CGFloat side = MIN(b.size.width, b.size.height) - 8;
        NSRect dst = NSMakeRect(floor((b.size.width - side) / 2), floor((b.size.height - side) / 2), side, side);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        /* StretchDIBits on a 32 bit BI_RGB bitmap ignores alpha: do the same. */
        CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, _bgra, (size_t)_w * _h * 4, NULL);
        CGImageRef img = CGImageCreate(_w, _h, 8, 32, (size_t)_w * 4, cs,
                                       kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, dp, NULL,
                                       false, kCGRenderingIntentDefault);
        CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
        CGContextSaveGState(ctx);
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextTranslateCTM(ctx, dst.origin.x, dst.origin.y + dst.size.height);
        CGContextScaleCTM(ctx, 1, -1);
        CGContextDrawImage(ctx, CGRectMake(0, 0, dst.size.width, dst.size.height), img);
        CGContextRestoreGState(ctx);
        CGImageRelease(img);
        CGDataProviderRelease(dp);
        CGColorSpaceRelease(cs);
        return;
    }
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.alignment = NSTextAlignmentCenter;
    NSDictionary *at = @{NSFontAttributeName : [NSFont systemFontOfSize:12], NSParagraphStyleAttributeName : ps,
                         NSForegroundColorAttributeName : NSColor.blackColor};
    NSString *t = self.caption ?: @"";
    NSSize s = [t sizeWithAttributes:at];
    [t drawInRect:NSMakeRect(0, (b.size.height - s.height) / 2, b.size.width, s.height) withAttributes:at];
}
@end

/* The ghost paints its own content view (orig: drag_ghost_proc). */
@interface MHGhostContent : NSView
@property(copy) NSString *text;
@end
@implementation MHGhostContent
- (void)drawRect:(NSRect)r {
    [[NSColor colorWithSRGBRed:0xec / 255. green:0xf5 / 255. blue:1 alpha:1] setFill];
    NSRectFill(self.bounds);
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.lineBreakMode = NSLineBreakByTruncatingTail;
    NSDictionary *at = @{NSFontAttributeName : [NSFont systemFontOfSize:12], NSParagraphStyleAttributeName : ps,
                         NSForegroundColorAttributeName : [NSColor colorWithSRGBRed:0x19 / 255. green:0x41 / 255. blue:0x6e / 255. alpha:1]};
    NSString *t = self.text ?: @"";
    NSSize s = [t sizeWithAttributes:at];
    [t drawInRect:NSMakeRect(12, (self.bounds.size.height - s.height) / 2, self.bounds.size.width - 20, s.height)
        withAttributes:at];
}
@end

@implementation MHGhostPanel
- (instancetype)init {
    self = [super initWithContentRect:NSMakeRect(0, 0, 320, 34)
                            styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                              backing:NSBackingStoreBuffered
                                defer:YES];
    self.level = NSStatusWindowLevel;
    self.ignoresMouseEvents = YES;
    self.hasShadow = NO;
    self.releasedWhenClosed = NO;
    self.contentView = [[MHGhostContent alloc] initWithFrame:NSMakeRect(0, 0, 320, 34)];
    return self;
}
- (BOOL)canBecomeKeyWindow { return NO; }
- (void)setText:(NSString *)t {
    ((MHGhostContent *)self.contentView).text = t;
    [self.contentView setNeedsDisplay:YES];
}
- (void)moveNearCursor {
    NSPoint m = NSEvent.mouseLocation; /* Win32: cursor + (16,16), y grows down */
    [self setFrameOrigin:NSMakePoint(m.x + 16, m.y - 16 - 34)];
}
@end

@implementation MHDropLine
- (void)drawRect:(NSRect)r {
    [[NSColor colorWithSRGBRed:0x23 / 255. green:0x73 / 255. blue:0xd2 / 255. alpha:1] setFill];
    NSRectFill(self.bounds);
}
@end
