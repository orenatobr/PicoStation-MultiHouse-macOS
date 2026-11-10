#import "MHTheme.h"
#import <QuartzCore/QuartzCore.h>
#include "mh/art.h"
#include <sys/stat.h>

/* Picks the light/dark and normal/high contrast variant of the current appearance. */
typedef struct { uint32_t light, dark, lightHC, darkHC; } MHPalette;

static NSColor *hex(uint32_t v) {
    return [NSColor colorWithSRGBRed:((v >> 16) & 255) / 255. green:((v >> 8) & 255) / 255. blue:(v & 255) / 255. alpha:1];
}

static NSColor *dynamic(NSString *name, MHPalette p) {
    return [NSColor colorWithName:name
             dynamicProvider:^NSColor *(NSAppearance *a) {
        NSAppearanceName n = [a bestMatchFromAppearancesWithNames:@[
            NSAppearanceNameAqua, NSAppearanceNameDarkAqua, NSAppearanceNameAccessibilityHighContrastAqua,
            NSAppearanceNameAccessibilityHighContrastDarkAqua ]];
        if ([n isEqualToString:NSAppearanceNameDarkAqua]) return hex(p.dark);
        if ([n isEqualToString:NSAppearanceNameAccessibilityHighContrastAqua]) return hex(p.lightHC);
        if ([n isEqualToString:NSAppearanceNameAccessibilityHighContrastDarkAqua]) return hex(p.darkHC);
        return hex(p.light);
    }];
}

static void free_buf(void *info, const void *data, size_t size) { free((void *)data); }

@implementation MHTheme

#pragma mark colors

+ (NSColor *)brandBlue {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.brandBlue", (MHPalette){ 0x0070D1, 0x3B9CFF, 0x00539E, 0x7AB8FF }); });
    return c;
}
+ (NSColor *)brandFill {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.brandFill", (MHPalette){ 0x0070D1, 0x0A64B8, 0x00539E, 0x0A5AA6 }); });
    return c;
}
+ (NSColor *)destructiveFill {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.destructiveFill", (MHPalette){ 0xC0211A, 0xB3261E, 0x9A150F, 0xA01D17 }); });
    return c;
}
+ (NSColor *)statusVerified {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.verified", (MHPalette){ 0x146A2D, 0x3DD06A, 0x0F5F26, 0x6BE08F }); });
    return c;
}
+ (NSColor *)statusReview {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.review", (MHPalette){ 0x9A5200, 0xFFB340, 0x7A4000, 0xFFC56E }); });
    return c;
}
+ (NSColor *)statusError {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.error", (MHPalette){ 0xC0211A, 0xFF6B63, 0x9A150F, 0xFF8F89 }); });
    return c;
}
+ (NSColor *)statusWritten { return self.brandBlue; }
+ (NSColor *)surface { return NSColor.windowBackgroundColor; }
+ (NSColor *)surfaceRaised { return NSColor.controlBackgroundColor; }
+ (NSColor *)separator { return NSColor.separatorColor; }
+ (NSColor *)textPrimary { return NSColor.labelColor; }
+ (NSColor *)textSecondary {
    static NSColor *c;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ c = dynamic(@"mh.textSecondary", (MHPalette){ 0x5E5E62, 0xA8A8AE, 0x3F3F43, 0xC8C8CE }); });
    return c;
}
+ (NSColor *)cardStroke {
    return [NSColor colorWithName:@"mh.cardStroke" dynamicProvider:^NSColor *(NSAppearance *a) {
        BOOL hc = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
        __block NSColor *base;
        [a performAsCurrentDrawingAppearance:^{ base = [NSColor.labelColor colorWithAlphaComponent:hc ? 0.45 : 0.12]; }];
        return base;
    }];
}
+ (NSColor *)selectionFill {
    return [NSColor colorWithName:@"mh.selectionFill" dynamicProvider:^NSColor *(NSAppearance *a) {
        BOOL hc = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
        __block NSColor *base;
        [a performAsCurrentDrawingAppearance:^{ base = [MHTheme.brandBlue colorWithAlphaComponent:hc ? 0.28 : 0.16]; }];
        return base;
    }];
}

#pragma mark fonts

+ (NSFont *)titleFont { return [NSFont systemFontOfSize:22 weight:NSFontWeightSemibold]; }
+ (NSFont *)headlineFont { return [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]; }
+ (NSFont *)bodyFont { return [NSFont systemFontOfSize:13 weight:NSFontWeightRegular]; }
+ (NSFont *)captionFont { return [NSFont systemFontOfSize:11 weight:NSFontWeightRegular]; }
+ (NSFont *)monoFont { return [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]; }
+ (NSFont *)monoCaptionFont { return [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]; }

#pragma mark symbols

+ (NSImage *)symbol:(NSString *)name size:(CGFloat)pt weight:(NSFontWeight)weight description:(NSString *)desc {
    NSImage *img = [NSImage imageWithSystemSymbolName:name accessibilityDescription:desc ?: @""];
    if (!img) { /* not available on this macOS: a plain circle keeps the layout intact */
        img = [NSImage imageWithSize:NSMakeSize(pt, pt) flipped:NO drawingHandler:^BOOL(NSRect r) {
            [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(r, 1.5, 1.5)] stroke];
            return YES;
        }];
        img.template = YES;
        img.accessibilityDescription = desc;
        return img;
    }
    NSImageSymbolConfiguration *cfg = [NSImageSymbolConfiguration configurationWithPointSize:pt weight:weight];
    return [img imageWithSymbolConfiguration:cfg];
}

+ (NSImage *)symbol:(NSString *)name size:(CGFloat)pt description:(NSString *)desc {
    return [self symbol:name size:pt weight:NSFontWeightRegular description:desc];
}

+ (NSImage *)symbol:(NSString *)name size:(CGFloat)pt color:(NSColor *)color description:(NSString *)desc {
    NSImage *base = [self symbol:name size:pt weight:NSFontWeightSemibold description:desc];
    NSImage *out = [NSImage imageWithSize:base.size flipped:NO drawingHandler:^BOOL(NSRect r) {
        [base drawInRect:r];
        [color set];
        NSRectFillUsingOperation(r, NSCompositingOperationSourceIn);
        return YES;
    }];
    out.accessibilityDescription = desc;
    return out;
}

#pragma mark accessibility settings and motion

+ (BOOL)reduceMotion { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion; }
+ (BOOL)reduceTransparency { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency; }
+ (BOOL)increaseContrast { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast; }
+ (NSTimeInterval)motionDuration { return self.reduceMotion ? 0 : 0.2; }

+ (void)animate:(void (^)(void))changes completion:(void (^)(void))completion {
    NSTimeInterval d = self.motionDuration;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *ctx) {
        ctx.duration = d;
        ctx.allowsImplicitAnimation = d > 0;
        ctx.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        changes();
    } completionHandler:completion];
}
+ (void)animate:(void (^)(void))changes { [self animate:changes completion:nil]; }

+ (void)crossFade:(NSView *)view {
    view.wantsLayer = YES;
    CATransition *t = [CATransition animation];
    t.type = kCATransitionFade;
    t.duration = self.reduceMotion ? 0.1 : 0.2;
    t.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    [view.layer addAnimation:t forKey:@"mh.crossfade"];
}

#pragma mark art

+ (NSImage *)artImageAtCPath:(const char *)path {
    if (!path || !path[0]) return nil;
    return [self artImageAtPath:[NSString stringWithUTF8String:path]];
}

+ (NSImage *)artImageAtPath:(NSString *)path {
    if (!path.length) return nil;
    static NSCache<NSString *, NSImage *> *cache;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ cache = [[NSCache alloc] init]; cache.countLimit = 512; });
    struct stat st;
    if (stat(path.fileSystemRepresentation, &st) != 0) return nil;
    NSString *key = [NSString stringWithFormat:@"%@|%lld|%ld.%ld", path, (long long)st.st_size,
                     (long)st.st_mtimespec.tv_sec, (long)st.st_mtimespec.tv_nsec];
    NSImage *hit = [cache objectForKey:key];
    if (hit) return hit;
    uint8_t *bgra = NULL;
    uint32_t w = 0, h = 0;
    if (!mh_art_decode(path.fileSystemRepresentation, &bgra, &w, &h) || !bgra || !w || !h) return nil;
    /* The decoder gives straight alpha; Core Graphics wants it premultiplied. */
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t *p = bgra + i * 4, a = p[3];
        if (a != 255) { p[0] = (uint8_t)(p[0] * a / 255); p[1] = (uint8_t)(p[1] * a / 255); p[2] = (uint8_t)(p[2] * a / 255); }
    }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(NULL, bgra, (size_t)w * h * 4,
                                                        free_buf);
    CGImageRef cg = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs,
                                  kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, dp, NULL, false,
                                  kCGRenderingIntentDefault);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
    if (!cg) return nil;
    NSImage *img = [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(w, h)];
    CGImageRelease(cg);
    [cache setObject:img forKey:key];
    return img;
}

@end

#pragma mark - status chip

@implementation MHStatusChip

+ (NSColor *)colorForKind:(MHStatusKind)k {
    switch (k) {
        case MHStatusVerified: return MHTheme.statusVerified;
        case MHStatusReview: case MHStatusFixable: return MHTheme.statusReview;
        case MHStatusError: return MHTheme.statusError;
        case MHStatusWritten: return MHTheme.statusWritten;
        case MHStatusReady: return MHTheme.textSecondary;
    }
    return MHTheme.textSecondary;
}
+ (NSString *)titleForKind:(MHStatusKind)k {
    switch (k) {
        case MHStatusVerified: return @"Verified";
        case MHStatusReview: return @"Review";
        case MHStatusFixable: return @"Fixable";
        case MHStatusError: return @"Error";
        case MHStatusWritten: return @"Written";
        case MHStatusReady: return @"Ready";
    }
    return @"";
}
+ (NSString *)symbolForKind:(MHStatusKind)k {
    switch (k) {
        case MHStatusVerified: return @"checkmark.seal.fill";
        case MHStatusReview: return @"exclamationmark.triangle.fill";
        case MHStatusFixable: return @"wrench.and.screwdriver.fill";
        case MHStatusError: return @"xmark.octagon.fill";
        case MHStatusWritten: return @"sdcard.fill";
        case MHStatusReady: return @"checkmark.circle";
    }
    return @"circle";
}

- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityStaticTextRole;
    }
    return self;
}

- (void)setKind:(MHStatusKind)k {
    _kind = k;
    [self invalidateIntrinsicContentSize];
    self.needsDisplay = YES;
    self.toolTip = [NSString stringWithFormat:@"Status: %@", [MHStatusChip titleForKind:k]];
}
- (void)setCompact:(BOOL)c {
    _compact = c;
    [self invalidateIntrinsicContentSize];
    self.needsDisplay = YES;
}

- (NSColor *)tint { return _tintOverride ?: [MHStatusChip colorForKind:_kind]; }
- (void)setTintOverride:(NSColor *)c { _tintOverride = c; self.needsDisplay = YES; }

- (NSDictionary *)textAttrs {
    return @{ NSFontAttributeName : [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold],
              NSForegroundColorAttributeName : [self tint] };
}

- (NSSize)intrinsicContentSize {
    if (_compact) return NSMakeSize(16, 16);
    CGFloat tw = ceil([[MHStatusChip titleForKind:_kind] sizeWithAttributes:[self textAttrs]].width);
    return NSMakeSize(MHSpace8 + 12 + MHSpace4 + tw + MHSpace8, 22);
}

- (NSString *)accessibilityLabel { return [NSString stringWithFormat:@"Status: %@", [MHStatusChip titleForKind:_kind]]; }
- (NSString *)accessibilityValue { return [MHStatusChip titleForKind:_kind]; }
- (BOOL)isOpaque { return NO; }

- (void)drawRect:(NSRect)dirty {
    NSColor *col = [self tint];
    NSRect b = self.bounds;
    NSImage *icon = [MHTheme symbol:[MHStatusChip symbolForKind:_kind] size:11 color:col
                        description:[MHStatusChip titleForKind:_kind]];
    if (_compact) {
        NSSize s = icon.size;
        [icon drawInRect:NSMakeRect(NSMidX(b) - s.width / 2, NSMidY(b) - s.height / 2, s.width, s.height)];
        return;
    }
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 0.5, 0.5) xRadius:MHRadiusChip yRadius:MHRadiusChip];
    [[col colorWithAlphaComponent:MHTheme.increaseContrast ? 0.22 : 0.14] setFill];
    [p fill];
    if (MHTheme.increaseContrast) { [col setStroke]; p.lineWidth = 1; [p stroke]; }
    NSSize is = icon.size;
    [icon drawInRect:NSMakeRect(MHSpace8, round(NSMidY(b) - is.height / 2), is.width, is.height)];
    NSDictionary *at = [self textAttrs];
    NSString *t = [MHStatusChip titleForKind:_kind];
    NSSize ts = [t sizeWithAttributes:at];
    [t drawAtPoint:NSMakePoint(MHSpace8 + 12 + MHSpace4, round(NSMidY(b) - ts.height / 2)) withAttributes:at];
}
@end

#pragma mark - thumbnail

@implementation MHThumbnailView {
    NSString *_what;
}

- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        _cornerRadius = MHRadiusThumb;
        _placeholderSymbol = @"photo";
        _what = @"Art";
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityImageRole;
        self.accessibilityLabel = @"Art, none";
    }
    return self;
}

- (BOOL)isOpaque { return NO; }

- (void)setImage:(NSImage *)i {
    _image = i;
    self.accessibilityLabel = [NSString stringWithFormat:@"%@, %@", _what, i ? @"preview" : @"none"];
    self.needsDisplay = YES;
}
- (void)setShape:(MHThumbShape)s { _shape = s; self.needsDisplay = YES; }
- (void)setPlaceholderSymbol:(NSString *)s { _placeholderSymbol = [s copy]; self.needsDisplay = YES; }
- (void)setAccessibilityWhat:(NSString *)w {
    _what = [w copy];
    self.accessibilityLabel = [NSString stringWithFormat:@"%@, %@", _what, _image ? @"preview" : @"none"];
}
- (void)setArtPath:(const char *)path { self.image = [MHTheme artImageAtCPath:path]; }

- (void)drawRect:(NSRect)dirty {
    NSRect b = NSInsetRect(self.bounds, 0.5, 0.5);
    CGFloat side = MIN(b.size.width, b.size.height);
    NSRect sq = NSMakeRect(NSMidX(b) - side / 2, NSMidY(b) - side / 2, side, side);
    NSBezierPath *shape = _shape == MHThumbDisc ? [NSBezierPath bezierPathWithOvalInRect:sq]
                                                : [NSBezierPath bezierPathWithRoundedRect:sq xRadius:_cornerRadius yRadius:_cornerRadius];
    if (_image) {
        [NSGraphicsContext saveGraphicsState];
        if (_shape == MHThumbRounded) {
            [[NSColor.labelColor colorWithAlphaComponent:0.04] setFill];
            [shape fill];
            [shape addClip];
        }
        [_image drawInRect:sq fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1
            respectFlipped:YES hints:@{ NSImageHintInterpolation : @(NSImageInterpolationHigh) }];
        [NSGraphicsContext restoreGraphicsState];
        if (_shape == MHThumbRounded) {
            [MHTheme.cardStroke setStroke];
            shape.lineWidth = 1;
            [shape stroke];
        }
        return;
    }
    [[NSColor.labelColor colorWithAlphaComponent:0.06] setFill];
    [shape fill];
    [MHTheme.cardStroke setStroke];
    shape.lineWidth = 1;
    [shape stroke];
    CGFloat pt = MAX(10, floor(side * 0.4));
    NSString *sym = _shape == MHThumbDisc && [_placeholderSymbol isEqualToString:@"photo"] ? @"opticaldisc" : _placeholderSymbol;
    NSImage *ic = [MHTheme symbol:sym size:pt color:MHTheme.textSecondary description:nil];
    NSSize is = ic.size;
    [ic drawInRect:NSMakeRect(NSMidX(b) - is.width / 2, NSMidY(b) - is.height / 2, is.width, is.height)];
}
@end
