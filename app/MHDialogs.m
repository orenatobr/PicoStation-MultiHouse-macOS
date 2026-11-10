#import "MHDialogs.h"
#import "MHApp.h"
#import "MHTheme.h"
#include "mh/v2write.h"

@implementation MHWriteSummary
@end

/* ---------------------------------------------------------------- shared helpers */

static NSWindow *make_sheet(NSView *content) {
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 480, 300)
                                              styleMask:NSWindowStyleMaskTitled
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.releasedWhenClosed = NO;
    w.contentView = content;
    return w;
}

static NSTextField *wrap_label(NSString *t, NSFont *f, NSColor *c) {
    NSTextField *l = [NSTextField wrappingLabelWithString:t];
    l.font = f;
    l.textColor = c;
    l.selectable = NO;
    return l;
}

static NSButton *push(NSString *title, id target, SEL sel) {
    NSButton *b = [NSButton buttonWithTitle:title target:target action:sel];
    b.bezelStyle = NSBezelStyleRounded;
    b.controlSize = NSControlSizeLarge;
    return b;
}

static void make_prominent(NSButton *b, NSColor *fill) {
    b.bezelColor = fill;
    b.attributedTitle = [[NSAttributedString alloc] initWithString:b.title attributes:@{
        NSForegroundColorAttributeName : NSColor.whiteColor, NSFontAttributeName : MHTheme.headlineFont }];
}

static NSString *size_text(uint64_t bytes) {
    return [NSByteCountFormatter stringFromByteCount:(long long)bytes countStyle:NSByteCountFormatterCountStyleFile];
}

static NSStackView *button_row(NSArray<NSView *> *leading, NSArray<NSView *> *trailing) {
    NSView *flex = [[NSView alloc] initWithFrame:NSZeroRect];
    [flex setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSMutableArray *v = [NSMutableArray arrayWithArray:leading];
    [v addObject:flex];
    [v addObjectsFromArray:trailing];
    NSStackView *r = [NSStackView stackViewWithViews:v];
    r.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    r.spacing = MHSpace12;
    return r;
}

/* ================================================================ write confirmation */

@interface MHConfirmController : NSObject
@property(strong) NSWindow *window;
@property(strong) NSButton *check, *apply;
@property BOOL approved;
@end
@implementation MHConfirmController
- (void)toggled:(id)s { self.apply.enabled = self.check.state == NSControlStateValueOn; }
- (void)accept:(id)s {
    if (self.check.state != NSControlStateValueOn) return;
    self.approved = YES;
    MHEndSheet(self.window);
}
- (void)cancel:(id)s { self.approved = NO; MHEndSheet(self.window); }
@end

BOOL MHConfirmWrite(NSWindow *parent, MHWriteSummary *s) {
    BOOL editing = s.editingCard;
    NSString *disk = [NSString stringWithFormat:@"%@ (%@)", s.model.length ? s.model : @"SD card", s.bsd];
    NSString *title = editing ? [NSString stringWithFormat:@"Write changes to %@?", disk]
                              : [NSString stringWithFormat:@"Erase and write to %@?", disk];
    NSString *action = editing ? @"Write Changes" : @"Erase and Write";
    if (MHAlertHook) return MHAlertHook(title, s.details ?: @"", @[ action, @"Cancel" ]) == 0;

    MHConfirmController *c = [[MHConfirmController alloc] init];

    NSImageView *icon = [NSImageView imageViewWithImage:[MHTheme symbol:@"exclamationmark.triangle.fill" size:34 color:MHTheme.statusReview description:@"Warning"]];
    icon.accessibilityElement = NO;
    NSTextField *t = wrap_label(title, [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold], MHTheme.textPrimary);
    NSTextField *body = wrap_label(editing
        ? @"The pending changes will be written to this card. Games already written are preserved."
        : @"Everything on this card will be erased and replaced with your library. This cannot be undone.",
        MHTheme.bodyFont, MHTheme.textSecondary);
    NSStackView *texts = [NSStackView stackViewWithViews:@[ t, body ]];
    texts.orientation = NSUserInterfaceLayoutOrientationVertical;
    texts.alignment = NSLayoutAttributeLeading;
    texts.spacing = MHSpace4;
    NSStackView *head = [NSStackView stackViewWithViews:@[ icon, texts ]];
    head.alignment = NSLayoutAttributeTop;
    head.spacing = MHSpace16;

    /* summary */
    NSArray *rows = @[ @[ @"Card", [NSString stringWithFormat:@"%@, %@", disk, size_text(s.cardSize)] ],
                       @[ @"Serial", s.serial.length ? s.serial : @"Not provided" ],
                       @[ @"Library", [NSString stringWithFormat:@"%u %@, %u %@", s.cases, s.cases == 1 ? @"case" : @"cases",
                                       s.discs, s.discs == 1 ? @"disc" : @"discs"] ],
                       @[ @"Estimated size", [NSString stringWithFormat:@"About %@", size_text(s.estimate)] ] ];
    NSMutableArray *grid = [NSMutableArray array];
    for (NSArray *r in rows) {
        NSTextField *k = [NSTextField labelWithString:r[0]];
        k.font = MHTheme.bodyFont;
        k.textColor = MHTheme.textSecondary;
        k.alignment = NSTextAlignmentRight;
        NSTextField *v = wrap_label(r[1], MHTheme.bodyFont, MHTheme.textPrimary);
        v.selectable = YES;
        v.lineBreakMode = NSLineBreakByTruncatingMiddle;
        [grid addObject:@[ k, v ]];
    }
    NSGridView *gv = [NSGridView gridViewWithViews:grid];
    gv.rowSpacing = MHSpace8;
    gv.columnSpacing = MHSpace12;
    [gv columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    gv.translatesAutoresizingMaskIntoConstraints = NO;
    NSBox *box = [[NSBox alloc] initWithFrame:NSZeroRect];
    box.boxType = NSBoxCustom;
    box.cornerRadius = MHRadiusCard;
    box.borderWidth = 1;
    box.borderColor = MHTheme.cardStroke;
    box.fillColor = MHTheme.surfaceRaised;
    box.contentViewMargins = NSMakeSize(MHSpace16, MHSpace12);
    box.contentView = gv;
    box.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField *warn = wrap_label(@"Choosing the wrong disk can erase another device. Check the card above before you continue.",
                                   MHTheme.captionFont, MHTheme.textSecondary);

    c.check = [NSButton checkboxWithTitle:@"I confirm this is the correct card and I authorize the raw write."
                                   target:c action:@selector(toggled:)];
    c.check.font = MHTheme.bodyFont;
    c.check.accessibilityLabel = @"I confirm this is the correct card and I authorize the raw write.";
    c.check.accessibilityHelp = @"Required before the write can start";

    NSButton *cancel = push(@"Cancel", c, @selector(cancel:));
    cancel.keyEquivalent = @"\033";
    c.apply = push(action, c, @selector(accept:));
    make_prominent(c.apply, MHTheme.destructiveFill);
    c.apply.enabled = NO;
    c.apply.accessibilityHelp = @"Starts the write. Available after you tick the confirmation.";
    c.apply.toolTip = @"Tick the confirmation first";
    NSStackView *buttons = button_row(@[], @[ cancel, c.apply ]);

    NSStackView *all = [NSStackView stackViewWithViews:@[ head, box, warn, c.check, buttons ]];
    all.orientation = NSUserInterfaceLayoutOrientationVertical;
    all.alignment = NSLayoutAttributeLeading;
    all.spacing = MHSpace16;
    all.edgeInsets = NSEdgeInsetsMake(MHSpace24, MHSpace24, MHSpace24, MHSpace24);
    for (NSView *v in @[ box, warn, buttons ]) [v.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-2 * MHSpace24].active = YES;
    [all.widthAnchor constraintEqualToConstant:540].active = YES;
    [texts.widthAnchor constraintEqualToConstant:540 - 2 * MHSpace24 - 34 - MHSpace16].active = YES;
    NSView *host = [[NSView alloc] initWithFrame:NSZeroRect];
    all.translatesAutoresizingMaskIntoConstraints = NO;
    [host addSubview:all];
    [NSLayoutConstraint activateConstraints:@[ [all.topAnchor constraintEqualToAnchor:host.topAnchor],
                                               [all.leadingAnchor constraintEqualToAnchor:host.leadingAnchor],
                                               [all.trailingAnchor constraintEqualToAnchor:host.trailingAnchor],
                                               [all.bottomAnchor constraintEqualToAnchor:host.bottomAnchor] ]];
    [host layoutSubtreeIfNeeded];
    NSWindow *w = make_sheet(host);
    [w setContentSize:all.fittingSize];
    c.window = w;
    w.initialFirstResponder = c.check;
    [w makeFirstResponder:c.check];
    MHRunSheet(w, parent);
    return c.approved;
}

/* ================================================================ menu settings */

@interface MHMenuDefaultsController : NSObject
@property(strong) NSWindow *window;
@property(strong) NSPopUpButton *theme, *language, *visual;
@property(strong) NSSwitch *favorites, *music;
@property(strong) NSSlider *volume;
@property(strong) NSTextField *volumeLabel;
@property MhMenuDefaults d;
@property BOOL saved;
@property(assign) uint8_t *rec;
@end

@implementation MHMenuDefaultsController
- (void)populate {
    [self.theme selectItemAtIndex:self.d.theme];
    [self.language selectItemAtIndex:self.d.language];
    [self.visual selectItemAtIndex:self.d.visual];
    self.favorites.state = self.d.favorites ? NSControlStateValueOn : NSControlStateValueOff;
    self.music.state = self.d.music ? NSControlStateValueOn : NSControlStateValueOff;
    self.volume.intValue = self.d.volume;
    [self volumeChanged:nil];
}
- (void)volumeChanged:(id)s { self.volumeLabel.stringValue = [NSString stringWithFormat:@"%d", self.volume.intValue]; }
- (void)restore:(id)s {
    uint8_t r[MH_MENU_DEFAULTS_SIZE];
    mh_menu_defaults_factory(r);
    MhMenuDefaults d;
    if (mh_menu_defaults_parse(r, &d)) { self.d = d; [self populate]; }
}
- (void)save:(id)s {
    int v = self.volume.intValue;
    if (v < 0 || v > 100) return;
    MhMenuDefaults d = self.d;
    d.theme = (uint8_t)self.theme.indexOfSelectedItem;
    d.language = (uint8_t)self.language.indexOfSelectedItem;
    d.visual = (uint8_t)self.visual.indexOfSelectedItem;
    d.favorites = self.favorites.state == NSControlStateValueOn;
    d.music = self.music.state == NSControlStateValueOn;
    d.volume = (uint8_t)v;
    uint8_t r[MH_MENU_DEFAULTS_SIZE];
    if (!mh_menu_defaults_build(r, &d)) return;
    memcpy(self.rec, r, sizeof r);
    self.saved = YES;
    MHEndSheet(self.window);
}
- (void)cancel:(id)s { MHEndSheet(self.window); }
@end

static NSPopUpButton *choices(NSString *name, const char *const *items, int n, int sel) {
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    p.font = MHTheme.bodyFont;
    for (int i = 0; i < n; i++) [p addItemWithTitle:MHStr(items[i])];
    [p selectItemAtIndex:sel];
    p.accessibilityLabel = name;
    return p;
}

BOOL MHMenuDefaultsDialog(NSWindow *parent, uint8_t rec[MH_MENU_DEFAULTS_SIZE]) {
    MhMenuDefaults d;
    if (!mh_menu_defaults_parse(rec, &d)) return NO;
    MHMenuDefaultsController *c = [[MHMenuDefaultsController alloc] init];
    c.d = d;
    c.rec = rec;

    c.theme = choices(@"Theme", MH_MENU_THEMES, 3, d.theme);
    c.language = choices(@"Language", MH_MENU_LANGUAGES, 3, d.language);
    c.visual = choices(@"Layout", MH_MENU_VISUALS, 2, d.visual);
    c.favorites = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    c.favorites.accessibilityLabel = @"Start in favorites";
    c.music = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    c.music.accessibilityLabel = @"Umbrella theme music";
    c.volume = [NSSlider sliderWithValue:d.volume minValue:0 maxValue:100 target:c action:@selector(volumeChanged:)];
    c.volume.numberOfTickMarks = 0;
    c.volume.accessibilityLabel = @"Volume";
    c.volume.accessibilityHelp = @"From 0 to 100";
    c.volumeLabel = [NSTextField labelWithString:@"0"];
    c.volumeLabel.font = MHTheme.monoFont;
    c.volumeLabel.alignment = NSTextAlignmentRight;
    c.volumeLabel.accessibilityElement = NO;
    [c.volumeLabel.widthAnchor constraintEqualToConstant:32].active = YES;
    NSStackView *vol = [NSStackView stackViewWithViews:@[ c.volume, c.volumeLabel ]];
    vol.spacing = MHSpace8;
    [c.volume.widthAnchor constraintGreaterThanOrEqualToConstant:160].active = YES;

    NSString *labels[] = { @"Theme", @"Language", @"Layout", @"Start in favorites", @"Umbrella theme music", @"Volume" };
    NSView *ctrls[] = { c.theme, c.language, c.visual, c.favorites, c.music, vol };
    NSMutableArray *rows = [NSMutableArray array];
    for (int i = 0; i < 6; i++) {
        NSTextField *l = [NSTextField labelWithString:labels[i]];
        l.font = MHTheme.bodyFont;
        l.alignment = NSTextAlignmentRight;
        [rows addObject:@[ l, ctrls[i] ]];
    }
    NSGridView *gv = [NSGridView gridViewWithViews:rows];
    gv.rowSpacing = MHSpace12;
    gv.columnSpacing = MHSpace12;
    [gv columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [gv columnAtIndex:1].xPlacement = NSGridCellPlacementLeading;

    NSTextField *title = [NSTextField labelWithString:@"Menu Settings"];
    title.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    NSTextField *sub = wrap_label(@"These defaults are written to the menu on the card. A valid save on the Memory Card takes priority.",
                                  MHTheme.bodyFont, MHTheme.textSecondary);

    NSButton *restore = push(@"Restore Defaults", c, @selector(restore:));
    NSButton *cancel = push(@"Cancel", c, @selector(cancel:));
    cancel.keyEquivalent = @"\033";
    NSButton *save = push(@"Save", c, @selector(save:));
    make_prominent(save, MHTheme.brandFill);
    save.keyEquivalent = @"\r";
    NSStackView *buttons = button_row(@[ restore ], @[ cancel, save ]);

    NSStackView *all = [NSStackView stackViewWithViews:@[ title, sub, gv, buttons ]];
    all.orientation = NSUserInterfaceLayoutOrientationVertical;
    all.alignment = NSLayoutAttributeLeading;
    all.spacing = MHSpace16;
    all.edgeInsets = NSEdgeInsetsMake(MHSpace24, MHSpace24, MHSpace24, MHSpace24);
    [all.widthAnchor constraintEqualToConstant:480].active = YES;
    [sub.widthAnchor constraintEqualToConstant:480 - 2 * MHSpace24].active = YES;
    [buttons.widthAnchor constraintEqualToConstant:480 - 2 * MHSpace24].active = YES;
    [all setCustomSpacing:MHSpace24 afterView:gv];
    all.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *host = [[NSView alloc] initWithFrame:NSZeroRect];
    [host addSubview:all];
    [NSLayoutConstraint activateConstraints:@[ [all.topAnchor constraintEqualToAnchor:host.topAnchor],
                                               [all.leadingAnchor constraintEqualToAnchor:host.leadingAnchor],
                                               [all.trailingAnchor constraintEqualToAnchor:host.trailingAnchor],
                                               [all.bottomAnchor constraintEqualToAnchor:host.bottomAnchor] ]];
    [host layoutSubtreeIfNeeded];
    NSWindow *w = make_sheet(host);
    [w setContentSize:all.fittingSize];
    c.window = w;
    [c populate];
    if (MHAlertHook) { /* tests: index 0 = Save keeping the shown values */
        if (MHAlertHook(@"Menu Settings", @"", @[ @"Save", @"Cancel" ]) == 0) {
            [c save:nil];
            return c.saved;
        }
        return NO;
    }
    MHRunSheet(w, parent);
    return c.saved;
}

/* ================================================================ details sheet */

@interface MHDetailsController : NSObject
@property(strong) NSWindow *window;
@property(strong) NSTextView *text;
@property(strong) NSButton *btnCopy;
@end
@implementation MHDetailsController
- (void)copyAll:(id)s {
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:self.text.string forType:NSPasteboardTypeString];
    self.btnCopy.title = @"Copied";
}
- (void)close:(id)s { MHEndSheet(self.window); }
@end

void MHShowDetails(NSWindow *parent, NSString *title, NSString *text) {
    if (MHAlertHook) { MHAlertHook(title, text, @[ @"OK" ]); return; }
    MHDetailsController *c = [[MHDetailsController alloc] init];
    NSTextField *t = [NSTextField labelWithString:title];
    t.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    c.text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 440, 240)];
    c.text.editable = NO;
    c.text.selectable = YES;
    c.text.font = MHTheme.bodyFont;
    c.text.textColor = MHTheme.textPrimary;
    c.text.drawsBackground = NO;
    c.text.textContainerInset = NSMakeSize(MHSpace8, MHSpace8);
    c.text.verticallyResizable = YES;
    c.text.autoresizingMask = NSViewWidthSizable;
    c.text.textContainer.widthTracksTextView = YES;
    c.text.string = MHDisplay(text);
    c.text.accessibilityLabel = title;
    NSScrollView *sc = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sc.documentView = c.text;
    sc.hasVerticalScroller = YES;
    sc.autohidesScrollers = YES;
    sc.borderType = NSNoBorder;
    sc.drawsBackground = NO;
    NSBox *box = [[NSBox alloc] initWithFrame:NSZeroRect];
    box.boxType = NSBoxCustom;
    box.cornerRadius = MHRadiusCard;
    box.borderWidth = 1;
    box.borderColor = MHTheme.cardStroke;
    box.fillColor = MHTheme.surfaceRaised;
    box.contentViewMargins = NSZeroSize;
    box.contentView = sc;
    box.translatesAutoresizingMaskIntoConstraints = NO;
    c.btnCopy = push(@"Copy", c, @selector(copyAll:));
    NSButton *close = push(@"Close", c, @selector(close:));
    make_prominent(close, MHTheme.brandFill);
    close.keyEquivalent = @"\r";
    NSStackView *buttons = button_row(@[ c.btnCopy ], @[ close ]);
    NSStackView *all = [NSStackView stackViewWithViews:@[ t, box, buttons ]];
    all.orientation = NSUserInterfaceLayoutOrientationVertical;
    all.alignment = NSLayoutAttributeLeading;
    all.spacing = MHSpace16;
    all.edgeInsets = NSEdgeInsetsMake(MHSpace24, MHSpace24, MHSpace24, MHSpace24);
    [all.widthAnchor constraintEqualToConstant:500].active = YES;
    [box.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-2 * MHSpace24].active = YES;
    [box.heightAnchor constraintEqualToConstant:240].active = YES;
    [buttons.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-2 * MHSpace24].active = YES;
    all.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *host = [[NSView alloc] initWithFrame:NSZeroRect];
    [host addSubview:all];
    [NSLayoutConstraint activateConstraints:@[ [all.topAnchor constraintEqualToAnchor:host.topAnchor],
                                               [all.leadingAnchor constraintEqualToAnchor:host.leadingAnchor],
                                               [all.trailingAnchor constraintEqualToAnchor:host.trailingAnchor],
                                               [all.bottomAnchor constraintEqualToAnchor:host.bottomAnchor] ]];
    [host layoutSubtreeIfNeeded];
    NSWindow *w = make_sheet(host);
    [w setContentSize:all.fittingSize];
    c.window = w;
    MHRunSheet(w, parent);
}

/* ================================================================ progress sheet */

NSString *MHPhaseTitle(int phase) {
    switch (phase) {
        case MH_V2_PHASE_PREPARING: return @"Preparing";
        case MH_V2_PHASE_VERIFYING_SOURCES: return @"Verifying sources";
        case MH_V2_PHASE_WRITING_DISCS: return @"Writing discs";
        case MH_V2_PHASE_UPDATING_TABLES: return @"Updating tables and art";
        case MH_V2_PHASE_COMMITTING_HEADER: return @"Committing the library";
        case MH_V2_PHASE_VERIFYING_CARD: return @"Verifying the card";
        case MH_V2_PHASE_DONE: return @"Done";
        default: return @"Working";
    }
}

@implementation MHProgressSheet {
    NSWindow *_parent, *_window;
    NSTextField *_title, *_step, *_item, *_resultTitle;
    NSProgressIndicator *_bar;
    NSButton *_cancel, *_done, *_copy;
    NSView *_progressBox, *_resultBox;
    NSImageView *_resultIcon;
    NSTextView *_resultText;
    NSString *_failureText;
    dispatch_block_t _completion;
    BOOL _closed;
}

+ (instancetype)presentOn:(NSWindow *)parent title:(NSString *)title {
    MHProgressSheet *s = [[MHProgressSheet alloc] init];
    [s build:title];
    s->_parent = parent;
    if (parent && parent.isVisible && !parent.attachedSheet) [parent beginSheet:s->_window completionHandler:nil];
    else { [s->_window center]; [s->_window makeKeyAndOrderFront:nil]; }
    return s;
}

- (void)build:(NSString *)titleText {
    _title = [NSTextField labelWithString:titleText];
    _title.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    _title.lineBreakMode = NSLineBreakByTruncatingTail;
    _step = [NSTextField labelWithString:@"Preparing"];
    _step.font = MHTheme.bodyFont;
    _step.textColor = MHTheme.textSecondary;
    _bar = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _bar.style = NSProgressIndicatorStyleBar;
    _bar.indeterminate = NO;
    _bar.minValue = 0;
    _bar.maxValue = 100;
    _bar.accessibilityLabel = @"Write progress";
    _item = [NSTextField labelWithString:@""];
    _item.font = MHTheme.captionFont;
    _item.textColor = MHTheme.textSecondary;
    _item.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _item.accessibilityLabel = @"Current item";
    _cancel = push(@"Cancel", self, @selector(cancel:));
    _cancel.keyEquivalent = @"\033";
    _cancel.accessibilityHelp = @"Stops the write at the next safe point";
    NSStackView *pb = [NSStackView stackViewWithViews:@[ _title, _step, _bar, _item, button_row(@[], @[ _cancel ]) ]];
    pb.orientation = NSUserInterfaceLayoutOrientationVertical;
    pb.alignment = NSLayoutAttributeLeading;
    pb.spacing = MHSpace8;
    [pb setCustomSpacing:MHSpace12 afterView:_step];
    [pb setCustomSpacing:MHSpace16 afterView:_item];
    pb.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView *v in @[ _bar, _item, pb.views.lastObject ]) [v.widthAnchor constraintEqualToAnchor:pb.widthAnchor].active = YES;
    _progressBox = pb;

    _resultIcon = [[NSImageView alloc] initWithFrame:NSZeroRect];
    _resultIcon.accessibilityElement = NO;
    _resultTitle = [NSTextField labelWithString:@""];
    _resultTitle.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    _resultTitle.lineBreakMode = NSLineBreakByTruncatingTail;
    NSStackView *rh = [NSStackView stackViewWithViews:@[ _resultIcon, _resultTitle ]];
    rh.spacing = MHSpace12;
    _resultText = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 380, 120)];
    _resultText.editable = NO;
    _resultText.selectable = YES;
    _resultText.font = MHTheme.bodyFont;
    _resultText.drawsBackground = NO;
    _resultText.textContainerInset = NSMakeSize(MHSpace8, MHSpace8);
    _resultText.verticallyResizable = YES;
    _resultText.autoresizingMask = NSViewWidthSizable;
    _resultText.textContainer.widthTracksTextView = YES;
    _resultText.accessibilityLabel = @"Result details";
    NSScrollView *rs = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    rs.documentView = _resultText;
    rs.hasVerticalScroller = YES;
    rs.autohidesScrollers = YES;
    rs.borderType = NSNoBorder;
    rs.drawsBackground = NO;
    NSBox *rbox = [[NSBox alloc] initWithFrame:NSZeroRect];
    rbox.boxType = NSBoxCustom;
    rbox.cornerRadius = MHRadiusCard;
    rbox.borderWidth = 1;
    rbox.borderColor = MHTheme.cardStroke;
    rbox.fillColor = MHTheme.surfaceRaised;
    rbox.contentViewMargins = NSZeroSize;
    rbox.contentView = rs;
    rbox.translatesAutoresizingMaskIntoConstraints = NO;
    _copy = push(@"Copy Details", self, @selector(copyDetails:));
    _done = push(@"Done", self, @selector(close:));
    make_prominent(_done, MHTheme.brandFill);
    _done.keyEquivalent = @"\r";
    NSStackView *rb = [NSStackView stackViewWithViews:@[ rh, rbox, button_row(@[ _copy ], @[ _done ]) ]];
    rb.orientation = NSUserInterfaceLayoutOrientationVertical;
    rb.alignment = NSLayoutAttributeLeading;
    rb.spacing = MHSpace16;
    rb.translatesAutoresizingMaskIntoConstraints = NO;
    [rbox.widthAnchor constraintEqualToAnchor:rb.widthAnchor].active = YES;
    [rbox.heightAnchor constraintEqualToConstant:132].active = YES;
    [rb.views.lastObject.widthAnchor constraintEqualToAnchor:rb.widthAnchor].active = YES;
    _resultBox = rb;
    _resultBox.hidden = YES;

    NSView *host = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 232)];
    for (NSView *v in @[ _progressBox, _resultBox ]) {
        [host addSubview:v];
        [NSLayoutConstraint activateConstraints:@[
            [v.topAnchor constraintEqualToAnchor:host.topAnchor constant:MHSpace24],
            [v.leadingAnchor constraintEqualToAnchor:host.leadingAnchor constant:MHSpace24],
            [v.trailingAnchor constraintEqualToAnchor:host.trailingAnchor constant:-MHSpace24] ]];
    }
    [_resultBox.bottomAnchor constraintEqualToAnchor:host.bottomAnchor constant:-MHSpace24].active = YES;
    [_progressBox.bottomAnchor constraintLessThanOrEqualToAnchor:host.bottomAnchor constant:-MHSpace24].active = YES;
    _window = make_sheet(host);
    [_window setContentSize:NSMakeSize(480, 232)];
    _window.initialFirstResponder = _cancel;
}

- (void)updatePhase:(int)phase step:(int)step steps:(int)steps fraction:(double)f text:(NSString *)text
      cancelEnabled:(BOOL)ce cancelling:(BOOL)cancelling {
    if (_closed) return;
    NSString *ph = MHPhaseTitle(phase);
    if (phase >= 1 && phase <= MH_V2_PHASE_STEPS)
        _step.stringValue = [NSString stringWithFormat:@"Step %d of %d  ·  %@", phase, MH_V2_PHASE_STEPS, ph];
    else
        _step.stringValue = ph;
    NSMutableString *item = [NSMutableString string];
    if (cancelling) [item appendString:@"Cancelling at the next safe point…"];
    else if (!ce) [item appendString:@"Finishing safely… this step cannot be cancelled."];
    else {
        if (steps > 0 && step > 0) [item appendFormat:@"Disc %d of %d. ", step, steps];
        [item appendString:text ?: @""];
    }
    _item.stringValue = item;
    _bar.doubleValue = MAX(0, MIN(100, f));
    _bar.accessibilityValue = [NSString stringWithFormat:@"%d percent", (int)_bar.doubleValue];
    _cancel.enabled = ce && !cancelling;
}

- (void)cancel:(id)s { if (self.onCancel) self.onCancel(); }

- (void)showResultIcon:(NSString *)sym color:(NSColor *)c desc:(NSString *)d title:(NSString *)t text:(NSString *)body {
    _resultIcon.image = [MHTheme symbol:sym size:28 color:c description:d];
    _resultTitle.stringValue = t;
    _resultText.string = MHDisplay(body ?: @"");
    _progressBox.hidden = YES;
    _resultBox.hidden = NO;
    [_window makeFirstResponder:_done];
}

- (void)finishSuccessTitle:(NSString *)title summary:(NSString *)summary completion:(dispatch_block_t)completion {
    if (MHAlertHook) {
        MHAlertHook(title, summary, @[ @"OK" ]);
        [self close:nil];
        if (completion) completion();
        return;
    }
    _completion = completion;
    _copy.hidden = YES;
    [self showResultIcon:@"checkmark.seal.fill" color:MHTheme.statusVerified desc:@"Success" title:title text:summary];
}

- (void)finishFailureTitle:(NSString *)title text:(NSString *)text completion:(dispatch_block_t)completion {
    if (MHAlertHook) {
        MHAlertHook(title, text, @[ @"OK" ]);
        [self close:nil];
        if (completion) completion();
        return;
    }
    _completion = completion;
    _failureText = text;
    _copy.hidden = NO;
    _done.title = @"Close";
    make_prominent(_done, MHTheme.brandFill);
    [self showResultIcon:@"xmark.octagon.fill" color:MHTheme.statusError desc:@"Error" title:title text:text];
}

- (void)copyDetails:(id)s {
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:MHDisplay(_failureText ?: _resultText.string) forType:NSPasteboardTypeString];
    _copy.title = @"Copied";
}

- (void)close:(id)s {
    if (_closed) return;
    _closed = YES;
    if (_window.sheetParent) [_window.sheetParent endSheet:_window];
    [_window orderOut:nil];
    dispatch_block_t c = _completion;
    _completion = nil;
    if (c && s) c();
}

- (void)dismiss { _completion = nil; [self close:nil]; }
@end
