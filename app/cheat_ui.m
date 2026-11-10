/* cheat_ui.m: the cheat windows: "Cheats" (manager), "Cheat Library" (sheet) and "Cheat Editor" (sheet).
 * Original: cheat_ui_edit, ui_proc (+ ui_profile, ui_details, ui_new_custom, ui_remove, ui_import,
 * ui_export, ui_accept, ui_commit_edit, ui_move_entry, ui_select_index, ui_header_proc),
 * catalog_proc (+ catalog_refresh, catalog_details, catalog_add), editor_proc, editor_save,
 * run_editor. The behaviour is the original one; the look follows docs/UI_DESIGN.md and MHTheme. */
#import "cheat_ui.h"
#import "MHTheme.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "mh/str.h"

#define MH_NONE 0xFFFFFFFFu
#define TITLE_CHEATS @"Cheats"
#define DIALECT_V5 "MHDialect=V5ORIG_BANK_V01"
#define DIALECT_CLASSIC "MHDialect=GAMESHARK_CLASSIC_V1"
#define MAX_ACTIVE_LINES 31

NSInteger (^MHCheatUIAlertHook)(NSString *, NSString *, NSArray<NSString *> *) = nil;
NSURL *(^MHCheatUIPanelHook)(BOOL) = nil;

static __weak id gManager, gCatalog, gEditor;

id<MHCheatUITestable> MHCheatUITestTarget(NSString *kind) {
    if ([kind isEqualToString:@"manager"]) return gManager;
    if ([kind isEqualToString:@"catalog"]) return gCatalog;
    if ([kind isEqualToString:@"editor"]) return gEditor;
    return nil;
}

/* ---- small toolkit ------------------------------------------------------------------------- */

static NSString *S(const char *c) {
    if (!c) return @"";
    NSString *s = [NSString stringWithUTF8String:c];
    return s ?: [[NSString alloc] initWithBytes:c length:strlen(c) encoding:NSISOLatin1StringEncoding];
}

static NSString *Num(unsigned long v) {
    static NSNumberFormatter *f;
    if (!f) { f = [[NSNumberFormatter alloc] init]; f.numberStyle = NSNumberFormatterDecimalStyle; f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US"]; }
    return [f stringFromNumber:@(v)];
}

/* Runs events until done() says so. Used to wait for sheets and alert sheets without leaving the
 * caller (the manager is modal and the original code was synchronous). */
static void SpinUntil(BOOL (^done)(void)) {
    while (!done()) {
        @autoreleasepool {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (e) [NSApp sendEvent:e];
        }
    }
}

/* The window a new sheet should attach to: the manager, or the sheet already on top of it. */
static NSWindow *TopWindow(void) {
    id<MHCheatUITestable> m = gManager;
    NSWindow *w = m.testWindow;
    while (w.attachedSheet) w = w.attachedSheet;
    return w;
}

static void RunSheet(NSWindow *parent, NSWindow *sheet) {
    if (!parent) { [NSApp runModalForWindow:sheet]; return; }
    __block BOOL done = NO;
    [parent beginSheet:sheet completionHandler:^(NSModalResponse r) { done = YES; }];
    SpinUntil(^BOOL { return done; });
}
static void EndSheet(NSWindow *sheet) {
    if (sheet.sheetParent) [sheet.sheetParent endSheet:sheet];
    else { [NSApp stopModal]; [sheet orderOut:nil]; }
}

static void Announce(NSString *text) {
    id<MHCheatUITestable> m = gManager;
    NSWindow *w = m.testWindow;
    if (!w || !text.length) return;
    NSAccessibilityPostNotificationWithUserInfo(w, NSAccessibilityAnnouncementRequestedNotification,
                                                @{NSAccessibilityAnnouncementKey : text,
                                                  NSAccessibilityPriorityKey : @(NSAccessibilityPriorityHigh)});
}

/* orig: MessageBoxW. Plain EN-US alert sheet on the top cheat window. defIdx answers Return,
 * destructive marks the first button as destructive. */
static NSInteger MsgBox(NSString *title, NSString *text, NSAlertStyle style, NSArray<NSString *> *buttons,
                        NSInteger defIdx, BOOL destructive) {
    if (MHCheatUIAlertHook) return MHCheatUIAlertHook(title, text, buttons);
    NSAlert *a = [[NSAlert alloc] init];
    a.alertStyle = style;
    a.messageText = title;
    a.informativeText = text;
    for (NSString *b in buttons) [a addButtonWithTitle:b];
    if (buttons.count > 1) {
        for (NSButton *b in a.buttons) b.keyEquivalent = @"";
        a.buttons[defIdx].keyEquivalent = @"\r";
        a.buttons.lastObject.keyEquivalent = @"\033";
        if (defIdx == (NSInteger)buttons.count - 1) a.buttons.lastObject.keyEquivalent = @"\r";
        if (destructive) a.buttons[0].hasDestructiveAction = YES;
    }
    NSWindow *p = TopWindow();
    if (!p) return [a runModal] - NSAlertFirstButtonReturn;
    __block NSInteger r = -1;
    __block BOOL done = NO;
    [a beginSheetModalForWindow:p completionHandler:^(NSModalResponse x) { r = x - NSAlertFirstButtonReturn; done = YES; }];
    SpinUntil(^BOOL { return done; });
    return r;
}
static void MsgErr(NSString *title, NSString *text) { MsgBox(title, text, NSAlertStyleCritical, @[ @"OK" ], 0, NO); }
static void MsgInfo(NSString *title, NSString *text) { MsgBox(title, text, NSAlertStyleInformational, @[ @"OK" ], 0, NO); }

/* ---- views ------------------------------------------------------------------------------------ */

@interface MHCard : NSView
@property(nonatomic, strong) NSColor *fill, *stroke;
@property(nonatomic) CGFloat radius;
@end
@implementation MHCard
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        self.wantsLayer = YES;
        self.translatesAutoresizingMaskIntoConstraints = NO;
        _radius = MHRadiusCard;
        _fill = MHTheme.surfaceRaised;
        _stroke = MHTheme.cardStroke;
    }
    return self;
}
- (BOOL)wantsUpdateLayer { return YES; }
- (void)updateLayer {
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        self.layer.cornerRadius = self.radius;
        self.layer.masksToBounds = YES;
        self.layer.backgroundColor = self.fill.CGColor;
        self.layer.borderColor = self.stroke.CGColor;
        self.layer.borderWidth = self.stroke ? 1 : 0;
    }];
}
- (void)viewDidChangeEffectiveAppearance { self.needsDisplay = YES; }
@end

/* Tinted capsule with an SF Symbol and a word (never color alone). */
typedef NS_ENUM(NSInteger, MHSource) { MHSourceLibrary, MHSourceUserV5, MHSourceUserClassic };

@interface MHSourceChip : NSView
@property(nonatomic) MHSource source;
@property(nonatomic) BOOL emphasized; /* drawn on a selected (accent) row */
@end
@implementation MHSourceChip
+ (NSString *)titleFor:(MHSource)s {
    return s == MHSourceLibrary ? @"Library" : s == MHSourceUserV5 ? @"User V5" : @"User Classic";
}
+ (NSString *)symbolFor:(MHSource)s {
    return s == MHSourceLibrary ? @"books.vertical.fill" : s == MHSourceUserV5 ? @"person.fill" : @"arrow.triangle.2.circlepath";
}
+ (NSColor *)colorFor:(MHSource)s {
    return s == MHSourceLibrary ? MHTheme.brandBlue : s == MHSourceUserV5 ? MHTheme.statusVerified : MHTheme.statusReview;
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) self.translatesAutoresizingMaskIntoConstraints = NO;
    return self;
}
- (void)setSource:(MHSource)s {
    _source = s;
    self.accessibilityLabel = [@"Source: " stringByAppendingString:[MHSourceChip titleFor:s]];
    self.accessibilityRole = NSAccessibilityStaticTextRole;
    [self invalidateIntrinsicContentSize];
    self.needsDisplay = YES;
}
- (void)setEmphasized:(BOOL)e { _emphasized = e; self.needsDisplay = YES; }
- (NSDictionary *)attrs {
    return @{NSFontAttributeName : [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold],
             NSForegroundColorAttributeName : _emphasized ? NSColor.whiteColor : [MHSourceChip colorFor:_source]};
}
- (NSImage *)icon:(NSColor *)c {
    return [MHTheme symbol:[MHSourceChip symbolFor:_source] size:11 color:c description:nil];
}
- (NSSize)intrinsicContentSize {
    CGFloat tw = ceil([[MHSourceChip titleFor:_source] sizeWithAttributes:[self attrs]].width);
    return NSMakeSize(MHSpace8 + ceil([self icon:NSColor.blackColor].size.width) + MHSpace4 + tw + MHSpace8, 22);
}
- (BOOL)isFlipped { return NO; }
- (void)drawRect:(NSRect)r {
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        NSColor *col = self->_emphasized ? NSColor.whiteColor : [MHSourceChip colorFor:self->_source];
        NSRect b = NSInsetRect(self.bounds, 0.5, 0.5);
        NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:b xRadius:b.size.height / 2 yRadius:b.size.height / 2];
        [[col colorWithAlphaComponent:self->_emphasized ? 0.25 : 0.14] setFill];
        [p fill];
        NSImage *ic = [self icon:col];
        NSSize is = ic.size;
        [ic drawInRect:NSMakeRect(MHSpace8, NSMidY(b) - is.height / 2, is.width, is.height)];
        NSDictionary *at = [self attrs];
        NSString *t = [MHSourceChip titleFor:self->_source];
        NSSize ts = [t sizeWithAttributes:at];
        [t drawAtPoint:NSMakePoint(MHSpace8 + ceil(is.width) + MHSpace4, NSMidY(b) - ts.height / 2) withAttributes:at];
    }];
}
@end

/* Capacity meter: track plus fill, colored by how full it is. The numbers are always printed next
 * to it, so color is only a reinforcement. */
@interface MHMeterBar : NSView
@property(nonatomic) double value, maximum;
@end
@implementation MHMeterBar
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        self.translatesAutoresizingMaskIntoConstraints = NO;
        self.accessibilityRole = NSAccessibilityLevelIndicatorRole;
    }
    return self;
}
- (NSSize)intrinsicContentSize { return NSMakeSize(NSViewNoIntrinsicMetric, 8); }
+ (NSColor *)colorForRatio:(double)r {
    return r > 1.0 ? MHTheme.statusError : r >= 0.85 ? MHTheme.statusReview : MHTheme.brandFill;
}
- (void)setValue:(double)v { _value = v; self.needsDisplay = YES; }
- (void)setMaximum:(double)m { _maximum = m; self.needsDisplay = YES; }
- (void)drawRect:(NSRect)r {
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        NSRect b = self.bounds;
        CGFloat rad = b.size.height / 2;
        [[NSColor.labelColor colorWithAlphaComponent:MHTheme.increaseContrast ? 0.28 : 0.12] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:b xRadius:rad yRadius:rad] fill];
        double ratio = self->_maximum > 0 ? self->_value / self->_maximum : 0;
        if (ratio <= 0) return;
        NSRect f = b;
        f.size.width = MAX(b.size.height, b.size.width * MIN(ratio, 1.0));
        [[MHMeterBar colorForRatio:ratio] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:f xRadius:rad yRadius:rad] fill];
    }];
}
@end

@interface MHMeterRow : NSView
@property(readonly) NSTextField *titleLabel, *valueLabel;
@property(readonly) NSImageView *stateIcon;
@property(readonly) MHMeterBar *bar;
- (instancetype)initWithTitle:(NSString *)title;
- (void)setValue:(double)v maximum:(double)m text:(NSString *)text;
@end
@implementation MHMeterRow
- (instancetype)initWithTitle:(NSString *)title {
    if ((self = [super initWithFrame:NSZeroRect])) {
        self.translatesAutoresizingMaskIntoConstraints = NO;
        _titleLabel = [NSTextField labelWithString:title];
        _titleLabel.font = MHTheme.headlineFont;
        _titleLabel.textColor = MHTheme.textPrimary;
        _valueLabel = [NSTextField labelWithString:@""];
        _valueLabel.font = MHTheme.captionFont;
        _valueLabel.textColor = MHTheme.textPrimary;
        _valueLabel.alignment = NSTextAlignmentRight;
        _stateIcon = [[NSImageView alloc] init];
        _stateIcon.imageScaling = NSImageScaleProportionallyDown;
        _bar = [[MHMeterBar alloc] init];
        _bar.accessibilityLabel = title;
        for (NSView *v in @[ _titleLabel, _valueLabel, _stateIcon, _bar ]) {
            v.translatesAutoresizingMaskIntoConstraints = NO;
            [self addSubview:v];
        }
        _titleLabel.accessibilityElement = NO;
        _valueLabel.accessibilityElement = NO;
        _stateIcon.accessibilityElement = NO;
        [_titleLabel setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
        [_valueLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [NSLayoutConstraint activateConstraints:@[
            [_titleLabel.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_titleLabel.topAnchor constraintEqualToAnchor:self.topAnchor],
            [_valueLabel.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_valueLabel.centerYAnchor constraintEqualToAnchor:_titleLabel.centerYAnchor],
            [_valueLabel.leadingAnchor constraintGreaterThanOrEqualToAnchor:_stateIcon.trailingAnchor constant:MHSpace4],
            [_stateIcon.trailingAnchor constraintEqualToAnchor:_valueLabel.leadingAnchor constant:-MHSpace4],
            [_stateIcon.centerYAnchor constraintEqualToAnchor:_titleLabel.centerYAnchor],
            [_stateIcon.widthAnchor constraintEqualToConstant:14],
            [_stateIcon.heightAnchor constraintEqualToConstant:14],
            [_stateIcon.leadingAnchor constraintGreaterThanOrEqualToAnchor:_titleLabel.trailingAnchor constant:MHSpace8],
            [_bar.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_bar.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_bar.topAnchor constraintEqualToAnchor:_titleLabel.bottomAnchor constant:MHSpace4],
            [_bar.heightAnchor constraintEqualToConstant:8],
            [_bar.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
    }
    return self;
}
- (void)setValue:(double)v maximum:(double)m text:(NSString *)text {
    _bar.value = v;
    _bar.maximum = m;
    double ratio = m > 0 ? v / m : 0;
    NSString *state = ratio > 1.0 ? @"over the limit" : ratio >= 1.0 ? @"limit reached" : ratio >= 0.85 ? @"near the limit" : nil;
    NSString *full = state ? [NSString stringWithFormat:@"%@, %@", text, state] : text;
    _valueLabel.stringValue = full;
    if (state) {
        BOOL bad = ratio >= 1.0;
        _stateIcon.image = [MHTheme symbol:bad ? @"xmark.octagon.fill" : @"exclamationmark.triangle.fill" size:11
                                     color:bad ? MHTheme.statusError : MHTheme.statusReview description:nil];
        _stateIcon.hidden = NO;
    } else {
        _stateIcon.image = nil;
        _stateIcon.hidden = YES;
    }
    _bar.accessibilityValue = full;
}
@end

/* Inline message line: icon plus wrapping text, two lines reserved so the layout does not jump. */
typedef NS_ENUM(NSInteger, MHMsgKind) { MHMsgNeutral, MHMsgSuccess, MHMsgError };

@interface MHMessageView : NSView
- (void)setText:(NSString *)text kind:(MHMsgKind)kind;
@end
@implementation MHMessageView {
    NSImageView *_icon;
    NSTextField *_label;
}
- (instancetype)init {
    if ((self = [super initWithFrame:NSZeroRect])) {
        self.translatesAutoresizingMaskIntoConstraints = NO;
        _icon = [[NSImageView alloc] init];
        _icon.imageScaling = NSImageScaleProportionallyDown;
        _icon.translatesAutoresizingMaskIntoConstraints = NO;
        _icon.accessibilityElement = NO;
        _label = [NSTextField wrappingLabelWithString:@""];
        _label.translatesAutoresizingMaskIntoConstraints = NO;
        _label.font = MHTheme.bodyFont;
        _label.selectable = YES;
        _label.maximumNumberOfLines = 2;
        [self addSubview:_icon];
        [self addSubview:_label];
        [NSLayoutConstraint activateConstraints:@[
            [_icon.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_icon.topAnchor constraintEqualToAnchor:self.topAnchor constant:1],
            [_icon.widthAnchor constraintEqualToConstant:16],
            [_icon.heightAnchor constraintEqualToConstant:16],
            [_label.leadingAnchor constraintEqualToAnchor:_icon.trailingAnchor constant:MHSpace8],
            [_label.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_label.topAnchor constraintEqualToAnchor:self.topAnchor],
            [self.heightAnchor constraintGreaterThanOrEqualToConstant:36],
            [_label.bottomAnchor constraintLessThanOrEqualToAnchor:self.bottomAnchor],
        ]];
        self.accessibilityRole = NSAccessibilityStaticTextRole;
    }
    return self;
}
- (void)setText:(NSString *)text kind:(MHMsgKind)kind {
    NSString *sym = kind == MHMsgError ? @"xmark.octagon.fill" : kind == MHMsgSuccess ? @"checkmark.circle.fill" : @"info.circle.fill";
    NSColor *c = kind == MHMsgError ? MHTheme.statusError : kind == MHMsgSuccess ? MHTheme.statusVerified : MHTheme.textSecondary;
    _icon.image = [MHTheme symbol:sym size:14 color:c description:nil];
    _label.stringValue = text ?: @"";
    _label.textColor = kind == MHMsgNeutral ? MHTheme.textSecondary : MHTheme.textPrimary;
    self.accessibilityLabel = [(kind == MHMsgError ? @"Error: " : @"") stringByAppendingString:text ?: @""];
}
@end

/* Label that lets clicks through (placeholder over the code box). */
@interface MHPassLabel : NSTextField
@end
@implementation MHPassLabel
- (NSView *)hitTest:(NSPoint)p { return nil; }
@end

/* Tab key leaves the code box (keyboard users must not be trapped in it). */
@interface MHCodeView : NSTextView
@end
@implementation MHCodeView
- (void)insertTab:(id)sender { [self.window selectNextKeyView:self]; }
- (void)insertBacktab:(id)sender { [self.window selectPreviousKeyView:self]; }
@end

@interface MHTable : NSTableView
@property(copy) void (^onDelete)(void);
@property(copy) void (^onSpace)(void);
@end
@implementation MHTable
- (void)keyDown:(NSEvent *)e {
    NSString *c = e.charactersIgnoringModifiers;
    unichar ch = c.length ? [c characterAtIndex:0] : 0;
    if ((ch == NSDeleteFunctionKey || ch == NSDeleteCharacter) && self.onDelete) { self.onDelete(); return; }
    if (ch == ' ' && !(e.modifierFlags & NSEventModifierFlagCommand) && self.onSpace) { self.onSpace(); return; }
    [super keyDown:e];
}
- (NSMenu *)menuForEvent:(NSEvent *)e {
    NSInteger r = [self rowAtPoint:[self convertPoint:e.locationInWindow fromView:nil]];
    if (r >= 0 && ![self.selectedRowIndexes containsIndex:r]) [self selectRowIndexes:[NSIndexSet indexSetWithIndex:r] byExtendingSelection:NO];
    return self.menu;
}
@end

/* orig: ui_header_proc, double click on the "Name" header sorts by name. */
@interface MHHeader : NSTableHeaderView
@property(copy) void (^onDouble)(NSInteger col);
@end
@implementation MHHeader
- (void)mouseDown:(NSEvent *)e {
    if (e.clickCount == 2 && self.onDouble) {
        NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
        self.onDouble([self columnAtPoint:p]);
        return;
    }
    [super mouseDown:e];
}
@end

@interface MHRow : NSTableRowView
@property(copy) NSString *axLabel;
@end
@implementation MHRow
- (NSString *)accessibilityLabel { return _axLabel ?: [super accessibilityLabel]; }
@end

/* Table cell: kind "act" (switch), "name" (lock + text), "src" (chip), "text"/"num" (labels). */
@interface MHCell : NSTableCellView
@property(strong) NSSwitch *toggle;
@property(strong) NSImageView *lock;
@property(strong) NSLayoutConstraint *lockW;
@property(strong) MHSourceChip *chip;
+ (MHCell *)cellOfKind:(NSString *)kind;
@end
@implementation MHCell
+ (MHCell *)cellOfKind:(NSString *)kind {
    MHCell *c = [[MHCell alloc] initWithFrame:NSZeroRect];
    c.identifier = kind;
    if ([kind isEqualToString:@"act"]) {
        c.toggle = [[NSSwitch alloc] init];
        c.toggle.controlSize = NSControlSizeSmall;
        c.toggle.translatesAutoresizingMaskIntoConstraints = NO;
        [c addSubview:c.toggle];
        [NSLayoutConstraint activateConstraints:@[
            [c.toggle.centerXAnchor constraintEqualToAnchor:c.centerXAnchor],
            [c.toggle.centerYAnchor constraintEqualToAnchor:c.centerYAnchor] ]];
        return c;
    }
    if ([kind isEqualToString:@"src"]) {
        c.chip = [[MHSourceChip alloc] init];
        [c addSubview:c.chip];
        [NSLayoutConstraint activateConstraints:@[
            [c.chip.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:MHSpace4],
            [c.chip.centerYAnchor constraintEqualToAnchor:c.centerYAnchor] ]];
        return c;
    }
    NSTextField *t = [NSTextField labelWithString:@""];
    t.translatesAutoresizingMaskIntoConstraints = NO;
    t.lineBreakMode = NSLineBreakByTruncatingTail;
    t.cell.truncatesLastVisibleLine = YES;
    [t setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [c addSubview:t];
    c.textField = t;
    if ([kind isEqualToString:@"name"]) {
        t.font = MHTheme.bodyFont;
        c.lock = [[NSImageView alloc] init];
        c.lock.imageScaling = NSImageScaleProportionallyDown;
        c.lock.translatesAutoresizingMaskIntoConstraints = NO;
        c.lock.image = [MHTheme symbol:@"lock.fill" size:11 description:@"Required by the source"];
        c.lock.contentTintColor = MHTheme.textSecondary;
        c.lock.toolTip = @"Required by the source";
        [c addSubview:c.lock];
        c.lockW = [c.lock.widthAnchor constraintEqualToConstant:14];
        [NSLayoutConstraint activateConstraints:@[
            [c.lock.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:MHSpace4],
            [c.lock.centerYAnchor constraintEqualToAnchor:c.centerYAnchor],
            c.lockW,
            [t.leadingAnchor constraintEqualToAnchor:c.lock.trailingAnchor constant:MHSpace4],
            [t.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-MHSpace4],
            [t.centerYAnchor constraintEqualToAnchor:c.centerYAnchor] ]];
    } else {
        BOOL num = [kind isEqualToString:@"num"];
        t.font = num ? MHTheme.monoFont : MHTheme.bodyFont;
        t.alignment = num ? NSTextAlignmentRight : NSTextAlignmentLeft;
        [NSLayoutConstraint activateConstraints:@[
            [t.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:MHSpace4],
            [t.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-MHSpace8],
            [t.centerYAnchor constraintEqualToAnchor:c.centerYAnchor] ]];
    }
    return c;
}
- (void)setBackgroundStyle:(NSBackgroundStyle)s {
    [super setBackgroundStyle:s];
    BOOL emph = s == NSBackgroundStyleEmphasized;
    self.chip.emphasized = emph;
    self.lock.contentTintColor = emph ? NSColor.whiteColor : MHTheme.textSecondary;
}
@end

static NSTextField *Txt(NSString *s, NSFont *f, NSColor *c, BOOL wrap) {
    NSTextField *t = wrap ? [NSTextField wrappingLabelWithString:s] : [NSTextField labelWithString:s];
    t.translatesAutoresizingMaskIntoConstraints = NO;
    t.font = f;
    t.textColor = c;
    return t;
}

static NSButton *Btn(NSString *title, NSString *symbol, id target, SEL action) {
    NSButton *b = [NSButton buttonWithTitle:title target:target action:action];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    b.bezelStyle = NSBezelStyleRounded;
    b.font = MHTheme.bodyFont;
    if (symbol) {
        b.image = [MHTheme symbol:symbol size:13 description:title];
        b.imagePosition = NSImageLeading;
        b.imageHugsTitle = YES;
    }
    b.accessibilityLabel = [title stringByReplacingOccurrencesOfString:@"..." withString:@""];
    [b.heightAnchor constraintGreaterThanOrEqualToConstant:MHMinHit].active = YES;
    return b;
}
/* Primary action: brand fill, answers Return. */
static void Prominent(NSButton *b) {
    b.bezelColor = MHTheme.brandFill;
    b.keyEquivalent = @"\r";
}
static void Cancelling(NSButton *b) { b.keyEquivalent = @"\033"; }
static void Key(NSButton *b, NSString *key, NSEventModifierFlags mods) {
    b.keyEquivalent = key;
    b.keyEquivalentModifierMask = mods;
}

static NSStackView *Row(NSArray<NSView *> *views, CGFloat spacing) {
    NSStackView *s = [NSStackView stackViewWithViews:views];
    s.translatesAutoresizingMaskIntoConstraints = NO;
    s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    s.alignment = NSLayoutAttributeCenterY;
    s.spacing = spacing;
    return s;
}

static NSBox *Hairline(void) {
    NSBox *b = [[NSBox alloc] init];
    b.boxType = NSBoxSeparator;
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

static NSSearchField *SearchField(NSString *placeholder, id target, SEL action) {
    NSSearchField *f = [[NSSearchField alloc] init];
    f.translatesAutoresizingMaskIntoConstraints = NO;
    f.placeholderString = placeholder;
    f.font = MHTheme.bodyFont;
    f.target = target;
    f.action = action;
    f.sendsSearchStringImmediately = YES;
    f.accessibilityLabel = placeholder;
    return f;
}

/* Read-only rich text in a scroll view with no chrome (details panes). */
static NSScrollView *DetailBox(NSTextView **out, NSString *axLabel) {
    NSScrollView *sv = [[NSScrollView alloc] init];
    sv.translatesAutoresizingMaskIntoConstraints = NO;
    sv.hasVerticalScroller = YES;
    sv.autohidesScrollers = YES;
    sv.drawsBackground = NO;
    sv.borderType = NSNoBorder;
    NSTextView *t = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    t.minSize = NSMakeSize(0, 0);
    t.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    t.verticallyResizable = YES;
    t.horizontallyResizable = NO;
    t.autoresizingMask = NSViewWidthSizable;
    t.textContainer.widthTracksTextView = YES;
    t.drawsBackground = NO;
    t.editable = NO;
    t.selectable = YES;
    t.richText = NO;
    t.textContainerInset = NSMakeSize(MHSpace8, MHSpace8);
    t.accessibilityLabel = axLabel;
    sv.documentView = t;
    *out = t;
    return sv;
}

static NSDictionary *Attrs(NSFont *f, NSColor *c, CGFloat after) {
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.paragraphSpacing = after;
    ps.lineBreakMode = NSLineBreakByWordWrapping;
    return @{NSFontAttributeName : f, NSForegroundColorAttributeName : c, NSParagraphStyleAttributeName : ps};
}
static void AddRich(NSMutableAttributedString *m, NSString *s, NSFont *f, NSColor *c, CGFloat after) {
    [m appendAttributedString:[[NSAttributedString alloc] initWithString:[s stringByAppendingString:@"\n"] attributes:Attrs(f, c, after)]];
}
/* "caption" heading followed by a body paragraph. */
static void AddSection(NSMutableAttributedString *m, NSString *head, NSString *body, BOOL mono) {
    AddRich(m, head, [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold], MHTheme.textSecondary, 2);
    AddRich(m, body, mono ? MHTheme.monoFont : MHTheme.bodyFont, MHTheme.textPrimary, 8);
}

static void Show(NSTextView *tv, NSAttributedString *a) {
    [tv.textStorage setAttributedString:a];
    [tv scrollRangeToVisible:NSMakeRange(0, 0)];
}

static NSScrollView *CodeEditorBox(MHCodeView **out) {
    NSScrollView *sv = [[NSScrollView alloc] init];
    sv.translatesAutoresizingMaskIntoConstraints = NO;
    sv.hasVerticalScroller = YES;
    sv.autohidesScrollers = YES;
    sv.borderType = NSNoBorder;
    sv.drawsBackground = YES;
    sv.backgroundColor = NSColor.textBackgroundColor;
    MHCodeView *t = [[MHCodeView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    t.minSize = NSMakeSize(0, 0);
    t.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    t.verticallyResizable = YES;
    t.horizontallyResizable = NO;
    t.autoresizingMask = NSViewWidthSizable;
    t.textContainer.widthTracksTextView = YES;
    t.font = MHTheme.monoFont;
    t.textColor = NSColor.textColor;
    t.backgroundColor = NSColor.textBackgroundColor;
    t.richText = NO;
    t.allowsUndo = YES;
    t.automaticQuoteSubstitutionEnabled = NO;
    t.automaticDashSubstitutionEnabled = NO;
    t.automaticTextReplacementEnabled = NO;
    t.automaticSpellingCorrectionEnabled = NO;
    t.continuousSpellCheckingEnabled = NO;
    t.textContainerInset = NSMakeSize(MHSpace8, MHSpace8);
    t.accessibilityLabel = @"Codes";
    t.accessibilityHelp = @"One code per line, for example 8007A2B0 0063. Press Tab to move to the next control.";
    sv.documentView = t;
    *out = t;
    return sv;
}

/* A grid of rows wrapped in a bordered card. */
static MHCard *TableCard(NSTableView *t, NSScrollView **svOut) {
    NSScrollView *sv = [[NSScrollView alloc] init];
    sv.translatesAutoresizingMaskIntoConstraints = NO;
    sv.documentView = t;
    sv.hasVerticalScroller = YES;
    sv.autohidesScrollers = YES;
    sv.borderType = NSNoBorder;
    sv.drawsBackground = NO;
    MHCard *card = [[MHCard alloc] init];
    card.fill = NSColor.controlBackgroundColor;
    [card addSubview:sv];
    [NSLayoutConstraint activateConstraints:@[
        [sv.leadingAnchor constraintEqualToAnchor:card.leadingAnchor],
        [sv.trailingAnchor constraintEqualToAnchor:card.trailingAnchor],
        [sv.topAnchor constraintEqualToAnchor:card.topAnchor],
        [sv.bottomAnchor constraintEqualToAnchor:card.bottomAnchor] ]];
    *svOut = sv;
    return card;
}

/* cols: @[@[ident, title, @(width), @(min), @(flex), @(rightAligned)]] */
static MHTable *MakeTable(NSArray<NSArray *> *cols, BOOL multi, id owner, NSString *axLabel) {
    MHTable *t = [[MHTable alloc] init];
    t.dataSource = owner;
    t.delegate = owner;
    t.allowsMultipleSelection = multi;
    t.allowsColumnReordering = NO;
    t.allowsColumnSelection = NO;
    t.allowsEmptySelection = YES;
    t.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    t.rowHeight = 36;
    t.intercellSpacing = NSMakeSize(0, 0);
    t.usesAlternatingRowBackgroundColors = YES;
    t.gridStyleMask = NSTableViewGridNone;
    t.style = NSTableViewStyleFullWidth;
    t.focusRingType = NSFocusRingTypeExterior;
    t.accessibilityLabel = axLabel;
    MHHeader *h = [[MHHeader alloc] initWithFrame:NSMakeRect(0, 0, 100, 28)];
    t.headerView = h;
    for (NSArray *c in cols) {
        NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:c[0]];
        col.title = c[1];
        col.width = [c[2] doubleValue];
        col.minWidth = [c[3] doubleValue];
        col.resizingMask = [c[4] boolValue] ? (NSTableColumnAutoresizingMask | NSTableColumnUserResizingMask) : NSTableColumnUserResizingMask;
        col.headerCell.font = MHTheme.headlineFont;
        if ([c[5] boolValue]) col.headerCell.alignment = NSTextAlignmentRight;
        if ([c[0] isEqualToString:@"act"]) col.headerCell.alignment = NSTextAlignmentCenter;
        [t addTableColumn:col];
    }
    return t;
}

static void CenterOn(NSWindow *w, NSWindow *parent) {
    if (!parent) { [w center]; return; }
    NSRect pf = parent.frame, f = w.frame;
    [w setFrameOrigin:NSMakePoint(NSMidX(pf) - f.size.width / 2, NSMidY(pf) - f.size.height / 2)];
}

static NSWindow *NewWindow(NSString *title, NSSize content, NSSize minContent, BOOL resizable, BOOL closable) {
    NSWindowStyleMask m = NSWindowStyleMaskTitled;
    if (closable) m |= NSWindowStyleMaskClosable;
    if (resizable) m |= NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, content.width, content.height)
                                              styleMask:m
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.title = title;
    w.releasedWhenClosed = NO;
    if (resizable) w.contentMinSize = minContent;
    return w;
}

static void Pin(NSView *v, NSView *sup, CGFloat l, CGFloat t, CGFloat r, CGFloat b) {
    [NSLayoutConstraint activateConstraints:@[
        [v.leadingAnchor constraintEqualToAnchor:sup.leadingAnchor constant:l],
        [v.trailingAnchor constraintEqualToAnchor:sup.trailingAnchor constant:-r],
        [v.topAnchor constraintEqualToAnchor:sup.topAnchor constant:t],
        [v.bottomAnchor constraintEqualToAnchor:sup.bottomAnchor constant:-b] ]];
}

/* Text of the lines of one entry. A classic origin shows the native 0x70/0x90 conditions as D0/D1. */
static NSString *CodeText(const uint8_t *lines, uint16_t n, uint8_t flags) {
    if (!lines || !n) return @"";
    MhWbuf b;
    mh_wbuf_init(&b);
    if (!(flags & 0x40)) {
        mh_cheat_codes_to_text(lines, n, &b);
    } else {
        uint8_t *tmp = malloc((size_t)n * 6);
        memcpy(tmp, lines, (size_t)n * 6);
        for (uint16_t i = 0; i < n; i++) {
            if (tmp[i * 6] == 0x70) tmp[i * 6] = 0xD0;
            else if (tmp[i * 6] == 0x90) tmp[i * 6] = 0xD1;
        }
        mh_cheat_codes_to_text(tmp, n, &b);
        free(tmp);
    }
    NSString *s = S(mh_wbuf_cstr(&b));
    mh_wbuf_free(&b);
    return [s stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
}

/* ---- Cheat Editor (orig: editor_proc, editor_save, run_editor) ---------------------------------- */

@interface MHCheatEditor : NSObject <NSTextViewDelegate, NSTextFieldDelegate, MHCheatUITestable>
@property NSWindow *window, *parent;
@property NSTextField *nameField, *countLabel, *placeholder;
@property NSPopUpButton *version;
@property MHCodeView *codes;
@property MHMessageView *message;
@property NSButton *saveButton;
@property BOOL accepted;
@end

@implementation MHCheatEditor {
    char _outName[MH_CHEAT_NAME_MAX];
    uint8_t *_outLines;
    uint16_t _outN;
    uint8_t _outFlags;
    NSString *_initName;
    const uint8_t *_lines;
    uint16_t _n;
    uint8_t _flags;
    BOOL _editing;
}
- (NSWindow *)testWindow { return _window; }
- (const MhCheatProfile *)testProfile { return NULL; }
- (void)dealloc { free(_outLines); }

- (instancetype)initWithParent:(NSWindow *)parent name:(const char *)name lines:(const uint8_t *)lines n:(uint16_t)n
                         flags:(uint8_t)flags {
    if ((self = [super init])) {
        _parent = parent;
        _initName = name ? S(name) : @"";
        _lines = lines;
        _n = n;
        _flags = flags;
        _editing = lines != NULL;
        [self build];
    }
    return self;
}

- (void)build {
    NSWindow *w = NewWindow(_editing ? @"Edit Cheat" : @"New Cheat", NSMakeSize(640, 600), NSZeroSize, NO, NO);
    NSView *cv = w.contentView;

    NSTextField *title = Txt(_editing ? @"Edit Cheat" : @"New Cheat", MHTheme.titleFont, MHTheme.textPrimary, NO);
    title.accessibilityRole = NSAccessibilityStaticTextRole;
    NSTextField *desc = Txt(@"Choose the code format. PicoStation Manager converts classic codes to V5 when it writes the card, but "
                             "remembers where they came from: a classic cheat is still shown as classic when you edit it again.",
                            MHTheme.captionFont, MHTheme.textSecondary, YES);
    desc.maximumNumberOfLines = 3;

    NSTextField *nameL = Txt(@"Name", MHTheme.headlineFont, MHTheme.textPrimary, NO);
    _nameField = [[NSTextField alloc] init];
    _nameField.translatesAutoresizingMaskIntoConstraints = NO;
    _nameField.font = MHTheme.bodyFont;
    _nameField.stringValue = _initName;
    _nameField.placeholderString = @"Cheat name";
    _nameField.delegate = self;
    _nameField.accessibilityLabel = @"Name";
    _nameField.accessibilityHelp = @"Up to 63 characters. Names are stored in upper case.";
    NSTextField *nameH = Txt(@"Up to 63 characters, stored in upper case.", MHTheme.captionFont, MHTheme.textSecondary, NO);

    NSTextField *verL = Txt(@"Code format", MHTheme.headlineFont, MHTheme.textPrimary, NO);
    _version = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _version.translatesAutoresizingMaskIntoConstraints = NO;
    _version.font = MHTheme.bodyFont;
    [_version addItemWithTitle:@"GameShark V5 / Xploder V4 (native)"];
    [_version addItemWithTitle:@"GameShark / Action Replay classic (convert)"];
    [_version selectItemAtIndex:_lines ? ((_flags >> 6) & 1) : 1];
    _version.target = self;
    _version.action = @selector(formatChanged:);
    _version.accessibilityLabel = @"Code format";

    NSTextField *codesL = Txt(@"Codes", MHTheme.headlineFont, MHTheme.textPrimary, NO);
    _countLabel = Txt(@"0 lines", MHTheme.captionFont, MHTheme.textSecondary, NO);
    _countLabel.alignment = NSTextAlignmentRight;
    _countLabel.accessibilityLabel = @"Line count";
    MHCodeView *codesTV;
    NSScrollView *codesSV = CodeEditorBox(&codesTV);
    _codes = codesTV;
    _codes.string = CodeText(_lines, _n, _flags);
    _codes.delegate = self;
    MHCard *codesCard = [[MHCard alloc] init];
    codesCard.fill = NSColor.textBackgroundColor;
    [codesCard addSubview:codesSV];
    Pin(codesSV, codesCard, 0, 0, 0, 0);
    _placeholder = [MHPassLabel wrappingLabelWithString:@"One code per line, for example\n8007A2B0 0063"];
    _placeholder.translatesAutoresizingMaskIntoConstraints = NO;
    _placeholder.font = MHTheme.monoFont;
    _placeholder.textColor = MHTheme.textSecondary;
    _placeholder.accessibilityElement = NO;
    [codesCard addSubview:_placeholder];
    [NSLayoutConstraint activateConstraints:@[
        [_placeholder.leadingAnchor constraintEqualToAnchor:codesCard.leadingAnchor constant:MHSpace8 + 5],
        [_placeholder.topAnchor constraintEqualToAnchor:codesCard.topAnchor constant:MHSpace8],
        [_placeholder.trailingAnchor constraintEqualToAnchor:codesCard.trailingAnchor constant:-MHSpace8] ]];

    _message = [[MHMessageView alloc] init];

    NSButton *cancel = Btn(@"Cancel", nil, self, @selector(cancel:));
    Cancelling(cancel);
    cancel.controlSize = NSControlSizeLarge;
    _saveButton = Btn(@"Save", nil, self, @selector(save:));
    Prominent(_saveButton);
    _saveButton.controlSize = NSControlSizeLarge;
    NSStackView *buttons = Row(@[ cancel, _saveButton ], MHSpace12);

    for (NSView *v in @[ title, desc, nameL, _nameField, nameH, verL, _version, codesL, _countLabel, codesCard, _message, buttons ])
        [cv addSubview:v];
    CGFloat m = MHSpace24;
    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [title.topAnchor constraintEqualToAnchor:cv.topAnchor constant:m],
        [title.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [desc.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [desc.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [desc.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:MHSpace8],
        [nameL.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [nameL.topAnchor constraintEqualToAnchor:desc.bottomAnchor constant:MHSpace16],
        [_nameField.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [_nameField.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [_nameField.topAnchor constraintEqualToAnchor:nameL.bottomAnchor constant:MHSpace4],
        [_nameField.heightAnchor constraintGreaterThanOrEqualToConstant:MHMinHit],
        [nameH.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [nameH.topAnchor constraintEqualToAnchor:_nameField.bottomAnchor constant:MHSpace4],
        [verL.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [verL.topAnchor constraintEqualToAnchor:nameH.bottomAnchor constant:MHSpace16],
        [_version.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [_version.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [_version.topAnchor constraintEqualToAnchor:verL.bottomAnchor constant:MHSpace4],
        [_version.heightAnchor constraintGreaterThanOrEqualToConstant:MHMinHit],
        [codesL.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [codesL.topAnchor constraintEqualToAnchor:_version.bottomAnchor constant:MHSpace16],
        [_countLabel.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [_countLabel.centerYAnchor constraintEqualToAnchor:codesL.centerYAnchor],
        [codesCard.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [codesCard.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [codesCard.topAnchor constraintEqualToAnchor:codesL.bottomAnchor constant:MHSpace4],
        [codesCard.heightAnchor constraintGreaterThanOrEqualToConstant:140],
        [codesCard.heightAnchor constraintEqualToConstant:200],
        [_message.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [_message.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [_message.topAnchor constraintEqualToAnchor:codesCard.bottomAnchor constant:MHSpace12],
        [buttons.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [buttons.topAnchor constraintEqualToAnchor:_message.bottomAnchor constant:MHSpace16],
        [buttons.bottomAnchor constraintEqualToAnchor:cv.bottomAnchor constant:-m],
        [cancel.widthAnchor constraintGreaterThanOrEqualToConstant:88],
        [_saveButton.widthAnchor constraintGreaterThanOrEqualToConstant:88],
    ]];
    _nameField.nextKeyView = _version;
    _version.nextKeyView = _codes;
    _codes.nextKeyView = cancel;
    cancel.nextKeyView = _saveButton;
    _saveButton.nextKeyView = _nameField;
    w.initialFirstResponder = _nameField;
    [cv.widthAnchor constraintEqualToConstant:640].active = YES;
    [w setContentSize:cv.fittingSize];
    _window = w;
    gEditor = self;
    [self updateLive];
}

- (void)controlTextDidChange:(NSNotification *)n { [self updateLive]; }
- (void)textDidChange:(NSNotification *)n { [self updateLive]; }
- (void)formatChanged:(id)sender { [self updateLive]; }

- (void)cancel:(id)sender { EndSheet(_window); }

/* orig: editor_save.isra.0. Parses the box into _out*. Returns nil, or the message to show.
 * final == NO is the live pass while typing: it does not demand a name or any code yet. */
- (NSString *)compute:(BOOL)final {
    free(_outLines);
    _outLines = NULL;
    _outN = 0;
    NSInteger sel = _version.indexOfSelectedItem;
    const char *dialect;
    if (sel == 0) dialect = DIALECT_V5;
    else if (sel == 1) dialect = DIALECT_CLASSIC;
    else return @"Choose the code format.";

    char name[MH_CHEAT_NAME_MAX];
    NSString *nm = _nameField.stringValue;
    if (nm.length > 255) nm = [nm substringToIndex:255];
    mh_normalize_title(nm.UTF8String, name, sizeof name);
    if (final && !name[0]) return @"Enter a name for the cheat.";

    NSString *text = [_codes.string stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
    text = [text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r\n"];
    BOOL blank = [[text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] length] == 0;
    if (blank) return final ? @"Enter the cheat codes." : nil;
    if (text.length >= 0xFFFF) return @"Enter the cheat codes.";

    const char *p = text.UTF8String;
    const char *rest = p;
    if (mh_str_starts_i(p, "MHDialect=")) {
        const char *e = p;
        while (*e && *e != '\r' && *e != '\n') e++;
        char head[256];
        size_t hl = (size_t)(e - p);
        if (hl >= sizeof head) hl = sizeof head - 1;
        memcpy(head, p, hl);
        head[hl] = 0;
        if (mh_str_icmp(head, dialect) != 0) return @"The format written in the text does not match the code format chosen above.";
        rest = e;
        while (*rest == '\r' || *rest == '\n') rest++;
    }
    MhWbuf b;
    mh_wbuf_init(&b);
    mh_wbuf_puts(&b, dialect);
    mh_wbuf_puts(&b, "\r\n");
    mh_wbuf_puts(&b, rest);
    uint8_t *lines = NULL;
    uint16_t n = 0;
    char err[256] = "";
    int ok = mh_cheat_parse_codes(mh_wbuf_cstr(&b), &lines, &n, err, sizeof err);
    mh_wbuf_free(&b);
    if (!ok) return S(err).length ? S(err) : @"The codes could not be read.";
    mh_str_copy(_outName, sizeof _outName, name);
    _outLines = lines;
    _outN = n;
    _outFlags = sel == 1 ? 0x40 : 0;
    return nil;
}

/* line count, placeholder and the inline validation message, refreshed on every change */
- (void)updateLive {
    NSString *t = _codes.string;
    NSUInteger cnt = 0;
    for (NSString *ln in [t componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSString *s = [ln stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (s.length && ![s.lowercaseString hasPrefix:@"mhdialect="]) cnt++;
    }
    _countLabel.stringValue = [NSString stringWithFormat:@"%lu %@", (unsigned long)cnt, cnt == 1 ? @"line" : @"lines"];
    _placeholder.hidden = t.length > 0;
    NSString *err = [self compute:NO];
    if (err) [_message setText:err kind:MHMsgError];
    else if (_outN) [_message setText:[NSString stringWithFormat:@"%u %@ valid. Ready to save.", _outN, _outN == 1 ? @"line is" : @"lines are"] kind:MHMsgSuccess];
    else [_message setText:@"Enter one code per line, for example 8007A2B0 0063. A first line like MHDialect=... is optional." kind:MHMsgNeutral];
}

- (void)save:(id)sender {
    NSString *err = [self compute:YES];
    if (err) {
        [_message setText:err kind:MHMsgError];
        Announce(err);
        BOOL nameProblem = [err isEqualToString:@"Enter a name for the cheat."];
        [_window makeFirstResponder:nameProblem ? _nameField : _codes];
        return;
    }
    _accepted = YES;
    EndSheet(_window);
}

- (void)testAction:(NSString *)a argument:(id)arg {
    if ([a isEqualToString:@"name"]) { _nameField.stringValue = arg; [self updateLive]; }
    else if ([a isEqualToString:@"codes"]) { _codes.string = arg; [self updateLive]; }
    else if ([a isEqualToString:@"version"]) { [_version selectItemAtIndex:[arg integerValue]]; [self updateLive]; }
    else if ([a isEqualToString:@"save"]) [self save:nil];
    else if ([a isEqualToString:@"cancel"]) [self cancel:nil];
}

/* orig: run_editor. outLines is malloc'ed (mh_free) on YES. */
+ (BOOL)runWithParent:(NSWindow *)parent name:(const char *)name lines:(const uint8_t *)lines n:(uint16_t)n
                flags:(uint8_t)flags outName:(char *)outName outLines:(uint8_t **)outLines outN:(uint16_t *)outN
             outFlags:(uint8_t *)outFlags {
    MHCheatEditor *ed = [[MHCheatEditor alloc] initWithParent:parent name:name lines:lines n:n flags:flags];
    if (!parent) CenterOn(ed.window, nil);
    RunSheet(parent, ed.window);
    if (!ed->_accepted) return NO;
    mh_str_copy(outName, MH_CHEAT_NAME_MAX, ed->_outName);
    *outLines = ed->_outLines;
    ed->_outLines = NULL;
    *outN = ed->_outN;
    *outFlags = ed->_outFlags;
    return YES;
}
@end

/* Save panel format chooser: orig filters "Cheats (*.cht)" and "Text (*.txt)", default extension cht. */
@interface MHFormatPicker : NSObject
@property(weak) NSSavePanel *panel;
- (void)changed:(NSPopUpButton *)pop;
@end
@implementation MHFormatPicker
- (void)changed:(NSPopUpButton *)pop {
    NSString *ext = pop.indexOfSelectedItem == 0 ? @"cht" : @"txt";
    UTType *t = [UTType typeWithFilenameExtension:ext];
    if (t) _panel.allowedContentTypes = @[ t ];
    _panel.nameFieldStringValue = [[_panel.nameFieldStringValue stringByDeletingPathExtension] stringByAppendingPathExtension:ext];
}
@end

static NSURL *RunPanel(BOOL save, NSString *title, NSString *message) {
    if (MHCheatUIPanelHook) return MHCheatUIPanelHook(save);
    UTType *cht = [UTType typeWithFilenameExtension:@"cht"], *txt = [UTType typeWithFilenameExtension:@"txt"];
    NSSavePanel *p;
    MHFormatPicker *pick = nil;
    if (save) {
        p = [NSSavePanel savePanel];
        p.canCreateDirectories = YES;
        p.nameFieldStringValue = @"cheats.cht";
        p.prompt = @"Export";
        if (cht) p.allowedContentTypes = @[ cht ];
        pick = [[MHFormatPicker alloc] init];
        pick.panel = p;
        NSPopUpButton *pop = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        pop.translatesAutoresizingMaskIntoConstraints = NO;
        [pop addItemsWithTitles:@[ @"Cheats (*.cht)", @"Text (*.txt)" ]];
        pop.target = pick;
        pop.action = @selector(changed:);
        pop.accessibilityLabel = @"File format";
        NSTextField *l = Txt(@"Format", MHTheme.bodyFont, MHTheme.textPrimary, NO);
        NSStackView *acc = Row(@[ l, pop ], MHSpace8);
        acc.edgeInsets = NSEdgeInsetsMake(MHSpace8, MHSpace16, MHSpace8, MHSpace16);
        acc.translatesAutoresizingMaskIntoConstraints = YES;
        acc.frame = NSMakeRect(0, 0, 320, 44);
        p.accessoryView = acc;
    } else {
        NSOpenPanel *o = [NSOpenPanel openPanel];
        NSMutableArray<UTType *> *types = [NSMutableArray array];
        if (cht) [types addObject:cht];
        if (txt) [types addObject:txt];
        o.allowedContentTypes = types;
        o.canChooseDirectories = NO;
        o.allowsMultipleSelection = NO;
        o.prompt = @"Import";
        p = o;
    }
    p.title = title;
    p.message = message;
    NSWindow *parent = TopWindow();
    if (!parent) return [p runModal] == NSModalResponseOK ? p.URL : nil;
    __block NSURL *result = nil;
    __block BOOL done = NO;
    [p beginSheetModalForWindow:parent completionHandler:^(NSModalResponse r) {
        result = r == NSModalResponseOK ? p.URL : nil;
        done = YES;
    }];
    SpinUntil(^BOOL { return done; });
    (void)pick;
    return result;
}

/* ---- Cheats (orig: cheat_ui_edit + ui_proc) -------------------------------------------------------- */

/* orig: ui_move_entry. Moves entry `from` to position `to`, shifting the ones in between. */
static void MoveEntry(MhCheatProfile *p, uint32_t from, uint32_t to) {
    if (!p) return;
    uint32_t hi = from > to ? from : to;
    if (hi >= p->count || from == to) return;
    MhCheatEntry tmp = p->entries[from];
    if (from < to) memmove(&p->entries[from], &p->entries[from + 1], (size_t)(to - from) * sizeof(MhCheatEntry));
    else memmove(&p->entries[to + 1], &p->entries[to], (size_t)(from - to) * sizeof(MhCheatEntry));
    p->entries[to] = tmp;
}

static MHSource SourceOf(const MhCheatEntry *e) {
    if (e->flags & MH_CHEAT_F_CUSTOM) return (e->flags & MH_CHEAT_F_SIMPLE) ? MHSourceUserClassic : MHSourceUserV5;
    return MHSourceLibrary;
}

static NSPasteboardType const kRowType = @"br.picostation.multihouse.cheatrow";

@interface MHCheatCatalog : NSObject
- (instancetype)initWithManager:(id)mgr;
- (void)run;
@end

@interface MHCheatManager : NSObject <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSMenuDelegate,
                                      NSMenuItemValidation, MHCheatUITestable>
@property NSWindow *window, *parent;
@property(nonatomic) const MhCheatDb *db;
@property MhCheatProfile *profile; /* the working clone (orig ctx+0x58) */
@property uint8_t scope;           /* orig ctx+0x74 */
@property uint32_t variant;        /* orig ctx+0x1f8 */
@property BOOL accepted, done;
@property NSMutableArray<NSNumber *> *rows;
@property MHTable *table;
@property NSScrollView *tableSV;
@property NSSearchField *search;
@property NSTextField *titleLabel, *metaLabel, *subtitleLabel, *countLabel;
@property MHMeterRow *linesMeter, *spaceMeter;
@property NSTextView *details;
@property NSView *emptyState;
@property NSTextField *emptyTitle, *emptyText;
@property NSButton *emptyAdd, *emptyNew, *bLibrary, *bNew, *bEdit, *bToggle, *bDelete, *bImport, *bExport, *bUp, *bDown, *bApply, *bCancel, *bClear;
@property NSMenu *menu;
- (unsigned)budget;
- (void)refresh;
- (void)selectProfileIndex:(NSInteger)idx;
@end

@implementation MHCheatManager
- (NSWindow *)testWindow { return _window; }
- (const MhCheatProfile *)testProfile { return _profile; }
- (unsigned)budget { return _scope == MH_CHEAT_UI_SCOPE_DISC ? MH_CHEAT_BUDGET_DISC : MH_CHEAT_BUDGET_CASE; }

- (void)dealloc { mh_cheat_profile_free(_profile); }

- (void)build:(NSString *)header {
    _rows = [NSMutableArray array];
    NSWindow *w = NewWindow(TITLE_CHEATS, NSMakeSize(1000, 720), NSMakeSize(920, 640), YES, YES);
    w.delegate = self;
    NSView *cv = w.contentView;
    CGFloat m = MHSpace24;

    /* header: icon tile, disc/case title, meters */
    MHCard *tile = [[MHCard alloc] init];
    tile.fill = MHTheme.brandFill;
    tile.stroke = nil;
    tile.radius = MHRadiusCard;
    NSImageView *bolt = [[NSImageView alloc] init];
    bolt.translatesAutoresizingMaskIntoConstraints = NO;
    bolt.image = [MHTheme symbol:@"bolt.fill" size:20 color:NSColor.whiteColor description:@"Cheats"];
    bolt.accessibilityElement = NO;
    [tile addSubview:bolt];
    [NSLayoutConstraint activateConstraints:@[
        [bolt.centerXAnchor constraintEqualToAnchor:tile.centerXAnchor],
        [bolt.centerYAnchor constraintEqualToAnchor:tile.centerYAnchor],
        [tile.widthAnchor constraintEqualToConstant:48], [tile.heightAnchor constraintEqualToConstant:48] ]];
    NSMutableArray<NSString *> *hl = [NSMutableArray array];
    for (NSString *ln in [header componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSString *tl = [ln stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (tl.length) [hl addObject:tl];
    }
    NSString *first = hl.firstObject ?: TITLE_CHEATS;
    NSString *tt = first, *meta = @"";
    NSRange bar = [first rangeOfString:@"|"];
    if (bar.location != NSNotFound) {
        tt = [[first substringToIndex:bar.location] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        meta = [[first substringFromIndex:bar.location + 1] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    }
    NSString *sub = hl.count > 1 ? [[hl subarrayWithRange:NSMakeRange(1, hl.count - 1)] componentsJoinedByString:@" "] : @"";
    _titleLabel = Txt(tt.length ? tt : TITLE_CHEATS, MHTheme.titleFont, MHTheme.textPrimary, NO);
    _titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    _titleLabel.accessibilityRole = NSAccessibilityStaticTextRole;
    _metaLabel = Txt(meta, MHTheme.monoCaptionFont, MHTheme.textSecondary, NO);
    _metaLabel.hidden = meta.length == 0;
    _metaLabel.selectable = YES;
    _subtitleLabel = Txt(sub, MHTheme.captionFont, MHTheme.textSecondary, YES);
    _subtitleLabel.maximumNumberOfLines = 2;
    _subtitleLabel.hidden = sub.length == 0;
    NSStackView *textStack = [NSStackView stackViewWithViews:@[ _titleLabel, _metaLabel, _subtitleLabel ]];
    textStack.translatesAutoresizingMaskIntoConstraints = NO;
    textStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    textStack.alignment = NSLayoutAttributeLeading;
    textStack.spacing = MHSpace4;
    [_subtitleLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [_titleLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    _linesMeter = [[MHMeterRow alloc] initWithTitle:@"Active lines"];
    _spaceMeter = [[MHMeterRow alloc] initWithTitle:@"Disc space"];
    NSStackView *meters = [NSStackView stackViewWithViews:@[ _linesMeter, _spaceMeter ]];
    meters.translatesAutoresizingMaskIntoConstraints = NO;
    meters.orientation = NSUserInterfaceLayoutOrientationVertical;
    meters.alignment = NSLayoutAttributeLeading;
    meters.spacing = MHSpace12;
    [_linesMeter.widthAnchor constraintEqualToConstant:340].active = YES;
    [_spaceMeter.widthAnchor constraintEqualToConstant:340].active = YES;

    NSBox *sep1 = Hairline();

    /* search and library actions */
    _search = SearchField(@"Search cheats", self, @selector(searchChanged:));
    _bLibrary = Btn(@"Add from Library...", @"books.vertical", self, @selector(choose:));
    Key(_bLibrary, @"l", NSEventModifierFlagCommand);
    _bLibrary.toolTip = @"Add cheat groups from the original GameShark V5 library (Command-L)";
    _bNew = Btn(@"New Cheat...", @"plus", self, @selector(newCheat:));
    Key(_bNew, @"n", NSEventModifierFlagCommand);
    _bNew.toolTip = @"Write a cheat of your own (Command-N)";
    NSStackView *topRow = Row(@[ _search, _bLibrary, _bNew ], MHSpace8);
    [_search.widthAnchor constraintGreaterThanOrEqualToConstant:200].active = YES;
    [_search.heightAnchor constraintGreaterThanOrEqualToConstant:MHMinHit].active = YES;

    /* table */
    _table = MakeTable(@[ @[ @"act", @"Active", @72, @64, @NO, @NO ], @[ @"name", @"Name", @400, @220, @YES, @NO ],
                          @[ @"src", @"Source", @150, @130, @NO, @NO ], @[ @"lines", @"Lines", @64, @56, @NO, @YES ] ],
                       YES, self, @"Cheats");
    __weak MHCheatManager *ws = self;
    _table.onDelete = ^{ [ws remove:nil]; };
    _table.onSpace = ^{ [ws toggle:nil]; };
    _table.doubleAction = @selector(editCheat:);
    _table.target = self;
    ((MHHeader *)_table.headerView).onDouble = ^(NSInteger col) { if (col == 1) [ws sortByName]; };
    [_table registerForDraggedTypes:@[ kRowType ]];
    [_table setDraggingSourceOperationMask:NSDragOperationMove forLocal:YES];
    NSScrollView *tsv;
    MHCard *tableCard = TableCard(_table, &tsv);
    _tableSV = tsv;
    _tableSV.accessibilityElement = NO;
    [self buildContextMenu];
    _table.menu = _menu;

    /* empty state over the table */
    NSImageView *eIcon = [[NSImageView alloc] init];
    eIcon.translatesAutoresizingMaskIntoConstraints = NO;
    eIcon.image = [MHTheme symbol:@"bolt.slash" size:48 weight:NSFontWeightLight description:nil];
    eIcon.contentTintColor = MHTheme.textSecondary;
    eIcon.accessibilityElement = NO;
    _emptyTitle = Txt(@"No cheats yet", MHTheme.titleFont, MHTheme.textPrimary, NO);
    _emptyTitle.alignment = NSTextAlignmentCenter;
    _emptyText = Txt(@"Add cheat groups from the library, or write your own.", MHTheme.bodyFont, MHTheme.textSecondary, YES);
    _emptyText.alignment = NSTextAlignmentCenter;
    NSButton *eAdd = Btn(@"Add from Library...", @"books.vertical", self, @selector(choose:));
    Prominent(eAdd);
    eAdd.keyEquivalent = @"";
    NSButton *eNew = Btn(@"New Cheat...", @"plus", self, @selector(newCheat:));
    _bClear = Btn(@"Clear Search", nil, self, @selector(clearSearch:));
    NSStackView *eButtons = Row(@[ eAdd, eNew, _bClear ], MHSpace12);
    NSStackView *eStack = [NSStackView stackViewWithViews:@[ eIcon, _emptyTitle, _emptyText, eButtons ]];
    eStack.translatesAutoresizingMaskIntoConstraints = NO;
    eStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    eStack.alignment = NSLayoutAttributeCenterX;
    eStack.spacing = MHSpace12;
    [eStack setCustomSpacing:MHSpace16 afterView:_emptyText];
    MHCard *es = [[MHCard alloc] init];
    es.fill = NSColor.controlBackgroundColor;
    es.stroke = nil;
    _emptyState = es;
    [_emptyState addSubview:eStack];
    [NSLayoutConstraint activateConstraints:@[
        [eStack.centerXAnchor constraintEqualToAnchor:_emptyState.centerXAnchor],
        [eStack.centerYAnchor constraintEqualToAnchor:_emptyState.centerYAnchor],
        [_emptyText.widthAnchor constraintLessThanOrEqualToConstant:420] ]];
    [tableCard addSubview:_emptyState];
    Pin(_emptyState, tableCard, 0, 0, 0, 0);
    _emptyState.hidden = YES;
    _emptyAdd = eAdd;
    _emptyNew = eNew;

    /* row actions */
    _bEdit = Btn(@"Edit...", @"pencil", self, @selector(editCheat:));
    Key(_bEdit, @"e", NSEventModifierFlagCommand);
    _bToggle = Btn(@"Enable", @"checkmark.circle", self, @selector(toggle:));
    _bToggle.toolTip = @"Enable or disable the selected cheat (Space)";
    _bDelete = Btn(@"Delete", @"trash", self, @selector(remove:));
    Key(_bDelete, [NSString stringWithFormat:@"%C", (unichar)NSDeleteCharacter], NSEventModifierFlagCommand);
    _bDelete.toolTip = @"Delete the selected cheats (Delete or Command-Delete)";
    _bImport = Btn(@"Import...", @"square.and.arrow.down", self, @selector(importCheats:));
    Key(_bImport, @"i", NSEventModifierFlagCommand);
    _bExport = Btn(@"Export...", @"square.and.arrow.up", self, @selector(exportCheats:));
    Key(_bExport, @"e", NSEventModifierFlagCommand | NSEventModifierFlagShift);
    _bUp = Btn(@"Move Up", @"chevron.up", self, @selector(moveUp:));
    Key(_bUp, [NSString stringWithFormat:@"%C", (unichar)NSUpArrowFunctionKey], NSEventModifierFlagCommand | NSEventModifierFlagOption);
    _bUp.toolTip = @"Move the selected cheat up (Option-Command-Up Arrow)";
    _bDown = Btn(@"Move Down", @"chevron.down", self, @selector(moveDown:));
    Key(_bDown, [NSString stringWithFormat:@"%C", (unichar)NSDownArrowFunctionKey], NSEventModifierFlagCommand | NSEventModifierFlagOption);
    _bDown.toolTip = @"Move the selected cheat down (Option-Command-Down Arrow)";
    for (NSButton *b in @[ _bEdit, _bToggle, _bDelete, _bImport, _bExport, _bUp, _bDown ]) b.controlSize = NSControlSizeSmall;
    NSStackView *actions = Row(@[ _bEdit, _bToggle, _bDelete, _bImport, _bExport, _bUp, _bDown ], MHSpace8);
    [actions setCustomSpacing:MHSpace16 afterView:_bDelete];
    [actions setCustomSpacing:MHSpace16 afterView:_bExport];
    _countLabel = Txt(@"", MHTheme.captionFont, MHTheme.textSecondary, NO);
    _countLabel.alignment = NSTextAlignmentRight;
    [_countLabel setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];

    /* details */
    MHCard *detCard = [[MHCard alloc] init];
    NSTextView *detTV;
    NSScrollView *detSV = DetailBox(&detTV, @"Cheat details");
    _details = detTV;
    [detCard addSubview:detSV];
    Pin(detSV, detCard, 0, 0, 0, 0);

    /* footer */
    _bCancel = Btn(@"Cancel", nil, self, @selector(cancel:));
    Cancelling(_bCancel);
    _bCancel.controlSize = NSControlSizeLarge;
    _bApply = Btn(@"Apply", nil, self, @selector(accept:));
    Prominent(_bApply);
    _bApply.controlSize = NSControlSizeLarge;
    _bApply.toolTip = @"Keep these changes and close (Return)";
    NSStackView *footer = Row(@[ _bCancel, _bApply ], MHSpace12);

    for (NSView *v in @[ tile, textStack, meters, sep1, topRow, tableCard, actions, _countLabel, detCard, footer ]) [cv addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [tile.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [tile.topAnchor constraintEqualToAnchor:cv.topAnchor constant:m],
        [textStack.leadingAnchor constraintEqualToAnchor:tile.trailingAnchor constant:MHSpace16],
        [textStack.topAnchor constraintEqualToAnchor:tile.topAnchor constant:-2],
        [textStack.trailingAnchor constraintLessThanOrEqualToAnchor:meters.leadingAnchor constant:-m],
        [meters.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [meters.topAnchor constraintEqualToAnchor:cv.topAnchor constant:m],
        [sep1.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor],
        [sep1.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor],
        [sep1.topAnchor constraintGreaterThanOrEqualToAnchor:meters.bottomAnchor constant:MHSpace16],
        [sep1.topAnchor constraintGreaterThanOrEqualToAnchor:textStack.bottomAnchor constant:MHSpace16],
        [sep1.topAnchor constraintGreaterThanOrEqualToAnchor:tile.bottomAnchor constant:MHSpace16],
        [topRow.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [topRow.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [topRow.topAnchor constraintEqualToAnchor:sep1.bottomAnchor constant:MHSpace16],
        [tableCard.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [tableCard.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [tableCard.topAnchor constraintEqualToAnchor:topRow.bottomAnchor constant:MHSpace12],
        [tableCard.heightAnchor constraintGreaterThanOrEqualToConstant:200],
        [actions.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [actions.topAnchor constraintEqualToAnchor:tableCard.bottomAnchor constant:MHSpace12],
        [_countLabel.leadingAnchor constraintGreaterThanOrEqualToAnchor:actions.trailingAnchor constant:MHSpace12],
        [_countLabel.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [_countLabel.centerYAnchor constraintEqualToAnchor:actions.centerYAnchor],
        [detCard.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:m],
        [detCard.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [detCard.topAnchor constraintEqualToAnchor:actions.bottomAnchor constant:MHSpace12],
        [detCard.heightAnchor constraintEqualToConstant:136],
        [footer.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-m],
        [footer.topAnchor constraintEqualToAnchor:detCard.bottomAnchor constant:MHSpace16],
        [footer.bottomAnchor constraintEqualToAnchor:cv.bottomAnchor constant:-MHSpace16],
        [_bCancel.widthAnchor constraintGreaterThanOrEqualToConstant:96],
        [_bApply.widthAnchor constraintGreaterThanOrEqualToConstant:96],
    ]];
    NSLayoutConstraint *tc = [tableCard.heightAnchor constraintEqualToConstant:360];
    tc.priority = NSLayoutPriorityDefaultLow;
    tc.active = YES;

    _search.nextKeyView = _bLibrary;
    w.initialFirstResponder = _search;
    _window = w;
    gManager = self;
    [self refresh];
}

- (void)buildContextMenu {
    NSMenu *mn = [[NSMenu alloc] initWithTitle:@"Cheats"];
    mn.delegate = self;
    mn.autoenablesItems = YES;
    NSArray *spec = @[ @[ @"Add from Library...", @"choose:" ], @[ @"New Cheat...", @"newCheat:" ], @"-",
                       @[ @"Edit...", @"editCheat:" ], @[ @"Enable", @"toggle:" ], @[ @"Delete", @"remove:" ], @"-",
                       @[ @"Move Up", @"moveUp:" ], @[ @"Move Down", @"moveDown:" ], @[ @"Sort by Name", @"sortMenu:" ], @"-",
                       @[ @"Import...", @"importCheats:" ], @[ @"Export...", @"exportCheats:" ] ];
    for (id s in spec) {
        if ([s isKindOfClass:NSString.class]) { [mn addItem:NSMenuItem.separatorItem]; continue; }
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:s[0] action:NSSelectorFromString(s[1]) keyEquivalent:@""];
        it.target = self;
        [mn addItem:it];
    }
    _menu = mn;
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    for (NSMenuItem *it in menu.itemArray)
        if (it.action == @selector(toggle:)) it.title = [self toggleTitle];
}

- (NSString *)toggleTitle {
    const MhCheatEntry *e = [self entryAtProfileIndex:[self firstSelected]];
    return (e && (e->flags & MH_CHEAT_F_ENABLED)) ? @"Disable" : @"Enable";
}

- (BOOL)validateMenuItem:(NSMenuItem *)it {
    return [self canPerform:it.action];
}

- (BOOL)canPerform:(SEL)a {
    NSInteger sel = [self firstSelected];
    NSUInteger n = _table.selectedRowIndexes.count;
    if (a == @selector(editCheat:) || a == @selector(toggle:)) return sel >= 0;
    if (a == @selector(remove:)) return n > 0;
    if (a == @selector(moveUp:)) return n == 1 && _table.selectedRow > 0;
    if (a == @selector(moveDown:)) return n == 1 && _table.selectedRow >= 0 && _table.selectedRow < (NSInteger)_rows.count - 1;
    if (a == @selector(sortMenu:)) return _profile && _profile->count > 1;
    if (a == @selector(exportCheats:)) return _profile && _profile->count > 0;
    return YES;
}

- (void)windowWillClose:(NSNotification *)n {
    _done = YES;
    [NSApp stopModal];
}

- (const MhCheatEntry *)entryAtProfileIndex:(NSInteger)i {
    if (!_profile || i < 0 || i >= (NSInteger)_profile->count) return NULL;
    return &_profile->entries[i];
}

- (void)searchChanged:(id)sender { [self refresh]; }
- (void)clearSearch:(id)sender { _search.stringValue = @""; [self refresh]; [_window makeFirstResponder:_search]; }

/* orig: ui_profile */
- (void)refresh {
    NSInteger keep = [self firstSelected];
    const char *flt = _search.stringValue.UTF8String ?: "";
    [_rows removeAllObjects];
    for (uint32_t i = 0; _profile && i < _profile->count; i++)
        if (mh_cheat_filter_match(_profile->entries[i].name, flt)) [_rows addObject:@(i)];
    [_table deselectAll:nil];
    [_table reloadData];
    uint32_t active = 0;
    for (uint32_t i = 0; _profile && i < _profile->count; i++)
        if (_profile->entries[i].flags & MH_CHEAT_F_ENABLED) active += _profile->entries[i].nlines;
    [_linesMeter setValue:active maximum:MAX_ACTIVE_LINES text:[NSString stringWithFormat:@"%u of %d lines", active, MAX_ACTIVE_LINES]];
    unsigned used = mh_cheat_profile_serialized_size(_profile);
    [_spaceMeter setValue:used maximum:[self budget]
                     text:[NSString stringWithFormat:@"%@ of %@ bytes", Num(used), Num([self budget])]];
    NSUInteger total = _profile ? _profile->count : 0;
    BOOL filtered = _search.stringValue.length > 0;
    _countLabel.stringValue = total == 0 ? @"No cheats"
        : filtered ? [NSString stringWithFormat:@"%lu of %lu %@", (unsigned long)_rows.count, (unsigned long)total, total == 1 ? @"cheat" : @"cheats"]
                   : [NSString stringWithFormat:@"%lu %@", (unsigned long)total, total == 1 ? @"cheat" : @"cheats"];
    BOOL empty = _rows.count == 0;
    _emptyState.hidden = !empty;
    BOOL noCheats = total == 0;
    _emptyTitle.stringValue = noCheats ? @"No cheats yet" : @"No matching cheats";
    _emptyText.stringValue = noCheats ? @"Add cheat groups from the library, or write your own."
                                      : [NSString stringWithFormat:@"Nothing matches \"%@\".", _search.stringValue];
    _emptyAdd.hidden = !noCheats;
    _emptyNew.hidden = !noCheats;
    _bClear.hidden = noCheats;
    if (keep >= 0) [self selectProfileIndex:keep];
    [self updateButtons];
    [self showDetails];
}

/* orig: ui_select_index */
- (void)selectProfileIndex:(NSInteger)idx {
    for (NSUInteger r = 0; r < _rows.count; r++)
        if (_rows[r].integerValue == idx) {
            [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:r] byExtendingSelection:NO];
            [_table scrollRowToVisible:r];
            return;
        }
}

- (NSInteger)firstSelected {
    NSInteger r = _table.selectedRowIndexes.firstIndex == NSNotFound ? -1 : (NSInteger)_table.selectedRowIndexes.firstIndex;
    if (r < 0 || r >= (NSInteger)_rows.count) return -1;
    return _rows[r].integerValue;
}

- (void)updateButtons {
    _bEdit.enabled = [self canPerform:@selector(editCheat:)];
    _bToggle.enabled = [self canPerform:@selector(toggle:)];
    _bDelete.enabled = [self canPerform:@selector(remove:)];
    _bUp.enabled = [self canPerform:@selector(moveUp:)];
    _bDown.enabled = [self canPerform:@selector(moveDown:)];
    _bExport.enabled = [self canPerform:@selector(exportCheats:)];
    NSString *t = [self toggleTitle];
    _bToggle.title = t;
    _bToggle.image = [MHTheme symbol:[t isEqualToString:@"Enable"] ? @"checkmark.circle" : @"minus.circle" size:13 description:t];
    _bToggle.accessibilityLabel = [t isEqualToString:@"Enable"] ? @"Enable selected cheat" : @"Disable selected cheat";
}

/* orig: ui_details */
- (void)showDetails {
    NSMutableAttributedString *m = [[NSMutableAttributedString alloc] init];
    NSUInteger n = _table.selectedRowIndexes.count;
    const MhCheatEntry *e = [self entryAtProfileIndex:[self firstSelected]];
    if (n > 1) {
        AddRich(m, [NSString stringWithFormat:@"%lu cheats selected", (unsigned long)n], MHTheme.headlineFont, MHTheme.textPrimary, 4);
        AddRich(m, @"Select a single cheat to see its notes and codes.", MHTheme.bodyFont, MHTheme.textSecondary, 0);
    } else if (!e) {
        AddRich(m, @"Select a cheat to see its notes and codes.", MHTheme.bodyFont, MHTheme.textSecondary, 0);
    } else {
        AddRich(m, S(e->name), MHTheme.headlineFont, MHTheme.textPrimary, 2);
        AddRich(m, [NSString stringWithFormat:@"%@  |  %u %@%@", [MHSourceChip titleFor:SourceOf(e)], e->nlines,
                                              e->nlines == 1 ? @"line" : @"lines",
                                              (e->flags & MH_CHEAT_F_MANDATORY) ? @"  |  Required by the source" : @""],
                MHTheme.captionFont, MHTheme.textSecondary, 6);
        AddSection(m, @"Notes", S(mh_cheat_entry_hardware_note()), NO);
        AddSection(m, @"Codes", CodeText(e->lines, e->nlines, e->flags), YES);
    }
    Show(_details, m);
}

#pragma mark table

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv { return (NSInteger)_rows.count; }

- (NSTableRowView *)tableView:(NSTableView *)tv rowViewForRow:(NSInteger)row {
    MHRow *rv = [[MHRow alloc] init];
    const MhCheatEntry *e = row < (NSInteger)_rows.count ? [self entryAtProfileIndex:_rows[row].integerValue] : NULL;
    if (e)
        rv.axLabel = [NSString stringWithFormat:@"%@, %@, %@, %u %@", S(e->name), (e->flags & MH_CHEAT_F_ENABLED) ? @"enabled" : @"disabled",
                                                [MHSourceChip titleFor:SourceOf(e)], e->nlines, e->nlines == 1 ? @"line" : @"lines"];
    return rv;
}

- (NSView *)tableView:(NSTableView *)tv viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    const MhCheatEntry *e = row < (NSInteger)_rows.count ? [self entryAtProfileIndex:_rows[row].integerValue] : NULL;
    if (!e) return nil;
    NSString *ident = col.identifier;
    NSString *kind = [ident isEqualToString:@"lines"] ? @"num" : [ident isEqualToString:@"act"] ? @"act" : [ident isEqualToString:@"src"] ? @"src" : @"name";
    MHCell *c = [tv makeViewWithIdentifier:kind owner:self];
    if (!c) c = [MHCell cellOfKind:kind];
    if ([kind isEqualToString:@"act"]) {
        c.toggle.state = (e->flags & MH_CHEAT_F_ENABLED) ? NSControlStateValueOn : NSControlStateValueOff;
        c.toggle.target = self;
        c.toggle.action = @selector(switchFlipped:);
        c.toggle.tag = _rows[row].integerValue;
        c.toggle.accessibilityLabel = [@"Active: " stringByAppendingString:S(e->name)];
        c.toggle.toolTip = @"Turn this cheat on or off";
    } else if ([kind isEqualToString:@"src"]) {
        c.chip.source = SourceOf(e);
    } else if ([kind isEqualToString:@"num"]) {
        c.textField.stringValue = [NSString stringWithFormat:@"%u", e->nlines];
    } else {
        c.textField.stringValue = S(e->name);
        c.toolTip = S(e->name);
        BOOL req = e->flags & MH_CHEAT_F_MANDATORY;
        c.lock.hidden = !req;
        c.lockW.constant = req ? 14 : 0;
    }
    return c;
}
- (void)tableViewSelectionDidChange:(NSNotification *)n { [self updateButtons]; [self showDetails]; }

- (void)switchFlipped:(NSSwitch *)sw {
    [self toggleProfileIndex:sw.tag];
}

- (id<NSPasteboardWriting>)tableView:(NSTableView *)tv pasteboardWriterForRow:(NSInteger)row {
    NSPasteboardItem *it = [[NSPasteboardItem alloc] init];
    [it setString:_rows[row].stringValue forType:kRowType];
    return it;
}
- (NSDragOperation)tableView:(NSTableView *)tv validateDrop:(id<NSDraggingInfo>)info proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)op {
    if (_rows.count == 0) return NSDragOperationNone;
    if (op == NSTableViewDropAbove) [tv setDropRow:MIN(row, (NSInteger)_rows.count - 1) dropOperation:NSTableViewDropOn];
    return NSDragOperationMove;
}
/* orig: WM_LBUTTONUP in ui_proc: ui_move_entry(src, target) then reselect the target */
- (BOOL)tableView:(NSTableView *)tv acceptDrop:(id<NSDraggingInfo>)info row:(NSInteger)row
    dropOperation:(NSTableViewDropOperation)op {
    NSString *s = [info.draggingPasteboard stringForType:kRowType];
    if (!s || row < 0 || row >= (NSInteger)_rows.count) return NO;
    NSInteger dst = _rows[row].integerValue;
    [self moveEntryFrom:(uint32_t)s.integerValue to:(uint32_t)dst];
    [self refresh];
    [self selectProfileIndex:dst];
    return YES;
}

- (void)moveEntryFrom:(uint32_t)from to:(uint32_t)to { MoveEntry(_profile, from, to); }

/* Move Up and Move Down: swap with the neighbour in the visible list (keyboard and menu path of the drag). */
- (void)moveBy:(NSInteger)d {
    NSInteger r = _table.selectedRow;
    if (_table.selectedRowIndexes.count != 1 || r < 0) return;
    NSInteger nr = r + d;
    if (nr < 0 || nr >= (NSInteger)_rows.count) return;
    NSInteger from = _rows[r].integerValue, to = _rows[nr].integerValue;
    [self moveEntryFrom:(uint32_t)from to:(uint32_t)to];
    [self refresh];
    [self selectProfileIndex:to];
    Announce([NSString stringWithFormat:@"Moved %@ to position %ld of %lu", S(_profile->entries[to].name), (long)to + 1, (unsigned long)_profile->count]);
}
- (void)moveUp:(id)sender { [self moveBy:-1]; }
- (void)moveDown:(id)sender { [self moveBy:1]; }
- (void)sortMenu:(id)sender { [self sortByName]; }

/* orig: ui_header_proc, insertion sort by name (a_icmp) */
- (void)sortByName {
    MhCheatProfile *p = _profile;
    if (p) {
        for (uint32_t i = 1; i < p->count; i++)
            for (uint32_t j = i; j > 0 && mh_str_icmp(p->entries[j - 1].name, p->entries[j].name) > 0; j--) {
                MhCheatEntry t = p->entries[j];
                p->entries[j] = p->entries[j - 1];
                p->entries[j - 1] = t;
            }
        [self refresh];
        Announce(@"Sorted by name");
    }
}

#pragma mark actions

/* orig: ui_commit_edit.isra.0. Takes ownership of clone. */
- (void)commit:(MhCheatProfile *)clone select:(NSInteger)keep {
    BOOL anyAudited = NO;
    for (uint32_t i = 0; i < clone->count; i++)
        if (clone->entries[i].flags & MH_CHEAT_F_AUDITED) { anyAudited = YES; break; }
    if (!anyAudited) {
        clone->game = 0;
        clone->db_crc = 0;
        clone->title = MH_NONE;
        clone->variant = MH_NONE;
    }
    char err[256] = "";
    if (!mh_cheat_profile_validate_hardware(clone, err, sizeof err)) {
        MsgErr(@"This change cannot be applied", S(err));
        mh_cheat_profile_free(clone);
        [self refresh]; /* put the switch back */
        return;
    }
    mh_cheat_profile_free(_profile);
    _profile = clone;
    [self refresh];
    if (keep >= 0) [self selectProfileIndex:keep];
}

/* orig: ui_new_custom */
- (void)newCheat:(id)sender {
    NSInteger sel = [self firstSelected];
    char name[MH_CHEAT_NAME_MAX];
    uint8_t *lines = NULL;
    uint16_t n = 0;
    uint8_t flags = 0;
    if (![MHCheatEditor runWithParent:_window name:"NEW CHEAT" lines:NULL n:0 flags:0 outName:name outLines:&lines
                                 outN:&n outFlags:&flags])
        return;
    MhCheatEntry probe = {0};
    if (mh_cheat_entry_hardware_exclusion(&probe)) { /* orig: always 0 */ }
    if (mh_cheat_profile_add_custom(_profile, name, lines, n, flags)) {
        uint32_t last = _profile->count - 1;
        NSInteger target;
        if (sel < 0 || (uint32_t)sel >= last) {
            free(lines);
            [self refresh];
            target = last;
        } else {
            target = sel + 1;
            MoveEntry(_profile, last, (uint32_t)target);
            free(lines);
            [self refresh];
        }
        [self selectProfileIndex:target];
        Announce([NSString stringWithFormat:@"Added %@", S(name)]);
        return;
    }
    char err[256] = "";
    (void)mh_cheat_profile_validate(_profile, err, sizeof err); /* orig overwrote the message anyway */
    MsgErr(@"The new cheat does not fit", @"It would go over the disc space or the limit of 31 active lines.");
    free(lines);
    [self refresh];
}

/* orig: Editar (0x13f0) */
- (void)editCheat:(id)sender {
    NSInteger i = [self firstSelected];
    const MhCheatEntry *e = [self entryAtProfileIndex:i];
    if (!e) return;
    char name[MH_CHEAT_NAME_MAX];
    uint8_t *lines = NULL;
    uint16_t n = 0;
    uint8_t oflags = 0;
    if (![MHCheatEditor runWithParent:_window name:e->name lines:e->lines n:e->nlines flags:e->flags outName:name
                             outLines:&lines outN:&n outFlags:&oflags])
        return;
    MhCheatProfile *c = mh_cheat_profile_clone(_profile);
    if (!c) { free(lines); MsgErr(@"Not enough memory", @"The cheat could not be changed."); return; }
    MhCheatEntry *x = &c->entries[i];
    free(x->lines);
    x->lines = lines;
    x->nlines = n;
    mh_str_copy(x->name, sizeof x->name, name);
    x->game = 0;
    x->variant = MH_NONE;
    x->id = MH_NONE;
    x->flags = (uint8_t)((x->flags & 0xA5) | oflags | 0x24);
    [self commit:c select:i];
}

/* orig: Ativar / Desativar (0x13f1) */
- (void)toggle:(id)sender { [self toggleProfileIndex:[self firstSelected]]; }

- (void)toggleProfileIndex:(NSInteger)i {
    const MhCheatEntry *e = [self entryAtProfileIndex:i];
    if (!e) return;
    if ((e->flags & (MH_CHEAT_F_ENABLED | MH_CHEAT_F_MANDATORY)) == 3 &&
        MsgBox(@"Turn off a required cheat?",
               @"The source marks this code as required. Turning it off makes it a custom cheat and disables it.",
               NSAlertStyleWarning, @[ @"Turn Off", @"Cancel" ], 1, NO) != 0) {
        [self refresh]; /* put the switch back */
        return;
    }
    MhCheatProfile *c = mh_cheat_profile_clone(_profile);
    if (!c) { MsgErr(@"Not enough memory", @"The cheat could not be changed."); return; }
    MhCheatEntry *x = &c->entries[i];
    uint8_t f = x->flags;
    if ((f & 3) == 3) {
        x->game = 0;
        f = (uint8_t)((f & 0xE5) | 0x24);
        x->variant = MH_NONE;
        x->id = MH_NONE;
    }
    x->flags = f ^ 1;
    NSString *nm = S(x->name);
    BOOL on = x->flags & 1;
    [self commit:c select:i];
    Announce([NSString stringWithFormat:@"%@ %@", nm, on ? @"enabled" : @"disabled"]);
}

/* orig: ui_remove */
- (void)remove:(id)sender {
    NSIndexSet *sel = _table.selectedRowIndexes;
    NSMutableIndexSet *idx = [NSMutableIndexSet indexSet];
    NSInteger first = -1;
    BOOL mandatory = NO;
    for (NSUInteger r = sel.firstIndex; r != NSNotFound; r = [sel indexGreaterThanIndex:r]) {
        if (r >= _rows.count) continue;
        NSInteger pi = _rows[r].integerValue;
        const MhCheatEntry *e = [self entryAtProfileIndex:pi];
        if (!e || [idx containsIndex:pi]) continue;
        if (first < 0) first = pi;
        [idx addIndex:pi];
        if (e->flags & MH_CHEAT_F_MANDATORY) mandatory = YES;
    }
    if (!idx.count) return;
    if (mandatory &&
        MsgBox(@"Delete required cheats?",
               @"The selection includes a code the source marks as required. Deleting it can stop other cheats from working.",
               NSAlertStyleWarning, @[ @"Delete", @"Cancel" ], 1, YES) != 0)
        return;
    NSUInteger count = idx.count;
    for (NSUInteger i = idx.lastIndex; i != NSNotFound; i = [idx indexLessThanIndex:i]) mh_cheat_profile_remove(_profile, (uint32_t)i);
    [self refresh];
    if (_profile->count) [self selectProfileIndex:MIN((NSUInteger)first, (NSUInteger)_profile->count - 1)];
    Announce([NSString stringWithFormat:@"Deleted %lu %@", (unsigned long)count, count == 1 ? @"cheat" : @"cheats"]);
}

/* orig: ui_import */
- (void)importCheats:(id)sender {
    NSURL *u = RunPanel(NO, @"Import Cheats", @"Choose a .cht or .txt file with cheat codes.");
    if (!u) return;
    NSInteger a = MsgBox(@"Which code format does this file use?",
                         @"Yes: GameShark / Action Replay classic (convert)\nNo: GameShark V5 / Xploder V4 (native)\nCancel: do not import",
                         NSAlertStyleInformational, @[ @"Yes", @"No", @"Cancel" ], 0, NO);
    if (a != 0 && a != 1) return;
    char err[256] = "";
    int imported = 0;
    if (!mh_cheat_profile_import_text_dialect(_profile, u.fileSystemRepresentation, a == 0 ? DIALECT_CLASSIC : DIALECT_V5,
                                              &imported, err, sizeof err)) {
        MsgErr(@"The file could not be imported", S(err));
        return;
    }
    [self refresh];
    Announce([NSString stringWithFormat:@"Imported %d %@", imported, imported == 1 ? @"cheat" : @"cheats"]);
}

/* orig: ui_export */
- (void)exportCheats:(id)sender {
    if (!_profile || !_profile->count) { MsgInfo(@"There is nothing to export", @"The cheat list is empty."); return; }
    NSURL *u = RunPanel(YES, @"Export Cheats", @"Save the cheats of this disc as a .cht or .txt file.");
    if (!u) return;
    char err[256] = "";
    if (!mh_cheat_profile_export_text(_profile, u.fileSystemRepresentation, err, sizeof err)) {
        MsgErr(@"The cheats could not be exported", S(err));
        return;
    }
    MsgInfo(@"Cheats exported", [NSString stringWithFormat:@"The list was saved to %@.", u.lastPathComponent]);
}

/* orig: ui_accept */
- (void)accept:(id)sender {
    char err[256] = "";
    if (!mh_cheat_profile_validate_hardware(_profile, err, sizeof err)) { MsgErr(@"The cheats cannot be applied", S(err)); return; }
    if (mh_cheat_profile_serialized_size(_profile) > [self budget]) {
        MsgErr(@"The cheats do not fit on the disc",
               @"The selection is larger than the space reserved for this disc. Delete some groups before applying; no group is cut automatically.");
        return;
    }
    _accepted = YES;
    [_window close];
}
- (void)cancel:(id)sender { [_window close]; }

/* orig: Escolher jogo / cheats (0x13f7) */
- (void)choose:(id)sender {
    if (!_db || !_db->loaded) {
        MsgInfo(@"The cheat library is not available", @"The embedded GameShark V5 database could not be loaded.");
        return;
    }
    MHCheatCatalog *c = [[MHCheatCatalog alloc] initWithManager:self];
    [c run];
}

- (void)testAction:(NSString *)a argument:(id)arg {
    if ([a isEqualToString:@"select"]) {
        NSMutableIndexSet *s = [NSMutableIndexSet indexSet];
        for (NSNumber *n in arg) [s addIndex:n.unsignedIntegerValue];
        [_table selectRowIndexes:s byExtendingSelection:NO];
    } else if ([a isEqualToString:@"search"]) { _search.stringValue = arg; [self refresh]; }
    else if ([a isEqualToString:@"toggle"]) [self toggle:nil];
    else if ([a isEqualToString:@"edit"]) [self editCheat:nil];
    else if ([a isEqualToString:@"new"]) [self newCheat:nil];
    else if ([a isEqualToString:@"remove"]) [self remove:nil];
    else if ([a isEqualToString:@"import"]) [self importCheats:nil];
    else if ([a isEqualToString:@"export"]) [self exportCheats:nil];
    else if ([a isEqualToString:@"accept"]) [self accept:nil];
    else if ([a isEqualToString:@"cancel"]) [self cancel:nil];
    else if ([a isEqualToString:@"choose"]) [self choose:nil];
    else if ([a isEqualToString:@"sort"]) [self sortByName];
    else if ([a isEqualToString:@"up"]) [self moveUp:nil];
    else if ([a isEqualToString:@"down"]) [self moveDown:nil];
    else if ([a isEqualToString:@"drag"]) {
        [self moveEntryFrom:[arg[0] unsignedIntValue] to:[arg[1] unsignedIntValue]];
        [self refresh];
        [self selectProfileIndex:[arg[1] integerValue]];
    }
}
@end

/* ---- Cheat Library (orig: catalog_proc, catalog_refresh, catalog_details, catalog_add) ----------- */

@interface MHCheatCatalog () <NSTableViewDataSource, NSTableViewDelegate, MHCheatUITestable>
@end

@implementation MHCheatCatalog {
    __weak MHCheatManager *_mgr;
    NSWindow *_window;
    NSPopUpButton *_combo;
    NSSearchField *_search;
    MHMessageView *_info;
    MHTable *_table;
    NSScrollView *_tableSV;
    NSTextView *_details;
    NSButton *_bAdd;
    NSMutableArray<NSNumber *> *_rows;
    NSMutableArray<NSString *> *_names, *_lineCounts;
}
- (NSWindow *)testWindow { return _window; }
- (const MhCheatProfile *)testProfile { return NULL; }

- (instancetype)initWithManager:(MHCheatManager *)mgr {
    if ((self = [super init])) {
        _mgr = mgr;
        _rows = [NSMutableArray array];
        _names = [NSMutableArray array];
        _lineCounts = [NSMutableArray array];
        [self build];
    }
    return self;
}

- (void)build {
    MHCheatManager *m = _mgr;
    const MhCheatDb *db = m.db;
    NSWindow *w = NewWindow(@"Cheat Library", NSMakeSize(780, 680), NSZeroSize, NO, NO);
    NSView *cv = w.contentView;
    CGFloat mg = MHSpace24;

    NSTextField *title = Txt(@"Cheat Library", MHTheme.titleFont, MHTheme.textPrimary, NO);
    NSTextField *desc = Txt(@"Add cheat groups from the original GameShark V5 database. They are added turned off.",
                            MHTheme.captionFont, MHTheme.textSecondary, YES);
    desc.maximumNumberOfLines = 1;
    NSTextField *gameL = Txt(@"Game", MHTheme.headlineFont, MHTheme.textPrimary, NO);
    _combo = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _combo.translatesAutoresizingMaskIntoConstraints = NO;
    _combo.font = MHTheme.bodyFont;
    _combo.target = self;
    _combo.action = @selector(comboChanged:);
    _combo.accessibilityLabel = @"Game";
    _combo.accessibilityHelp = @"Type the first letters of a game name to jump to it.";
    NSMenuItem *first = [[NSMenuItem alloc] initWithTitle:@"Choose a game..." action:nil keyEquivalent:@""];
    first.representedObject = @(MH_NONE);
    [_combo.menu addItem:first];
    [_combo selectItemAtIndex:0];
    for (uint32_t t = 0; t < db->n_titles; t++) {
        char name[768];
        MhCheatVariantInfo vi;
        if (!mh_cheat_db_title(db, t, name, sizeof name) || !mh_cheat_db_title_variant_at(db, t, 0, &vi)) continue;
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:S(name) action:nil keyEquivalent:@""];
        it.representedObject = @(vi.variant);
        [_combo.menu addItem:it];
        if (vi.variant == m.variant) [_combo selectItem:it];
    }
    _search = SearchField(@"Search cheat groups", self, @selector(searchChanged:));

    _table = MakeTable(@[ @[ @"name", @"Cheat Group", @560, @200, @YES, @NO ], @[ @"lines", @"Lines", @72, @56, @NO, @YES ] ], NO, self,
                       @"Cheat groups");
    _table.rowHeight = 32;
    _table.doubleAction = @selector(add:);
    _table.target = self;
    NSScrollView *tsv;
    MHCard *tableCard = TableCard(_table, &tsv);
    _tableSV = tsv;
    _tableSV.accessibilityElement = NO;

    NSTextField *detL = Txt(@"Details", MHTheme.headlineFont, MHTheme.textPrimary, NO);
    MHCard *detCard = [[MHCard alloc] init];
    NSTextView *detTV;
    NSScrollView *detSV = DetailBox(&detTV, @"Cheat group details");
    _details = detTV;
    [detCard addSubview:detSV];
    Pin(detSV, detCard, 0, 0, 0, 0);

    _info = [[MHMessageView alloc] init];

    NSButton *bBack = Btn(@"Done", nil, self, @selector(back:));
    Cancelling(bBack);
    bBack.controlSize = NSControlSizeLarge;
    _bAdd = Btn(@"Add Selection", nil, self, @selector(add:));
    Prominent(_bAdd);
    _bAdd.controlSize = NSControlSizeLarge;
    _bAdd.toolTip = @"Add the selected group to the disc, turned off (Return or double-click)";
    NSStackView *buttons = Row(@[ bBack, _bAdd ], MHSpace12);

    for (NSView *v in @[ title, desc, gameL, _combo, _search, tableCard, detL, detCard, _info, buttons ]) [cv addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [title.topAnchor constraintEqualToAnchor:cv.topAnchor constant:mg],
        [desc.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [desc.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [desc.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:MHSpace4],
        [gameL.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [gameL.topAnchor constraintEqualToAnchor:desc.bottomAnchor constant:MHSpace16],
        [_combo.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [_combo.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [_combo.topAnchor constraintEqualToAnchor:gameL.bottomAnchor constant:MHSpace4],
        [_combo.heightAnchor constraintGreaterThanOrEqualToConstant:MHMinHit],
        [_search.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [_search.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [_search.topAnchor constraintEqualToAnchor:_combo.bottomAnchor constant:MHSpace12],
        [_search.heightAnchor constraintGreaterThanOrEqualToConstant:MHMinHit],
        [tableCard.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [tableCard.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [tableCard.topAnchor constraintEqualToAnchor:_search.bottomAnchor constant:MHSpace12],
        [tableCard.heightAnchor constraintGreaterThanOrEqualToConstant:120],
        [detL.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [detL.topAnchor constraintEqualToAnchor:tableCard.bottomAnchor constant:MHSpace16],
        [detCard.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [detCard.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [detCard.topAnchor constraintEqualToAnchor:detL.bottomAnchor constant:MHSpace4],
        [detCard.heightAnchor constraintEqualToConstant:128],
        [_info.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:mg],
        [_info.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [_info.topAnchor constraintEqualToAnchor:detCard.bottomAnchor constant:MHSpace12],
        [buttons.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-mg],
        [buttons.topAnchor constraintEqualToAnchor:_info.bottomAnchor constant:MHSpace12],
        [buttons.bottomAnchor constraintEqualToAnchor:cv.bottomAnchor constant:-mg],
        [bBack.widthAnchor constraintGreaterThanOrEqualToConstant:96],
        [_bAdd.widthAnchor constraintGreaterThanOrEqualToConstant:120],
    ]];
    NSLayoutConstraint *tc = [tableCard.heightAnchor constraintEqualToConstant:184];
    tc.active = YES;
    _combo.nextKeyView = _search;
    _search.nextKeyView = _table;
    _table.nextKeyView = bBack;
    bBack.nextKeyView = _bAdd;
    _bAdd.nextKeyView = _combo;
    w.initialFirstResponder = _combo;
    [cv.widthAnchor constraintEqualToConstant:760].active = YES;
    [w setContentSize:cv.fittingSize];
    _window = w;
    gCatalog = self;
    [self refresh];
}

- (void)run {
    if (!_mgr.window) CenterOn(_window, nil);
    RunSheet(_mgr.window, _window);
}
- (void)back:(id)sender { EndSheet(_window); }

- (void)comboChanged:(id)sender {
    MHCheatManager *m = _mgr;
    m.variant = (uint32_t)[_combo.selectedItem.representedObject unsignedIntValue];
    [self refresh];
}
- (void)searchChanged:(id)sender { [self refresh]; }

/* orig: catalog_refresh */
- (void)refresh {
    MHCheatManager *m = _mgr;
    const MhCheatDb *db = m.db;
    const char *flt = _search.stringValue.UTF8String ?: "";
    [_rows removeAllObjects];
    [_names removeAllObjects];
    [_lineCounts removeAllObjects];
    MhCheatVariantInfo vi;
    int shown = 0;
    if (mh_cheat_db_variant_info(db, m.variant, &vi) && vi.entry_count) {
        for (uint32_t i = 0; i < vi.entry_count; i++) {
            MhCheatEntryInfo ei;
            if (mh_cheat_db_entry_info(db, m.variant, i, &ei) && mh_cheat_filter_match(ei.name, flt)) {
                [_rows addObject:@(i)];
                [_names addObject:S(ei.name)];
                [_lineCounts addObject:[NSString stringWithFormat:@"%u", ei.nlines]];
                shown++;
            }
        }
    }
    [_table deselectAll:nil];
    [_table reloadData];
    if (m.variant == MH_NONE)
        [_info setText:@"No game matches this disc's serial. Choose a game above." kind:MHMsgNeutral];
    else
        [_info setText:[NSString stringWithFormat:@"%d %@ from the original GameShark V5. At most %d lines can be active at once.",
                                                  shown, shown == 1 ? @"group" : @"groups", MAX_ACTIVE_LINES]
                  kind:MHMsgNeutral];
    [self details];
    _bAdd.enabled = NO;
}

- (NSInteger)selectedEntry {
    NSInteger r = _table.selectedRow;
    return (r < 0 || r >= (NSInteger)_rows.count) ? -1 : _rows[r].integerValue;
}

/* orig: catalog_details. Text kinds 0..4 (PT name, EN name, PT description, EN description, source). */
- (void)details {
    NSInteger e = [self selectedEntry];
    NSMutableAttributedString *out = [[NSMutableAttributedString alloc] init];
    if (e < 0) {
        AddRich(out, @"Select a cheat group to see its details.", MHTheme.bodyFont, MHTheme.textSecondary, 0);
        Show(_details, out);
        return;
    }
    static NSString *const head[5] = {@"PT", @"EN", @"Description (PT)", @"Description (EN)", @"Source"};
    MHCheatManager *m = _mgr;
    static char buf[0x10000];
    BOOL any = NO;
    for (int k = 0; k < 5; k++)
        if (mh_cheat_db_entry_text(m.db, m.variant, (uint32_t)e, k, buf, sizeof buf)) {
            NSString *t = [S(buf) stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
            if (!t.length) continue;
            AddSection(out, head[k], t, NO);
            any = YES;
        }
    if (!any) AddRich(out, @"This group has no description.", MHTheme.bodyFont, MHTheme.textSecondary, 0);
    Show(_details, out);
}

/* orig: catalog_add */
- (void)add:(id)sender {
    MHCheatManager *m = _mgr;
    NSInteger e = [self selectedEntry];
    if (e < 0) return;
    char err[256] = "";
    if (!mh_cheat_profile_add_catalog_entry(m.profile, m.db, m.variant, (uint32_t)e, [m budget], err, sizeof err)) {
        NSString *msg = S(err).length ? S(err) : @"This group could not be added.";
        [_info setText:msg kind:MHMsgError];
        Announce(msg);
        return;
    }
    [m refresh];
    NSString *ok = [NSString stringWithFormat:@"Group added, turned off. Space used: %@ of %@ bytes.",
                                              Num(mh_cheat_profile_serialized_size(m.profile)), Num([m budget])];
    [_info setText:ok kind:MHMsgSuccess];
    Announce(ok);
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv { return (NSInteger)_rows.count; }
- (NSTableRowView *)tableView:(NSTableView *)tv rowViewForRow:(NSInteger)row {
    MHRow *rv = [[MHRow alloc] init];
    if (row < (NSInteger)_names.count) rv.axLabel = [NSString stringWithFormat:@"%@, %@ lines", _names[row], _lineCounts[row]];
    return rv;
}
- (NSView *)tableView:(NSTableView *)tv viewForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    BOOL isName = [col.identifier isEqualToString:@"name"];
    NSString *kind = isName ? @"text" : @"num";
    MHCell *c = [tv makeViewWithIdentifier:kind owner:self];
    if (!c) c = [MHCell cellOfKind:kind];
    c.textField.stringValue = isName ? _names[row] : _lineCounts[row];
    if (isName) c.toolTip = _names[row];
    return c;
}
- (void)tableViewSelectionDidChange:(NSNotification *)n { _bAdd.enabled = [self selectedEntry] >= 0; [self details]; }

- (void)testAction:(NSString *)a argument:(id)arg {
    if ([a isEqualToString:@"select"]) {
        NSMutableIndexSet *s = [NSMutableIndexSet indexSet];
        for (NSNumber *n in arg) [s addIndex:n.unsignedIntegerValue];
        [_table selectRowIndexes:s byExtendingSelection:NO];
        [_table scrollRowToVisible:s.firstIndex];
    } else if ([a isEqualToString:@"search"]) { _search.stringValue = arg; [self refresh]; }
    else if ([a isEqualToString:@"combo"]) { [_combo selectItemAtIndex:[arg integerValue]]; [self comboChanged:nil]; }
    else if ([a isEqualToString:@"add"]) [self add:nil];
    else if ([a isEqualToString:@"back"]) [self back:nil];
}
@end

/* ---- entry point (orig: cheat_ui_edit) --------------------------------------------------------- */

BOOL MHCheatEditProfile(NSWindow *parent, const MhCheatDb *db, NSString *headerText, const char *serial, uint8_t scope,
                        MhCheatProfile **profile) {
    MhCheatProfile *clone = mh_cheat_profile_clone(profile ? *profile : NULL);
    if (!clone) return NO;
    mh_cheat_profile_prepare_hardware(clone, serial ? serial : "");
    MHCheatManager *m = [[MHCheatManager alloc] init];
    m.parent = parent;
    m.db = db;
    m.profile = clone;
    m.scope = scope;
    m.variant = MH_NONE;
    uint32_t title = MH_NONE;
    MhCheatVariantInfo vi;
    if (db && serial && mh_cheat_db_find_title_for_disc(db, serial, &title) && title != MH_NONE &&
        mh_cheat_db_title_variant_at(db, title, 0, &vi))
        m.variant = vi.variant;
    else if (clone->variant != MH_NONE)
        m.variant = clone->variant;
    [m build:headerText.length ? headerText : TITLE_CHEATS];
    CenterOn(m.window, parent);
    [NSApp runModalForWindow:m.window];
    if (m.accepted && profile) {
        mh_cheat_profile_free(*profile);
        *profile = m.profile;
        m.profile = NULL;
    }
    return m.accepted;
}
