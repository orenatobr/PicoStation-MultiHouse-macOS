#import "MHViews.h"

/* ---------------------------------------------------------------- helpers */

static NSTextField *make_label(NSFont *font, NSColor *color, NSLineBreakMode mode) {
    NSTextField *l = [NSTextField labelWithString:@""];
    l.font = font;
    l.textColor = color;
    l.lineBreakMode = mode;
    l.maximumNumberOfLines = 1;
    l.usesSingleLineMode = YES;
    l.cell.truncatesLastVisibleLine = YES;
    return l;
}

static NSString *plural(NSUInteger n, NSString *one, NSString *many) {
    return [NSString stringWithFormat:@"%lu %@", (unsigned long)n, n == 1 ? one : many];
}

/* ---------------------------------------------------------------- MHLibRow */

@implementation MHLibRow
- (MHStatusKind)statusKind {
    switch (_status) {
        case 4: return MHStatusError;
        case 3: return MHStatusFixable;
        case 2: return MHStatusReview;
        default: return _written ? MHStatusWritten : (_verified ? MHStatusVerified : MHStatusReady);
    }
}

- (NSString *)spokenDescriptionForDisc:(BOOL)disc {
    NSMutableString *s = [NSMutableString stringWithString:_title ?: @""];
    if (disc) {
        [s appendFormat:@", disc %ld", (long)_position];
        if (_serial.length) [s appendFormat:@", serial %@", _serial];
        [s appendFormat:@", %@", plural(_count, @"track", @"tracks")];
    } else {
        [s appendFormat:@", %@", plural(_count, @"disc", @"discs")];
    }
    [s appendFormat:@", %@", [[MHStatusChip titleForKind:self.statusKind] lowercaseString]];
    if (_written && self.statusKind != MHStatusWritten) [s appendString:@", written to the card"];
    return s;
}
@end

/* ---------------------------------------------------------------- list table */

@implementation MHListTable
- (BOOL)acceptsFirstResponder { return self.isEnabled; }

- (void)keyDown:(NSEvent *)e {
    NSString *c = e.charactersIgnoringModifiers;
    unichar ch = c.length ? [c characterAtIndex:0] : 0;
    BOOL plain = !(e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagControl));
    /* Delete, forward delete, F2 and Return (rename) are handled by the owner. */
    if (plain && (e.keyCode == 51 || e.keyCode == 117 || e.keyCode == 120 || e.keyCode == 36 || e.keyCode == 76)) {
        [self.owner list:self keyDown:ch keyCode:e.keyCode];
        return;
    }
    [super keyDown:e];
}

- (NSMenu *)menuForEvent:(NSEvent *)e {
    if (!self.isEnabled) return nil;
    NSInteger row = [self rowAtPoint:[self convertPoint:e.locationInWindow fromView:nil]];
    if (row < 0) return nil;
    [self.window makeFirstResponder:self];
    if (![self isRowSelected:row]) [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    return [self.owner list:self menuForRow:row];
}

/* The system draws the "drop on" rounded rectangle in the accent color; nothing else to add. */
@end

/* ---------------------------------------------------------------- serial pill */

@implementation MHPillView
- (void)setText:(NSString *)t { _text = [t copy]; [self invalidateIntrinsicContentSize]; self.needsDisplay = YES; }
- (void)setMonospaced:(BOOL)m { _monospaced = m; [self invalidateIntrinsicContentSize]; self.needsDisplay = YES; }
- (NSDictionary *)attrs {
    return @{ NSFontAttributeName : _monospaced ? MHTheme.monoFont : MHTheme.captionFont,
              NSForegroundColorAttributeName : _monospaced ? MHTheme.textPrimary : MHTheme.textSecondary };
}
- (NSSize)intrinsicContentSize {
    return NSMakeSize(ceil([(_text ?: @"") sizeWithAttributes:[self attrs]].width) + 2 * MHSpace8, 22);
}
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)r {
    NSRect b = self.bounds;
    if (_monospaced) {
        NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 0.5, 0.5) xRadius:MHRadiusChip yRadius:MHRadiusChip];
        [[NSColor.labelColor colorWithAlphaComponent:0.07] setFill];
        [p fill];
    }
    NSString *t = _text ?: @"";
    NSSize s = [t sizeWithAttributes:[self attrs]];
    [t drawAtPoint:NSMakePoint(round(NSMidX(b) - s.width / 2), round(NSMidY(b) - s.height / 2)) withAttributes:[self attrs]];
}
@end

/* ---------------------------------------------------------------- sidebar cell */

@implementation MHCaseCellView {
    BOOL _emphasized;
}

- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        _thumb = [[MHThumbnailView alloc] initWithFrame:NSMakeRect(0, 0, 40, 40)];
        _thumb.accessibilityElement = NO;
        _thumb.placeholderSymbol = @"opticaldisc";
        NSTextField *t = make_label(MHTheme.bodyFont, MHTheme.textPrimary, NSLineBreakByTruncatingMiddle);
        _caption = make_label(MHTheme.captionFont, MHTheme.textSecondary, NSLineBreakByTruncatingTail);
        _chip = [[MHStatusChip alloc] initWithFrame:NSMakeRect(0, 0, 16, 16)];
        _chip.compact = YES;
        _chip.accessibilityElement = NO;
        self.textField = t;
        for (NSView *v in @[ _thumb, t, _caption, _chip ]) [self addSubview:v];
    }
    return self;
}

- (void)configureWithRow:(MHLibRow *)r {
    self.textField.stringValue = r.title ?: @"";
    _caption.stringValue = plural(r.count, @"disc", @"discs");
    [_thumb setArtPath:r.artPath.UTF8String];
    _chip.kind = r.statusKind;
    self.toolTip = r.title;
    [self applyColors];
}

- (void)applyColors {
    self.textField.textColor = _emphasized ? NSColor.alternateSelectedControlTextColor : MHTheme.textPrimary;
    _caption.textColor = _emphasized ? [NSColor.alternateSelectedControlTextColor colorWithAlphaComponent:0.85] : MHTheme.textSecondary;
    _chip.tintOverride = _emphasized ? NSColor.alternateSelectedControlTextColor : nil;
}

- (void)setBackgroundStyle:(NSBackgroundStyle)s {
    [super setBackgroundStyle:s];
    _emphasized = s == NSBackgroundStyleEmphasized;
    [self applyColors];
}

- (void)layout {
    [super layout];
    NSRect b = self.bounds;
    CGFloat h = b.size.height, w = b.size.width;
    _thumb.frame = NSMakeRect(MHSpace4, round((h - 40) / 2), 40, 40);
    _chip.frame = NSMakeRect(w - MHSpace4 - 16, round((h - 16) / 2), 16, 16);
    CGFloat x = MHSpace4 + 40 + MHSpace8, tw = MAX(0, w - x - MHSpace4 - 16 - MHSpace4);
    self.textField.frame = NSMakeRect(x, round(h / 2), tw, 17);
    _caption.frame = NSMakeRect(x, round(h / 2) - 16, tw, 14);
}
@end

/* ---------------------------------------------------------------- disc card */

@implementation MHDiscCellView
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        _thumb = [[MHThumbnailView alloc] initWithFrame:NSMakeRect(0, 0, 44, 44)];
        _thumb.shape = MHThumbDisc;
        _thumb.accessibilityElement = NO;
        NSTextField *t = make_label(MHTheme.headlineFont, MHTheme.textPrimary, NSLineBreakByTruncatingTail);
        _caption = make_label(MHTheme.captionFont, MHTheme.textSecondary, NSLineBreakByTruncatingTail);
        _pill = [[MHPillView alloc] initWithFrame:NSMakeRect(0, 0, 100, 22)];
        _chip = [[MHStatusChip alloc] initWithFrame:NSMakeRect(0, 0, 90, 22)];
        _writtenChip = [[MHStatusChip alloc] initWithFrame:NSMakeRect(0, 0, 16, 16)];
        _writtenChip.kind = MHStatusWritten;
        _writtenChip.compact = YES;
        for (NSView *v in @[ _pill, _chip, _writtenChip ]) v.accessibilityElement = NO;
        self.textField = t;
        for (NSView *v in @[ _thumb, t, _caption, _pill, _chip, _writtenChip ]) [self addSubview:v];
    }
    return self;
}

- (void)configureWithRow:(MHLibRow *)r {
    self.textField.stringValue = r.title ?: @"";
    _caption.stringValue = [NSString stringWithFormat:@"Disc %ld  ·  %@", (long)r.position, plural(r.count, @"track", @"tracks")];
    [_thumb setArtPath:r.artPath.UTF8String];
    _pill.monospaced = r.serial.length > 0;
    _pill.text = r.serial.length ? r.serial : @"No serial";
    _chip.kind = r.statusKind;
    _writtenChip.hidden = !(r.written && r.statusKind != MHStatusWritten);
    self.toolTip = r.title;
    [self setNeedsLayout:YES];
}

- (void)layout {
    [super layout];
    NSRect b = self.bounds;
    CGFloat h = b.size.height, w = b.size.width;
    CGFloat cardTop = MHSpace4, cardH = h - 2 * cardTop; /* the card is inset 4 pt top and bottom */
    CGFloat cy = cardTop + cardH / 2;
    _thumb.frame = NSMakeRect(MHSpace12, round(cy - 22), 44, 44);
    CGFloat right = w - MHSpace12;
    NSSize cs = _chip.intrinsicContentSize;
    _chip.frame = NSMakeRect(right - cs.width, round(cy - cs.height / 2), cs.width, cs.height);
    right -= cs.width + MHSpace8;
    if (!_writtenChip.hidden) {
        _writtenChip.frame = NSMakeRect(right - 16, round(cy - 8), 16, 16);
        right -= 16 + MHSpace8;
    }
    NSSize ps = _pill.intrinsicContentSize;
    CGFloat x = MHSpace12 + 44 + MHSpace12;
    BOOL pillFits = right - ps.width - MHSpace12 > x + 96;
    _pill.hidden = !pillFits;
    if (pillFits) {
        _pill.frame = NSMakeRect(right - ps.width, round(cy - ps.height / 2), ps.width, ps.height);
        right -= ps.width + MHSpace12;
    }
    CGFloat tw = MAX(0, right - x);
    self.textField.frame = NSMakeRect(x, round(cy), tw, 17);
    _caption.frame = NSMakeRect(x, round(cy) - 17, tw, 14);
}
@end

@implementation MHDiscRowView
- (NSBackgroundStyle)interiorBackgroundStyle { return NSBackgroundStyleNormal; }
- (NSRect)cardRect { return NSInsetRect(self.bounds, 0, MHSpace4); }

- (void)drawBackgroundInRect:(NSRect)dirty {
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.cardRect, 0.5, 0.5) xRadius:MHRadiusCard yRadius:MHRadiusCard];
    [MHTheme.surfaceRaised setFill];
    [p fill];
    [MHTheme.cardStroke setStroke];
    p.lineWidth = 1;
    [p stroke];
}

- (void)drawSelectionInRect:(NSRect)dirty {
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.cardRect, 1, 1) xRadius:MHRadiusCard yRadius:MHRadiusCard];
    [MHTheme.selectionFill setFill];
    [p fill];
    [(self.isEmphasized ? MHTheme.brandBlue : MHTheme.textSecondary) setStroke];
    p.lineWidth = self.isEmphasized ? 2 : 1.5;
    [p stroke];
}

- (BOOL)isOpaque { return NO; }
@end

/* ---------------------------------------------------------------- empty state */

@implementation MHEmptyStateView {
    NSImageView *_icon;
}

- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        _icon = [[NSImageView alloc] initWithFrame:NSZeroRect];
        _icon.accessibilityElement = NO;
        _icon.imageScaling = NSImageScaleProportionallyUpOrDown;
        _titleLabel = [NSTextField labelWithString:@""];
        _titleLabel.font = MHTheme.titleFont;
        _titleLabel.alignment = NSTextAlignmentCenter;
        _messageLabel = [NSTextField wrappingLabelWithString:@""];
        _messageLabel.font = MHTheme.bodyFont;
        _messageLabel.textColor = MHTheme.textSecondary;
        _messageLabel.alignment = NSTextAlignmentCenter;
        _messageLabel.selectable = NO;
        _addGame = [NSButton buttonWithTitle:@"Add Game…" target:nil action:nil];
        _addGame.controlSize = NSControlSizeLarge;
        _addGame.bezelStyle = NSBezelStyleRounded;
        _addGame.bezelColor = MHTheme.brandFill;
        _addGame.image = [MHTheme symbol:@"plus.rectangle.on.folder" size:14 description:nil];
        _addGame.imagePosition = NSImageLeft;
        _addGame.contentTintColor = NSColor.whiteColor;
        _addGame.attributedTitle = [[NSAttributedString alloc] initWithString:@"Add Game…" attributes:@{
            NSForegroundColorAttributeName : NSColor.whiteColor, NSFontAttributeName : MHTheme.headlineFont }];
        _addGame.toolTip = @"Add game folders or .cue files";
        _addGame.accessibilityLabel = @"Add Game";
        _addDisc = [NSButton buttonWithTitle:@"Add Disc…" target:nil action:nil];
        _addDisc.controlSize = NSControlSizeLarge;
        _addDisc.bezelStyle = NSBezelStyleRounded;
        _addDisc.image = [MHTheme symbol:@"opticaldisc" size:14 description:nil];
        _addDisc.imagePosition = NSImageLeft;
        _addDisc.accessibilityLabel = @"Add Disc";
        NSStackView *buttons = [NSStackView stackViewWithViews:@[ _addGame, _addDisc ]];
        buttons.spacing = MHSpace12;
        NSStackView *st = [NSStackView stackViewWithViews:@[ _icon, _titleLabel, _messageLabel, buttons ]];
        st.orientation = NSUserInterfaceLayoutOrientationVertical;
        st.alignment = NSLayoutAttributeCenterX;
        st.spacing = MHSpace12;
        [st setCustomSpacing:MHSpace16 afterView:_icon];
        [st setCustomSpacing:MHSpace24 afterView:_messageLabel];
        st.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:st];
        [NSLayoutConstraint activateConstraints:@[
            [st.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [st.centerYAnchor constraintEqualToAnchor:self.centerYAnchor constant:-MHSpace16],
            [st.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.leadingAnchor constant:MHSpace24],
            [st.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor constant:-MHSpace24],
            [_icon.widthAnchor constraintEqualToConstant:96],
            [_icon.heightAnchor constraintEqualToConstant:80],
            [_messageLabel.widthAnchor constraintLessThanOrEqualToConstant:380]
        ]];
        [self setLibraryEmpty:YES];
    }
    return self;
}

- (void)setTarget:(id)target addGameAction:(SEL)g addDiscAction:(SEL)d {
    _addGame.target = target;
    _addGame.action = g;
    _addDisc.target = target;
    _addDisc.action = d;
}

- (void)setLibraryEmpty:(BOOL)empty {
    NSImageSymbolConfiguration *size = [NSImageSymbolConfiguration configurationWithPointSize:72 weight:NSFontWeightLight];
    NSImageSymbolConfiguration *tint = [NSImageSymbolConfiguration configurationWithHierarchicalColor:MHTheme.brandBlue];
    NSImage *img = [NSImage imageWithSystemSymbolName:empty ? @"opticaldisc" : @"square.stack" accessibilityDescription:nil];
    _icon.image = [img imageWithSymbolConfiguration:[size configurationByApplyingConfiguration:tint]];
    _titleLabel.stringValue = empty ? @"Build your PS1 library" : @"Select a case";
    _messageLabel.stringValue = empty ? @"Drop game folders or .cue files here, or choose Add Game."
                                      : @"Choose a case in the sidebar to see its discs, or drop more games here.";
    _addDisc.hidden = !empty;
}
@end

/* ---------------------------------------------------------------- drop overlay */

@implementation MHDropOverlay {
    NSTextField *_title, *_sub;
    NSImageView *_icon;
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        self.wantsLayer = YES;
        self.alphaValue = 0;
        self.hidden = YES;
        _icon = [[NSImageView alloc] initWithFrame:NSZeroRect];
        _icon.image = [MHTheme symbol:@"square.and.arrow.down" size:40 weight:NSFontWeightLight description:nil];
        _icon.contentTintColor = MHTheme.brandBlue;
        _icon.accessibilityElement = NO;
        _title = [NSTextField labelWithString:@"Drop to add"];
        _title.font = [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];
        _title.textColor = MHTheme.brandBlue;
        _sub = [NSTextField labelWithString:@""];
        _sub.font = MHTheme.bodyFont;
        _sub.textColor = MHTheme.textSecondary;
        NSStackView *st = [NSStackView stackViewWithViews:@[ _icon, _title, _sub ]];
        st.orientation = NSUserInterfaceLayoutOrientationVertical;
        st.spacing = MHSpace8;
        st.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:st];
        [NSLayoutConstraint activateConstraints:@[ [st.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
                                                   [st.centerYAnchor constraintEqualToAnchor:self.centerYAnchor] ]];
        self.accessibilityElement = NO;
    }
    return self;
}
- (NSView *)hitTest:(NSPoint)p { return nil; } /* never steals events */
- (void)setDetail:(NSString *)d { _detail = [d copy]; _sub.stringValue = d ?: @""; }

- (void)setActive:(BOOL)on {
    if (on == !self.hidden && self.alphaValue == (on ? 1 : 0)) return;
    if (on) self.hidden = NO;
    [MHTheme animate:^{ self.animator.alphaValue = on ? 1 : 0; } completion:^{ if (!on) self.hidden = YES; }];
}

- (void)drawRect:(NSRect)r {
    NSRect b = NSInsetRect(self.bounds, MHSpace12, MHSpace12);
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:b xRadius:MHRadiusCard * 1.6 yRadius:MHRadiusCard * 1.6];
    NSColor *fill = MHTheme.reduceTransparency ? MHTheme.surfaceRaised : [MHTheme.brandBlue colorWithAlphaComponent:0.10];
    [fill setFill];
    [p fill];
    [MHTheme.brandBlue setStroke];
    p.lineWidth = 3;
    CGFloat dash[2] = { 10, 7 };
    [p setLineDash:dash count:2 phase:0];
    [p stroke];
}
@end

/* ---------------------------------------------------------------- primary button */

@implementation MHPrimaryButton
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        self.bezelStyle = NSBezelStyleRounded;
        self.controlSize = NSControlSizeLarge;
        self.bezelColor = MHTheme.brandFill;
        self.font = MHTheme.headlineFont;
    }
    return self;
}
- (void)restyle {
    NSColor *c = self.isEnabled ? NSColor.whiteColor : NSColor.disabledControlTextColor;
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.alignment = NSTextAlignmentCenter;
    self.attributedTitle = [[NSAttributedString alloc] initWithString:self.title attributes:@{
        NSForegroundColorAttributeName : c, NSFontAttributeName : MHTheme.headlineFont, NSParagraphStyleAttributeName : ps }];
}
- (void)setTitle:(NSString *)t { [super setTitle:t]; [self restyle]; }
- (void)setEnabled:(BOOL)e { [super setEnabled:e]; [self restyle]; }
@end

/* ---------------------------------------------------------------- bottom bar */

@implementation MHBottomBar {
    NSImageView *_cardIcon;
}

- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        self.material = NSVisualEffectMaterialHeaderView;
        self.blendingMode = NSVisualEffectBlendingModeWithinWindow;
        self.state = NSVisualEffectStateFollowsWindowActiveState;

        NSBox *line = [[NSBox alloc] initWithFrame:NSZeroRect];
        line.boxType = NSBoxSeparator;
        line.translatesAutoresizingMaskIntoConstraints = NO;

        _cardIcon = [NSImageView imageViewWithImage:[MHTheme symbol:@"sdcard" size:16 description:nil]];
        _cardIcon.contentTintColor = MHTheme.textSecondary;
        _cardIcon.accessibilityElement = NO;

        _popCard = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        _popCard.font = MHTheme.bodyFont;
        _popCard.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _popCard.accessibilityLabel = @"Card";
        _popCard.accessibilityHelp = @"Choose the SD card to write to";
        _popCard.toolTip = @"The SD card to write to";

        _btnRefresh = [NSButton buttonWithImage:[MHTheme symbol:@"arrow.clockwise" size:13 weight:NSFontWeightMedium description:@"Refresh Cards"]
                                         target:nil action:nil];
        _btnRefresh.bezelStyle = NSBezelStyleTexturedRounded;
        _btnRefresh.accessibilityLabel = @"Refresh Cards";
        _btnRefresh.accessibilityHelp = @"Looks for SD cards again";
        _btnRefresh.toolTip = @"Refresh Cards (⌘R)";

        _status = make_label(MHTheme.bodyFont, MHTheme.textPrimary, NSLineBreakByTruncatingTail);
        _status.accessibilityLabel = @"Status";
        _progress = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
        _progress.style = NSProgressIndicatorStyleBar;
        _progress.indeterminate = NO;
        _progress.minValue = 0;
        _progress.maxValue = 100;
        _progress.controlSize = NSControlSizeSmall;
        _progress.accessibilityLabel = @"Progress";
        _progress.hidden = YES;
        NSStackView *mid = [NSStackView stackViewWithViews:@[ _status, _progress ]];
        mid.orientation = NSUserInterfaceLayoutOrientationVertical;
        mid.alignment = NSLayoutAttributeLeading;
        mid.spacing = MHSpace4;

        _btnWrite = [[MHPrimaryButton alloc] initWithFrame:NSZeroRect];
        _btnWrite.accessibilityRole = NSAccessibilityButtonRole;

        for (NSView *v in @[ line, _cardIcon, _popCard, _btnRefresh, mid, _btnWrite ]) {
            v.translatesAutoresizingMaskIntoConstraints = NO;
            [self addSubview:v];
        }
        [_popCard setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [mid setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [_status setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [_btnWrite setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
        [NSLayoutConstraint activateConstraints:@[
            [self.heightAnchor constraintEqualToConstant:52],
            [line.topAnchor constraintEqualToAnchor:self.topAnchor],
            [line.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [line.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_cardIcon.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:MHSpace16],
            [_cardIcon.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_popCard.leadingAnchor constraintEqualToAnchor:_cardIcon.trailingAnchor constant:MHSpace8],
            [_popCard.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_popCard.widthAnchor constraintGreaterThanOrEqualToConstant:160],
            [_popCard.widthAnchor constraintLessThanOrEqualToConstant:300],
            [_btnRefresh.leadingAnchor constraintEqualToAnchor:_popCard.trailingAnchor constant:MHSpace8],
            [_btnRefresh.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_btnRefresh.widthAnchor constraintEqualToConstant:36],
            [mid.leadingAnchor constraintEqualToAnchor:_btnRefresh.trailingAnchor constant:MHSpace24],
            [mid.trailingAnchor constraintEqualToAnchor:_btnWrite.leadingAnchor constant:-MHSpace16],
            [mid.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_progress.widthAnchor constraintEqualToAnchor:mid.widthAnchor],
            [_btnWrite.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-MHSpace16],
            [_btnWrite.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_btnWrite.widthAnchor constraintGreaterThanOrEqualToConstant:176]
        ]];
    }
    return self;
}

- (void)setStatusText:(NSString *)t {
    _status.stringValue = t ?: @"";
    _status.toolTip = t.length > 60 ? t : nil;
}
- (void)setProgressVisible:(BOOL)on { _progress.hidden = !on; }
- (void)setWriteTitle:(NSString *)t {
    _btnWrite.title = t;
    _btnWrite.accessibilityLabel = t;
}
@end

/* ---------------------------------------------------------------- drop root */

@implementation MHDropRootView
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    return self;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)i { return [self.dropDelegate dropOperationFor:i]; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)i { return [self.dropDelegate dropOperationFor:i]; }
- (void)draggingExited:(id<NSDraggingInfo>)i { [self.dropDelegate dropEnded]; }
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)i { return [self.dropDelegate dropOperationFor:i] != NSDragOperationNone; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)i { return [self.dropDelegate performDrop:i]; }
- (void)concludeDragOperation:(id<NSDraggingInfo>)i { [self.dropDelegate dropEnded]; }
- (void)draggingEnded:(id<NSDraggingInfo>)i { [self.dropDelegate dropEnded]; }
@end
