/* MHTheme.h: design tokens of the UI (docs/UI_DESIGN.md "Tokens"): dynamic colors, fonts,
 * spacing, radii, SF Symbol helper, motion, and two small reusable views (status chip and art
 * thumbnail). Every color is dynamic (resolves for the view's effective appearance), so dark mode
 * and Increase Contrast need no extra code in callers. Main thread only. */
#ifndef MH_APP_THEME_H
#define MH_APP_THEME_H

#import <Cocoa/Cocoa.h>

/* ---- spacing (8 pt grid) and radii ---- */
static const CGFloat MHSpace4 = 4, MHSpace8 = 8, MHSpace12 = 12, MHSpace16 = 16, MHSpace24 = 24;
static const CGFloat MHRadiusCard = 10, MHRadiusThumb = 6, MHRadiusChip = 6;
static const CGFloat MHRowHeightCard = 64, MHRowHeightArt = 48, MHMinHit = 28;

@interface MHTheme : NSObject

/* ---- colors (dynamic) ---- */
@property(class, readonly) NSColor *brandBlue;        /* text, links, accents, focus tint */
@property(class, readonly) NSColor *brandFill;        /* fill of prominent buttons (white text on it >= 4.5:1) */
@property(class, readonly) NSColor *destructiveFill;  /* fill of destructive buttons (white text on it >= 4.5:1) */
@property(class, readonly) NSColor *statusVerified;   /* verified discs */
@property(class, readonly) NSColor *statusReview;     /* discs with observations */
@property(class, readonly) NSColor *statusError;      /* errors, destructive */
@property(class, readonly) NSColor *statusWritten;    /* already on the card (= brandBlue) */
@property(class, readonly) NSColor *surface;          /* content background */
@property(class, readonly) NSColor *surfaceRaised;    /* cards, inspector sections */
@property(class, readonly) NSColor *separator;        /* hairlines */
@property(class, readonly) NSColor *textPrimary;
@property(class, readonly) NSColor *textSecondary;    /* captions (>= 4.5:1 on surface and surfaceRaised) */
/* Card outline: stronger under Increase Contrast. */
@property(class, readonly) NSColor *cardStroke;
/* Selection fill for custom rows (accent at fixed alpha, stronger when the window is inactive-safe). */
@property(class, readonly) NSColor *selectionFill;

/* ---- fonts ---- */
@property(class, readonly) NSFont *titleFont;     /* 22 semibold */
@property(class, readonly) NSFont *headlineFont;  /* 13 semibold */
@property(class, readonly) NSFont *bodyFont;      /* 13 regular */
@property(class, readonly) NSFont *captionFont;   /* 11 regular */
@property(class, readonly) NSFont *monoFont;      /* 12 monospaced (serials, numbers) */
@property(class, readonly) NSFont *monoCaptionFont; /* 11 monospaced */

/* ---- SF Symbols (never emoji, never bitmaps). Returns a template image, or a plain circle when
 * the symbol does not exist on this macOS. description becomes the accessibility description. */
+ (NSImage *)symbol:(NSString *)name size:(CGFloat)pt weight:(NSFontWeight)weight description:(NSString *)desc;
+ (NSImage *)symbol:(NSString *)name size:(CGFloat)pt description:(NSString *)desc; /* regular weight */
/* The symbol drawn in a color (color is resolved at draw time, so it follows the appearance). */
+ (NSImage *)symbol:(NSString *)name size:(CGFloat)pt color:(NSColor *)color description:(NSString *)desc;

/* ---- accessibility display settings ---- */
@property(class, readonly) BOOL reduceMotion;
@property(class, readonly) BOOL reduceTransparency;
@property(class, readonly) BOOL increaseContrast;

/* ---- motion: 150 to 250 ms ease in-out; 0 (no animation) with Reduce Motion ---- */
@property(class, readonly) NSTimeInterval motionDuration;
/* Runs changes (use the animator proxies inside) animated, or at once with Reduce Motion. */
+ (void)animate:(void (^)(void))changes completion:(void (^)(void))completion;
+ (void)animate:(void (^)(void))changes;
/* Cross-fade of a view's content (the only effect used with Reduce Motion). */
+ (void)crossFade:(NSView *)view;

/* ---- art ---- */
/* Decodes a .art file (mh_art_decode) into an image; cached by path, size and modification date.
 * Returns nil when path is empty or the file cannot be decoded. */
+ (NSImage *)artImageAtPath:(NSString *)path;
+ (NSImage *)artImageAtCPath:(const char *)path;

@end

/* ---- status chip: icon plus word on a tinted capsule. Never color alone. ---- */
typedef NS_ENUM(NSInteger, MHStatusKind) {
    MHStatusVerified,  /* checkmark.seal.fill, "Verified" */
    MHStatusReview,    /* exclamationmark.triangle.fill, "Review" */
    MHStatusFixable,   /* wrench.and.screwdriver.fill, "Fixable" (missing file, can be located) */
    MHStatusError,     /* xmark.octagon.fill, "Error" */
    MHStatusWritten,   /* sdcard.fill, "Written" */
    MHStatusReady      /* checkmark.circle, "Ready" (structure is fine but the disc was not fully verified) */
};

@interface MHStatusChip : NSView
@property(nonatomic) MHStatusKind kind;
@property(nonatomic) BOOL compact; /* icon only (the word stays in the accessibility label) */
@property(nonatomic, strong) NSColor *tintOverride; /* e.g. white on an emphasized selection; nil = kind color */
+ (NSColor *)colorForKind:(MHStatusKind)k;
+ (NSString *)titleForKind:(MHStatusKind)k;
+ (NSString *)symbolForKind:(MHStatusKind)k;
@end

/* ---- art thumbnail: rounded square (cases) or circle (discs), hairline border, placeholder
 * symbol when there is no art. Reads as an image to VoiceOver. ---- */
typedef NS_ENUM(NSInteger, MHThumbShape) { MHThumbRounded, MHThumbDisc };

@interface MHThumbnailView : NSView
@property(nonatomic, strong) NSImage *image;
@property(nonatomic) MHThumbShape shape;
@property(nonatomic, copy) NSString *placeholderSymbol; /* default "photo" */
@property(nonatomic) CGFloat cornerRadius;              /* rounded shape only, default MHRadiusThumb */
/* Sets image (and the accessibility label "<what>, no art" when nil) from a .art path. */
- (void)setArtPath:(const char *)path;
- (void)setAccessibilityWhat:(NSString *)what; /* "Case art", "Disc art" */
@end

#endif
