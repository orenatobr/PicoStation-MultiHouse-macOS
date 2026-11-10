#import "MHMainController.h"
#import "MHApp.h"
#import "MHViews.h"
#import "MHPanes.h"
#import "MHDialogs.h"
#import "MHTheme.h"
#include "mh/v2write.h"
#import "cheat_ui.h"
#import <DiskArbitration/DiskArbitration.h>
#include "mh/base.h"
#include "mh/disc.h"
#include "mh/menu.h"
#include "mh/art.h"
#include "mh/blockdev.h"
#include <strings.h>

/* Control ids of the original, used as accessibility identifiers and in docs/map/app.md. */
enum {
    ID_ADD_GAME = 0x64, ID_ADD_DISC = 0x65, ID_ABOUT = 0x66, ID_MENU = 0x67, ID_IMAGE = 0x68,
    ID_VERIFY = 0x69, ID_LIST_CASES = 0xc8, ID_LIST_DISCS = 0xc9, ID_EDIT_CASE = 0xca,
    ID_EDIT_DISC = 0xcb, ID_CASE_ART = 0xcc, ID_DISC_ART = 0xcd, ID_CHEATS = 0xce,
    ID_COMBO_SD = 0x12c, ID_REFRESH = 0x12d, ID_WRITE = 0x12e
};

#pragma clang diagnostic ignored "-Wdeprecated-declarations" /* allowedFileTypes: simplest and still works */
#define MH_BLUE_FLAGS (MH_DF_FROM_V2 | MH_DF_KEEP_PACKAGE) /* orig: 0x48000 */

typedef struct { char s[MH_ERR_MSG_MAX]; } MhMsg; /* blocks cannot capture arrays */

static NSString *const MHRowPboardType = @"io.github.orenatobr.picostation.row";
static NSString *const kSidebarHidden = @"MHSidebarHidden", *const kInspectorHidden = @"MHInspectorHidden";

/* Tests set MH_NO_ZOOM (kept from the original) and must neither read nor write the user's saved
 * window state. */
static BOOL test_mode(void) { return getenv("MH_NO_ZOOM") != NULL; }

static NSString *plural(NSUInteger n, NSString *one, NSString *many) {
    return [NSString stringWithFormat:@"%lu %@", (unsigned long)n, n == 1 ? one : many];
}

static NSString *status_word(unsigned s) { /* the OK / Review / Fixable / Error ladder of refresh_discs */
    return s == 2 ? @"Review" : s == 3 ? @"Fixable" : s == 4 ? @"Error" : @"OK";
}

/* ---------------------------------------------------------------- shell */

@interface MHShellViewController : NSViewController
@property(strong) NSSplitViewController *split;
@property(strong) MHBottomBar *bar;
@end
@implementation MHShellViewController
- (void)loadView {
    MHDropRootView *v = [[MHDropRootView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 800)];
    self.view = v;
    [self addChildViewController:self.split];
    self.split.view.translatesAutoresizingMaskIntoConstraints = NO;
    self.bar.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:self.split.view];
    [v addSubview:self.bar];
    [NSLayoutConstraint activateConstraints:@[
        [self.split.view.topAnchor constraintEqualToAnchor:v.topAnchor],
        [self.split.view.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [self.split.view.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [self.split.view.bottomAnchor constraintEqualToAnchor:self.bar.topAnchor],
        [self.bar.leadingAnchor constraintEqualToAnchor:v.leadingAnchor],
        [self.bar.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [self.bar.bottomAnchor constraintEqualToAnchor:v.bottomAnchor]
    ]];
}
@end

@interface MHMainController () <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate,
                                MHListOwner, MHDropDelegate, NSToolbarDelegate, NSMenuItemValidation,
                                NSToolbarItemValidation>
@end

@implementation MHMainController {
    MHApp *_app;
    NSWindow *_window;
    MHDropRootView *_shell;
    NSSplitViewController *_split;
    NSSplitViewItem *_sideItem, *_contentItem, *_inspItem;
    MHSidebarViewController *_side;
    MHContentViewController *_content;
    MHInspectorViewController *_insp;
    MHBottomBar *_bar;
    NSToolbar *_toolbar;
    NSMenu *_imageMenu;

    MHListTable *_tCases, *_tDiscs;
    NSTextField *_fCase, *_fCaseInsp, *_fDisc; /* header title, inspector case name, inspector disc name */
    NSPopUpButton *_popSD;
    MHPrimaryButton *_btnWrite;

    NSMutableArray<MHLibRow *> *_caseRows, *_discRows;
    BOOL _suppressSel;   /* programmatic selection changes */
    BOOL _suppressEdit;  /* orig: DAT_140e7b3a4 */

    MhDiskInfo _disks[MH_DISK_MAX];
    int _diskCount;
    NSUInteger _sdGen;

    MhProgress _prog;
    dispatch_queue_t _queue;
    NSLock *_progLock;
    uint64_t _pDone, _pTotal;
    NSString *_pText;
    int _pPhase, _pStep, _pSteps;
    BOOL _pPending;
    int _lastPhase, _lastStep;

    MHProgressSheet *_sheet;
    DASessionRef _da;
}

#pragma mark - setup

- (instancetype)init {
    if ((self = [super init])) {
        _app = MHApp.shared;
        _queue = dispatch_queue_create("mh.worker", DISPATCH_QUEUE_SERIAL);
        _progLock = [[NSLock alloc] init];
        mh_progress_init(&_prog, NULL, NULL);
        _caseRows = [NSMutableArray array];
        _discRows = [NSMutableArray array];
        [self buildWindow];
    }
    return self;
}

- (NSWindow *)window { return _window; }

- (void)tagControl:(NSView *)v id:(NSInteger)tag {
    v.identifier = [NSString stringWithFormat:@"0x%lx", (long)tag];
    if ([v isKindOfClass:NSControl.class]) ((NSControl *)v).tag = tag;
}

/* orig: main_proc WM_CREATE. */
- (void)buildWindow {
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1280, 800)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                                                    NSWindowStyleMaskFullSizeContentView
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = MH_TITLE;
    _window.releasedWhenClosed = NO;
    _window.delegate = self;
    _window.contentMinSize = NSMakeSize(1000, 640);
    _window.toolbarStyle = NSWindowToolbarStyleUnified;
    _window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;

    _side = [[MHSidebarViewController alloc] init];
    _content = [[MHContentViewController alloc] init];
    _insp = [[MHInspectorViewController alloc] init];
    [_side view];
    [_content view];
    [_insp view];
    _tCases = _side.table;
    _tDiscs = _content.table;
    _fCase = _content.fCase;
    _fCaseInsp = _insp.fCase;
    _fDisc = _insp.fDisc;

    /* lists */
    for (MHListTable *t in @[ _tCases, _tDiscs ]) {
        t.owner = self;
        t.dataSource = self;
        t.delegate = self;
        t.doubleAction = @selector(rowDoubleClicked:);
        t.target = self;
        [t registerForDraggedTypes:@[ MHRowPboardType ]];
        [t setDraggingSourceOperationMask:NSDragOperationMove forLocal:YES];
        [t setDraggingSourceOperationMask:NSDragOperationNone forLocal:NO];
    }
    [self tagControl:_tCases id:ID_LIST_CASES];
    [self tagControl:_tDiscs id:ID_LIST_DISCS];
    _tDiscs.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;

    /* fields and buttons carry the original control ids as identifiers */
    for (NSTextField *f in @[ _fCase, _fCaseInsp, _fDisc ]) f.delegate = self;
    [self tagControl:_fCase id:ID_EDIT_CASE];
    [self tagControl:_fCaseInsp id:ID_EDIT_CASE];
    [self tagControl:_fDisc id:ID_EDIT_DISC];
    for (NSButton *b in @[ _insp.btnCaseArt, _insp.btnDiscArt, _insp.btnCheats ]) b.target = self;
    _insp.btnCaseArt.action = @selector(caseArt:);
    _insp.btnDiscArt.action = @selector(discArt:);
    _insp.btnCheats.action = @selector(cmdCheatsAction:);
    [self tagControl:_insp.btnCaseArt id:ID_CASE_ART];
    [self tagControl:_insp.btnDiscArt id:ID_DISC_ART];
    [self tagControl:_insp.btnCheats id:ID_CHEATS];
    _content.chkVerify.target = self;
    _content.chkVerify.action = @selector(toggleVerify:);
    [self tagControl:_content.chkVerify id:ID_VERIFY];
    [_content.empty setTarget:self addGameAction:@selector(addGame:) addDiscAction:@selector(addDisc:)];
    [self tagControl:_content.empty.addGame id:ID_ADD_GAME];
    [self tagControl:_content.empty.addDisc id:ID_ADD_DISC];

    /* bottom bar */
    _bar = [[MHBottomBar alloc] initWithFrame:NSZeroRect];
    _popSD = _bar.popCard;
    _btnWrite = _bar.btnWrite;
    _popSD.target = self;
    _popSD.action = @selector(sdChanged:);
    _bar.btnRefresh.target = self;
    _bar.btnRefresh.action = @selector(refreshSDAction:);
    _btnWrite.target = self;
    _btnWrite.action = @selector(cmdWrite:);
    [self tagControl:_popSD id:ID_COMBO_SD];
    [self tagControl:_bar.btnRefresh id:ID_REFRESH];
    [self tagControl:_btnWrite id:ID_WRITE];
    [_bar setWriteTitle:@"Create V2 Card"];
    _btnWrite.enabled = NO;
    [_bar setStatusText:@"Ready."];

    /* split view: sidebar | content | inspector */
    _split = [[NSSplitViewController alloc] init];
    _split.splitView.vertical = YES;
    _split.splitView.dividerStyle = NSSplitViewDividerStyleThin;
    _sideItem = [NSSplitViewItem sidebarWithViewController:_side];
    _sideItem.minimumThickness = 240;
    _sideItem.maximumThickness = 360;
    _sideItem.canCollapse = YES;
    _sideItem.collapseBehavior = NSSplitViewItemCollapseBehaviorPreferResizingSplitViewWithFixedSiblings;
    _contentItem = [NSSplitViewItem splitViewItemWithViewController:_content];
    _contentItem.minimumThickness = 420;
    _contentItem.holdingPriority = NSLayoutPriorityDefaultLow;
    if (@available(macOS 14.0, *)) {
        _inspItem = [NSSplitViewItem inspectorWithViewController:_insp];
    } else {
        _inspItem = [NSSplitViewItem splitViewItemWithViewController:_insp];
        _inspItem.holdingPriority = NSLayoutPriorityDefaultHigh;
    }
    _inspItem.minimumThickness = 300;
    _inspItem.maximumThickness = 360;
    _inspItem.canCollapse = YES;
    [_split addSplitViewItem:_sideItem];
    [_split addSplitViewItem:_contentItem];
    [_split addSplitViewItem:_inspItem];

    MHShellViewController *shell = [[MHShellViewController alloc] init];
    shell.split = _split;
    shell.bar = _bar;
    _window.contentViewController = shell;
    [_window setContentSize:NSMakeSize(1280, 800)];
    _shell = (MHDropRootView *)shell.view;
    _shell.dropDelegate = self;
    MHSetHostWindow(_window);

    [self buildToolbar];
    [self buildKeyLoop];
    [self refreshAccessibility];

    if (!test_mode()) {
        _split.splitView.autosaveName = @"MHMainSplit";
        [_window setFrameAutosaveName:@"MHMainWindow"];
    }
}

/* Logical tab order: toolbar, sidebar, content, inspector, bottom bar. */
- (void)buildKeyLoop {
    _window.autorecalculatesKeyViewLoop = NO;
    NSArray<NSView *> *chain = @[ _tCases, _fCase, _content.chkVerify, _tDiscs, _content.empty.addGame, _content.empty.addDisc,
                                  _insp.btnCaseArt, _fCaseInsp, _insp.btnDiscArt, _fDisc, _insp.btnCheats, _popSD,
                                  _bar.btnRefresh, _btnWrite ];
    for (NSUInteger i = 0; i < chain.count; i++) chain[i].nextKeyView = chain[(i + 1) % chain.count];
    _window.initialFirstResponder = _tCases;
}

- (void)refreshAccessibility {
    _window.accessibilityLabel = MH_TITLE;
    _btnWrite.accessibilityHelp = @"Writes the library to the selected card";
}

#pragma mark - toolbar

- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)t {
    return @[ @"toggleSidebar", @"sidebarSep", @"addGame", @"addDisc", @"menuSettings", @"image", NSToolbarFlexibleSpaceItemIdentifier,
              @"inspectorSep", @"toggleInspector" ];
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)t {
    return [[self toolbarDefaultItemIdentifiers:t] arrayByAddingObject:NSToolbarSpaceItemIdentifier];
}

- (NSToolbarItem *)tbItem:(NSString *)ident label:(NSString *)label symbol:(NSString *)sym tip:(NSString *)tip action:(SEL)a {
    NSToolbarItem *it = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
    it.label = label;
    it.paletteLabel = label;
    it.toolTip = tip;
    it.image = [MHTheme symbol:sym size:16 weight:NSFontWeightRegular description:label];
    it.target = self;
    it.action = a;
    it.bordered = YES;
    return it;
}

- (NSToolbarItem *)toolbar:(NSToolbar *)tb itemForItemIdentifier:(NSToolbarItemIdentifier)ident willBeInsertedIntoToolbar:(BOOL)flag {
    if ([ident isEqual:@"sidebarSep"])
        return [NSTrackingSeparatorToolbarItem trackingSeparatorToolbarItemWithIdentifier:ident splitView:_split.splitView dividerIndex:0];
    if ([ident isEqual:@"inspectorSep"])
        return [NSTrackingSeparatorToolbarItem trackingSeparatorToolbarItemWithIdentifier:ident splitView:_split.splitView dividerIndex:1];
    if ([ident isEqual:@"toggleSidebar"])
        return [self tbItem:ident label:@"Sidebar" symbol:@"sidebar.left" tip:@"Show or hide the sidebar (⌥⌘S)" action:@selector(toggleSidebarAction:)];
    if ([ident isEqual:@"toggleInspector"])
        return [self tbItem:ident label:@"Inspector" symbol:@"sidebar.right" tip:@"Show or hide the inspector (⌥⌘I)" action:@selector(toggleInspectorAction:)];
    if ([ident isEqual:@"addGame"])
        return [self tbItem:ident label:@"Add Game" symbol:@"plus.rectangle.on.folder" tip:@"Add game folders or .cue files (⌘N)" action:@selector(addGame:)];
    if ([ident isEqual:@"addDisc"])
        return [self tbItem:ident label:@"Add Disc" symbol:@"opticaldisc" tip:@"Add a disc to the selected case (⇧⌘N)" action:@selector(addDisc:)];
    if ([ident isEqual:@"menuSettings"])
        return [self tbItem:ident label:@"Menu Settings" symbol:@"slider.horizontal.3" tip:@"Menu defaults written to the card" action:@selector(cmdDefaults:)];
    if ([ident isEqual:@"image"]) {
        NSMenuToolbarItem *it = [[NSMenuToolbarItem alloc] initWithItemIdentifier:ident];
        it.label = @"Image";
        it.paletteLabel = @"Image";
        it.toolTip = @"Open, save or start a V2 image";
        it.image = [MHTheme symbol:@"externaldrive" size:16 weight:NSFontWeightRegular description:@"Image"];
        it.menu = [self buildImageMenu];
        it.showsIndicator = YES;
        return it;
    }
    return nil;
}

- (NSMenu *)buildImageMenu {
    NSMenu *m = [[NSMenu alloc] initWithTitle:@"Image"];
    struct { NSString *t; SEL a; } items[] = {
        { @"Open Image…", @selector(imageOpen:) }, { @"Save Image As…", @selector(imageSaveAs:) },
        { @"Apply to Open Image", @selector(imageApply:) }, { nil, nil }, { @"New Project", @selector(imageNew:) } };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        if (!items[i].t) { [m addItem:NSMenuItem.separatorItem]; continue; }
        NSMenuItem *it = [m addItemWithTitle:items[i].t action:items[i].a keyEquivalent:@""];
        it.target = self;
    }
    _imageMenu = m;
    return m;
}

- (void)buildToolbar {
    _toolbar = [[NSToolbar alloc] initWithIdentifier:@"MHMainToolbar"];
    _toolbar.delegate = self;
    _toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
    _toolbar.allowsUserCustomization = NO;
    _window.toolbar = _toolbar;
}

#pragma mark - state restoration and panes

- (void)showWindow {
    if (!test_mode() && ![NSUserDefaults.standardUserDefaults stringForKey:@"NSWindow Frame MHMainWindow"]) [_window center];
    if (test_mode()) [_window center];
    [_window makeKeyAndOrderFront:nil];
    [self restoreState];
}

- (void)restoreState {
    if (test_mode()) return;
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    _sideItem.collapsed = [d boolForKey:kSidebarHidden];
    _inspItem.collapsed = [d boolForKey:kInspectorHidden];
}

- (void)saveState {
    if (test_mode()) return;
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    [d setBool:_sideItem.isCollapsed forKey:kSidebarHidden];
    [d setBool:_inspItem.isCollapsed forKey:kInspectorHidden];
}

- (void)toggleSidebarAction:(id)s {
    [MHTheme animate:^{ self->_sideItem.animator.collapsed = !self->_sideItem.isCollapsed; }];
    [self saveStateSoon];
}

- (void)toggleInspectorAction:(id)s {
    [MHTheme animate:^{ self->_inspItem.animator.collapsed = !self->_inspItem.isCollapsed; }];
    [self saveStateSoon];
}

- (void)saveStateSoon {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.4 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self saveState]; });
}

- (void)setPaneVisible:(BOOL)sidebar visible:(BOOL)vis {
    NSSplitViewItem *it = sidebar ? _sideItem : _inspItem;
    it.collapsed = !vis;
    [self saveState];
}

#pragma mark - table data

- (NSMutableArray<MHLibRow *> *)rowsFor:(NSTableView *)t { return t == _tCases ? _caseRows : _discRows; }

- (NSInteger)numberOfRowsInTableView:(NSTableView *)t { return [self rowsFor:t].count; }

- (NSView *)tableView:(NSTableView *)t viewForTableColumn:(NSTableColumn *)c row:(NSInteger)r {
    NSArray *rows = [self rowsFor:t];
    if (r < 0 || r >= (NSInteger)rows.count) return nil;
    MHLibRow *row = rows[r];
    if (t == _tCases) {
        MHCaseCellView *v = [t makeViewWithIdentifier:@"caseCell" owner:self];
        if (!v) { v = [[MHCaseCellView alloc] initWithFrame:NSMakeRect(0, 0, 260, 56)]; v.identifier = @"caseCell"; }
        [v configureWithRow:row];
        return v;
    }
    MHDiscCellView *v = [t makeViewWithIdentifier:@"discCell" owner:self];
    if (!v) { v = [[MHDiscCellView alloc] initWithFrame:NSMakeRect(0, 0, 600, 72)]; v.identifier = @"discCell"; }
    [v configureWithRow:row];
    return v;
}

- (NSTableRowView *)tableView:(NSTableView *)t rowViewForRow:(NSInteger)r {
    if (t == _tDiscs) return [[MHDiscRowView alloc] initWithFrame:NSZeroRect];
    return nil;
}

- (void)tableView:(NSTableView *)t didAddRowView:(NSTableRowView *)rv forRow:(NSInteger)r {
    NSArray<MHLibRow *> *rows = [self rowsFor:t];
    if (r < 0 || r >= (NSInteger)rows.count) return;
    rv.accessibilityRole = NSAccessibilityRowRole;
    rv.accessibilityLabel = [rows[r] spokenDescriptionForDisc:t == _tDiscs];
    rv.accessibilityHelp = t == _tDiscs ? @"Disc. Press Return to rename, Delete to remove."
                                         : @"Case. Press Return to rename, Delete to remove.";
}

- (void)tableViewSelectionDidChange:(NSNotification *)n {
    if (_suppressSel) return;
    NSTableView *t = n.object;
    NSInteger row = t.selectedRow;
    int param = -1;
    NSArray *rows = [self rowsFor:t];
    if (row >= 0 && row < (NSInteger)rows.count) param = (int)((MHLibRow *)rows[row]).param;
    if (t == _tCases) { /* orig: LVN_ITEMCHANGED on 0xc8 */
        if (_app.curCase == param) return;
        _app.curCase = param;
        _app.curDisc = -1;
        [self refreshDiscs];
        [self refreshFields];
    } else { /* 0xc9 */
        if (_app.curDisc == param) return;
        _app.curDisc = param;
        [self refreshFields];
    }
}

- (void)selectRowOf:(MHListTable *)t param:(int)param {
    NSArray *rows = [self rowsFor:t];
    for (NSUInteger i = 0; i < rows.count; i++) {
        if (((MHLibRow *)rows[i]).param == param) {
            _suppressSel = YES;
            [t selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];
            [t scrollRowToVisible:i];
            _suppressSel = NO;
            return;
        }
    }
}

- (void)clearSelection:(MHListTable *)t {
    _suppressSel = YES;
    [t deselectAll:nil];
    _suppressSel = NO;
}

#pragma mark - refresh_* (orig: refresh_cases, refresh_discs, refresh_fields, refresh_footer)

/* Worst status of the case's discs (orig: the nested ladder of refresh_cases). */
static unsigned case_worst(MhCase *c) {
    unsigned worst = 1;
    for (unsigned i = 0; i < c->disc_count; i++)
        if (c->discs[i] && c->discs[i]->status > worst) worst = c->discs[i]->status;
    return worst;
}

static BOOL case_blue(MhCase *c) { /* orig: list_custom_draw for list 0xc8 */
    if (!c->disc_count) return NO;
    for (unsigned i = 0; i < c->disc_count; i++)
        if (!c->discs[i] || !(c->discs[i]->flags & MH_BLUE_FLAGS)) return NO;
    return YES;
}

static BOOL case_verified(MhCase *c) {
    if (!c->disc_count) return NO;
    for (unsigned i = 0; i < c->disc_count; i++)
        if (!c->discs[i] || !c->discs[i]->verified) return NO;
    return YES;
}

/* .cue stem without the language tag "(En,Fr,De)" and, for a case, the "(Disc N)" tag; region and revision
 * tags such as "(USA)" or "(Rev 1)" stay as written. */
static NSString *strip_cue_tags(NSString *stem, BOOL discTag) {
    NSString *lang = @"\\s*\\([A-Z][a-z](?:-[A-Za-z]+)?(?:,[A-Z][a-z](?:-[A-Za-z]+)?)*\\)";
    NSString *pat = discTag ? [NSString stringWithFormat:@"%@|\\s*\\(Disc \\d+\\)", lang] : lang;
    NSString *out = [stem stringByReplacingOccurrencesOfString:pat
                                                    withString:@""
                                                       options:NSRegularExpressionSearch
                                                         range:NSMakeRange(0, stem.length)];
    return out.length ? out : stem;
}

/* Sidebar title: the .cue name as written (no extension). The stored
 * case title is the normalised 32-char card name, so it is used only when the user renamed the case. */
static NSString *case_sidebar_title(const MhCase *c) {
    NSString *stored = MHStr(c->title);
    const MhDisc *d = c->disc_count ? c->discs[0] : NULL;
    if (!d || !d->cue_path[0]) return stored;
    char derived[MH_CASE_TITLE_MAX], auto_title[MH_CASE_TITLE_MAX];
    mh_case_title_for_disc(d, derived, sizeof derived);
    mh_str_copy(auto_title, sizeof auto_title, derived);
    if (strcmp(c->title, auto_title) != 0) return stored;
    NSString *stem = strip_cue_tags(MHStr(d->cue_path).lastPathComponent.stringByDeletingPathExtension, YES);
    return stem.length ? stem : stored;
}

/* Disc name shown in the UI: the .cue name without the language tag (the "(Disc N)" tag stays, it tells
 * the discs of a case apart), unless the user typed a name. */
static NSString *disc_display_name(const MhDisc *d) {
    NSString *stored = MHStr(d->name);
    if ((d->flags & MH_DF_USER_NAMED) || !d->cue_path[0]) return stored;
    NSString *stem = strip_cue_tags(MHStr(d->cue_path).lastPathComponent.stringByDeletingPathExtension, NO);
    return stem.length ? stem : stored;
}

/* Widen the sidebar so the longest title shows in full (cell = thumbnail + text + status chip + insets). */
- (void)fitSidebarToTitles {
    /* the selected row draws heavier text, so measure semibold to never truncate */
    NSDictionary *attrs = @{NSFontAttributeName : [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]};
    CGFloat w = 0;
    for (MHLibRow *r in _caseRows) w = MAX(w, ceil([r.title sizeWithAttributes:attrs].width));
    CGFloat need = MIN(MAX(w + 52 + 24 + 24 + 12, 240), 640);
    _sideItem.minimumThickness = need;
    _sideItem.maximumThickness = MAX(need, 360);
}

- (void)refreshCases {
    [_caseRows removeAllObjects];
    MhProject *p = _app.project;
    NSInteger pos = 0;
    for (int i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        MhCase *c = &p->cases[i];
        if (!c->in_use) continue;
        MHLibRow *r = [[MHLibRow alloc] init];
        r.param = i;
        r.position = ++pos;
        r.title = case_sidebar_title(c);
        r.count = c->disc_count;
        r.artPath = MHStr(c->art_path);
        r.status = c->disc_count ? case_worst(c) : 1;
        r.written = case_blue(c);
        r.verified = case_verified(c);
        r.cells = @[ [NSString stringWithFormat:@"%d", i + 1], r.title, [NSString stringWithFormat:@"%u", c->disc_count],
                     status_word(r.status) ];
        [_caseRows addObject:r];
    }
    [self fitSidebarToTitles];
    _suppressSel = YES;
    [_tCases reloadData];
    _suppressSel = NO;
    if ([_app caseAt:_app.curCase]) {
        [self selectRowOf:_tCases param:_app.curCase];
    } else {
        _app.curCase = -1;
        [self clearSelection:_tCases];
        for (int i = 0; i < MH_PROJECT_MAX_CASES; i++) {
            if (p->cases[i].in_use) {
                _app.curCase = i;
                [self selectRowOf:_tCases param:i];
                break;
            }
        }
    }
    [_side setTotalsCases:p->case_count discs:p->disc_count];
    [self refreshDiscs];
}

- (void)refreshDiscs {
    [_discRows removeAllObjects];
    MhCase *c = _app.currentCase;
    if (c) {
        for (unsigned i = 0; i < c->disc_count; i++) {
            MhDisc *d = c->discs[i];
            MHLibRow *r = [[MHLibRow alloc] init];
            r.param = i;
            r.position = i + 1;
            r.title = d ? disc_display_name(d) : @"";
            r.serial = MHStr(d ? d->serial : "");
            r.artPath = MHStr(d ? d->cd_art_path : "");
            r.count = d ? d->ntracks : 0;
            r.status = d ? d->status : 0;
            r.written = d && (d->flags & MH_BLUE_FLAGS);
            r.verified = d && d->verified;
            r.cells = @[ [NSString stringWithFormat:@"%u", i + 1], r.title, r.serial,
                         [NSString stringWithFormat:@"%u", d ? d->ntracks : 0], status_word(d ? d->status : 0) ];
            [_discRows addObject:r];
        }
    }
    _suppressSel = YES;
    [_tDiscs reloadData];
    _suppressSel = NO;
    if (c && c->disc_count) {
        unsigned sel = (unsigned)_app.curDisc;
        if (c->disc_count <= sel) sel = 0; /* orig: unsigned compare, so -1 selects the first */
        _app.curDisc = (int)sel;
        [self selectRowOf:_tDiscs param:(int)sel];
    } else {
        _app.curDisc = -1;
        [self clearSelection:_tDiscs];
    }
}

- (void)refreshFields {
    MhProject *p = _app.project;
    MhCase *c = _app.currentCase;
    MhDisc *d = _app.currentDisc;
    BOOL enAddDisc = NO, enCase = NO, enDisc = NO;
    _suppressEdit = YES;
    NSString *ct = c ? case_sidebar_title(c) : @"";
    if (![_fCase.stringValue isEqualToString:ct] && _window.firstResponder != _fCase.currentEditor) _fCase.stringValue = ct;
    if (![_fCaseInsp.stringValue isEqualToString:ct] && _window.firstResponder != _fCaseInsp.currentEditor) _fCaseInsp.stringValue = ct;
    NSString *dn = d ? disc_display_name(d) : @"";
    if (![_fDisc.stringValue isEqualToString:dn] && _window.firstResponder != _fDisc.currentEditor) _fDisc.stringValue = dn;
    _insp.btnCheats.title = [NSString stringWithFormat:@"Cheats (%u)…", d && d->cheats ? d->cheats->count : 0];
    _insp.btnCheats.accessibilityLabel = [NSString stringWithFormat:@"Cheats, %u", d && d->cheats ? d->cheats->count : 0];
    if (c) {
        enDisc = d != NULL;
        enCase = YES;
        enAddDisc = c->disc_count < 5 ? p->disc_count < 0x100 : NO;
    }
    _suppressEdit = NO;
    _content.caption.stringValue = c ? plural(c->disc_count, @"disc", @"discs") : @"";
    _content.mode = p->case_count == 0 ? MHContentLibraryEmpty : (c ? MHContentCase : MHContentNoSelection);
    _insp.hasSelection = c != NULL;
    [self enableEditControlsAddDisc:enAddDisc caseCtl:enCase discCtl:enDisc];

    NSMutableString *t = [NSMutableString string];
    if (d) {
        [t appendString:MHStr(d->notes)];
        if (d->cheat_note[0]) [t appendFormat:@"\r\nCheats: %@", MHStr(d->cheat_note)];
        if (d->verify_text[0]) [t appendFormat:@"\r\n\r\nVerification: %@", MHStr(d->verify_text)];
    }
    _insp.details.string = MHDisplay(t);
    [_insp.pvCase setArtPath:c ? c->art_path : NULL];
    [_insp.pvDisc setArtPath:d ? d->cd_art_path : NULL];
    [self refreshFooter];
}

/* Busy state wins over the per-selection state, like the sequence of EnableWindow calls. */
- (void)enableEditControlsAddDisc:(BOOL)add caseCtl:(BOOL)cs discCtl:(BOOL)ds {
    BOOL idle = !_app.busy;
    _content.empty.addDisc.enabled = add && idle;
    _content.empty.addDisc.toolTip = add ? @"Add a disc to the selected case" : @"Select a case that has room for another disc";
    _insp.btnCaseArt.enabled = cs && idle;
    _insp.btnDiscArt.enabled = ds && idle;
    _insp.btnCheats.enabled = ds && idle;
    _fCase.enabled = cs && idle;
    _fCaseInsp.enabled = cs && idle;
    _fDisc.enabled = ds && idle;
    [_toolbar validateVisibleItems];
}

- (void)refreshAllEnable {
    MhCase *c = _app.currentCase;
    [self enableEditControlsAddDisc:c && c->disc_count < 5 && _app.project->disc_count < 0x100
                            caseCtl:c != NULL
                            discCtl:_app.currentDisc != NULL];
}

- (void)setStatus:(NSString *)s { [_bar setStatusText:s]; }

- (void)announce:(NSString *)text {
    if (!text.length) return;
    NSAccessibilityPostNotificationWithUserInfo(NSApp, NSAccessibilityAnnouncementRequestedNotification, @{
        NSAccessibilityAnnouncementKey : text, NSAccessibilityPriorityKey : @(NSAccessibilityPriorityHigh) });
}

- (NSString *)writeTitleIdle { return _app.project->card_loaded ? @"Apply Changes" : @"Create V2 Card"; }

- (void)refreshFooter {
    MhProject *p = _app.project;
    BOOL en = NO;
    NSString *reason = nil;
    if (p->disc_count) {
        en = YES;
        if (p->card_loaded) { en = p->dirty != 0; if (!en) reason = @"No changes to apply."; }
    } else {
        reason = @"Add at least one game to create a card.";
    }
    NSMutableString *s = [NSMutableString stringWithFormat:@"%@, %@", plural(p->case_count, @"case", @"cases"),
                                                           plural(p->disc_count, @"disc", @"discs")];
    NSString *mode = nil;
    if (!p->card_loaded) {
        if (!p->image_loaded) { if (p->dirty) mode = @"New project"; }
        else mode = p->dirty ? @"V2 image loaded, changes pending" : @"V2 image loaded";
    } else {
        mode = p->dirty ? @"V2 card loaded, changes pending" : @"V2 card loaded";
    }
    if (mode) [s appendFormat:@"  ·  %@", mode];
    _window.subtitle = mode ?: (p->disc_count ? @"Library" : @"Empty library");
    if (!_app.busy) {
        [self setStatus:(!en && reason && p->disc_count == 0) ? [NSString stringWithFormat:@"%@  ·  %@", s, reason] : s];
        [_bar setWriteTitle:[self writeTitleIdle]];
        _btnWrite.enabled = en;
        _btnWrite.toolTip = en ? (p->card_loaded ? @"Write the pending changes to the card (⌘↩)" : @"Erase the selected card and write the library (⌘↩)")
                               : reason;
    }
    [_toolbar validateVisibleItems];
}

/* The shared tail of every project change: orig `project_recount; refresh_cases; refresh_fields`. */
- (void)refreshProject {
    mh_project_recount(_app.project);
    [self refreshCases];
    [self refreshFields];
}

#pragma mark - busy / progress

/* orig: set_busy. */
- (void)setBusy:(BOOL)b {
    _app.busy = b;
    if (b) { _app.cancelRequested = NO; _app.cannotCancel = NO; _lastPhase = 0; _lastStep = 0; }
    [_bar setWriteTitle:b ? @"Cancel" : [self writeTitleIdle]];
    [_bar setProgressVisible:b];
    BOOL en = !b;
    for (NSControl *c in @[ _popSD, _bar.btnRefresh, _content.chkVerify, _tCases, _tDiscs, _fCase, _fCaseInsp, _fDisc,
                            _content.empty.addGame, _content.empty.addDisc, _insp.btnCaseArt, _insp.btnDiscArt, _insp.btnCheats ])
        c.enabled = en;
    _btnWrite.enabled = YES;
    if (!b) { _bar.progress.doubleValue = 0; }
    if (!b) [self refreshAllEnable];
    if (!b) [self refreshFooter];
    [_toolbar validateVisibleItems];
}

static void progress_cb_c(void *user, uint64_t done, uint64_t total, const char *text) {
    [(__bridge MHMainController *)user queueProgress:done total:total text:text];
}

/* Called from the worker. Latest value wins, one main-queue hop at a time. */
- (void)queueProgress:(uint64_t)done total:(uint64_t)total text:(const char *)text {
    [_progLock lock];
    _pDone = done;
    _pTotal = total;
    _pText = text ? MHStr(text) : nil;
    _pPhase = atomic_load(&_prog.phase);
    _pStep = atomic_load(&_prog.step);
    _pSteps = atomic_load(&_prog.steps);
    BOOL post = !_pPending;
    _pPending = YES;
    [_progLock unlock];
    if (post) dispatch_async(dispatch_get_main_queue(), ^{ [self applyProgress]; });
}

/* orig: progress_cb. The phase fields replace parsing the text. */
- (void)applyProgress {
    [_progLock lock];
    uint64_t done = _pDone, total = _pTotal;
    NSString *text = _pText;
    int phase = _pPhase, step = _pStep, steps = _pSteps;
    _pPending = NO;
    [_progLock unlock];
    double pct = 0;
    if (total) pct = done < total ? (double)(done * 100 / total) : 100;
    _bar.progress.doubleValue = pct;
    /* The card is being committed from UPDATING_TABLES on: no safe cancel any more (orig: kind 7). */
    if (phase >= MH_V2_PHASE_UPDATING_TABLES && phase < MH_V2_PHASE_DONE && !_app.cannotCancel) [self markCannotCancel];
    /* A disc finished: repaint the rows (it turns "Written"). Disc k is flagged before the report of k+1. */
    if ((_lastPhase == MH_V2_PHASE_WRITING_DISCS && (phase != _lastPhase || step != _lastStep))) [self refreshRowColors];
    _lastPhase = phase;
    _lastStep = step;
    NSString *line = text ?: @"";
    if (phase >= 1 && phase <= MH_V2_PHASE_STEPS)
        line = [NSString stringWithFormat:@"Step %d of %d  ·  %@", phase, MH_V2_PHASE_STEPS, text.length ? text : MHPhaseTitle(phase)];
    [self setStatus:_app.cancelRequested ? @"Cancelling at the next safe point…" : line];
    [_sheet updatePhase:phase step:step steps:steps fraction:pct text:text cancelEnabled:!_app.cannotCancel
             cancelling:_app.cancelRequested];
}

/* Re-reads the written state of the cached rows without touching the selection. */
- (void)refreshRowColors {
    MhProject *p = _app.project;
    for (MHLibRow *r in _caseRows) {
        MhCase *c = &p->cases[r.param];
        if (c->in_use) { r.written = case_blue(c); r.status = c->disc_count ? case_worst(c) : 1; }
    }
    MhCase *c = _app.currentCase;
    for (MHLibRow *r in _discRows)
        if (c && r.param < c->disc_count && c->discs[r.param]) r.written = (c->discs[r.param]->flags & MH_BLUE_FLAGS) != 0;
    _suppressSel = YES;
    [_tCases reloadData];
    [_tDiscs reloadData];
    _suppressSel = NO;
    [self selectRowOf:_tCases param:_app.curCase];
    [self selectRowOf:_tDiscs param:_app.curDisc];
}

/* The core cannot be cancelled once the card is being committed. */
- (void)markCannotCancel {
    _app.cannotCancel = YES;
    _btnWrite.enabled = NO;
}

/* Starts a worker job under busy state. work runs off the main thread; done on the main thread
 * with busy still set (it must call setBusy:NO itself, like the original sequences did). */
- (void)startJob:(void (^)(MhProgress *))work done:(void (^)(void))done {
    [self setBusy:YES];
    mh_progress_init(&_prog, progress_cb_c, (__bridge void *)self);
    dispatch_async(_queue, ^{
        work(&self->_prog);
        dispatch_async(dispatch_get_main_queue(), ^{ done(); });
    });
}

/* orig: WM_CLOSE during an operation and the CANCELAR button (LAB_1400205b4). */
- (void)requestCancel {
    if (_app.cannotCancel) return;
    _app.cancelRequested = YES;
    mh_progress_request_cancel(&_prog);
    _btnWrite.enabled = NO;
    [_bar setWriteTitle:@"Cancelling…"];
    [_sheet updatePhase:_lastPhase step:_lastStep steps:0 fraction:_bar.progress.doubleValue text:nil cancelEnabled:NO cancelling:YES];
}

- (BOOL)windowShouldClose:(NSWindow *)w {
    if (_app.busy) { [self requestCancel]; return NO; }
    [NSApp terminate:nil]; /* applicationShouldTerminate asks shouldQuit once */
    return NO;
}

- (BOOL)shouldQuit {
    if (_app.busy) { [self requestCancel]; return NO; }
    if (_app.project->dirty &&
        !MHConfirm(@"Quit with unapplied changes?", @"Some changes were not applied to the card or image. Quit anyway?", @"Quit", YES))
        return NO;
    [self saveState];
    return YES;
}

#pragma mark - text fields (orig: EN_KILLFOCUS on 0xca / 0xcb)

- (void)controlTextDidEndEditing:(NSNotification *)n {
    if (_suppressEdit || _app.busy || _app.loading) return;
    NSTextField *f = n.object;
    if (![f isKindOfClass:NSTextField.class]) return;
    if ([n.userInfo[@"NSTextMovement"] integerValue] == NSCancelTextMovement) { /* Escape reverts */
        [self refreshFields];
        return;
    }
    /* untouched fields show the .cue name; do not write its normalised form back */
    MhCase *c = _app.currentCase;
    MhDisc *d = _app.currentDisc;
    if (f == _fCase || f == _fCaseInsp) {
        if (c && [f.stringValue isEqualToString:case_sidebar_title(c)]) return;
        [self applyCaseName:f.stringValue];
    } else if (f == _fDisc) {
        if (d && [f.stringValue isEqualToString:disc_display_name(d)]) return;
        [self applyDiscName:f.stringValue];
    }
}

- (void)applyCaseName:(NSString *)text {
    MhCase *c = _app.currentCase;
    if (!c) return;
    char norm[MH_CASE_TITLE_MAX];
    mh_normalize_title(text.UTF8String, norm, sizeof norm);
    if (norm[0] && mh_str_icmp(c->title, norm) != 0) {
        mh_str_copy(c->title, sizeof c->title, norm);
        _app.project->dirty = 1;
        [self refreshCases];
        [self refreshFields];
    } else {
        [self refreshFields];
    }
}

- (void)applyDiscName:(NSString *)text {
    MhDisc *d = _app.currentDisc;
    if (!d) return;
    char norm[MH_DISC_NAME_MAX];
    mh_normalize_title(text.UTF8String, norm, sizeof norm);
    if (norm[0] && mh_str_icmp(d->name, norm) != 0) {
        mh_str_copy(d->name, sizeof d->name, norm);
        d->flags |= MH_DF_USER_NAMED;
        _app.project->dirty = 1;
        [self refreshDiscs];
        [self refreshFields];
    } else {
        [self refreshFields];
    }
}

#pragma mark - list owner (keys, double click, context menu)

/* Which list the user is working in: 1 cases, 2 discs. */
- (int)activeKind {
    NSResponder *r = _window.firstResponder;
    if ([r isKindOfClass:NSView.class]) {
        NSView *v = (NSView *)r;
        if (v == _tCases || [v isDescendantOf:_tCases]) return 1;
        if (v == _tDiscs || [v isDescendantOf:_tDiscs]) return 2;
        if (v == _fCase || [v isDescendantOf:_fCase] || v == _fCaseInsp || [v isDescendantOf:_fCaseInsp]) return 1;
        if (v == _fDisc || [v isDescendantOf:_fDisc]) return 2;
    }
    return _app.currentDisc ? 2 : 1;
}

- (void)list:(MHListTable *)t keyDown:(unichar)key keyCode:(unsigned short)code {
    if (_app.busy) return;
    if (code == 120 || code == 36 || code == 76) { /* F2 / Return */
        [self renameKind:t == _tCases ? 1 : 2];
        return;
    }
    if (t == _tCases) [self removeCase]; /* VK_DELETE */
    else [self cmdRemoveDisc];
}

- (void)rowDoubleClicked:(id)s {
    MHListTable *t = s;
    if (_app.busy || t.clickedRow < 0) return;
    [self renameKind:t == _tCases ? 1 : 2];
}

- (void)renameKind:(int)kind {
    if (_app.busy) return;
    if (kind == 1) {
        if (!_fCase.isEnabled) return;
        [_window makeFirstResponder:_fCase];
        [_fCase selectText:nil];
    } else {
        if (!_fDisc.isEnabled) return;
        if (_inspItem.isCollapsed) [self setPaneVisible:NO visible:YES];
        [_window makeFirstResponder:_fDisc];
        [_fDisc selectText:nil];
    }
}

- (void)renameItem:(id)s { [self renameKind:[self activeKind]]; }

- (NSMenuItem *)addItem:(NSString *)title action:(SEL)a to:(NSMenu *)m {
    NSMenuItem *it = [m addItemWithTitle:title action:a keyEquivalent:@""];
    it.target = self;
    return it;
}

- (NSMenu *)list:(MHListTable *)t menuForRow:(NSInteger)row {
    if (_app.busy) return nil;
    NSMenu *m = [[NSMenu alloc] init];
    [self addItem:@"Move Up" action:@selector(moveItemUp:) to:m];
    [self addItem:@"Move Down" action:@selector(moveItemDown:) to:m];
    if (t == _tDiscs) {
        MhDisc *d = _app.currentDisc;
        [m addItem:NSMenuItem.separatorItem];
        [self addItem:@"Verify Disc" action:@selector(verifyDiscAction:) to:m];
        if (d && d->serial[0] == 0) [self addItem:@"Recover Serial…" action:@selector(recoverSerialAction:) to:m];
        if (d && d->kind == 1 && (d->verify_issues & 0xc0) != 0 && (d->verify_issues & 8) == 0)
            [self addItem:@"Create EDC/ECC Copy…" action:@selector(recodeAction:) to:m];
        [self addItem:@"Cheats…" action:@selector(cmdCheatsAction:) to:m];
        [self addItem:@"Technical Details…" action:@selector(detailsAction:) to:m];
    }
    [m addItem:NSMenuItem.separatorItem];
    [self addItem:@"Rename" action:@selector(renameItem:) to:m];
    [self addItem:@"Remove…" action:@selector(removeItem:) to:m];
    return m;
}

- (void)removeItem:(id)s {
    if ([self activeKind] == 1) [self removeCase];
    else [self cmdRemoveDisc];
}

- (void)removeCase {
    MhCase *c = _app.currentCase;
    if (!c) return;
    NSString *q = [NSString stringWithFormat:@"\"%@\" and its %@ will be removed from the library. The change is only written to the card when you apply changes.",
                   MHStr(c->title), plural(c->disc_count, @"disc", @"discs")];
    if (!MHConfirm(@"Remove this case?", q, @"Remove", YES)) return;
    mh_project_remove_case(_app.project, c->slot);
    _app.curCase = -1;
    _app.curDisc = -1;
    [self refreshProject];
}

- (void)cmdRemoveDisc { /* orig: cmd_remove_disc */
    MhCase *c = _app.currentCase;
    MhDisc *d = _app.currentDisc;
    if (!c || _app.curDisc < 0 || !d) return;
    NSString *q = [NSString stringWithFormat:@"\"%@\" will be removed from \"%@\". The change is only written to the card when you apply changes.",
                   MHStr(d->name), MHStr(c->title)];
    if (!MHConfirm(@"Remove this disc?", q, @"Remove", YES)) return;
    mh_case_remove_disc(_app.project, c, (uint8_t)_app.curDisc);
    _app.curDisc = -1;
    if (c->disc_count == 0) mh_project_remove_case(_app.project, c->slot);
    [self refreshProject];
}

#pragma mark - reorder (drag, menu and keyboard share these)

/* ins is the insertion row 0...count of the dragged case's list; same arithmetic as the original drop. */
- (void)moveCaseSlot:(int)slot toInsertion:(NSInteger)ins {
    MhProject *pr = _app.project;
    int idx = (int)ins;
    for (NSUInteger i = 0; i < _caseRows.count; i++) {
        if (_caseRows[i].param == slot) {
            if ((int)i < idx) idx--;
            break;
        }
    }
    if (idx < 0) idx = 0;
    if (pr->case_count <= idx) idx = pr->case_count ? pr->case_count - 1 : 0;
    uint8_t out = 0;
    if (mh_project_reorder_case_index(pr, (uint8_t)slot, (uint16_t)idx, &out)) {
        _app.curCase = out;
        _app.curDisc = -1;
    }
    [self refreshProject];
}

/* ins < 0: append to case dc (drop on a case row). Otherwise ins is the insertion row in the disc list. */
- (void)moveDisc:(int)disc ofCase:(int)cs toCase:(int)dc insertion:(NSInteger)ins {
    int to;
    MhCase *dst = [_app caseAt:dc];
    if (!dst) return;
    if (dc == cs && ins >= 0) {
        to = (int)ins;
        if (disc < to) to--;
        MhCase *sc = [_app caseAt:cs];
        int cnt = sc ? sc->disc_count : 0;
        if (cnt <= to) to = cnt - 1;
    } else {
        to = dst->disc_count;
    }
    if (to < 0) to = 0;
    uint8_t oc = 0, od = 0;
    if (mh_project_move_disc(_app.project, (uint8_t)cs, (uint8_t)disc, (uint8_t)dc, (uint8_t)to, &oc, &od)) {
        _app.curCase = oc;
        _app.curDisc = od;
    }
    [self refreshProject];
}

- (BOOL)canMoveKind:(int)kind delta:(int)delta {
    if (_app.busy || _app.loading) return NO;
    if (kind == 1) {
        NSInteger i = _tCases.selectedRow;
        return i >= 0 && i + delta >= 0 && i + delta < (NSInteger)_caseRows.count;
    }
    MhCase *c = _app.currentCase;
    return c && _app.curDisc >= 0 && _app.curDisc + delta >= 0 && _app.curDisc + delta < c->disc_count;
}

- (void)moveKind:(int)kind delta:(int)delta {
    if (![self canMoveKind:kind delta:delta]) return;
    if (kind == 1) {
        NSInteger i = _tCases.selectedRow;
        /* the insertion row is before row i-1 (up) or after row i+1 (down) */
        [self moveCaseSlot:(int)_caseRows[i].param toInsertion:delta < 0 ? i - 1 : i + 2];
        [self announce:[NSString stringWithFormat:@"Moved to position %ld", (long)(i + delta + 1)]];
    } else {
        int d = _app.curDisc;
        [self moveDisc:d ofCase:_app.curCase toCase:_app.curCase insertion:delta < 0 ? d - 1 : d + 2];
        [self announce:[NSString stringWithFormat:@"Moved to position %d", d + delta + 1]];
    }
}

- (void)moveItemUp:(id)s { [self moveKind:[self activeKind] delta:-1]; }
- (void)moveItemDown:(id)s { [self moveKind:[self activeKind] delta:1]; }

/* ---- native drag and drop between rows (the row pasteboard carries "case:<slot>" or "disc:<case>:<index>") ---- */

- (id<NSPasteboardWriting>)tableView:(NSTableView *)t pasteboardWriterForRow:(NSInteger)row {
    if (_app.busy || _app.loading) return nil;
    NSArray<MHLibRow *> *prow = [self rowsFor:t];
    if (row < 0 || row >= (NSInteger)prow.count) return nil;
    MHLibRow *r = prow[row];
    NSPasteboardItem *it = [[NSPasteboardItem alloc] init];
    [it setString:t == _tCases ? [NSString stringWithFormat:@"case:%ld", (long)r.param]
                               : [NSString stringWithFormat:@"disc:%d:%ld", _app.curCase, (long)r.param]
          forType:MHRowPboardType];
    return it;
}

- (NSDragOperation)tableView:(NSTableView *)t validateDrop:(id<NSDraggingInfo>)info proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)op {
    if (_app.busy || _app.loading) return NSDragOperationNone;
    NSString *s = [info.draggingPasteboard stringForType:MHRowPboardType];
    if (!s) return NSDragOperationNone;
    NSInteger count = [self rowsFor:t].count;
    if ([s hasPrefix:@"case:"]) {
        if (t != _tCases) return NSDragOperationNone;
        [t setDropRow:MIN(row, count) dropOperation:NSTableViewDropAbove];
        return NSDragOperationMove;
    }
    NSArray *parts = [s componentsSeparatedByString:@":"];
    int fromCase = parts.count > 1 ? [parts[1] intValue] : -1;
    if (t == _tDiscs) {
        if (fromCase != _app.curCase) return NSDragOperationNone;
        [t setDropRow:MIN(row, count) dropOperation:NSTableViewDropAbove];
        return NSDragOperationMove;
    }
    /* disc onto a case row of the sidebar */
    if (row < 0 || row >= count) return NSDragOperationNone;
    [t setDropRow:row dropOperation:NSTableViewDropOn];
    MhCase *c = [_app caseAt:(int)_caseRows[row].param];
    if (!c) return NSDragOperationNone;
    if ((int)_caseRows[row].param != fromCase && c->disc_count >= 5) return NSDragOperationNone;
    return NSDragOperationMove;
}

- (BOOL)tableView:(NSTableView *)t acceptDrop:(id<NSDraggingInfo>)info row:(NSInteger)row dropOperation:(NSTableViewDropOperation)op {
    NSString *s = [info.draggingPasteboard stringForType:MHRowPboardType];
    NSArray *parts = [s componentsSeparatedByString:@":"];
    if (!s || parts.count < 2) return NO;
    if ([parts[0] isEqualToString:@"case"]) {
        int sl = [parts[1] intValue];
        if (sl < 0 || sl >= MH_PROJECT_MAX_CASES) return NO;
        [self moveCaseSlot:[parts[1] intValue] toInsertion:row];
        return YES;
    }
    int fromCase = [parts[1] intValue], disc = parts.count > 2 ? [parts[2] intValue] : -1;
    if (fromCase < 0 || fromCase >= MH_PROJECT_MAX_CASES || disc < 0 || disc > 255) return NO;
    if (t == _tDiscs) [self moveDisc:disc ofCase:fromCase toCase:fromCase insertion:row];
    else if (row >= 0 && row < (NSInteger)_caseRows.count)
        [self moveDisc:disc ofCase:fromCase toCase:(int)_caseRows[row].param insertion:-1];
    else return NO;
    return YES;
}

#pragma mark - button actions

- (void)toggleVerify:(id)s {
    if ([s isKindOfClass:NSMenuItem.class]) _content.chkVerify.state = _content.chkVerify.state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn;
    _app.verifyOnAdd = _content.chkVerify.state == NSControlStateValueOn;
}

- (void)showAbout {
    NSDictionary *info = NSBundle.mainBundle.infoDictionary;
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.alignment = NSTextAlignmentCenter;
    NSDictionary *base = @{ NSFontAttributeName : [NSFont systemFontOfSize:11], NSParagraphStyleAttributeName : ps,
                            NSForegroundColorAttributeName : NSColor.secondaryLabelColor };
    NSMutableAttributedString *c = [[NSMutableAttributedString alloc]
        initWithString:@"A macOS port of the PicoStation Multi House V3 Manager.\n\nOriginal Manager by ALBERONISSE (Multi House).\n" attributes:base];
    NSString *links[][2] = { { @"ALBERONISSE on multihouse.net", @"https://www.multihouse.net" },
                             { @"Source code on GitHub", @"https://github.com/orenatobr/PicoStation-MultiHouse-macOS" } };
    for (int i = 0; i < 2; i++) {
        NSMutableDictionary *a = [base mutableCopy];
        a[NSLinkAttributeName] = [NSURL URLWithString:links[i][1]];
        [c appendAttributedString:[[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@"\n%@", links[i][0]] attributes:a]];
    }
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName : MH_TITLE,
        NSAboutPanelOptionApplicationVersion : info[@"CFBundleShortVersionString"] ?: @"",
        NSAboutPanelOptionVersion : @"",
        NSAboutPanelOptionCredits : c }];
}

- (BOOL)commandsAllowed { return !_app.loading; }

- (void)addGame:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    [self addDialogFilesTarget:-1];
}

- (void)addDisc:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    if (_app.currentCase) [self addDialogFilesTarget:_app.curCase];
    else MHMessage(@"No Case Selected", @"Select a case first, then add the disc to it.", MHIconError);
}

- (void)caseArt:(id)s { [self makeCustomArtForCase:YES]; }
- (void)discArt:(id)s { [self makeCustomArtForCase:NO]; }
- (void)cmdCheatsAction:(id)s { [self cmdCheats]; }
- (void)refreshSDAction:(id)s { if (!_app.busy && !_app.loading) [self refreshSD]; }
- (void)sdChanged:(id)s {
    _popSD.toolTip = _popSD.titleOfSelectedItem;
    if (!_app.loading) [self loadSelectedV2:YES];
}
- (void)toggleSidebar:(id)s { [self toggleSidebarAction:s]; }

- (void)verifyDiscAction:(id)s {
    MhDisc *vd = _app.currentDisc;
    if (!vd || _app.busy) return;
    [self admitDiscUI:vd verify:YES completion:^(BOOL ok) {
        [self refreshProject];
        if (ok && vd->verified && vd->verify_issues == 0)
            MHMessage(@"Disc Verified", MHStr(vd->verify_text), MHIconInfo);
    }];
}
- (void)recoverSerialAction:(id)s { [self recoverSerial:_app.currentDisc silent:NO]; }
- (void)recodeAction:(id)s { [self recodeDisc]; }
- (void)detailsAction:(id)s { [self showTechnicalDetails]; }

#pragma mark - adding CUEs (orig: add_dialog_files, add_cue_path, process_drop, finish_batch)

/* orig: add_dialog_files. target < 0 means "group by game" (0xffffffff). */
- (void)addDialogFilesTarget:(int)target {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.title = @"Add Discs";
    p.prompt = @"Add";
    p.allowsMultipleSelection = YES;
    p.canChooseDirectories = NO;
    p.message = @"Choose one or more PlayStation CUE files.";
    p.allowedFileTypes = @[ @"cue" ];
    void (^go)(NSModalResponse) = ^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        NSMutableArray *paths = [NSMutableArray array];
        for (NSURL *u in p.URLs) [paths addObject:u.path];
        [self addCuePaths:paths target:target duplicates:0 ignored:0];
    };
    [p beginSheetModalForWindow:_window completionHandler:go];
}

/* ---- the dialogs the original opened from inside manager_parse / add_cue_path (hooks) ---- */

static bool hook_ask(void *ctx, const char *text) { return MHAsk(MHStr(text)); }

/* orig: common_locate. */
static bool hook_locate(void *ctx, char out[MH_PATH_MAX]) {
    __block bool ok = false;
    MHMain(^{
        NSOpenPanel *p = [NSOpenPanel openPanel];
        p.title = @"Locate the BIN File";
        p.message = @"Choose the BIN file that belongs to this CUE.";
        p.prompt = @"Choose";
        p.allowedFileTypes = @[ @"bin" ];
        p.canChooseDirectories = NO;
        if ([p runModal] == NSModalResponseOK && p.URL.path.length < MH_PATH_MAX) {
            mh_str_copy(out, MH_PATH_MAX, p.URL.fileSystemRepresentation);
            ok = true;
        }
    });
    return ok;
}

/* orig: admit_cue_disc (the busy toggling is done by the batch job). */
static bool hook_admit(void *ctx, MhDisc *d) {
    MHMainController *c = (__bridge MHMainController *)ctx;
    return [c admitOnWorker:d verify:MHApp.shared.verifyOnAdd];
}

static void hook_attach(void *ctx, MhDisc *d) { mh_v2_auto_attach_cheats(MHApp.shared.env, d); }

- (BOOL)admitOnWorker:(MhDisc *)d verify:(BOOL)verify {
    MHMain(^{ [self setStatus:verify ? @"Verifying the disc before adding…" : @"Checking CUE and BIN files…"]; });
    MhV2Writer w = *_app.writer;
    w.prog = &_prog;
    BOOL ok = mh_v2_manager_admit(&w, d, verify, mh_keep_disc_with_observations, NULL);
    if (!ok && !mh_progress_cancelled(&_prog) && d && d->verified == 0)
        MHMessage(@"Disc Not Added", MHStr(d->verify_text), MHIconError);
    return ok;
}

/* orig: admit_cue_disc as a stand-alone command ("Verify Disc"). */
- (void)admitDiscUI:(MhDisc *)d verify:(BOOL)verify completion:(void (^)(BOOL))completion {
    __block BOOL ok = NO;
    [self startJob:^(MhProgress *prog) { ok = [self admitOnWorker:d verify:verify]; }
              done:^{
                  [self setBusy:NO];
                  completion(ok);
              }];
}

/* orig: add_cue_path.constprop.0 minus the dialogs it owns. Worker thread. */
- (BOOL)addCuePath:(NSString *)path target:(int)target {
    MHMain(^{ [self setStatus:@"Reading CUE and BIN files…"]; });
    MhCueHooks hooks = {.ctx = (__bridge void *)self, .ask = hook_ask, .locate = hook_locate,
                        .admit = hook_admit, .attach_cheats = hook_attach};
    char err[MH_ERR_MSG_MAX] = "";
    MhAddResult res = {0};
    MhAddStatus st = mh_project_add_cue(_app.project, _app.db, path.UTF8String,
                                        target < 0 ? 0x100u : (unsigned)target, &hooks, &res, err, sizeof err);
    switch (st) {
        case MH_ADD_OK:
            { int sl = res.cas->slot, di = res.disc_index; MHMain(^{ self->_app.curCase = sl; self->_app.curDisc = di; }); }
            return YES;
        case MH_ADD_PARSE_FAILED:
            MHMessage(@"Couldn't Read Disc", err[0] ? MHStr(err) : @"A CUE file could not be parsed.", MHIconError);
            break;
        case MH_ADD_LIMIT:
        case MH_ADD_NO_ROOM:
            if (err[0]) MHMessage(@"Couldn't Add Disc", MHStr(err), MHIconError);
            break;
        default: break; /* not a CUE, duplicate, rejected (already reported) */
    }
    return NO;
}

- (void)finishBatchOK:(unsigned)ok failed:(unsigned)fail {
    [self refreshProject];
    NSMutableString *s = [NSMutableString stringWithString:plural(ok, @"disc", @"discs")];
    [s appendString:@" added"];
    if (fail) [s appendFormat:@"; %u skipped or failed", fail];
    [self setStatus:s];
    [self announce:s];
}

/* orig: process_drop (after the hit test) for the Finder and the file panel. target < 0: group. */
- (void)importPaths:(NSArray<NSString *> *)paths target:(int)target {
    if (_app.busy || _app.loading || !paths.count) return;
    __block NSString *failure = nil;
    __block unsigned ok = 0, fail = 0;
    [self startJob:^(MhProgress *prog) {
        MhCollect *c = mh_collect_new();
        if (!c) return;
        for (NSString *path in paths) {
            if (path.length >= 0x3ff) {
                c->failed = 1;
                mh_str_copy(c->error, sizeof c->error, "A dropped path is longer than the supported limit.");
                break;
            }
            if (target < 0 || mh_str_ends_i(path.UTF8String, ".cue")) {
                if (!mh_import_collect(c, path.UTF8String)) break;
            } else {
                c->ignored++;
            }
        }
        if (c->failed) {
            failure = MHStr(c->error);
        } else if (c->count == 0) {
            failure = @"No CUE files were found. Drop game folders, or .cue files onto a case.";
        } else {
            mh_import_sort(c);
            fail = c->duplicates + c->ignored;
            for (uint32_t i = 0; i < c->count; i++) {
                if ([self addCuePath:MHStr(c->paths[i]) target:target]) ok++;
                else fail++;
                if (mh_progress_cancelled(prog)) break;
            }
        }
        mh_collect_free(c);
    } done:^{
        [self setBusy:NO];
        if (failure) {
            [self refreshProject];
            MHMessage(@"Nothing to Add", failure, MHIconError);
        } else {
            [self finishBatchOK:ok failed:fail];
        }
    }];
}

- (void)importPaths:(NSArray<NSString *> *)paths { [self importPaths:paths target:-1]; }

- (void)addCuePaths:(NSArray<NSString *> *)paths target:(int)target duplicates:(unsigned)d ignored:(unsigned)i {
    /* orig: add_dialog_files: plain CUE paths, no folder walk */
    if (_app.busy || _app.loading || !paths.count) return;
    __block unsigned ok = 0, fail = d + i;
    [self startJob:^(MhProgress *prog) {
        for (NSString *p in paths) {
            if ([self addCuePath:p target:target]) ok++;
            else fail++;
            if (mh_progress_cancelled(prog)) break;
        }
    } done:^{
        [self setBusy:NO];
        [self finishBatchOK:ok failed:fail];
    }];
}

#pragma mark - drag and drop from Finder (orig: WM_DROPFILES, DragAcceptFiles)

/* Where a Finder drop goes: -2 nowhere, -1 group by game (sidebar or no case), >= 0 the case receiving the discs. */
- (int)dropTargetFor:(id<NSDraggingInfo>)s {
    NSPoint p = [_shell convertPoint:s.draggingLocation fromView:nil];
    NSRect sr = _sideItem.isCollapsed ? NSZeroRect : [_shell convertRect:_side.view.bounds fromView:_side.view];
    NSRect cr = [_shell convertRect:_content.view.bounds fromView:_content.view];
    if (NSPointInRect(p, sr)) return -1;
    if (NSPointInRect(p, cr)) return _app.currentCase ? _app.curCase : -1;
    return -2;
}

- (NSDragOperation)dropOperationFor:(id<NSDraggingInfo>)s {
    if (_app.busy || _app.loading) { [_content.overlay setActive:NO]; return NSDragOperationNone; }
    int t = [self dropTargetFor:s];
    if (t == -2) { [_content.overlay setActive:NO]; return NSDragOperationNone; }
    MhCase *c = t >= 0 ? [_app caseAt:t] : NULL;
    _content.overlay.detail = c ? [NSString stringWithFormat:@"Discs will be added to \"%@\"", MHStr(c->title)]
                                : @"Folders become cases, .cue files become discs";
    [_content.overlay setActive:YES];
    return NSDragOperationCopy;
}

- (void)dropEnded { [_content.overlay setActive:NO]; }

- (BOOL)performDrop:(id<NSDraggingInfo>)s {
    if (_app.busy || _app.loading) return NO;
    int target = [self dropTargetFor:s];
    if (target == -2) return NO;
    NSArray<NSURL *> *urls = [s.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    NSMutableArray *paths = [NSMutableArray array];
    for (NSURL *u in urls) if (u.path) [paths addObject:u.path];
    [self importPaths:paths target:target];
    return YES;
}

#pragma mark - Menu Settings (orig: cmd_defaults)

- (void)cmdDefaults:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    MhProject *p = _app.project;
    uint8_t rec[MH_MENU_DEFAULTS_SIZE];
    if (p->has_menu_defaults) {
        memcpy(rec, p->menu_defaults, sizeof rec);
    } else {
        char path[MH_PATH_MAX];
        mh_path_join(_app.embeddedDir.fileSystemRepresentation, "picostation_menu.bin", path, sizeof path);
        uint8_t *img = NULL;
        uint64_t size = 0;
        BOOL ok = mh_read_entire_file(path, &img, &size) && mh_menu_defaults_read(img, size, rec, NULL);
        mh_free(img);
        if (!ok) {
            MHMessage(@"Menu Settings Unavailable", @"The built-in menu defaults are invalid. Reinstall the app.", MHIconError);
            return;
        }
    }
    if (!MHMenuDefaultsDialog(_window, rec)) return;
    char path[MH_PATH_MAX];
    mh_path_join(_app.dataDir.fileSystemRepresentation, "menu-defaults.dat", path, sizeof path);
    if (mh_menu_defaults_valid(rec)) {
        if (mh_menu_defaults_save(path, rec)) {
            mh_project_set_menu_defaults(p, rec);
            p->dirty = 1;
            [self refreshFooter];
            [self setStatus:@"Menu settings saved. Apply them to the card or image. The Memory Card save takes priority."];
        } else {
            MHMessage(@"Couldn't Save Menu Settings", @"The settings could not be saved next to the app.", MHIconError);
        }
    } else {
        MHMessage(@"Invalid Menu Settings", @"The settings are not valid. Restore the defaults and try again.", MHIconError);
    }
}

#pragma mark - Image menu (orig: the popup of main_proc, cmd_image_open, cmd_image_save_as)

- (void)imageApply:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    MhProject *p = _app.project;
    if (!p->image_loaded || !p->image_path[0]) {
        MHMessage(@"No Image Open", @"No V2 image is open. Use Save Image As instead.", MHIconError);
    } else if (MHConfirm(@"Apply changes to the open image?", @"The changes will be written directly into the open V2 image.", @"Apply", NO)) {
        [self writeImageTo:MHStr(p->image_path)];
    }
}

- (void)imageNew:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    MhProject *p = _app.project;
    if (!p->dirty || MHConfirm(@"Start a new project?", @"Changes that were not applied will be discarded.", @"Discard and Start New", YES)) {
        mh_project_clear(p);
        [self useDefaultMenu];
        _app.curCase = _app.curDisc = -1;
        [self refreshProject];
        [self setStatus:@"Empty project. Add games, then save an image or create a card."];
    }
}

/* orig: project_use_default_menu after a clear. */
- (void)useDefaultMenu {
    char path[MH_PATH_MAX];
    uint8_t rec[MH_MENU_DEFAULTS_SIZE];
    mh_path_join(_app.dataDir.fileSystemRepresentation, "menu-defaults.dat", path, sizeof path);
    if (mh_menu_defaults_load(path, rec)) mh_project_set_menu_defaults(_app.project, rec);
}

static NSString *image_cache_dir(NSString *leaf) {
    NSString *d = [NSTemporaryDirectory() stringByAppendingPathComponent:leaf];
    mh_ensure_dir(d.fileSystemRepresentation);
    return d;
}

- (void)imageOpen:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    MhProject *p = _app.project;
    if (p->dirty && !MHConfirm(@"Open another image?", @"Changes that were not applied will be discarded.", @"Discard and Open", YES)) return;
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.title = @"Open V2 Image";
    op.message = @"Choose a PSXRAW V2 image (.img, .raw or .psxraw).";
    op.allowedFileTypes = @[ @"img", @"raw", @"psxraw" ];
    op.canChooseDirectories = NO;
    [op beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [self loadImageAtPath:op.URL.path];
    }];
}

- (void)loadImageAtPath:(NSString *)path {
    if (!path.length) return;
    NSString *cache = image_cache_dir(@"PicoStationV2ImageCache");
    __block MhMsg msg = {{0}};
    __block BOOL ok = NO;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@"Loading the V2 image…"]; });
        MhV2Result r;
        memset(&r, 0, sizeof r);
        ok = mh_v2_load_image(path.fileSystemRepresentation, cache.fileSystemRepresentation, self->_app.env, self->_app.project, &r);
        mh_str_copy(msg.s, sizeof msg.s, r.msg);
        if (ok) mh_v2_hydrate_project(self->_app.project, self->_app.env);
    } done:^{
        [self setBusy:NO];
        if (ok) {
            self->_app.curCase = self->_app.curDisc = -1;
            [self refreshProject];
            [self setStatus:MHStr(msg.s)];
        } else {
            MHMessage(@"Couldn't Open Image", msg.s[0] ? MHStr(msg.s) : @"This is not a valid V2 image.", MHIconError);
        }
    }];
}

- (void)imageSaveAs:(id)s {
    if (![self commandsAllowed] || _app.busy) return;
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.title = @"Save V2 Image";
    sp.message = @"Save the library as a PSXRAW V2 image.";
    sp.nameFieldStringValue = @"PicoStation V2.img";
    sp.allowedFileTypes = @[ @"img" ];
    [sp beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [self writeImageTo:sp.URL.path];
    }];
}

/* orig: write_image_direct_to.isra.0 */
- (void)writeImageTo:(NSString *)path {
    if (!path.length) return;
    const char *blocked = mh_v2_blocked_text(_app.dbOK, _app.cheatOK);
    if (blocked) { MHMessage(@"Can't Write Image", MHStr(blocked), MHIconError); return; }
    if (![self allReady]) return;
    MhV2Result prep;
    memset(&prep, 0, sizeof prep);
    if (!mh_v2_prepare_write(_app.project, &prep)) {
        MHMessage(@"Can't Write Image", prep.msg[0] ? MHStr(prep.msg) : @MH_V2W_NOT_PREPARED, MHIconError);
        return;
    }
    __block MhV2Result res;
    memset(&res, 0, sizeof res);
    __block BOOL ok = NO, loaded = NO;
    NSString *cache = image_cache_dir(@"PicoStationV2ImageCache");
    _bar.progress.doubleValue = 0;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@MH_V2W_IMAGE_STATUS]; });
        MhV2Writer w = *self->_app.writer;
        w.prog = prog;
        ok = mh_v2_write_image_direct(&w, self->_app.project, path.fileSystemRepresentation, &res);
        if (ok) {
            MhV2Result lr;
            memset(&lr, 0, sizeof lr);
            loaded = mh_v2_load_image(path.fileSystemRepresentation, cache.fileSystemRepresentation, self->_app.env, self->_app.project, &lr);
            if (loaded) mh_v2_hydrate_project(self->_app.project, self->_app.env);
        }
    } done:^{
        if (!ok) {
            [self writeFailure:res.msg[0] ? MHStr(res.msg) : @MH_V2W_IMAGE_FAILED];
        } else if (!loaded) {
            [self setBusy:NO];
            [self dismissSheet];
            MHMessage(@"Image Written", @MH_V2W_IMAGE_NOT_REOPENED, MHIconError);
        } else {
            [self setBusy:NO];
            self->_app.curCase = self->_app.curDisc = -1;
            [self refreshProject];
            [self setStatus:@MH_V2W_IMAGE_DONE];
            [self announce:@MH_V2W_IMAGE_DONE];
            if (self->_sheet) [self->_sheet finishSuccessTitle:@"Image saved" summary:[NSString stringWithFormat:@"%@\n\n%@", @MH_V2W_IMAGE_DONE, path]
                                                     completion:^{ self->_sheet = nil; }];
        }
    }];
    [self presentProgressSheet:@"Saving V2 image"];
}

#pragma mark - write (orig: all_ready, cmd_write, write_failure)

- (void)presentProgressSheet:(NSString *)title {
    __weak typeof(self) weak = self;
    _sheet = [MHProgressSheet presentOn:_window title:title];
    _sheet.onCancel = ^{ [weak requestCancel]; };
}

- (void)dismissSheet {
    [_sheet dismiss];
    _sheet = nil;
}

/* orig: all_ready (text from the core). */
- (BOOL)allReady {
    char msg[MH_ERR_MSG_MAX] = "";
    bool obs = false;
    if (mh_v2_all_ready(_app.project, &obs, msg, sizeof msg)) return YES;
    MHMessage(@"Library Not Ready", msg[0] ? MHStr(msg) : @"Some discs are incomplete.", MHIconError);
    return NO;
}

- (void)cmdWrite:(id)s {
    if (_app.busy) { /* orig: LAB_1400205b4 */
        [self requestCancel];
        return;
    }
    if (_app.loading) return;
    [self doWrite];
}

- (int)selectedDiskIndex {
    NSInteger i = _popSD.indexOfSelectedItem;
    return (i < 0 || i >= _diskCount) ? -1 : (int)i;
}

/* orig: cmd_write, up to the confirmation window. */
- (void)doWrite {
    int di = [self selectedDiskIndex];
    MhProject *p = _app.project;
    if (!_app.dbOK || !_app.cheatOK) {
        MHMessage(@"Can't Write", MHStr(mh_v2_blocked_text(_app.dbOK, _app.cheatOK)), MHIconError);
        return;
    }
    if (![self allReady]) return;
    uint64_t need = 0;
    const char *why = mh_v2_write_precheck(p, _app.dbOK, _app.cheatOK, di >= 0, di >= 0 ? _disks[di].number : 0,
                                           di >= 0 ? _disks[di].size : 0, &need);
    if (why) { MHMessage(@"Can't Write", MHStr(why), MHIconError); return; }
    MhV2Result prep;
    memset(&prep, 0, sizeof prep);
    if (!mh_v2_prepare_write(p, &prep)) {
        MHMessage(@"Can't Write", prep.msg[0] ? MHStr(prep.msg) : @MH_V2W_NOT_PREPARED, MHIconError);
        return;
    }
    if (di < 0) return;
    [self confirmAndWrite:_disks[di] needBytes:need];
}

- (void)confirmAndWrite:(MhDiskInfo)disk needBytes:(uint64_t)need {
    char text[2048];
    mh_v2_write_confirm_text(_app.project, disk.bsd_name, disk.model, disk.serial, need, text, sizeof text);
    MHWriteSummary *sum = [[MHWriteSummary alloc] init];
    sum.bsd = MHStr(disk.bsd_name);
    sum.model = MHStr(disk.model);
    sum.serial = MHStr(disk.serial);
    sum.cardSize = disk.size;
    sum.estimate = need;
    sum.cases = _app.project->case_count;
    sum.discs = _app.project->disc_count;
    sum.editingCard = _app.project->card_loaded != 0;
    sum.details = MHStr(text);
    if (!MHConfirmWrite(_window, sum)) {
        [_window makeKeyAndOrderFront:nil];
        return;
    }
    __block MhV2Result res;
    memset(&res, 0, sizeof res);
    __block MhMsg err = {{0}};
    __block BOOL ok = NO;
    _bar.progress.doubleValue = 0;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@MH_V2W_SD_STATUS]; });
        MhV2Writer w = *self->_app.writer;
        w.prog = prog;
        MhDiskOpts opts = self->_app.diskOpts;
        ok = mh_sd_write_project_direct(&disk, &opts, self->_app.project, true, &w, err.s, MH_ERR_MSG_MAX, &res);
    } done:^{
        if (!ok) {
            const char *m = err.s[0] ? err.s : res.msg;
            [self writeFailure:m[0] ? MHStr(m) : @MH_V2W_SD_FAILED];
            return;
        }
        self->_bar.progress.doubleValue = 100;
        [self setBusy:NO];
        self->_app.project->dirty = 0;
        [self setStatus:@MH_V2W_SD_DONE];
        [self announce:@MH_V2W_SD_DONE];
        char done[1024];
        mh_v2_write_done_text(res.cases, res.discs, res.size, done, sizeof done);
        void (^follow)(void) = ^{
            self->_sheet = nil;
            self->_app.project->card_loaded = 0;
            if (!self->_app.loading) [self refreshSD];
            [self loadSelectedV2:NO];
        };
        if (self->_sheet) [self->_sheet finishSuccessTitle:@"Card written" summary:MHStr(done) completion:follow];
        else { MHMessage(@"Card Written", MHStr(done), MHIconInfo); follow(); }
    }];
    [self presentProgressSheet:[NSString stringWithFormat:@"Writing to %@", MHStr(disk.bsd_name)]];
}

/* orig: write_failure */
- (void)writeFailure:(NSString *)text {
    BOOL cancelled = _app.cancelRequested;
    [self setBusy:NO];
    [self refreshProject];
    char out[2048];
    mh_v2_write_failure_text(text.UTF8String, cancelled, out, sizeof out);
    if (cancelled) {
        [self dismissSheet];
        [self setStatus:MHStr(out)];
        [self announce:MHStr(out)];
        return;
    }
    [self setStatus:@MH_V2W_FAIL_STATUS];
    [self announce:@"Write interrupted"];
    if (_sheet) [_sheet finishFailureTitle:@"Write interrupted" text:MHStr(out) completion:^{ self->_sheet = nil; }];
    else MHMessage(@"Write Interrupted", MHStr(out), MHIconError);
}

#pragma mark - SD list (orig: refresh_sd, load_selected_v2, WM_DEVICECHANGE)

- (void)startDeviceWatch {
    if (_da) return;
    _da = DASessionCreate(kCFAllocatorDefault);
    if (!_da) return;
    DASessionSetDispatchQueue(_da, dispatch_get_main_queue());
    DARegisterDiskAppearedCallback(_da, NULL, device_changed, (__bridge void *)self);
    DARegisterDiskDisappearedCallback(_da, NULL, device_changed, (__bridge void *)self);
}

static void device_changed(DADiskRef disk, void *ctx) {
    MHMainController *c = (__bridge MHMainController *)ctx;
    /* debounce: a card produces several disks within a moment */
    [NSObject cancelPreviousPerformRequestsWithTarget:c selector:@selector(deviceChanged) object:nil];
    [c performSelector:@selector(deviceChanged) withObject:nil afterDelay:1.0];
}

- (void)deviceChanged { /* orig: WM_DEVICECHANGE (0x219): ignored while busy or loading */
    if (_app.busy || _app.loading) return;
    [self refreshSD];
}

/* orig: refresh_sd.part.0. The scan reads headers, so it runs off the main thread. */
- (void)refreshSD {
    NSUInteger gen = ++_sdGen;
    int previous = -1;
    int sel = [self selectedDiskIndex];
    if (sel >= 0) previous = (int)_disks[sel].number;
    dispatch_async(_queue, ^{
        MhDiskInfo *list = calloc(MH_DISK_MAX, sizeof(MhDiskInfo));
        char err[256] = "";
        MhDiskOpts opts = self->_app.diskOpts;
        int n = mh_disk_enumerate(list, MH_DISK_MAX, &opts, err, sizeof err);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (gen == self->_sdGen) [self applyDisks:list count:n previous:previous];
            free(list);
        });
    });
}

- (void)applyDisks:(MhDiskInfo *)list count:(int)n previous:(int)previous {
    _diskCount = n < 0 ? 0 : n;
    memcpy(_disks, list, sizeof(MhDiskInfo) * (size_t)_diskCount);
    [_popSD removeAllItems];
    int pick = -1;
    for (int i = 0; i < _diskCount; i++) {
        char label[256];
        mh_disk_label_text(&_disks[i], label, sizeof label);
        [_popSD addItemWithTitle:MHStr(label)];
        if ((int)_disks[i].number == previous) pick = i;
    }
    if (_diskCount == 0) {
        [_popSD addItemWithTitle:@"No card found"];
        _popSD.enabled = NO;
        if (!_app.busy) [self setStatus:@MH_DISK_TXT_NONE];
        return;
    }
    _popSD.enabled = !_app.busy;
    int idx = pick >= 0 ? pick : 0;
    [_popSD selectItemAtIndex:idx];
    _popSD.toolTip = _popSD.titleOfSelectedItem;
    MhDiskInfo *d = &_disks[idx];
    if (d->has_v2 && d->v2_state == 2 && _app.project->disc_count == 0) {
        [self loadSelectedV2:NO];
        return;
    }
    if (!_app.busy) [self setStatus:@MH_DISK_TXT_FOUND];
}

/* orig: load_selected_v2.isra.0 */
- (void)loadSelectedV2:(BOOL)userInitiated {
    int di = [self selectedDiskIndex];
    if (di < 0) return;
    MhDiskInfo disk = _disks[di];
    MhProject *p = _app.project;
    if (!(disk.has_v2 && disk.v2_state == 2)) return;
    if (!(!p->card_loaded || p->device != (int32_t)disk.number || p->dirty)) return;
    if (userInitiated && p->disc_count != 0 && !(p->dirty == 0 && p->card_loaded) &&
        !MHConfirm(@"Load the selected card?", @"The current project has not been applied and will be discarded.", @"Discard and Load", YES))
        return;
    NSString *cache = image_cache_dir([NSString stringWithFormat:@"PicoStationV2Cache-%s", disk.bsd_name]);
    __block MhMsg msg = {{0}};
    __block BOOL ok = NO;
    _app.loading = YES;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@"Loading the existing V2 library…"]; });
        MhV2Result r;
        memset(&r, 0, sizeof r);
        MhDiskOpts opts = self->_app.diskOpts;
        ok = mh_v2_load_physical(&disk, &opts, cache.fileSystemRepresentation, self->_app.env, self->_app.project, &r);
        mh_str_copy(msg.s, sizeof msg.s, r.msg);
        if (ok) mh_v2_hydrate_project(self->_app.project, self->_app.env);
    } done:^{
        self->_app.loading = NO;
        [self setBusy:NO];
        if (!ok) {
            MHMessage(@"Couldn't Load Card", msg.s[0] ? MHStr(msg.s) : @"The V2 card could not be loaded.", MHIconError);
            return;
        }
        MhProject *pr = self->_app.project;
        mh_str_copy(pr->device_name, sizeof pr->device_name, disk.model);
        mh_str_copy(pr->device_label, sizeof pr->device_label, disk.serial);
        self->_app.curCase = self->_app.curDisc = -1;
        [self refreshProject];
        [self setStatus:msg.s[0] ? MHStr(msg.s) : @"PSXRAW V2 card loaded. Drop CUE files, reorganize, then apply the changes."];
    }];
}

#pragma mark - per disc commands

- (void)cmdCheats { /* orig: cmd_cheats */
    MhDisc *d = _app.currentDisc;
    if (!d || _app.busy) return;
    if (d->serial[0] == 0) [self recoverSerial:d silent:YES];
    NSMutableString *h = [NSMutableString stringWithString:MHStr(d->name)];
    if (d->serial[0]) {
        [h appendFormat:@"  |  Serial: %@", MHStr(d->serial)];
        [h appendString:@"\r\nLinked by serial. Use the button next to it to choose another profile."];
    } else {
        [h appendString:@"\r\nNo serial: you can edit a custom list, but the menu cannot link it to the game automatically."];
    }
    if (MHCheatEditProfile(_window, _app.cheatOK ? _app.cheatDb : NULL, h, d->serial, MH_CHEAT_UI_SCOPE_DISC, &d->cheats)) {
        _app.project->dirty = 1;
        [self refreshFields];
    }
}

/* orig: show_technical_details.constprop.0 */
- (void)showTechnicalDetails {
    MhDisc *d = _app.currentDisc;
    if (!d) return;
    MhWbuf b;
    mh_wbuf_init(&b);
    mh_wbuf_puts(&b, "DISC\r\n\r\nName: ");
    mh_wbuf_puts(&b, d->name);
    mh_wbuf_puts(&b, "\r\nSerial: ");
    mh_wbuf_puts(&b, d->serial);
    mh_wbuf_puts(&b, "\r\nTracks: ");
    mh_wbuf_u32(&b, d->ntracks);
    mh_wbuf_puts(&b, "\r\nPS1 sectors: ");
    mh_wbuf_u32(&b, d->total_sectors);
    mh_wbuf_puts(&b, "\r\nStatus: ");
    mh_wbuf_puts(&b, status_word(d->status).UTF8String);
    if (d->v2_raw_lba512) {
        mh_wbuf_puts(&b, "\r\nPackage LBA: ");
        mh_wbuf_u64(&b, d->v2_pkg_a);
        mh_wbuf_puts(&b, "\r\nData LBA: ");
        mh_wbuf_u64(&b, d->v2_raw_lba512);
    }
    mh_wbuf_puts(&b, "\r\n\r\n");
    mh_wbuf_puts(&b, d->notes);
    if (d->verify_text[0]) {
        mh_wbuf_puts(&b, "\r\n\r\nLast verification: ");
        mh_wbuf_puts(&b, d->verify_text);
    }
    MHShowDetails(_window, @"Technical Details", MHStr(mh_wbuf_cstr(&b)));
    mh_wbuf_free(&b);
}

/* orig: recover_disc_serial_internal.isra.0 (silent = param_2 != 0: no message boxes) */
- (void)recoverSerial:(MhDisc *)d silent:(BOOL)silent {
    if (!d || d->serial[0]) return;
    NSString *errTitle = @"Serial Not Recovered";
    MhPsSerial res;
    memset(&res, 0, sizeof res);
    uint32_t status = MH_PSS_NOT_FOUND;
    NSString *fail = nil;
    if (d->kind == MH_DISC_KIND_CUE) {
        if (d->nfiles && d->files[0].path[0]) status = mh_psserial_extract(&res, d->files[0].path);
        else fail = @"The original CUE/BIN is not available to identify the serial.";
    } else if (d->kind == MH_DISC_KIND_V2_DEVICE || d->kind == MH_DISC_KIND_V2_IMAGE) {
        if (d->v2_raw_lba512 && d->total_sectors) {
            char path[MH_PATH_MAX] = "";
            if (d->kind == MH_DISC_KIND_V2_IMAGE) {
                mh_str_copy(path, sizeof path, d->source_path);
            } else {
                char bsd[MH_DISK_BSD_MAX];
                snprintf(bsd, sizeof bsd, "disk%u", d->v2_drive);
                if (!mh_disk_device_path(bsd, true, path, sizeof path)) path[0] = 0;
            }
            if (path[0] == 0) {
                if (!silent) MHMessage(errTitle, @"The raw source of this disc is not available.", MHIconError);
                return;
            }
            status = mh_psserial_extract_raw(&res, path, d->v2_raw_lba512, d->total_sectors);
        } else {
            fail = @"The raw record has no valid location to read from.";
        }
    } else {
        if (!silent) MHMessage(errTitle, @"This disc source does not allow serial recovery.", MHIconError);
        return;
    }
    if (fail) {
        if (!silent) MHMessage(errTitle, fail, MHIconError);
        return;
    }
    if (status == MH_PSS_STRONG && res.serial[0]) {
        mh_str_copy(d->serial, sizeof d->serial, res.serial);
        d->ps = res;
        d->flags |= MH_DF_SERIAL;
        if (d->cheats) mh_cheat_profile_prepare_hardware(d->cheats, d->serial);
        _app.project->dirty = 1;
        [self refreshProject];
        if (silent) {
            [self setStatus:@"Internal serial verified and recovered. Apply the changes to the card."];
        } else {
            MHMessage(@"Serial Recovered", [NSString stringWithFormat:@"Internal serial recovered: %@. The raw game data was preserved. Apply the changes to write the identity to the card.", MHStr(d->serial)], MHIconInfo);
        }
        return;
    }
    if (silent) return;
    MHMessage(@"No Proven Serial Found", [@"No proven internal serial was found. Nothing was changed.\r\n\r\n" stringByAppendingString:MHStr(res.message)], MHIconError);
}

/* orig: recode_disc + manager_recode_dialog */
- (void)recodeDisc {
    MhDisc *d0 = _app.currentDisc;
    if (!d0 || d0->kind != 1 || (d0->verify_issues & 0xc0) == 0 || _app.busy) return;
    int rcase = _app.curCase, rdisc = _app.curDisc;
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.title = @"Create EDC/ECC Copy";
    sp.message = @"Creates a new CUE/BIN copy with recalculated EDC/ECC. The game content stays the same.";
    sp.allowedFileTypes = @[ @"cue" ];
    [sp beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK || !sp.URL.path) return;
        /* re-resolve: the project may have changed while the sheet was open */
        MhCase *rc = [self->_app caseAt:rcase];
        MhDisc *d = (rc && rdisc >= 0 && rdisc < rc->disc_count) ? rc->discs[rdisc] : NULL;
        if (!d || d->kind != 1 || self->_app.busy) return;
        NSString *dst = sp.URL.path;
        __block MhMsg err = {{0}};
        __block BOOL ok = NO;
        [self startJob:^(MhProgress *prog) {
            ok = mh_manager_recode_copy(d, dst.fileSystemRepresentation, prog, err.s, sizeof err.s);
        } done:^{
            BOOL cancelled = self->_app.cancelRequested;
            [self setBusy:NO];
            if (ok) {
                MHMessage(@"Copy Created", [NSString stringWithFormat:@"Copy created: %@\r\nAdd this CUE in place of the original disc to test it. EDC/ECC were recalculated; the game content is preserved, but corrupted data is not recovered.", dst], MHIconInfo);
            } else if (!cancelled) {
                MHMessage(@"Couldn't Create Copy", MHStr(err.s), MHIconError);
            }
        }];
    }];
}

/* orig: make_custom_art.isra.0 (the file picker and the project update; the art is the core's) */
- (void)makeCustomArtForCase:(BOOL)isCase {
    MhCase *c0 = _app.currentCase;
    MhDisc *d0 = _app.currentDisc;
    if (!c0 || (!isCase && !d0) || _app.busy) return;
    int cslot = c0->slot, ddisc = _app.curDisc;
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.title = isCase ? @"Choose Case Art" : @"Choose Disc Art";
    op.message = @"Choose an image (PNG, JPEG, BMP, GIF, TIFF) or a .art file.";
    op.prompt = @"Choose";
    op.allowedFileTypes = @[ @"png", @"jpg", @"jpeg", @"bmp", @"gif", @"tif", @"tiff", @"art" ];
    op.canChooseDirectories = NO;
    [op beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        MhCase *c = [self->_app caseAt:cslot];
        MhDisc *d = (c && ddisc >= 0 && ddisc < c->disc_count) ? c->discs[ddisc] : NULL;
        if (!c || (!isCase && !d) || self->_app.busy) return;
        char dir[MH_PATH_MAX], name[128], dst[MH_PATH_MAX], err[MH_ERR_MSG_MAX] = "";
        mh_path_join(self->_app.dataDir.fileSystemRepresentation, "custom/art", dir, sizeof dir);
        mh_ensure_dir(dir);
        const char *serial = d ? (d->serial[0] ? d->serial : d->name) : "";
        mh_art_custom_name(isCase, serial, c->slot, name, sizeof name);
        mh_path_join(dir, name, dst, sizeof dst);
        if (!mh_art_import(self->_app.art, isCase, op.URL.fileSystemRepresentation, dst, err, sizeof err)) {
            MHMessage(@"Couldn't Replace Art", MHStr(err), MHIconError);
            return;
        }
        if (isCase) mh_str_copy(c->art_path, sizeof c->art_path, dst);
        else mh_str_copy(d->cd_art_path, sizeof d->cd_art_path, dst);
        self->_app.project->dirty = 1;
        [self refreshProject];
    }];
}

#pragma mark - validation (menu bar, toolbar, context menus)

- (BOOL)validateAction:(SEL)a item:(id)item {
    BOOL idle = !_app.busy && !_app.loading;
    MhCase *c = _app.currentCase;
    MhProject *p = _app.project;
    if (a == @selector(addGame:)) return idle;
    if (a == @selector(addDisc:)) return idle && c && c->disc_count < 5 && p->disc_count < 0x100;
    if (a == @selector(imageOpen:) || a == @selector(imageSaveAs:) || a == @selector(imageNew:) || a == @selector(cmdDefaults:)) return idle;
    if (a == @selector(imageApply:)) return idle && p->image_loaded != 0;
    if (a == @selector(refreshSDAction:)) return idle;
    if (a == @selector(cmdWrite:)) {
        if ([item isKindOfClass:NSMenuItem.class]) ((NSMenuItem *)item).title = _app.busy ? @"Cancel Operation" : [self writeTitleIdle];
        return _btnWrite.isEnabled;
    }
    if (a == @selector(caseArt:)) return idle && c != NULL;
    if (a == @selector(discArt:) || a == @selector(cmdCheatsAction:) || a == @selector(verifyDiscAction:) || a == @selector(detailsAction:))
        return idle && _app.currentDisc != NULL;
    if (a == @selector(recoverSerialAction:)) return idle && _app.currentDisc && _app.currentDisc->serial[0] == 0;
    if (a == @selector(recodeAction:)) {
        MhDisc *d = _app.currentDisc;
        return idle && d && d->kind == 1 && (d->verify_issues & 0xc0) != 0;
    }
    if (a == @selector(moveItemUp:)) return [self canMoveKind:[self activeKind] delta:-1];
    if (a == @selector(moveItemDown:)) return [self canMoveKind:[self activeKind] delta:1];
    if (a == @selector(removeItem:) || a == @selector(renameItem:))
        return idle && ([self activeKind] == 1 ? c != NULL : _app.currentDisc != NULL);
    if (a == @selector(toggleVerify:)) {
        if ([item isKindOfClass:NSMenuItem.class])
            ((NSMenuItem *)item).state = _content.chkVerify.state == NSControlStateValueOn ? NSControlStateValueOn : NSControlStateValueOff;
        return !_app.busy;
    }
    if (a == @selector(toggleSidebarAction:)) {
        if ([item isKindOfClass:NSMenuItem.class]) ((NSMenuItem *)item).title = _sideItem.isCollapsed ? @"Show Sidebar" : @"Hide Sidebar";
        return YES;
    }
    if (a == @selector(toggleInspectorAction:)) {
        if ([item isKindOfClass:NSMenuItem.class]) ((NSMenuItem *)item).title = _inspItem.isCollapsed ? @"Show Inspector" : @"Hide Inspector";
        return YES;
    }
    return YES;
}

- (BOOL)validateMenuItem:(NSMenuItem *)item { return [self validateAction:item.action item:item]; }
- (BOOL)validateToolbarItem:(NSToolbarItem *)item { return [self validateAction:item.action item:item]; }

#pragma mark - startup (orig: app_main tail)

- (void)start {
    if (!_app.dbOK)
        MHMessage(@"Built-in Database Problem", @"The built-in identification, trackset and SBI database failed validation. Reinstall the official app.", MHIconError);
    if (!_app.cheatOK)
        MHMessage(@"Cheats Database Missing", @"The audited cheats database could not be loaded. Keep database/cheats next to the app. Covers and cheats are the only external data.", MHIconError);
    [self refreshProject];
    [self startDeviceWatch];
    [self refreshSD]; /* orig: WM_CREATE ends with refresh_sd */
}

#pragma mark - test hooks

- (NSString *)snapshotReport {
    NSMutableString *s = [NSMutableString string];
    [s appendFormat:@"cases=%u discs=%u dirty=%u curCase=%d curDisc=%d\n", _app.project->case_count,
                    _app.project->disc_count, _app.project->dirty, _app.curCase, _app.curDisc];
    [s appendFormat:@"  write button: \"%@\" enabled=%d  status: %@  key=%d\n", _btnWrite.title, _btnWrite.isEnabled, _bar.status.stringValue, _window.isKeyWindow];
    for (MHLibRow *r in _caseRows) [s appendFormat:@"  case slot=%ld %@\n", (long)r.param, [r.cells componentsJoinedByString:@" | "]];
    for (MHLibRow *r in _discRows) [s appendFormat:@"  disc idx=%ld %@\n", (long)r.param, [r.cells componentsJoinedByString:@" | "]];
    return s;
}

/* Walks the accessibility tree: one line per element with role, label, help and value. */
static void ax_line(id el, id queryTarget, int depth, NSMutableString *out) {
    NSString *role = [el respondsToSelector:@selector(accessibilityRole)] ? [el accessibilityRole] : @"?";
    id t = queryTarget ?: el;
    NSString *label = [t respondsToSelector:@selector(accessibilityLabel)] ? [t accessibilityLabel] : nil;
    NSString *help = [t respondsToSelector:@selector(accessibilityHelp)] ? [t accessibilityHelp] : nil;
    id value = [t respondsToSelector:@selector(accessibilityValue)] ? [t accessibilityValue] : nil;
    NSString *ident = [t respondsToSelector:@selector(accessibilityIdentifier)] ? [t accessibilityIdentifier] : nil;
    [out appendFormat:@"%*s%@", depth * 2, "", role ?: @"(none)"];
    if (label.length) [out appendFormat:@"  label=\"%@\"", label];
    if (help.length) [out appendFormat:@"  help=\"%@\"", help];
    if (value && [value isKindOfClass:NSString.class] && [value length]) [out appendFormat:@"  value=\"%@\"", value];
    if (ident.length) [out appendFormat:@"  id=%@", ident];
    [out appendString:@"\n"];
}

/* Walks the accessibility tree: one line per element with role, label, help and value. Controls are
 * asked through their view (cells are only proxies); table rows are listed through their row views. */
static void ax_walk(id el, int depth, NSMutableString *out, NSMutableSet *seen) {
    if (!el || depth > 18) return;
    NSValue *key = [NSValue valueWithPointer:(__bridge const void *)el];
    if ([seen containsObject:key]) return;
    [seen addObject:key];
    id target = [el isKindOfClass:NSCell.class] ? [(NSCell *)el controlView] : el;
    ax_line(el, target, depth, out);
    if ([target isKindOfClass:NSTableView.class]) {
        NSTableView *tv = target;
        for (NSInteger r = 0; r < tv.numberOfRows; r++) {
            NSTableRowView *rv = [tv rowViewAtRow:r makeIfNecessary:YES];
            if (rv) ax_line(rv, rv, depth + 1, out);
        }
        return;
    }
    if ([el respondsToSelector:@selector(accessibilityChildren)]) {
        for (id k in [el accessibilityChildren]) ax_walk(k, depth + 1, out, seen);
    }
}

/* Commands: report, select-case N, select-disc N, drag-case <rowFrom> <rowTo> <after>,
 * drag-disc <discRow> <targetKind c|d> <targetRow> <after>, remove-disc, remove-case, shot <file>,
 * name-case TEXT, name-disc TEXT, busy 0|1, appearance light|dark|system, toggle-inspector,
 * toggle-sidebar, ax-dump, demo-sheet <kind>. Output goes to stdout. */
- (void)testCommand:(NSString *)cmd {
    NSArray *a = [cmd componentsSeparatedByString:@" "];
    NSString *op = a[0];
#define ARG(n) (a.count > (n) ? a[n] : @"")
    NSString *rest = a.count > 1 ? [[a subarrayWithRange:NSMakeRange(1, a.count - 1)] componentsJoinedByString:@" "] : @"";
    if ([op isEqualToString:@"report"]) {
        printf("%s", self.snapshotReport.UTF8String);
    } else if ([op isEqualToString:@"select-case"]) {
        _app.curCase = [ARG(1) intValue];
        _app.curDisc = -1;
        [self refreshCases];
        [self refreshFields];
    } else if ([op isEqualToString:@"select-disc"]) {
        _app.curDisc = [ARG(1) intValue];
        [self refreshDiscs];
        [self refreshFields];
    } else if ([op isEqualToString:@"drag-case"]) { /* from row, to row, after */
        [self dragProgrammaticKind:1 fromRow:[ARG(1) integerValue] toCases:YES row:[ARG(2) integerValue] after:[ARG(3) intValue]];
    } else if ([op isEqualToString:@"drag-disc"]) { /* from disc row, "d"/"c", row, after */
        [self dragProgrammaticKind:2 fromRow:[ARG(1) integerValue] toCases:[ARG(2) isEqualToString:@"c"] row:[ARG(3) integerValue] after:[ARG(4) intValue]];
    } else if ([op isEqualToString:@"remove-disc"]) {
        [self cmdRemoveDisc];
    } else if ([op isEqualToString:@"remove-case"]) {
        [self removeCase];
    } else if ([op isEqualToString:@"name-case"]) {
        _fCase.stringValue = rest;
        _fCaseInsp.stringValue = rest;
        [self applyCaseName:rest];
    } else if ([op isEqualToString:@"name-disc"]) {
        _fDisc.stringValue = rest;
        [self applyDiscName:rest];
    } else if ([op isEqualToString:@"busy"]) {
        [self setBusy:[ARG(1) boolValue]];
    } else if ([op isEqualToString:@"shot"]) {
        [self writeShot:ARG(1)];
    } else if ([op isEqualToString:@"add-folder"]) {
        [self importPaths:@[ rest ]];
    } else if ([op isEqualToString:@"save-image"]) {
        [self writeImageTo:rest];
    } else if ([op isEqualToString:@"open-image"]) {
        [self loadImageAtPath:rest];
    } else if ([op isEqualToString:@"new-project"]) {
        mh_project_clear(_app.project);
        [self useDefaultMenu];
        _app.curCase = _app.curDisc = -1;
        [self refreshProject];
    } else if ([op isEqualToString:@"refresh-sd"]) {
        [self refreshSD];
    } else if ([op isEqualToString:@"write-device"]) {
        /* Only a disk that mh_disk_enumerate offered (eligible) can be picked, and never the
         * system disks of the dev machine. */
        NSString *bsd = ARG(1);
        int idx = -1;
        for (int i = 0; i < _diskCount; i++) if (!strcmp(_disks[i].bsd_name, bsd.UTF8String ?: "")) idx = i;
        BOOL realOK = getenv("MH_TEST_ALLOW_REAL_DEVICE") && !strcmp(getenv("MH_TEST_ALLOW_REAL_DEVICE"), "1");
        if (idx < 0 || [@[ @"disk0", @"disk3", @"disk6" ] containsObject:bsd] || (!_disks[idx].is_image && !realOK)) {
            printf("write-device refused: %s not an eligible target (only disk images are allowed in tests)\n", bsd.UTF8String);
        } else {
            [_popSD selectItemAtIndex:idx];
            [self doWrite];
        }
    } else if ([op isEqualToString:@"sd-list"]) {
        for (int i = 0; i < _diskCount; i++) printf("sd %s has_v2=%d state=%d size=%llu\n", _disks[i].bsd_name, _disks[i].has_v2, _disks[i].v2_state, _disks[i].size);
    } else if ([op isEqualToString:@"select-sd"]) {
        for (int i = 0; i < _diskCount; i++) if (!strcmp(_disks[i].bsd_name, [ARG(1) UTF8String] ?: "")) [_popSD selectItemAtIndex:i];
    } else if ([op isEqualToString:@"load-sd"]) {
        [self loadSelectedV2:NO];
    } else if ([op isEqualToString:@"context"]) {
        printf("context menu for disc %d\n", _app.curDisc);
    } else if ([op isEqualToString:@"progress"]) {
        [self queueProgress:[ARG(1) longLongValue] total:100 text:"Progress test"];
    } else if ([op isEqualToString:@"appearance"]) {
        NSString *m = a.count > 1 ? ARG(1) : @"system";
        NSApp.appearance = [m isEqualToString:@"dark"] ? [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]
                         : [m isEqualToString:@"light"] ? [NSAppearance appearanceNamed:NSAppearanceNameAqua] : nil;
        printf("appearance %s\n", m.UTF8String);
    } else if ([op isEqualToString:@"toggle-inspector"]) {
        _inspItem.collapsed = !_inspItem.isCollapsed;
        printf("inspector %s\n", _inspItem.isCollapsed ? "hidden" : "shown");
    } else if ([op isEqualToString:@"toggle-sidebar"]) {
        _sideItem.collapsed = !_sideItem.isCollapsed;
        printf("sidebar %s\n", _sideItem.isCollapsed ? "hidden" : "shown");
    } else if ([op isEqualToString:@"ax-dump"]) {
        NSMutableString *out = [NSMutableString string];
        NSMutableSet *seen = [NSMutableSet set];
        ax_walk(_window, 0, out, seen);
        if (_window.toolbar) {
            [out appendString:@"-- toolbar items\n"];
            for (NSToolbarItem *it in _window.toolbar.visibleItems)
                [out appendFormat:@"  toolbar item id=%@ label=\"%@\" help=\"%@\" enabled=%d\n", it.itemIdentifier, it.label ?: @"", it.toolTip ?: @"", it.enabled];
        }
        printf("%s", out.UTF8String);
    } else if ([op isEqualToString:@"demo-sheet"]) {
        [self demoSheet:a.count > 1 ? ARG(1) : @""];
    } else if ([op isEqualToString:@"demo-sheet-close"]) {
        [self dismissSheet];
    } else if ([op isEqualToString:@"focus"]) { /* focus cases|discs|case-name|disc-name */
        NSString *w = a.count > 1 ? ARG(1) : @"";
        NSView *v = [w isEqualToString:@"cases"] ? _tCases : [w isEqualToString:@"discs"] ? _tDiscs
                  : [w isEqualToString:@"case-name"] ? _fCase : [w isEqualToString:@"disc-name"] ? _fDisc : _btnWrite;
        [_window makeFirstResponder:v];
    } else if ([op isEqualToString:@"drop-overlay"]) { /* drop-overlay on|off: the Finder drag highlight */
        BOOL on = a.count > 1 && [ARG(1) isEqualToString:@"on"];
        _content.overlay.detail = @"Folders become cases, .cue files become discs";
        [_content.overlay setActive:on];
    } else if ([op isEqualToString:@"resize"]) { /* resize W H (content size) */
        [_window setContentSize:NSMakeSize([ARG(1) doubleValue], [ARG(2) doubleValue])];
        printf("window content size %.0fx%.0f\n", _window.contentView.frame.size.width, _window.contentView.frame.size.height);
    } else if ([op isEqualToString:@"about"]) {
        [self showAbout];
    } else if ([op isEqualToString:@"verify-on-add"]) {
        BOOL on = a.count > 1 && [ARG(1) boolValue];
        _content.chkVerify.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        _app.verifyOnAdd = on;
    } else if ([op isEqualToString:@"move-up"]) {
        [self moveItemUp:nil];
    } else if ([op isEqualToString:@"move-down"]) {
        [self moveItemDown:nil];
    }
    fflush(stdout);
}

/* Screenshot helper: puts a sheet on screen without a real operation. */
- (void)demoSheet:(NSString *)kind {
    if ([kind isEqualToString:@"progress"]) {
        [self presentProgressSheet:@"Writing to disk8"];
        [_sheet updatePhase:MH_V2_PHASE_WRITING_DISCS step:3 steps:8 fraction:37 text:@"Alundra (USA)" cancelEnabled:YES cancelling:NO];
    } else if ([kind isEqualToString:@"progress-late"]) {
        [self presentProgressSheet:@"Writing to disk8"];
        [_sheet updatePhase:MH_V2_PHASE_UPDATING_TABLES step:0 steps:0 fraction:82 text:@"" cancelEnabled:NO cancelling:NO];
    } else if ([kind isEqualToString:@"success"]) {
        [self presentProgressSheet:@"Writing to disk8"];
        [_sheet finishSuccessTitle:@"Card written" summary:@"Operation completed directly on the card.\n\nCases: 8\nDiscs: 12\nRAW size used: 6.4 GB\n\nThe card is PSXRAW and has no files visible in Finder. Do not format the card." completion:nil];
    } else if ([kind isEqualToString:@"failure"]) {
        [self presentProgressSheet:@"Writing to disk8"];
        [_sheet finishFailureTitle:@"Write interrupted" text:@"Direct write to the card failed.\n\nKeep the app open. Reconnect the same target and apply again: Discs already finished will be reused." completion:nil];
    } else if ([kind isEqualToString:@"confirm"]) {
        MHWriteSummary *s = [[MHWriteSummary alloc] init];
        s.bsd = @"disk8";
        s.model = @"Generic STORAGE DEVICE";
        s.serial = @"";
        s.cardSize = 32ull * 1000 * 1000 * 1000;
        s.estimate = 6ull * 1000 * 1000 * 1000;
        s.cases = _app.project->case_count;
        s.discs = _app.project->disc_count;
        MHConfirmWrite(_window, s);
    } else if ([kind isEqualToString:@"menu"]) {
        uint8_t rec[MH_MENU_DEFAULTS_SIZE];
        mh_menu_defaults_factory(rec);
        MHMenuDefaultsDialog(_window, rec);
    } else if ([kind isEqualToString:@"alert"]) {
        MHConfirm(@"Remove this case?", @"\"Alundra (USA) (Rev 1)\" and its 1 disc will be removed from the library. The change is only written to the card when you apply changes.", @"Remove", YES);
    } else if ([kind isEqualToString:@"error"]) {
        MHMessage(@"Couldn't Read Disc", @"A CUE file could not be parsed.", MHIconError);
    } else if ([kind isEqualToString:@"details"]) {
        MHShowDetails(_window, @"Technical Details", @"DISC\r\n\r\nName: Alundra\r\nSerial: SLUS-00553\r\nTracks: 1\r\nPS1 sectors: 312345\r\nStatus: OK");
    }
}

/* Drives the same functions as the mouse: where the pointer would drop, then the move. */
- (void)dragProgrammaticKind:(int)kind fromRow:(NSInteger)from toCases:(BOOL)toCases row:(NSInteger)to after:(int)after {
    MHListTable *src = kind == 1 ? _tCases : _tDiscs;
    NSArray<MHLibRow *> *rows = [self rowsFor:src];
    if (from < 0 || from >= (NSInteger)rows.count || to < 0) return;
    MHLibRow *r = rows[from];
    int dragCase = kind == 1 ? (int)r.param : _app.curCase, dragDisc = kind == 1 ? -1 : (int)r.param;
    NSArray<MHLibRow *> *trows = toCases ? _caseRows : _discRows;
    NSInteger count = trows.count;
    int dropCase = -1, dropDisc = -1, dropRow = -1, dropAfter = 0, dropHi = -1;
    NSInteger ins = -1;
    if (kind == 1) {
        if (count > 0) {
            NSInteger row = MIN(to, count - 1);
            dropAfter = to >= count ? 1 : after;
            dropRow = (int)row;
            dropCase = (int)trows[row].param;
            ins = row + dropAfter;
        }
    } else if (!toCases) {
        if (count > 0) {
            NSInteger row = MIN(to, count - 1);
            dropAfter = to >= count ? 1 : after;
            dropRow = (int)row;
            dropCase = dragCase;
            dropDisc = (int)trows[row].param;
            ins = row + dropAfter;
        }
    } else if (to >= 0 && to < count) {
        int param = (int)trows[to].param;
        MhCase *c = [_app caseAt:param];
        if (c && (param == dragCase || c->disc_count < 5)) {
            dropRow = dropHi = (int)to;
            dropCase = param;
            dropDisc = c->disc_count;
        }
    }
    printf("drop target: case=%d disc=%d row=%d after=%d hi=%d\n", dropCase, dropDisc, dropRow, dropAfter, dropHi);
    if (kind == 1) {
        if (dropRow >= 0 && dragCase >= 0) [self moveCaseSlot:dragCase toInsertion:ins];
    } else if (dropCase >= 0 && dragCase >= 0 && dragDisc >= 0) {
        [self moveDisc:dragDisc ofCase:dragCase toCase:dropCase insertion:dropHi < 0 ? ins : -1];
    }
}

- (void)writeShot:(NSString *)path {
    NSView *v = _window.contentView;
    [v layoutSubtreeIfNeeded];
    NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
    [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}

@end
