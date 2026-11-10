#import "MHPanes.h"

@interface MHFlipped : NSView
@end
@implementation MHFlipped
- (BOOL)isFlipped { return YES; }
@end

/* Re-resolves layer colors (CGColor does not follow the appearance by itself). */
@interface MHAppearanceView : NSView
@property(copy) void (^onAppearance)(void);
@end
@implementation MHAppearanceView
- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{ if (self.onAppearance) self.onAppearance(); }];
}
@end

static NSTextField *caption_label(NSString *t) {
    NSTextField *l = [NSTextField labelWithString:t];
    l.font = MHTheme.captionFont;
    l.textColor = MHTheme.textSecondary;
    return l;
}

static NSView *hairline(void) {
    NSBox *b = [[NSBox alloc] initWithFrame:NSZeroRect];
    b.boxType = NSBoxSeparator;
    b.translatesAutoresizingMaskIntoConstraints = NO;
    return b;
}

/* ---------------------------------------------------------------- sidebar */

@implementation MHSidebarViewController {
    NSTextField *_footer;
}

- (void)loadView {
    NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 280, 600)];
    self.view = v;

    NSTextField *head = [NSTextField labelWithString:@"LIBRARY"];
    head.font = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    head.textColor = MHTheme.textSecondary;
    head.accessibilityRole = NSAccessibilityStaticTextRole;
    head.accessibilityLabel = @"Library";

    _table = [[MHListTable alloc] initWithFrame:NSZeroRect];
    _table.style = NSTableViewStyleSourceList;
    _table.headerView = nil;
    _table.rowHeight = 56;
    _table.allowsMultipleSelection = NO;
    _table.allowsEmptySelection = YES;
    _table.usesAutomaticRowHeights = NO;
    _table.intercellSpacing = NSMakeSize(0, 2);
    _table.focusRingType = NSFocusRingTypeDefault;
    _table.identifier = @"cases";
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"case"];
    col.resizingMask = NSTableColumnAutoresizingMask;
    [_table addTableColumn:col];
    _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    _table.accessibilityLabel = @"Cases";
    _table.accessibilityHelp = @"Your library. Use the arrow keys to choose a case, Option Command Up or Down to reorder.";

    _scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _scroll.documentView = _table;
    _scroll.hasVerticalScroller = YES;
    _scroll.autohidesScrollers = YES;
    _scroll.drawsBackground = NO;
    _scroll.borderType = NSNoBorder;
    _scroll.accessibilityLabel = @"Cases";

    _footer = caption_label(@"No cases");
    _footer.accessibilityLabel = @"Library totals";
    NSView *line = hairline();

    for (NSView *x in @[ head, _scroll, line, _footer ]) {
        x.translatesAutoresizingMaskIntoConstraints = NO;
        [v addSubview:x];
    }
    NSLayoutGuide *safe = v.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [head.topAnchor constraintEqualToAnchor:safe.topAnchor constant:MHSpace8],
        [head.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:MHSpace16 + MHSpace4],
        [_scroll.topAnchor constraintEqualToAnchor:head.bottomAnchor constant:MHSpace4],
        [_scroll.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [_scroll.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [_scroll.bottomAnchor constraintEqualToAnchor:line.topAnchor],
        [line.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:MHSpace12],
        [line.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-MHSpace12],
        [line.bottomAnchor constraintEqualToAnchor:_footer.topAnchor constant:-MHSpace8],
        [_footer.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:MHSpace16 + MHSpace4],
        [_footer.trailingAnchor constraintLessThanOrEqualToAnchor:v.trailingAnchor constant:-MHSpace16],
        [_footer.bottomAnchor constraintEqualToAnchor:v.bottomAnchor constant:-MHSpace8]
    ]];
}

- (void)setTotalsCases:(NSUInteger)c discs:(NSUInteger)d {
    [self view];
    _footer.stringValue = c == 0 ? @"No cases"
        : [NSString stringWithFormat:@"%lu %@, %lu %@", (unsigned long)c, c == 1 ? @"case" : @"cases",
                                     (unsigned long)d, d == 1 ? @"disc" : @"discs"];
}
@end

/* ---------------------------------------------------------------- content */

@implementation MHContentViewController {
    NSView *_header, *_listBox;
}

- (void)loadView {
    NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 600)];
    self.view = v;

    /* header */
    _fCase = [NSTextField textFieldWithString:@""];
    _fCase.font = MHTheme.titleFont;
    _fCase.bordered = NO;
    _fCase.bezeled = NO;
    _fCase.drawsBackground = NO;
    _fCase.editable = YES;
    _fCase.usesSingleLineMode = YES;
    _fCase.lineBreakMode = NSLineBreakByTruncatingTail;
    _fCase.cell.scrollable = YES;
    _fCase.placeholderString = @"Case name";
    _fCase.accessibilityLabel = @"Case name";
    _fCase.accessibilityHelp = @"Edit the case title. Press Return to apply.";
    _fCase.toolTip = @"Click to rename this case";
    _fCase.focusRingType = NSFocusRingTypeExterior;
    _caption = caption_label(@"");
    _caption.font = MHTheme.bodyFont;
    _caption.accessibilityLabel = @"Disc count";

    _chkVerify = [NSButton checkboxWithTitle:@"Verify on add" target:nil action:nil];
    _chkVerify.font = MHTheme.bodyFont;
    _chkVerify.state = NSControlStateValueOn;
    _chkVerify.accessibilityLabel = @"Verify on add";
    _chkVerify.accessibilityHelp = @"Read every disc fully when it is added. Slower, but catches bad dumps.";
    _chkVerify.toolTip = @"Read every disc fully when it is added (slower, catches bad dumps)";

    _header = [[NSView alloc] initWithFrame:NSZeroRect];
    NSView *hl = hairline();
    for (NSView *x in @[ _fCase, _caption, _chkVerify, hl ]) {
        x.translatesAutoresizingMaskIntoConstraints = NO;
        [_header addSubview:x];
    }
    [_chkVerify setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [NSLayoutConstraint activateConstraints:@[
        [_fCase.topAnchor constraintEqualToAnchor:_header.topAnchor constant:MHSpace16],
        [_fCase.leadingAnchor constraintEqualToAnchor:_header.leadingAnchor constant:MHSpace16],
        [_fCase.trailingAnchor constraintEqualToAnchor:_header.trailingAnchor constant:-MHSpace16],
        [_caption.topAnchor constraintEqualToAnchor:_fCase.bottomAnchor constant:MHSpace4],
        [_caption.leadingAnchor constraintEqualToAnchor:_header.leadingAnchor constant:MHSpace16 + 5],
        [_chkVerify.centerYAnchor constraintEqualToAnchor:_caption.centerYAnchor],
        [_chkVerify.trailingAnchor constraintEqualToAnchor:_header.trailingAnchor constant:-MHSpace16],
        [_chkVerify.leadingAnchor constraintGreaterThanOrEqualToAnchor:_caption.trailingAnchor constant:MHSpace16],
        [hl.topAnchor constraintEqualToAnchor:_caption.bottomAnchor constant:MHSpace12],
        [hl.leadingAnchor constraintEqualToAnchor:_header.leadingAnchor],
        [hl.trailingAnchor constraintEqualToAnchor:_header.trailingAnchor],
        [hl.bottomAnchor constraintEqualToAnchor:_header.bottomAnchor]
    ]];

    /* disc cards */
    _table = [[MHListTable alloc] initWithFrame:NSZeroRect];
    _table.headerView = nil;
    _table.rowHeight = 72;
    _table.intercellSpacing = NSMakeSize(0, 0);
    _table.allowsMultipleSelection = NO;
    _table.allowsEmptySelection = YES;
    _table.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    _table.backgroundColor = NSColor.clearColor;
    _table.gridStyleMask = NSTableViewGridNone;
    _table.focusRingType = NSFocusRingTypeNone; /* the selected card draws its own ring */
    _table.identifier = @"discs";
    NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:@"disc"];
    col.resizingMask = NSTableColumnAutoresizingMask;
    [_table addTableColumn:col];
    _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    if (@available(macOS 11.0, *)) _table.style = NSTableViewStylePlain;
    _table.accessibilityLabel = @"Discs";
    _table.accessibilityHelp = @"Discs of the selected case. Option Command Up or Down reorders the selected disc.";

    _scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _scroll.documentView = _table;
    _scroll.hasVerticalScroller = YES;
    _scroll.autohidesScrollers = YES;
    _scroll.drawsBackground = NO;
    _scroll.automaticallyAdjustsContentInsets = NO;
    _scroll.contentInsets = NSEdgeInsetsMake(MHSpace8, MHSpace16, MHSpace16, MHSpace16);
    _scroll.borderType = NSNoBorder;
    _scroll.accessibilityLabel = @"Discs";

    _empty = [[MHEmptyStateView alloc] initWithFrame:NSZeroRect];
    _overlay = [[MHDropOverlay alloc] initWithFrame:NSZeroRect];

    for (NSView *x in @[ _header, _scroll, _empty, _overlay ]) {
        x.translatesAutoresizingMaskIntoConstraints = NO;
        [v addSubview:x];
    }
    NSLayoutGuide *safe = v.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_header.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [_header.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [_header.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [_scroll.topAnchor constraintEqualToAnchor:_header.bottomAnchor],
        [_scroll.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [_scroll.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [_scroll.bottomAnchor constraintEqualToAnchor:v.bottomAnchor],
        [_empty.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [_empty.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [_empty.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [_empty.bottomAnchor constraintEqualToAnchor:v.bottomAnchor],
        [_overlay.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [_overlay.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [_overlay.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [_overlay.bottomAnchor constraintEqualToAnchor:v.bottomAnchor]
    ]];
    self.mode = MHContentLibraryEmpty;
}

- (void)setMode:(MHContentMode)m {
    [self view];
    BOOL changed = m != _mode;
    _mode = m;
    BOOL cs = m == MHContentCase;
    _header.hidden = !cs;
    _scroll.hidden = !cs;
    _empty.hidden = cs;
    if (!cs) [_empty setLibraryEmpty:m == MHContentLibraryEmpty];
    if (changed) [MHTheme crossFade:self.view];
}
@end

/* ---------------------------------------------------------------- inspector */

@implementation MHInspectorViewController {
    NSButton *_disclosure;
    NSScrollView *_detailsScroll, *_mainScroll;
    NSStackView *_placeholder;
}

- (void)setHasSelection:(BOOL)h {
    [self view];
    _hasSelection = h;
    _mainScroll.hidden = !h;
    _placeholder.hidden = h;
}

- (NSStackView *)vstack:(NSArray *)views spacing:(CGFloat)s {
    NSStackView *st = [NSStackView stackViewWithViews:views];
    st.orientation = NSUserInterfaceLayoutOrientationVertical;
    st.alignment = NSLayoutAttributeLeading;
    st.spacing = s;
    return st;
}

- (NSButton *)actionButton:(NSString *)title symbol:(NSString *)sym help:(NSString *)help {
    NSButton *b = [NSButton buttonWithTitle:title target:nil action:nil];
    b.bezelStyle = NSBezelStyleRounded;
    b.controlSize = NSControlSizeRegular;
    b.image = [MHTheme symbol:sym size:13 description:nil];
    b.imagePosition = NSImageLeft;
    b.accessibilityLabel = [title stringByReplacingOccurrencesOfString:@"…" withString:@""];
    b.accessibilityHelp = help;
    b.toolTip = help;
    return b;
}

- (NSTextField *)field:(NSString *)label {
    NSTextField *f = [NSTextField textFieldWithString:@""];
    f.font = MHTheme.bodyFont;
    f.editable = YES;
    f.usesSingleLineMode = YES;
    f.cell.scrollable = YES;
    f.lineBreakMode = NSLineBreakByTruncatingTail;
    f.accessibilityLabel = label;
    f.placeholderString = label;
    return f;
}

- (void)loadView {
    MHAppearanceView *root = [[MHAppearanceView alloc] initWithFrame:NSMakeRect(0, 0, 320, 600)];
    self.view = root;
    __weak typeof(self) weak = self;
    root.onAppearance = ^{ typeof(self) s = weak; if (!s) return; s->_detailsScroll.layer.borderColor = MHTheme.cardStroke.CGColor; };

    _pvCase = [[MHThumbnailView alloc] initWithFrame:NSMakeRect(0, 0, 96, 96)];
    _pvCase.cornerRadius = MHRadiusCard;
    _pvCase.placeholderSymbol = @"photo";
    [_pvCase setAccessibilityWhat:@"Case art"];
    _pvDisc = [[MHThumbnailView alloc] initWithFrame:NSMakeRect(0, 0, 96, 96)];
    _pvDisc.shape = MHThumbDisc;
    [_pvDisc setAccessibilityWhat:@"Disc art"];
    for (MHThumbnailView *t in @[ _pvCase, _pvDisc ]) {
        t.translatesAutoresizingMaskIntoConstraints = NO;
        [t.widthAnchor constraintEqualToConstant:96].active = YES;
        [t.heightAnchor constraintEqualToConstant:96].active = YES;
    }
    _btnCaseArt = [self actionButton:@"Replace Case Art…" symbol:@"photo" help:@"Choose an image for the case art"];
    _btnDiscArt = [self actionButton:@"Replace Disc Art…" symbol:@"photo" help:@"Choose an image for the disc art"];
    _btnCheats = [self actionButton:@"Cheats (0)…" symbol:@"bolt" help:@"Edit the cheats of the selected disc"];
    _fCase = [self field:@"Case name"];
    _fDisc = [self field:@"Disc name"];

    NSTextField *hCase = [NSTextField labelWithString:@"Case"], *hDisc = [NSTextField labelWithString:@"Disc"];
    for (NSTextField *h in @[ hCase, hDisc ]) {
        h.font = MHTheme.headlineFont;
        h.accessibilityRole = NSAccessibilityStaticTextRole;
    }

    NSStackView *caseRow = [NSStackView stackViewWithViews:@[ _pvCase, [self vstack:@[ _btnCaseArt ] spacing:MHSpace8] ]];
    caseRow.alignment = NSLayoutAttributeCenterY;
    caseRow.spacing = MHSpace12;
    NSStackView *discRow = [NSStackView stackViewWithViews:@[ _pvDisc, [self vstack:@[ _btnDiscArt ] spacing:MHSpace8] ]];
    discRow.alignment = NSLayoutAttributeCenterY;
    discRow.spacing = MHSpace12;

    NSTextField *lCase = caption_label(@"Case name"), *lDisc = caption_label(@"Disc name");

    /* details disclosure */
    _disclosure = [NSButton buttonWithTitle:@"Details" target:self action:@selector(toggleDetails:)];
    _disclosure.bezelStyle = NSBezelStyleDisclosure;
    _disclosure.buttonType = NSButtonTypePushOnPushOff;
    _disclosure.state = NSControlStateValueOn;
    _disclosure.accessibilityLabel = @"Details";
    _disclosure.accessibilityHelp = @"Shows or hides the verification notes of the selected disc";
    NSTextField *hDet = [NSTextField labelWithString:@"Details"];
    hDet.font = MHTheme.headlineFont;
    NSStackView *detHead = [NSStackView stackViewWithViews:@[ _disclosure, hDet ]];
    detHead.spacing = MHSpace4;
    _disclosure.title = @"";

    _details = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 280, 100)];
    _details.editable = NO;
    _details.selectable = YES;
    _details.font = MHTheme.bodyFont;
    _details.textColor = MHTheme.textPrimary;
    _details.drawsBackground = NO;
    _details.verticallyResizable = YES;
    _details.horizontallyResizable = NO;
    _details.autoresizingMask = NSViewWidthSizable;
    _details.textContainer.widthTracksTextView = YES;
    _details.textContainerInset = NSMakeSize(MHSpace8, MHSpace8);
    _details.accessibilityLabel = @"Details";
    _detailsScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _detailsScroll.documentView = _details;
    _detailsScroll.hasVerticalScroller = YES;
    _detailsScroll.autohidesScrollers = YES;
    _detailsScroll.borderType = NSNoBorder;
    _detailsScroll.drawsBackground = YES;
    _detailsScroll.backgroundColor = MHTheme.surfaceRaised;
    _detailsScroll.wantsLayer = YES;
    _detailsScroll.layer.cornerRadius = MHRadiusCard;
    _detailsScroll.layer.borderWidth = 1;
    _detailsScroll.accessibilityLabel = @"Verification notes";
    _detailsScroll.translatesAutoresizingMaskIntoConstraints = NO;

    NSStackView *stack = [self vstack:@[ hCase, caseRow, lCase, _fCase, [self spacer], hDisc, discRow, lDisc, _fDisc, _btnCheats,
                                         [self spacer], detHead, _detailsScroll ]
                              spacing:MHSpace8];
    [stack setCustomSpacing:MHSpace4 afterView:lCase];
    [stack setCustomSpacing:MHSpace4 afterView:lDisc];
    [stack setCustomSpacing:MHSpace12 afterView:_fDisc];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView *v in @[ _fCase, _fDisc, _btnCheats, _detailsScroll ])
        [v.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;

    MHFlipped *doc = [[MHFlipped alloc] initWithFrame:NSMakeRect(0, 0, 320, 600)];
    [doc addSubview:stack];
    NSScrollView *sc = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sc.documentView = doc;
    sc.hasVerticalScroller = YES;
    sc.autohidesScrollers = YES;
    sc.drawsBackground = NO;
    sc.borderType = NSNoBorder;
    sc.automaticallyAdjustsContentInsets = NO;
    sc.contentInsets = NSEdgeInsetsMake(0, 0, 0, 0);
    sc.accessibilityLabel = @"Inspector";
    sc.translatesAutoresizingMaskIntoConstraints = NO;
    doc.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:sc];
    _mainScroll = sc;
    NSImageView *pi = [NSImageView imageViewWithImage:[MHTheme symbol:@"sidebar.right" size:34 weight:NSFontWeightLight description:nil]];
    pi.contentTintColor = MHTheme.textSecondary;
    pi.accessibilityElement = NO;
    NSTextField *pt = [NSTextField labelWithString:@"Select a case"];
    pt.font = MHTheme.headlineFont;
    NSTextField *pm = [NSTextField wrappingLabelWithString:@"Its art, names, cheats and verification details appear here."];
    pm.font = MHTheme.bodyFont;
    pm.textColor = MHTheme.textSecondary;
    pm.alignment = NSTextAlignmentCenter;
    _placeholder = [NSStackView stackViewWithViews:@[ pi, pt, pm ]];
    _placeholder.orientation = NSUserInterfaceLayoutOrientationVertical;
    _placeholder.spacing = MHSpace8;
    _placeholder.translatesAutoresizingMaskIntoConstraints = NO;
    _placeholder.accessibilityLabel = @"No case selected";
    [root addSubview:_placeholder];
    [NSLayoutConstraint activateConstraints:@[
        [_placeholder.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
        [_placeholder.centerYAnchor constraintEqualToAnchor:root.centerYAnchor],
        [_placeholder.widthAnchor constraintLessThanOrEqualToAnchor:root.widthAnchor constant:-2 * MHSpace24] ]];
    _placeholder.hidden = YES;
    root.onAppearance();
    NSLayoutGuide *safe = root.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [sc.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [sc.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [sc.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [sc.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [doc.topAnchor constraintEqualToAnchor:sc.contentView.topAnchor],
        [doc.leadingAnchor constraintEqualToAnchor:sc.contentView.leadingAnchor],
        [doc.trailingAnchor constraintEqualToAnchor:sc.contentView.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:doc.topAnchor constant:MHSpace16],
        [stack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor constant:MHSpace16],
        [stack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor constant:-MHSpace16],
        [stack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor constant:-MHSpace16],
        [_detailsScroll.heightAnchor constraintEqualToConstant:168]
    ]];
}

- (NSView *)spacer {
    NSView *v = [[NSView alloc] initWithFrame:NSZeroRect];
    v.translatesAutoresizingMaskIntoConstraints = NO;
    [v.heightAnchor constraintEqualToConstant:MHSpace4].active = YES;
    return v;
}

- (void)toggleDetails:(id)s {
    BOOL open = _disclosure.state == NSControlStateValueOn;
    _detailsScroll.hidden = !open;
}

@end
