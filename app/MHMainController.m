#import "MHMainController.h"
#import "MHApp.h"
#import "MHViews.h"
#import "MHDialogs.h"
#import "MHBridge.h"
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

/* Context menu codes of the disc list (orig: 600, 0x259..0x25e) and the Imagem menu (700..0x2bf). */
enum {
    CM_VERIFY = 600, CM_RENAME = 0x259, CM_CHEATS = 0x25a, CM_DETAILS = 0x25b, CM_REMOVE = 0x25c,
    CM_RECODE = 0x25d, CM_SERIAL = 0x25e,
    IM_OPEN = 700, IM_SAVE = 0x2bd, IM_APPLY = 0x2be, IM_NEW = 0x2bf
};

#pragma clang diagnostic ignored "-Wdeprecated-declarations" /* allowedFileTypes: simplest and still works */
#define MH_BLUE_FLAGS (MH_DF_FROM_V2 | MH_DF_KEEP_PACKAGE) /* orig: 0x48000 */

static NSFont *g_font, *g_bold;
typedef struct { char s[MH_ERR_MSG_MAX]; } MhMsg; /* blocks cannot capture arrays */

static NSString *status_text(unsigned s) { /* orig: the OK/OBS/CORR/ERRO ladder of refresh_discs */
    return s == 2 ? @"OBS" : s == 3 ? @"CORR" : s == 4 ? @"ERRO" : @"OK";
}

@interface MHMainController () <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate,
                                NSTextFieldDelegate, MHListOwner>
@end

@implementation MHMainController {
    MHApp *_app;
    NSWindow *_window;
    MHRootView *_root;
    NSButton *_btnAddGame, *_btnAddDisc, *_btnMenu, *_btnImage, *_chkVerify, *_btnAbout;
    NSTextField *_hint, *_lblCases, *_lblDiscs, *_lblDetails, *_lblSD, *_status;
    NSScrollView *_scCases, *_scDiscs, *_scDetails;
    MHListTable *_tCases, *_tDiscs;
    NSTextView *_details;
    MHPreviewView *_pvCase, *_pvDisc;
    NSButton *_btnCaseArt, *_btnDiscArt, *_btnCheats, *_btnRefresh, *_btnWrite;
    NSTextField *_fCase, *_fDisc;
    NSPopUpButton *_popSD;
    NSProgressIndicator *_progress;
    MHGhostPanel *_ghost;
    MHDropLine *_dropLine;

    NSMutableArray<MHRow *> *_caseRows, *_discRows;
    BOOL _suppressSel;   /* programmatic selection changes (orig: handled by value compare) */
    BOOL _suppressEdit;  /* orig: DAT_140e7b3a4 */

    MhDiskInfo _disks[MH_DISK_MAX];
    int _diskCount;
    NSUInteger _sdGen;

    MhProgress _prog;
    dispatch_queue_t _queue;
    NSLock *_progLock;
    uint64_t _pDone, _pTotal;
    NSString *_pText;
    BOOL _pPending;

    /* drag state (orig: DAT_140f08c50.. in main_proc) */
    int _dragKind;      /* 0 none, 1 case, 2 disc */
    int _dragCase;      /* 8c54 */
    int _dragDisc;      /* 8c58 */
    int _dropCase;      /* 8c5c */
    int _dropDisc;      /* 8c60 */
    int _dropRow;       /* 8c64 */
    int _dropHi;        /* 8c68 */
    int _dropAfter;     /* 8c6c */
    BOOL _dragCancelled;

    NSInteger _picked;  /* last menu choice */
    DASessionRef _da;
}

#pragma mark - setup

- (instancetype)init {
    if ((self = [super init])) {
        _app = MHApp.shared;
        _queue = dispatch_queue_create("mh.worker", DISPATCH_QUEUE_SERIAL);
        _progLock = [[NSLock alloc] init];
        mh_progress_init(&_prog, NULL, NULL);
        g_font = [NSFont systemFontOfSize:12];
        g_bold = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
        _caseRows = [NSMutableArray array];
        _discRows = [NSMutableArray array];
        _dragCase = _dragDisc = _dropCase = _dropDisc = _dropRow = _dropHi = -1;
        [self buildWindow];
    }
    return self;
}

- (NSWindow *)window { return _window; }

- (NSTextField *)labelWith:(NSString *)t font:(NSFont *)f {
    NSTextField *l = [NSTextField labelWithString:t];
    l.font = f;
    [_root addSubview:l];
    return l;
}

- (NSButton *)buttonWith:(NSString *)t tag:(NSInteger)tag action:(SEL)sel {
    NSButton *b = [NSButton buttonWithTitle:t target:self action:sel];
    b.font = g_font;
    b.tag = tag;
    b.bezelStyle = NSBezelStyleRounded;
    b.identifier = [NSString stringWithFormat:@"0x%lx", (long)tag];
    [b setAccessibilityLabel:t];
    [_root addSubview:b];
    return b;
}

- (MHListTable *)tableIn:(NSScrollView * __strong *)sc tag:(NSInteger)tag columns:(NSArray *)cols label:(NSString *)label {
    MHListTable *t = [[MHListTable alloc] initWithFrame:NSZeroRect];
    t.owner = self;
    t.dataSource = self;
    t.delegate = self;
    t.tag = tag;
    t.allowsMultipleSelection = NO;
    t.allowsEmptySelection = YES;
    t.allowsColumnReordering = NO;
    t.usesAlternatingRowBackgroundColors = NO;
    t.rowHeight = 21;
    t.focusRingType = NSFocusRingTypeNone;
    t.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
    t.gridStyleMask = NSTableViewGridNone;
    t.intercellSpacing = NSMakeSize(3, 0);
    int i = 0;
    for (NSArray *c in cols) { /* @[title, centered, width] */
        NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:[NSString stringWithFormat:@"%d", i++]];
        col.title = c[0];
        col.width = [c[2] doubleValue];
        col.minWidth = 20;
        col.resizingMask = NSTableColumnUserResizingMask;
        NSTextAlignment al = [c[1] boolValue] ? NSTextAlignmentCenter : NSTextAlignmentLeft;
        col.headerCell.alignment = al;
        col.headerCell.font = g_font;
        NSTextFieldCell *cell = [[NSTextFieldCell alloc] initTextCell:@""];
        cell.font = g_font;
        cell.alignment = al;
        cell.lineBreakMode = NSLineBreakByTruncatingTail;
        col.dataCell = cell;
        [t addTableColumn:col];
    }
    NSScrollView *s = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    s.documentView = t;
    s.hasVerticalScroller = YES;
    s.autohidesScrollers = YES;
    s.borderType = NSBezelBorder;
    [s setAccessibilityLabel:label];
    [t setAccessibilityLabel:label];
    [_root addSubview:s];
    *sc = s;
    return t;
}

- (NSTextField *)fieldWithTag:(NSInteger)tag label:(NSString *)label {
    NSTextField *f = [[NSTextField alloc] initWithFrame:NSZeroRect];
    f.font = g_font;
    f.bezeled = YES;
    f.bezelStyle = NSTextFieldSquareBezel;
    f.editable = YES;
    f.delegate = self;
    f.tag = tag;
    f.usesSingleLineMode = YES;
    f.cell.scrollable = YES;
    [f setAccessibilityLabel:label];
    [_root addSubview:f];
    return f;
}

/* orig: main_proc WM_CREATE. */
- (void)buildWindow {
    NSRect cr = NSMakeRect(0, 0, 1364, 801); /* CreateWindowEx 0x564 x 0x348 outer */
    _window = [[NSWindow alloc] initWithContentRect:cr
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = MH_TITLE;
    _window.releasedWhenClosed = NO;
    _window.delegate = self;
    _window.contentMinSize = NSMakeSize(1104, 661); /* orig: WM_GETMINMAXINFO 0x460 x 0x2bc outer */
    _root = [[MHRootView alloc] initWithFrame:cr];
    _window.contentView = _root;
    [_root registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];

    _btnAddGame = [self buttonWith:@"+ Adicionar Jogo" tag:ID_ADD_GAME action:@selector(addGame:)];
    _btnAddDisc = [self buttonWith:@"+ Adicionar Disco" tag:ID_ADD_DISC action:@selector(addDisc:)];
    _btnMenu = [self buttonWith:@"Menu" tag:ID_MENU action:@selector(cmdDefaults:)];
    _btnImage = [self buttonWith:@"Imagem" tag:ID_IMAGE action:@selector(cmdImageMenu:)];
    _chkVerify = [NSButton checkboxWithTitle:@"Verificar ao adicionar" target:self action:@selector(toggleVerify:)];
    _chkVerify.font = g_font;
    _chkVerify.state = NSControlStateValueOn;
    _chkVerify.tag = ID_VERIFY;
    [_chkVerify setAccessibilityLabel:@"Verificar ao adicionar"];
    [_root addSubview:_chkVerify];
    _hint = [self labelWith:@"Pastas: Jogos/Cases | CUEs: Discos | Azul: ja gravado" font:g_font];
    _btnAbout = [self buttonWith:@"Sobre" tag:ID_ABOUT action:@selector(about:)];
    _lblCases = [self labelWith:@"CASES" font:g_bold];
    _lblDiscs = [self labelWith:@"DISCOS DA CASE" font:g_bold];
    _lblDetails = [self labelWith:@"DETALHES" font:g_bold];

    _tCases = [self tableIn:&_scCases tag:ID_LIST_CASES
                    columns:@[ @[ @"#", @YES, @38 ], @[ @"Jogo / Case", @NO, @230 ], @[ @"Discos", @YES, @58 ], @[ @"Status", @YES, @54 ] ]
                      label:@"CASES"];
    _tDiscs = [self tableIn:&_scDiscs tag:ID_LIST_DISCS
                    columns:@[ @[ @"#", @YES, @36 ], @[ @"Disco", @NO, @150 ], @[ @"Serial", @YES, @112 ],
                               @[ @"Tracks", @YES, @58 ], @[ @"Status", @YES, @54 ] ]
                      label:@"DISCOS DA CASE"];

    _fCase = [self fieldWithTag:ID_EDIT_CASE label:@"Nome da Case"];
    _fDisc = [self fieldWithTag:ID_EDIT_DISC label:@"Nome do Disco"];
    _btnCaseArt = [self buttonWith:@"Trocar Case Art" tag:ID_CASE_ART action:@selector(caseArt:)];
    _btnDiscArt = [self buttonWith:@"Trocar Disc Art" tag:ID_DISC_ART action:@selector(discArt:)];
    _btnCheats = [self buttonWith:@"Cheats do Disco (0)" tag:ID_CHEATS action:@selector(cmdCheatsAction:)];

    _scDetails = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _details = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 300, 100)];
    _details.editable = NO;
    _details.selectable = YES;
    _details.font = g_font;
    _details.verticallyResizable = YES;
    _details.horizontallyResizable = NO;
    _details.autoresizingMask = NSViewWidthSizable;
    _details.textContainer.widthTracksTextView = YES;
    _details.textContainerInset = NSMakeSize(2, 2);
    _scDetails.documentView = _details;
    _scDetails.hasVerticalScroller = YES;
    _scDetails.autohidesScrollers = NO;
    _scDetails.borderType = NSBezelBorder;
    [_scDetails setAccessibilityLabel:@"DETALHES"];
    [_details setAccessibilityLabel:@"DETALHES"];
    [_root addSubview:_scDetails];

    _pvCase = [[MHPreviewView alloc] initWithFrame:NSZeroRect];
    _pvCase.caption = @"CASE";
    [_pvCase setAccessibilityLabel:@"Previa da Case"];
    [_root addSubview:_pvCase];
    _pvDisc = [[MHPreviewView alloc] initWithFrame:NSZeroRect];
    _pvDisc.caption = @"DISCO";
    [_pvDisc setAccessibilityLabel:@"Previa do Disco"];
    [_root addSubview:_pvDisc];

    _lblSD = [self labelWith:@"Cartao SD:" font:g_font];
    _popSD = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _popSD.font = g_font;
    _popSD.target = self;
    _popSD.action = @selector(sdChanged:);
    _popSD.tag = ID_COMBO_SD;
    [_popSD setAccessibilityLabel:@"Cartao SD"];
    [_root addSubview:_popSD];
    _btnRefresh = [self buttonWith:@"Atualizar" tag:ID_REFRESH action:@selector(refreshSDAction:)];
    _btnWrite = [self buttonWith:@"CRIAR CARTAO V2" tag:ID_WRITE action:@selector(cmdWrite:)];
    _btnWrite.bezelColor = NSColor.controlAccentColor;
    _btnWrite.attributedTitle = [[NSAttributedString alloc]
        initWithString:@"CRIAR CARTAO V2"
            attributes:@{NSFontAttributeName : g_font, NSForegroundColorAttributeName : NSColor.whiteColor}];
    _progress = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _progress.style = NSProgressIndicatorStyleBar;
    _progress.indeterminate = NO;
    _progress.minValue = 0;
    _progress.maxValue = 100;
    _progress.doubleValue = 0;
    [_progress setAccessibilityLabel:@"Progresso"];
    [_root addSubview:_progress];
    _status = [self labelWith:@"Pronto." font:g_font];
    _status.lineBreakMode = NSLineBreakByWordWrapping;
    _status.maximumNumberOfLines = 2;
    _status.cell.wraps = YES;
    [_status setAccessibilityLabel:@"Estado"];

    _ghost = [[MHGhostPanel alloc] init];
    _dropLine = [[MHDropLine alloc] initWithFrame:NSMakeRect(0, 0, 10, 3)];
    _dropLine.hidden = YES;
    [_root addSubview:_dropLine];

    [self layoutAll];
}

- (void)showWindow {
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    /* orig: ShowWindow(SW_MAXIMIZE). Zoomed unless disabled for tests. */
    if (!getenv("MH_NO_ZOOM")) [_window zoom:nil];
    [self layoutAll];
}

#pragma mark - layout (orig: WM_SIZE)

- (void)windowDidResize:(NSNotification *)n { [self layoutAll]; }

static void put(NSView *v, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
    v.frame = NSMakeRect(x, y, MAX(w, 0), MAX(h, 0));
}

- (void)layoutAll {
    NSSize cs = _root.bounds.size;
    int w = (int)cs.width, h = (int)cs.height;
    int detW = 0x15e;
    if (w < 0x41a) detW = w / 3;
    int caseW = (w - 0x18a) * 0x2f;
    int discX, discW, detX, btnX;
    if (caseW < 30000) {
        detW = w - 0x284;
        discX = 0x142;
        btnX = 0x2ee;
        discW = 300;
        detX = 0x278;
        caseW = 300;
        if (detW < 300) detW = 300;
    } else {
        detX = w - 0x16a;
        caseW /= 100;
        discX = caseW + 0x16;
        discW = (w - 0x18a) - caseW;
        btnX = w - 0xf4;
    }
    put(_btnAddGame, 0xc, 0xc, 0x82, 0x20);
    put(_btnAddDisc, 0x96, 0xc, 0x82, 0x20);
    put(_btnMenu, 0x120, 0xc, 0x4c, 0x20);
    put(_btnImage, 0x174, 0xc, 0x54, 0x20);
    put(_chkVerify, 0x1d2, 0x10, 0xbe, 0x18);
    put(_btnAbout, w - 0x52, 0xc, 0x46, 0x1e);
    put(_hint, 0x296, 0x11, w - 0x2f2, 0x18);
    put(_lblCases, 0xc, 0x34, caseW, 0x14);
    put(_lblDiscs, discX, 0x34, discW, 0x14);
    put(_lblDetails, detX, 0x34, detW, 0x14);
    put(_scCases, 0xc, 0x4e, caseW, h - 0xc2);
    put(_scDiscs, discX, 0x4e, discW, h - 0xc2);
    put(_pvCase, detX, 0x4e, 0x6c, 0x6c);
    put(_btnCaseArt, btnX, 0x74, 0x84, 0x1e);
    put(_fCase, detX, 0xc4, detW, 0x1b);
    put(_pvDisc, detX, 0xec, 0x6c, 0x6c);
    put(_btnDiscArt, btnX, 0xfa, 0x84, 0x1e);
    put(_btnCheats, btnX, 0x124, 0xb8, 0x1e);
    put(_fDisc, detX, 0x162, detW, 0x1b);
    put(_scDetails, detX, 0x187, detW, h - 0x1fb);
    put(_lblSD, 0xc, h - 0x6c, 0x4c, 0x16);
    int combo = w - 0x19e;
    if (combo < 300) combo = 300;
    put(_popSD, 0x60, h - 0x6d, combo, 0x1c);
    put(_btnRefresh, combo + 0x68, h - 0x6d, 0x52, 0x1c);
    put(_btnWrite, w - 0xda, h - 0x6f, 0xce, 0x23);
    put(_progress, 0xc, h - 0x46, w - 0x18, 0x10);
    put(_status, 0xc, h - 0x2f, w - 0x18, 0x26);

    /* column widths (orig: LVM_SETCOLUMNWIDTH at the end of WM_SIZE) */
    CGFloat cw = _scCases.contentSize.width;
    CGFloat c1 = MAX(cw - 0x9e, 0x96);
    [_tCases tableColumnWithIdentifier:@"0"].width = 0x26;
    [_tCases tableColumnWithIdentifier:@"1"].width = c1;
    [_tCases tableColumnWithIdentifier:@"2"].width = 0x3a;
    [_tCases tableColumnWithIdentifier:@"3"].width = 0x36;
    cw = _scDiscs.contentSize.width;
    c1 = MAX(cw - 0x10c, 0x8c);
    [_tDiscs tableColumnWithIdentifier:@"0"].width = 0x24;
    [_tDiscs tableColumnWithIdentifier:@"1"].width = c1;
    [_tDiscs tableColumnWithIdentifier:@"2"].width = 0x70;
    [_tDiscs tableColumnWithIdentifier:@"3"].width = 0x3a;
    [_tDiscs tableColumnWithIdentifier:@"4"].width = 0x36;
}

#pragma mark - table data

- (NSMutableArray<MHRow *> *)rowsFor:(NSTableView *)t { return t == _tCases ? _caseRows : _discRows; }

- (NSInteger)numberOfRowsInTableView:(NSTableView *)t { return [self rowsFor:t].count; }

- (id)tableView:(NSTableView *)t objectValueForTableColumn:(NSTableColumn *)c row:(NSInteger)r {
    NSArray *rows = [self rowsFor:t];
    if (r < 0 || r >= (NSInteger)rows.count) return @"";
    NSArray *cells = ((MHRow *)rows[r]).cells;
    NSUInteger i = (NSUInteger)c.identifier.integerValue;
    return i < cells.count ? cells[i] : @"";
}

/* orig: list_custom_draw. Blue text for written rows, red for "OBS" in the Status column. */
- (void)tableView:(NSTableView *)t willDisplayCell:(id)cell forTableColumn:(NSTableColumn *)c row:(NSInteger)r {
    NSArray *rows = [self rowsFor:t];
    if (r < 0 || r >= (NSInteger)rows.count || ![cell isKindOfClass:NSTextFieldCell.class]) return;
    MHRow *row = rows[r];
    NSTextFieldCell *tc = cell;
    NSInteger last = (NSInteger)t.tableColumns.count - 1;
    NSColor *col = NSColor.controlTextColor;
    if (![t isRowSelected:r]) {
        if (row.blue) col = [NSColor colorWithSRGBRed:0 green:0x21 / 255. blue:0xb4 / 255. alpha:1];
        else if (row.obs && c.identifier.integerValue == last) col = [NSColor colorWithSRGBRed:0xc4 / 255. green:0 blue:0 alpha:1];
    }
    tc.textColor = col;
}

- (void)tableViewSelectionDidChange:(NSNotification *)n {
    if (_suppressSel || _dragKind) return;
    NSTableView *t = n.object;
    NSInteger row = t.selectedRow;
    int param = -1;
    NSArray *rows = [self rowsFor:t];
    if (row >= 0 && row < (NSInteger)rows.count) param = (int)((MHRow *)rows[row]).param;
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
        if (((MHRow *)rows[i]).param == param) {
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

/* Worst status of the case's discs as a number (orig: the nested ladder of refresh_cases). */
static NSString *case_status(MhCase *c) {
    if (!c->disc_count) return @"OK";
    unsigned worst = 1;
    for (unsigned i = 0; i < c->disc_count; i++)
        if (c->discs[i] && c->discs[i]->status > worst) worst = c->discs[i]->status;
    return worst == 1 ? @"OK" : worst == 2 ? @"OBS" : worst == 3 ? @"CORR" : @"ERRO";
}

static BOOL case_blue(MhCase *c) { /* orig: list_custom_draw for list 0xc8 */
    if (!c->disc_count) return NO;
    for (unsigned i = 0; i < c->disc_count; i++)
        if (!c->discs[i] || !(c->discs[i]->flags & MH_BLUE_FLAGS)) return NO;
    return YES;
}

- (void)refreshCases {
    [_caseRows removeAllObjects];
    MhProject *p = _app.project;
    for (int i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        MhCase *c = &p->cases[i];
        if (!c->in_use) continue;
        MHRow *r = [[MHRow alloc] init];
        NSString *st = case_status(c);
        r.cells = @[ [NSString stringWithFormat:@"%d", i + 1], MHStr(c->title),
                     [NSString stringWithFormat:@"%u", c->disc_count], st ];
        r.param = i;
        r.blue = case_blue(c);
        r.obs = [st caseInsensitiveCompare:@"OBS"] == NSOrderedSame;
        [_caseRows addObject:r];
    }
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
    [self refreshDiscs];
}

- (void)refreshDiscs {
    [_discRows removeAllObjects];
    MhCase *c = _app.currentCase;
    if (c) {
        for (unsigned i = 0; i < c->disc_count; i++) {
            MhDisc *d = c->discs[i];
            MHRow *r = [[MHRow alloc] init];
            NSString *st = status_text(d ? d->status : 0);
            r.cells = @[ [NSString stringWithFormat:@"%u", i + 1], MHStr(d ? d->name : ""), MHStr(d ? d->serial : ""),
                         [NSString stringWithFormat:@"%u", d ? d->ntracks : 0], st ];
            r.param = i;
            r.blue = d && (d->flags & MH_BLUE_FLAGS);
            r.obs = [st isEqualToString:@"OBS"];
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

static void set_preview(MHPreviewView *v, const char *path) {
    uint8_t *bgra = NULL;
    uint32_t w = 0, h = 0;
    if (path && path[0] && mh_art_decode(path, &bgra, &w, &h)) [v setBitmap:bgra width:w height:h];
    else [v setBitmap:NULL width:0 height:0];
}

- (void)refreshFields {
    MhCase *c = _app.currentCase;
    MhDisc *d = _app.currentDisc;
    BOOL enAddDisc = NO, enCase = NO, enDisc = NO;
    _suppressEdit = YES;
    if (c) {
        _fCase.stringValue = MHStr(c->title);
        if (d) {
            _fDisc.stringValue = MHStr(d->name);
            _btnCheats.title = [NSString stringWithFormat:@"Cheats do Disco (%u)", d->cheats ? d->cheats->count : 0];
        } else {
            _fDisc.stringValue = @"";
            _btnCheats.title = @"Cheats do Disco (0)";
        }
        enDisc = d != NULL;
        enCase = YES;
        enAddDisc = c->disc_count < 5 ? _app.project->disc_count < 0x100 : NO;
    } else {
        _fCase.stringValue = @"";
        _fDisc.stringValue = @"";
        _btnCheats.title = @"Cheats do Disco (0)";
    }
    _suppressEdit = NO;
    [self enableEditControlsAddDisc:enAddDisc caseCtl:enCase discCtl:enDisc];

    NSMutableString *t = [NSMutableString string];
    if (d) {
        [t appendString:MHStr(d->notes)];
        if (d->cheat_note[0]) [t appendFormat:@"\r\nCheats: %@", MHStr(d->cheat_note)];
        if (d->verify_text[0]) [t appendFormat:@"\r\n\r\nVerificacao: %@", MHStr(d->verify_text)];
    }
    _details.string = MHDisplay(t);

    set_preview(_pvCase, c ? c->art_path : NULL);
    set_preview(_pvDisc, d ? d->cd_art_path : NULL);
    [self refreshFooter];
}

/* Busy state wins over the per-selection state, like the sequence of EnableWindow calls. */
- (void)enableEditControlsAddDisc:(BOOL)add caseCtl:(BOOL)cs discCtl:(BOOL)ds {
    BOOL idle = !_app.busy;
    _btnAddDisc.enabled = add && idle;
    _btnCaseArt.enabled = cs && idle;
    _btnDiscArt.enabled = ds && idle;
    _btnCheats.enabled = ds && idle;
    _fCase.enabled = cs && idle;
    _fDisc.enabled = ds && idle;
}

- (void)refreshAllEnable {
    MhCase *c = _app.currentCase;
    [self enableEditControlsAddDisc:c && c->disc_count < 5 && _app.project->disc_count < 0x100
                            caseCtl:c != NULL
                            discCtl:_app.currentDisc != NULL];
}

- (void)setStatus:(NSString *)s { _status.stringValue = s ?: @""; }

- (void)refreshFooter {
    MhProject *p = _app.project;
    BOOL en = NO;
    if (p->disc_count) { en = YES; if (p->card_loaded) en = p->dirty != 0; }
    NSMutableString *s = [NSMutableString stringWithFormat:@"%u Cases  |  %u Discos", p->case_count, p->disc_count];
    if (!p->card_loaded) {
        if (!p->image_loaded) {
            if (p->dirty) [s appendString:@"  |  Projeto novo"];
        } else {
            [s appendString:@"  |  Imagem V2 carregada"];
            if (p->dirty) [s appendString:@"  |  Alteracoes pendentes"];
        }
    } else {
        [s appendString:@"  |  Cartao V2 carregado"];
        if (p->dirty) [s appendString:@"  |  Alteracoes pendentes"];
    }
    if (!_app.busy) {
        [self setStatus:s];
        NSString *title = p->card_loaded ? @"APLICAR ALTERACOES" : @"CRIAR CARTAO V2";
        [self setWriteTitle:title];
        _btnWrite.enabled = en;
    }
}

- (void)setWriteTitle:(NSString *)t {
    _btnWrite.attributedTitle = [[NSAttributedString alloc]
        initWithString:t attributes:@{NSFontAttributeName : g_font, NSForegroundColorAttributeName : NSColor.whiteColor}];
    [_btnWrite setAccessibilityLabel:t];
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
    if (b) { _app.cancelRequested = NO; _app.cannotCancel = NO; }
    MhProject *p = _app.project;
    [self setWriteTitle:b ? @"CANCELAR" : (p->card_loaded ? @"APLICAR ALTERACOES" : @"CRIAR CARTAO V2")];
    BOOL en = !b;
    for (NSControl *c in @[ _btnAddGame, _btnAddDisc, _btnMenu, _btnImage, _chkVerify, _btnAbout, _btnCaseArt,
                            _btnDiscArt, _btnCheats, _btnRefresh, _popSD, _tCases, _tDiscs, _fCase, _fDisc ])
        c.enabled = en;
    _btnWrite.enabled = YES;
    if (!b) [self refreshAllEnable];
    if (!b) [self refreshFooter];
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
    BOOL post = !_pPending;
    _pPending = YES;
    [_progLock unlock];
    if (post) dispatch_async(dispatch_get_main_queue(), ^{ [self applyProgress]; });
}

/* orig: progress_cb. */
- (void)applyProgress {
    [_progLock lock];
    uint64_t done = _pDone, total = _pTotal;
    NSString *text = _pText;
    _pPending = NO;
    [_progLock unlock];
    double pct = 0;
    if (total) pct = done < total ? (double)(done * 100 / total) : 100;
    _progress.doubleValue = pct;
    [self setStatus:_app.cancelRequested ? @"Cancelando no limite seguro da operacao..." : (text ?: @"")];
    [_tCases reloadData];
    [_tDiscs reloadData]; /* orig: kind 8 invalidated both lists; cheap with cached rows */
}

/* The core cannot be cancelled once the card is being committed (orig: progress kind 7). */
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
    [self setWriteTitle:@"CANCELANDO..."];
}

- (BOOL)windowShouldClose:(NSWindow *)w {
    if (_app.busy) { [self requestCancel]; return NO; }
    [NSApp terminate:nil]; /* applicationShouldTerminate asks shouldQuit once */
    return NO;
}

- (BOOL)shouldQuit {
    if (_app.busy) { [self requestCancel]; return NO; }
    if (_app.project->dirty)
        return MHAsk(@"Existem alteracoes ainda nao aplicadas ao SD ou imagem. Deseja sair mesmo assim?");
    return YES;
}

#pragma mark - text fields (orig: EN_KILLFOCUS on 0xca / 0xcb)

- (void)controlTextDidEndEditing:(NSNotification *)n {
    if (_suppressEdit || _app.busy || _app.loading) return;
    if (n.object == _fCase) [self applyCaseName];
    else if (n.object == _fDisc) [self applyDiscName];
}

- (void)applyCaseName {
    MhCase *c = _app.currentCase;
    if (!c) return;
    char norm[MH_CASE_TITLE_MAX];
    mh_normalize_title(_fCase.stringValue.UTF8String, norm, sizeof norm);
    if (norm[0] && mh_str_icmp(c->title, norm) != 0) {
        mh_str_copy(c->title, sizeof c->title, norm);
        _app.project->dirty = 1;
        [self refreshCases];
        [self refreshFields];
    }
}

- (void)applyDiscName {
    MhDisc *d = _app.currentDisc;
    if (!d) return;
    char norm[MH_DISC_NAME_MAX];
    mh_normalize_title(_fDisc.stringValue.UTF8String, norm, sizeof norm);
    if (norm[0] && mh_str_icmp(d->name, norm) != 0) {
        mh_str_copy(d->name, sizeof d->name, norm);
        d->flags |= MH_DF_USER_NAMED;
        _app.project->dirty = 1;
        [self refreshDiscs];
        [self refreshFields];
    }
}

#pragma mark - list owner (keys, double click, drag)

- (void)list:(MHListTable *)t keyDown:(unichar)key keyCode:(unsigned short)code {
    if (_app.busy) return;
    if (code == 120) { /* F2 */
        NSTextField *f = t == _tCases ? _fCase : _fDisc;
        if (!f.isEnabled) return;
        [_window makeFirstResponder:f];
        [f selectText:nil];
        return;
    }
    if (t == _tCases) [self removeCase]; /* VK_DELETE */
    else [self cmdRemoveDisc];
}

- (void)removeCase {
    MhCase *c = _app.currentCase;
    if (!c) return;
    NSString *q = [NSString stringWithFormat:@"Remover a Case selecionada e seus %u disco(s)?\r\n\r\nA alteracao so sera gravada ao clicar em APLICAR ALTERACOES.", c->disc_count];
    if (!MHAsk(q)) return;
    mh_project_remove_case(_app.project, c->slot);
    _app.curCase = -1;
    _app.curDisc = -1;
    [self refreshProject];
}

- (void)cmdRemoveDisc { /* orig: cmd_remove_disc */
    MhCase *c = _app.currentCase;
    if (!c || _app.curDisc < 0) return;
    if (!MHAsk(@"Remover o Disco selecionado?\r\n\r\nA alteracao so sera gravada ao clicar em APLICAR ALTERACOES.")) return;
    mh_case_remove_disc(_app.project, c, (uint8_t)_app.curDisc);
    _app.curDisc = -1;
    if (c->disc_count == 0) mh_project_remove_case(_app.project, c->slot);
    [self refreshProject];
}

/* Double click (and right click) on a disc: the context menu (orig: NM_DBLCLK on 0xc9). */
- (void)list:(MHListTable *)t doubleClickRow:(NSInteger)row {
    if (t != _tDiscs || _app.busy) return;
    MhDisc *d = _app.currentDisc;
    if (!d) return;
    NSMenu *m = [[NSMenu alloc] init];
    m.autoenablesItems = NO;
    void (^add)(NSString *, NSInteger) = ^(NSString *title, NSInteger tag) {
        NSMenuItem *it = [m addItemWithTitle:title action:@selector(menuPicked:) keyEquivalent:@""];
        it.target = self;
        it.tag = tag;
    };
    add(@"Verificar Disco", CM_VERIFY);
    if (d->serial[0] == 0) add(@"Recuperar Serial do disco...", CM_SERIAL);
    if (d->kind == 1 && (d->verify_issues & 0xc0) != 0 && (d->verify_issues & 8) == 0)
        add(@"Criar copia com EDC/ECC recalculados...", CM_RECODE);
    add(@"Renomear", CM_RENAME);
    add(@"Cheats do Disco", CM_CHEATS);
    add(@"Detalhes tecnicos", CM_DETAILS);
    [m addItem:NSMenuItem.separatorItem];
    add(@"Remover", CM_REMOVE);
    _picked = 0;
    NSRect rr = [t rectOfRow:row];
    [m popUpMenuPositioningItem:nil atLocation:NSMakePoint(NSMidX(rr), NSMaxY(rr)) inView:t];
    switch (_picked) {
        case CM_VERIFY: {
            MhDisc *vd = d;
            [self admitDiscUI:vd verify:YES completion:^(BOOL ok) {
                [self refreshProject];
                if (ok && vd->verified && vd->verify_issues == 0)
                    MHMessage(MH_TITLE, MHStr(vd->verify_text), MHIconInfo);
            }];
            break;
        }
        case CM_RENAME: [self list:t keyDown:0 keyCode:120]; break;
        case CM_CHEATS: [self cmdCheats]; break;
        case CM_DETAILS: [self showTechnicalDetails]; break;
        case CM_REMOVE: [self cmdRemoveDisc]; break;
        case CM_RECODE: [self recodeDisc]; break;
        case CM_SERIAL: [self recoverSerial:d silent:NO]; break;
        default: break;
    }
}

- (void)menuPicked:(NSMenuItem *)it { _picked = it.tag; }

#pragma mark - drag reorder (orig: drag_start, drag_list_row, drag_show_line, drag_cancel, WM_MOUSEMOVE/LBUTTONUP)

- (NSRect)rootRectForRow:(NSInteger)row inTable:(NSTableView *)t {
    return [_root convertRect:[t rectOfRow:row] fromView:t];
}

/* orig: lv_hit: row under a point of the main window, -1 outside the list or on no item. */
- (NSInteger)hitRowIn:(MHListTable *)t at:(NSPoint)p {
    NSScrollView *sc = t == _tCases ? _scCases : _scDiscs;
    if (!NSPointInRect(p, sc.frame)) return -1;
    return [t rowAtPoint:[t convertPoint:p fromView:_root]];
}

/* orig: drag_list_row. Row to drop on and whether the drop goes after it. */
- (NSInteger)dragRowIn:(MHListTable *)t at:(NSPoint)p after:(int *)after {
    NSScrollView *sc = t == _tCases ? _scCases : _scDiscs;
    NSInteger hit = [self hitRowIn:t at:p];
    NSInteger count = [self rowsFor:t].count;
    *after = 0;
    if (!NSPointInRect(p, sc.frame) || count <= 0) return -1;
    if (hit < 0) {
        NSRect r0 = [self rootRectForRow:0 inTable:t];
        if (p.y >= r0.origin.y) { *after = 1; return count - 1; }
        hit = 0;
    }
    NSRect r = [self rootRectForRow:hit inTable:t];
    *after = NSMidY(r) <= p.y; /* (top + bottom) / 2 <= y */
    return hit;
}

- (void)showLineIn:(MHListTable *)t row:(NSInteger)row after:(int)after {
    NSScrollView *sc = t == _tCases ? _scCases : _scDiscs;
    if (row < 0 || row >= (NSInteger)[self rowsFor:t].count) { _dropLine.hidden = YES; return; }
    NSRect r = [self rootRectForRow:row inTable:t];
    CGFloat y = after ? NSMaxY(r) : r.origin.y;
    _dropLine.frame = NSMakeRect(sc.frame.origin.x + 2, y - 1 - 1, sc.frame.size.width - 4, 3);
    _dropLine.hidden = NO;
    [_dropLine setNeedsDisplay:YES];
}

- (void)setDropHighlight:(int)row {
    _tCases.dropRow = row;
}

- (void)dragStartKind:(int)kind row:(NSInteger)row {
    _dropAfter = 0;
    _dropCase = _dropDisc = _dropRow = _dropHi = -1;
    _dragKind = kind;
    _dragCancelled = NO;
    MHListTable *t = kind == 1 ? _tCases : _tDiscs;
    NSString *name = ((MHRow *)[self rowsFor:t][row]).cells[1];
    [_ghost setText:[NSString stringWithFormat:@"%@%@", kind == 1 ? @"Case: " : @"Disco: ", name]];
    [_ghost moveNearCursor];
    [_ghost orderFront:nil];
    [self setStatus:kind == 1 ? @"Arraste a Case; a linha azul mostra a nova posicao."
                              : @"Arraste o Disco; a linha azul mostra a posicao ou uma Case fica destacada."];
}

- (void)list:(MHListTable *)t beginDragRow:(NSInteger)row event:(NSEvent *)e {
    if (_app.busy || row < 0) return;
    int kind = t == _tCases ? 1 : 2;
    MHRow *r = [self rowsFor:t][row];
    if (kind == 1) { _dragCase = (int)r.param; _dragDisc = -1; }
    else { _dragCase = _app.curCase; _dragDisc = (int)r.param; }
    [self dragStartKind:kind row:row];
    for (;;) {
        NSEvent *n = [_window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp | NSEventMaskKeyDown];
        if (n.type == NSEventTypeKeyDown) {
            if (n.keyCode == 53 && _dragKind) [self dragCancel]; /* VK_ESCAPE */
            continue;
        }
        NSPoint p = [_root convertPoint:n.locationInWindow fromView:nil];
        if (n.type == NSEventTypeLeftMouseDragged) {
            if (_dragKind) [self dragMoveTo:p];
        } else {
            if (_dragKind) [self dragDropAt:p];
            break;
        }
    }
}

/* orig: drag_cancel */
- (void)dragCancel {
    _dragKind = 0;
    [self setDropHighlight:-1];
    _dropHi = -1;
    [_ghost orderOut:nil];
    _dropLine.hidden = YES;
    _dropRow = -1;
    [self setStatus:@"Arraste cancelado."];
}

/* orig: main_proc WM_MOUSEMOVE while dragging. Public for the test hook. */
- (void)dragMoveTo:(NSPoint)p {
    [_ghost moveNearCursor];
    _dropRow = -1;
    _dropCase = _dropDisc = -1;
    if (_dropHi >= 0) { [self setDropHighlight:-1]; _dropHi = -1; }
    _dropLine.hidden = YES;
    int after = 0;
    if (_dragKind == 1) {
        NSInteger row = [self dragRowIn:_tCases at:p after:&after];
        if (row < 0) return;
        _dropRow = (int)row;
        _dropAfter = after;
        _dropCase = (int)((MHRow *)_caseRows[row]).param;
        [self showLineIn:_tCases row:row after:after];
        return;
    }
    if (_dragKind != 2) return;
    NSInteger row = [self dragRowIn:_tDiscs at:p after:&after];
    if (row >= 0) {
        _dropCase = _dragCase;
        _dropDisc = (int)((MHRow *)_discRows[row]).param;
        _dropRow = (int)row;
        _dropAfter = after;
        [self showLineIn:_tDiscs row:row after:after];
        return;
    }
    row = [self hitRowIn:_tCases at:p];
    if (row < 0) return;
    int param = (int)((MHRow *)_caseRows[row]).param;
    MhCase *c = [_app caseAt:param];
    if (!c) return;
    if (param == _dragCase || c->disc_count < 5) {
        _dropRow = _dropHi = (int)row;
        _dropCase = param;
        _dropDisc = c->disc_count;
        [self setDropHighlight:(int)row];
    }
}

/* orig: main_proc WM_LBUTTONUP while dragging. */
- (void)dragDropAt:(NSPoint)p {
    MhProject *pr = _app.project;
    if (_dragKind == 1) {
        if (_dropRow >= 0 && _dragCase >= 0) {
            int idx = (_dropAfter != 0) + _dropRow;
            for (NSUInteger i = 0; i < _caseRows.count; i++) {
                if (_caseRows[i].param == _dragCase) {
                    if ((int)i < idx) idx--;
                    break;
                }
            }
            if (idx < 0) idx = 0;
            if (pr->case_count <= idx) idx = pr->case_count ? pr->case_count - 1 : 0;
            uint8_t out = 0;
            if (mh_project_reorder_case_index(pr, (uint8_t)_dragCase, (uint16_t)idx, &out)) {
                _app.curCase = out;
                _app.curDisc = -1;
            }
        }
    } else if (_dragKind == 2 && _dropCase >= 0 && _dragCase >= 0 && _dragDisc >= 0) {
        int to = _dropDisc;
        if (_dropCase == _dragCase && _dropRow >= 0 && _dropHi < 0) {
            to = _dropRow + (_dropAfter != 0);
            if (_dragDisc < to) to--;
            MhCase *sc = [_app caseAt:_dragCase];
            int cnt = sc ? sc->disc_count : 0;
            if (cnt <= to) to = cnt - 1;
        }
        if (to < 0) to = 0;
        uint8_t oc = 0, od = 0;
        if (mh_project_move_disc(pr, (uint8_t)_dragCase, (uint8_t)_dragDisc, (uint8_t)_dropCase, (uint8_t)to, &oc, &od)) {
            _app.curCase = oc;
            _app.curDisc = od;
        }
    }
    _dragKind = 0;
    [self setDropHighlight:-1];
    _dropHi = -1;
    [_ghost orderOut:nil];
    _dropLine.hidden = YES;
    _dropRow = -1;
    [self refreshProject];
}

#pragma mark - button actions

- (void)toggleVerify:(id)s { _app.verifyOnAdd = _chkVerify.state == NSControlStateValueOn; }

- (void)about:(id)s { [self showAbout]; }

- (void)showAbout {
    MHMessage(@"Sobre", @"PicoStation Multi House V3\r\n\r\nAlberonisse da Multi House\r\n\r\n                 www.multihouse.net", MHIconInfo);
}

- (BOOL)commandsAllowed { return !_app.loading; }

- (void)addGame:(id)s {
    if (![self commandsAllowed]) return;
    [self addDialogFilesTarget:-1];
}

- (void)addDisc:(id)s {
    if (![self commandsAllowed]) return;
    if (_app.currentCase) [self addDialogFilesTarget:_app.curCase];
    else MHMessage(MH_TITLE, @"Selecione uma Case primeiro.", MHIconError);
}

- (void)caseArt:(id)s { [self makeCustomArtForCase:YES]; }
- (void)discArt:(id)s { [self makeCustomArtForCase:NO]; }
- (void)cmdCheatsAction:(id)s { [self cmdCheats]; }
- (void)refreshSDAction:(id)s { if (!_app.busy && !_app.loading) [self refreshSD]; }
- (void)sdChanged:(id)s { if (!_app.loading) [self loadSelectedV2:YES]; }

#pragma mark - adding CUEs (orig: add_dialog_files, add_cue_path, process_drop, finish_batch)

/* orig: add_dialog_files. target < 0 means "group by game" (0xffffffff). */
- (void)addDialogFilesTarget:(int)target {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.title = @"Adicionar um ou varios CUEs";
    p.allowsMultipleSelection = YES;
    p.canChooseDirectories = NO;
    p.message = @"CUE do PlayStation (*.cue)";
    p.allowedFileTypes = @[ @"cue" ];
    if ([p runModal] != NSModalResponseOK) return;
    NSMutableArray *paths = [NSMutableArray array];
    for (NSURL *u in p.URLs) [paths addObject:u.path];
    [self addCuePaths:paths target:target duplicates:0 ignored:0];
}

/* ---- the dialogs the original opened from inside manager_parse / add_cue_path (hooks) ---- */

static bool hook_ask(void *ctx, const char *text) { return MHAsk(MHStr(text)); }

/* orig: common_locate. */
static bool hook_locate(void *ctx, char out[MH_PATH_MAX]) {
    __block bool ok = false;
    MHMain(^{
        NSOpenPanel *p = [NSOpenPanel openPanel];
        p.title = @"Localize o BIN correspondente";
        p.message = @"Arquivos BIN (*.bin)";
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

static void hook_attach(void *ctx, MhDisc *d) { mhb_attach_cheats(d, MHApp.shared.cheatDb); }

- (BOOL)admitOnWorker:(MhDisc *)d verify:(BOOL)verify {
    MHMain(^{ [self setStatus:verify ? @"Verificando disco antes de adicionar..." : @"Conferindo CUE/BIN..."]; });
    BOOL ok = mhb_manager_admit(d, verify, &_prog);
    if (!ok && !mh_progress_cancelled(&_prog) && d && d->verified == 0)
        MHMessage(MH_TITLE, MHStr(d->verify_text), MHIconError);
    return ok;
}

/* orig: admit_cue_disc as a stand-alone command ("Verificar Disco"). */
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
    MHMain(^{ [self setStatus:@"Analisando CUE/BIN..."]; });
    MhCueHooks hooks = {.ctx = (__bridge void *)self, .ask = hook_ask, .locate = hook_locate,
                        .admit = hook_admit, .attach_cheats = hook_attach};
    char err[MH_ERR_MSG_MAX] = "";
    MhAddResult res = {0};
    MhAddStatus st = mh_project_add_cue(_app.project, _app.db, path.UTF8String,
                                        target < 0 ? 0x100u : (unsigned)target, &hooks, &res, err, sizeof err);
    switch (st) {
        case MH_ADD_OK:
            _app.curCase = res.cas->slot;
            _app.curDisc = res.disc_index;
            return YES;
        case MH_ADD_PARSE_FAILED:
            MHMessage(MH_TITLE, err[0] ? MHStr(err) : @"Nao foi possivel analisar um CUE.", MHIconError);
            break;
        case MH_ADD_LIMIT:
        case MH_ADD_NO_ROOM:
            if (err[0]) MHMessage(MH_TITLE, MHStr(err), MHIconError);
            break;
        default: break; /* not a CUE, duplicate, rejected (already reported) */
    }
    return NO;
}

- (void)finishBatchOK:(unsigned)ok failed:(unsigned)fail {
    [self refreshProject];
    NSMutableString *s = [NSMutableString stringWithFormat:@"%u disco(s) adicionado(s)", ok];
    if (fail) [s appendFormat:@"; %u ignorado(s)/com erro", fail];
    [self setStatus:s];
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
                mh_str_copy(c->error, sizeof c->error, "Um caminho arrastado excede o limite suportado.");
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
            failure = @"Nenhum CUE encontrado. Arraste pastas em Jogos/Cases ou CUEs na lista de discos.";
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
            MHMessage(MH_TITLE, failure, MHIconError);
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

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)s { return [self dropOperation:s]; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)s { return [self dropOperation:s]; }

/* Which list is under the pointer; -2 none, -1 case list, >= 0 the case receiving the discs. */
- (int)dropTarget:(id<NSDraggingInfo>)s {
    NSPoint p = [_root convertPoint:s.draggingLocation fromView:nil];
    if (NSPointInRect(p, _scDiscs.frame)) return _app.currentCase ? _app.curCase : -3;
    if (NSPointInRect(p, _scCases.frame)) return -1;
    return -2;
}

- (NSDragOperation)dropOperation:(id<NSDraggingInfo>)s {
    if (_app.busy || _app.loading) return NSDragOperationNone;
    return [self dropTarget:s] == -2 ? NSDragOperationNone : NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)s {
    if (_app.busy || _app.loading) return NO;
    int target = [self dropTarget:s];
    if (target == -2) return NO;
    if (target == -3) {
        MHMessage(MH_TITLE, @"Selecione um jogo/case para receber os discos.", MHIconError);
        return NO;
    }
    NSArray<NSURL *> *urls = [s.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    NSMutableArray *paths = [NSMutableArray array];
    for (NSURL *u in urls) [paths addObject:u.path];
    [self importPaths:paths target:target];
    return YES;
}

#pragma mark - Menu button (orig: cmd_defaults)

- (void)cmdDefaults:(id)s {
    if (![self commandsAllowed]) return;
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
            MHMessage(MH_TITLE, @"Padroes incorporados invalidos.", MHIconError);
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
            [self setStatus:@"Padroes salvos. Aplique ao SD ou imagem. O save do Memory Card tem prioridade."];
        } else {
            MHMessage(MH_TITLE, @"Nao foi possivel salvar os padroes ao lado do Manager.", MHIconError);
        }
    } else {
        MHMessage(MH_TITLE, @"Configuracao invalida.", MHIconError);
    }
}

#pragma mark - Imagem button (orig: the popup of main_proc, cmd_image_open, cmd_image_save_as)

- (void)cmdImageMenu:(id)s {
    if (![self commandsAllowed]) return;
    MhProject *p = _app.project;
    NSMenu *m = [[NSMenu alloc] init];
    m.autoenablesItems = NO;
    void (^add)(NSString *, NSInteger, BOOL) = ^(NSString *title, NSInteger tag, BOOL enabled) {
        NSMenuItem *it = [m addItemWithTitle:title action:@selector(menuPicked:) keyEquivalent:@""];
        it.target = self;
        it.tag = tag;
        it.enabled = enabled;
    };
    add(@"Abrir imagem V2...", IM_OPEN, YES);
    add(@"Salvar imagem como...", IM_SAVE, YES);
    add(@"Aplicar na imagem aberta", IM_APPLY, p->image_loaded != 0);
    [m addItem:NSMenuItem.separatorItem];
    add(@"Novo projeto", IM_NEW, YES);
    _picked = 0;
    [m popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, NSHeight(_btnImage.bounds) + 2) inView:_btnImage];
    switch (_picked) {
        case IM_OPEN: [self cmdImageOpen]; break;
        case IM_SAVE: [self cmdImageSaveAs]; break;
        case IM_APPLY:
            if (!p->image_loaded || !p->image_path[0]) {
                MHMessage(MH_TITLE, @"Nenhuma imagem V2 esta aberta. Use Salvar imagem como.", MHIconError);
            } else if (MHAsk(@"Aplicar as alteracoes diretamente na imagem V2 aberta?")) {
                [self writeImageTo:MHStr(p->image_path)];
            }
            break;
        case IM_NEW:
            if (!p->dirty || MHAsk(@"Criar um projeto vazio e descartar as alteracoes pendentes?")) {
                mh_project_clear(p);
                [self useDefaultMenu];
                _app.curCase = _app.curDisc = -1;
                [self refreshProject];
                [self setStatus:@"Projeto vazio. Adicione CUEs e use Imagem > Salvar imagem como."];
            }
            break;
        default: break;
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

- (void)cmdImageOpen {
    MhProject *p = _app.project;
    if (p->dirty && !MHAsk(@"Abrir outra imagem e descartar as alteracoes ainda nao aplicadas?")) return;
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.title = @"Abrir imagem PSXRAW V2";
    op.message = @"Imagens V2 (*.img;*.raw;*.psxraw)";
    op.allowedFileTypes = @[ @"img", @"raw", @"psxraw" ];
    op.canChooseDirectories = NO;
    if ([op runModal] != NSModalResponseOK) return;
    [self loadImageAtPath:op.URL.path];
}

- (void)loadImageAtPath:(NSString *)path {
    NSString *cache = image_cache_dir(@"PicoStationV2ImageCache");
    __block MhMsg msg = {{0}};
    __block BOOL ok = NO;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@"Carregando imagem V2..."]; });
        ok = mhb_load_image(path.fileSystemRepresentation, cache.fileSystemRepresentation, self->_app.project, msg.s, sizeof msg.s);
        if (ok) mhb_hydrate(self->_app.project, self->_app.db, self->_app.cheatDb);
    } done:^{
        [self setBusy:NO];
        if (ok) {
            self->_app.curCase = self->_app.curDisc = -1;
            [self refreshProject];
            [self setStatus:MHStr(msg.s)];
        } else {
            MHMessage(MH_TITLE, msg.s[0] ? MHStr(msg.s) : @"Imagem V2 invalida.", MHIconError);
        }
    }];
}

- (void)cmdImageSaveAs {
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.title = @"Salvar imagem PSXRAW V2";
    sp.message = @"Imagem PSXRAW V2 (*.img)";
    sp.allowedFileTypes = @[ @"img" ];
    if ([sp runModal] == NSModalResponseOK) [self writeImageTo:sp.URL.path];
}

/* orig: write_image_direct_to.isra.0 */
- (void)writeImageTo:(NSString *)path {
    if (!_app.dbOK) {
        MHMessage(MH_TITLE, @"GRAVACAO BLOQUEADA: o banco incorporado de identificacao, hashes e SBI falhou na validacao interna. Reinstale o executavel oficial.", MHIconError);
        return;
    }
    if (!_app.cheatOK) {
        MHMessage(MH_TITLE, @"GRAVACAO BLOQUEADA: o banco externo auditado de cheats precisa permanecer em database\\cheats ao lado do Manager.", MHIconError);
        return;
    }
    if (![self allReady:NULL]) return;
    MhbWriteInfo prep = {0};
    if (!mhb_prepare_write(_app.project, &prep)) {
        MHMessage(MH_TITLE, prep.msg[0] ? MHStr(prep.msg) : @"Existem discos que ainda nao foram preparados para gravacao.", MHIconError);
        return;
    }
    __block MhbWriteInfo info = {0};
    __block BOOL ok = NO, loaded = NO;
    NSString *cache = image_cache_dir(@"PicoStationV2ImageCache");
    _progress.doubleValue = 0;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@"Gravando imagem V2..."]; });
        ok = mhb_write_image(self->_app.project, path.fileSystemRepresentation, prog, &info);
        if (ok) {
            loaded = mhb_load_image(path.fileSystemRepresentation, cache.fileSystemRepresentation, self->_app.project, info.msg, sizeof info.msg);
            if (loaded) mhb_hydrate(self->_app.project, self->_app.db, self->_app.cheatDb);
        }
    } done:^{
        if (!ok) {
            [self writeFailure:info.msg[0] ? MHStr(info.msg) : @"Falha ao gravar a imagem V2."];
        } else if (!loaded) {
            [self setBusy:NO];
            MHMessage(MH_TITLE, @"A imagem foi gravada, mas nao pode ser reaberta para edicao.", MHIconError);
        } else {
            [self setBusy:NO];
            self->_app.curCase = self->_app.curDisc = -1;
            [self refreshProject];
            [self setStatus:@"Imagem V2 concluida e carregada para edicao."];
        }
    }];
}

#pragma mark - write (orig: all_ready, cmd_write, write_failure)

/* orig: all_ready. anyNote receives whether a disc carries observations. */
- (BOOL)allReady:(BOOL *)anyNote {
    if (anyNote) *anyNote = NO;
    MhProject *p = _app.project;
    if (p->disc_count == 0) {
        MHMessage(MH_TITLE, @"Adicione ao menos um jogo antes de gravar.", MHIconError);
        return NO;
    }
    for (int i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        MhCase *c = &p->cases[i];
        if (!c->in_use || !c->disc_count) continue;
        for (unsigned k = 0; k < c->disc_count; k++) {
            MhDisc *d = c->discs[k];
            NSMutableString *m = nil;
            if (!d) {
                m = [NSMutableString stringWithFormat:@"Gravacao nao iniciada. Case: %@\r\nDisco %u", MHStr(c->title), k + 1];
            } else if (d->status == MH_DISC_STATUS_FIXABLE || d->status == MH_DISC_STATUS_ERROR) {
                char base[MH_PATH_MAX];
                mh_path_basename(d->cue_path, base, sizeof base);
                m = [NSMutableString stringWithFormat:@"Gravacao nao iniciada. Case: %@\r\nDisco %u\r\n%@", MHStr(c->title), k + 1, MHStr(d->notes)];
                /* orig: the separator between disc number and notes is the 2 byte "\r\n" DAT_140f0f7c6 */
                if (d->verify_text[0]) [m appendFormat:@"\r\n\r\nUltima verificacao: %@", MHStr(d->verify_text)];
                [m appendFormat:@"\r\nCUE: %@", MHStr(base)];
            }
            if (m) {
                MHMessage(MH_TITLE, m.length ? m : @"Estrutura do disco incompleta.", MHIconError);
                return NO;
            }
            if (d->status == MH_DISC_STATUS_NOTE && anyNote) *anyNote = YES;
        }
    }
    return YES;
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

- (void)doWrite {
    int di = [self selectedDiskIndex];
    MhProject *p = _app.project;
    NSString *msg;
    if (!_app.dbOK) {
        msg = @"GRAVACAO BLOQUEADA: o banco incorporado de identificacao, hashes e SBI falhou na validacao interna. Reinstale o executavel oficial.";
    } else if (!_app.cheatOK) {
        msg = @"GRAVACAO BLOQUEADA: o banco externo auditado de cheats precisa permanecer em database\\cheats ao lado do Manager.";
    } else {
        if (![self allReady:NULL]) return;
        if (di < 0) {
            msg = @"Selecione um cartao SD.";
        } else {
            MhDiskInfo disk = _disks[di];
            if (p->card_loaded && p->device != (int32_t)disk.number) {
                msg = @"Este projeto foi carregado de outro cartao. Selecione o mesmo PhysicalDrive para aplicar as alteracoes.";
            } else {
                uint64_t need = mhb_estimate_bytes(p, (int)disk.number);
                if (need > disk.size) {
                    msg = @"Os jogos nao cabem no cartao selecionado.";
                } else {
                    MhbWriteInfo prep = {0};
                    if (!mhb_prepare_write(p, &prep)) {
                        MHMessage(MH_TITLE, prep.msg[0] ? MHStr(prep.msg) : @"Existem discos que ainda nao foram preparados para gravacao.", MHIconError);
                        return;
                    }
                    [self confirmAndWrite:disk needBytes:need];
                    return;
                }
            }
        }
    }
    MHMessage(MH_TITLE, msg, MHIconError);
}

- (void)confirmAndWrite:(MhDiskInfo)disk needBytes:(uint64_t)need {
    MhProject *p = _app.project;
    MhWbuf b;
    mh_wbuf_init(&b);
    mh_wbuf_puts(&b, p->card_loaded
        ? "As alteracoes serao aplicadas diretamente no cartao PSXRAW V2. Os dados RAW dos jogos preservados permanecerao nos mesmos LBAs.\r\n\r\n"
        : "O cartao inteiro sera preparado diretamente como PSXRAW V2.\r\n\r\n");
    /* orig: "PhysicalDrive<N>"; the macOS equivalent is the BSD name of the disk */
    mh_wbuf_puts(&b, disk.bsd_name);
    mh_wbuf_puts(&b, "\r\nModelo: ");
    mh_wbuf_puts(&b, disk.model);
    mh_wbuf_puts(&b, "\r\nSerial: ");
    mh_wbuf_puts(&b, disk.serial[0] ? disk.serial : "nao informado");
    mh_wbuf_puts(&b, "\r\nCases: ");
    mh_wbuf_u32(&b, p->case_count);
    mh_wbuf_puts(&b, "  |  Discos: ");
    mh_wbuf_u32(&b, p->disc_count);
    mh_wbuf_puts(&b, "\r\nEspaco RAW final aproximado: ");
    mh_wbuf_size(&b, need);
    mh_wbuf_puts(&b, "\r\nDiscos novos preparados na inclusao; discos ja gravados serao preservados.");
    mh_wbuf_puts(&b, "\r\n\r\nATENCAO: escolher o PhysicalDrive errado pode apagar outro dispositivo.");
    NSString *text = MHStr(mh_wbuf_cstr(&b));
    mh_wbuf_free(&b);
    if (!MHConfirmWrite(_window, text)) {
        [_window makeKeyAndOrderFront:nil];
        return;
    }
    __block MhbWriteInfo info = {0};
    __block BOOL ok = NO;
    _progress.doubleValue = 0;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@"Gravando discos pendentes e atualizando a biblioteca..."]; });
        ok = mhb_write_sd(&disk, self->_app.project, prog, &info);
    } done:^{
        if (!ok) {
            [self writeFailure:info.msg[0] ? MHStr(info.msg) : @"Falha na gravacao direta do SD."];
            return;
        }
        self->_progress.doubleValue = 100;
        [self setBusy:NO];
        self->_app.project->dirty = 0;
        [self setStatus:@"Concluido: discos pendentes gravados; biblioteca atualizada."];
        MhWbuf m;
        mh_wbuf_init(&m);
        mh_wbuf_puts(&m, "Operacao concluida diretamente no SD.\r\n\r\nCases: ");
        mh_wbuf_u32(&m, info.cases);
        mh_wbuf_puts(&m, "\r\nDiscos: ");
        mh_wbuf_u32(&m, info.discs);
        mh_wbuf_puts(&m, "\r\nTamanho RAW utilizado: ");
        mh_wbuf_size(&m, info.raw_bytes);
        mh_wbuf_puts(&m, "\r\n\r\nO cartao e PSXRAW e nao possui arquivos visiveis no Windows. Nao formate o cartao.");
        MHMessage(MH_TITLE, MHStr(mh_wbuf_cstr(&m)), MHIconInfo);
        mh_wbuf_free(&m);
        self->_app.project->card_loaded = 0;
        if (!self->_app.loading) [self refreshSD];
        [self loadSelectedV2:NO];
    }];
}

/* orig: write_failure */
- (void)writeFailure:(NSString *)text {
    BOOL cancelled = _app.cancelRequested;
    [self setBusy:NO];
    [self refreshProject];
    if (!cancelled) {
        [self setStatus:@"Gravacao interrompida. Reconecte o mesmo SD e reaplique sem fechar o Manager."];
        MHMessage(MH_TITLE, [text stringByAppendingString:@"\r\n\r\nMantenha o Manager aberto. Reconecte o mesmo destino e reaplique: os discos azuis ja concluidos serao aproveitados. A biblioteca precisa ser finalizada antes do uso no console."], MHIconError);
    } else {
        [self setStatus:@"Cancelado. Mantenha o Manager aberto e reaplique para retomar os discos pendentes."];
    }
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
        MhDiskOpts opts = {.flags = mh_disk_flags_from_env()};
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
        [self setStatus:@MH_DISK_TXT_NONE];
        return;
    }
    int idx = pick >= 0 ? pick : 0;
    [_popSD selectItemAtIndex:idx];
    MhDiskInfo *d = &_disks[idx];
    if (d->has_v2 && d->v2_state == 2 && _app.project->disc_count == 0) {
        [self loadSelectedV2:NO];
        return;
    }
    [self setStatus:@MH_DISK_TXT_FOUND];
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
        !MHAsk(@"Carregar o cartao V2 selecionado e descartar o projeto atual ainda nao aplicado?"))
        return;
    NSString *cache = image_cache_dir([NSString stringWithFormat:@"PicoStationV2Cache-%s", disk.bsd_name]);
    __block MhMsg msg = {{0}};
    __block BOOL ok = NO;
    _app.loading = YES;
    [self startJob:^(MhProgress *prog) {
        MHMain(^{ [self setStatus:@"Carregando biblioteca V2 existente..."]; });
        ok = mhb_load_physical(&disk, cache.fileSystemRepresentation, self->_app.project, msg.s, sizeof msg.s);
        if (ok) mhb_hydrate(self->_app.project, self->_app.db, self->_app.cheatDb);
    } done:^{
        self->_app.loading = NO;
        [self setBusy:NO];
        if (!ok) {
            MHMessage(MH_TITLE, msg.s[0] ? MHStr(msg.s) : @"Nao foi possivel carregar o cartao V2.", MHIconError);
            return;
        }
        MhProject *pr = self->_app.project;
        mh_str_copy(pr->device_name, sizeof pr->device_name, disk.model);
        mh_str_copy(pr->device_label, sizeof pr->device_label, disk.serial);
        self->_app.curCase = self->_app.curDisc = -1;
        [self refreshProject];
        [self setStatus:msg.s[0] ? MHStr(msg.s) : @"Cartao PSXRAW V2 carregado. Arraste CUEs, reorganize e aplique as alteracoes."];
    }];
}

#pragma mark - per disc commands

- (void)cmdCheats { /* orig: cmd_cheats */
    MhDisc *d = _app.currentDisc;
    if (!d) return;
    if (d->serial[0] == 0) [self recoverSerial:d silent:YES];
    NSMutableString *h = [NSMutableString stringWithString:MHStr(d->name)];
    if (d->serial[0]) {
        [h appendFormat:@"  |  Serial: %@", MHStr(d->serial)];
        [h appendString:@"\r\nVinculo por serial. Use o botao ao lado para escolher outro perfil."];
    } else {
        [h appendString:@"\r\nSerial ausente: e possivel editar uma lista personalizada, mas o menu nao podera fazer vinculo automatico por serial."];
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
    mh_wbuf_puts(&b, "DISCO\r\n\r\nNome: ");
    mh_wbuf_puts(&b, d->name);
    mh_wbuf_puts(&b, "\r\nSerial: ");
    mh_wbuf_puts(&b, d->serial);
    mh_wbuf_puts(&b, "\r\nTracks: ");
    mh_wbuf_u32(&b, d->ntracks);
    mh_wbuf_puts(&b, "\r\nSetores PS1: ");
    mh_wbuf_u32(&b, d->total_sectors);
    mh_wbuf_puts(&b, "\r\nStatus: ");
    mh_wbuf_puts(&b, status_text(d->status).UTF8String);
    if (d->v2_raw_lba512) {
        mh_wbuf_puts(&b, "\r\nPackage LBA: ");
        mh_wbuf_u64(&b, d->v2_pkg_a);
        mh_wbuf_puts(&b, "\r\nData LBA: ");
        mh_wbuf_u64(&b, d->v2_raw_lba512);
    }
    mh_wbuf_puts(&b, "\r\n\r\n");
    mh_wbuf_puts(&b, d->notes);
    if (d->verify_text[0]) {
        mh_wbuf_puts(&b, "\r\n\r\nUltima verificacao: ");
        mh_wbuf_puts(&b, d->verify_text);
    }
    MHMessage(@"Detalhes tecnicos", MHStr(mh_wbuf_cstr(&b)), MHIconInfo);
    mh_wbuf_free(&b);
}

/* orig: recover_disc_serial_internal.isra.0 (silent = param_2 != 0: no message boxes) */
- (void)recoverSerial:(MhDisc *)d silent:(BOOL)silent {
    if (!d || d->serial[0]) return;
    MhPsSerial res;
    memset(&res, 0, sizeof res);
    uint32_t status = MH_PSS_NOT_FOUND;
    NSString *fail = nil;
    if (d->kind == MH_DISC_KIND_CUE) {
        if (d->nfiles && d->files[0].path[0]) status = mh_psserial_extract(&res, d->files[0].path);
        else fail = @"O CUE/BIN original nao esta disponivel para identificar o serial.";
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
                if (!silent) MHMessage(MH_TITLE, @"A origem RAW do disco nao esta disponivel.", MHIconError);
                return;
            }
            status = mh_psserial_extract_raw(&res, path, d->v2_raw_lba512, d->total_sectors);
        } else {
            fail = @"O registro RAW nao possui localizacao valida para leitura.";
        }
    } else {
        if (!silent) MHMessage(MH_TITLE, @"Esta origem nao permite recuperar o serial.", MHIconError);
        return;
    }
    if (fail) {
        if (!silent) MHMessage(MH_TITLE, fail, MHIconError);
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
            [self setStatus:@"Serial interno comprovado recuperado. Aplique as alteracoes ao SD."];
        } else {
            MHMessage(MH_TITLE, [NSString stringWithFormat:@"Serial interno recuperado: %@. O RAW do jogo foi preservado. Aplique as alteracoes para gravar a identidade no cartao.", MHStr(d->serial)], MHIconInfo);
        }
        return;
    }
    if (silent) return;
    MHMessage(MH_TITLE, [@"Nao foi encontrado um serial interno comprovado. Nenhum dado foi alterado.\r\n\r\n" stringByAppendingString:MHStr(res.message)], MHIconError);
}

/* orig: recode_disc + manager_recode_dialog */
- (void)recodeDisc {
    MhDisc *d = _app.currentDisc;
    if (!d || d->kind != 1 || (d->verify_issues & 0xc0) == 0) return;
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.title = @"Recalcular EDC/ECC em copia; conteudo do jogo permanece igual";
    sp.message = @"Nova copia CUE/BIN";
    sp.allowedFileTypes = @[ @"cue" ];
    if ([sp runModal] != NSModalResponseOK) return;
    NSString *dst = sp.URL.path;
    __block MhMsg err = {{0}};
    __block BOOL ok = NO;
    [self startJob:^(MhProgress *prog) {
        ok = mhb_recode_copy(d, dst.fileSystemRepresentation, prog, err.s, sizeof err.s);
    } done:^{
        BOOL cancelled = self->_app.cancelRequested;
        [self setBusy:NO];
        if (ok) {
            MHMessage(MH_TITLE, [NSString stringWithFormat:@"Copia criada: %@\r\nAdicione esse CUE no lugar do disco original para testar. EDC/ECC recalculados; conteudo do jogo preservado, sem recuperacao de dados corrompidos.", dst], MHIconInfo);
        } else if (!cancelled) {
            MHMessage(MH_TITLE, MHStr(err.s), MHIconError);
        }
    }];
}

/* orig: make_custom_art.isra.0 (the file picker and the project update; the art is the core's) */
- (void)makeCustomArtForCase:(BOOL)isCase {
    MhCase *c = _app.currentCase;
    MhDisc *d = _app.currentDisc;
    if (!c || (!isCase && !d)) return;
    NSOpenPanel *op = [NSOpenPanel openPanel];
    op.title = isCase ? @"Escolher imagem para a Case" : @"Escolher imagem para o Disco";
    op.message = @"Imagens (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.art)";
    op.allowedFileTypes = @[ @"png", @"jpg", @"jpeg", @"bmp", @"gif", @"tif", @"tiff", @"art" ];
    op.canChooseDirectories = NO;
    if ([op runModal] != NSModalResponseOK) return;
    char dir[MH_PATH_MAX], name[128], dst[MH_PATH_MAX], err[MH_ERR_MSG_MAX] = "";
    mh_path_join(_app.dataDir.fileSystemRepresentation, "custom/art", dir, sizeof dir);
    mh_ensure_dir(dir);
    const char *serial = d ? (d->serial[0] ? d->serial : d->name) : "";
    mh_art_custom_name(isCase, serial, c->slot, name, sizeof name);
    mh_path_join(dir, name, dst, sizeof dst);
    if (!mh_art_import(_app.art, isCase, op.URL.fileSystemRepresentation, dst, err, sizeof err)) {
        MHMessage(MH_TITLE, MHStr(err), MHIconError);
        return;
    }
    if (isCase) mh_str_copy(c->art_path, sizeof c->art_path, dst);
    else mh_str_copy(d->cd_art_path, sizeof d->cd_art_path, dst);
    _app.project->dirty = 1;
    [self refreshProject];
}

#pragma mark - startup (orig: app_main tail)

- (void)start {
    if (!_app.dbOK)
        MHMessage(@"Banco incorporado", @"O banco incorporado de identificacao, tracksets e SBI falhou na validacao. Reinstale o executavel oficial.", MHIconError);
    if (!_app.cheatOK)
        MHMessage(@"Cheats", @"Banco auditado de cheats nao carregado. Mantenha database\\cheats ao lado do EXE. Covers e cheats sao os unicos dados externos.", MHIconError);
    [self refreshProject];
    [self startDeviceWatch];
    [self refreshSD]; /* orig: WM_CREATE ends with refresh_sd */
}

#pragma mark - test hooks

- (NSString *)snapshotReport {
    NSMutableString *s = [NSMutableString string];
    [s appendFormat:@"cases=%u discs=%u dirty=%u curCase=%d curDisc=%d\n", _app.project->case_count,
                    _app.project->disc_count, _app.project->dirty, _app.curCase, _app.curDisc];
    for (MHRow *r in _caseRows) [s appendFormat:@"  case slot=%ld %@\n", (long)r.param, [r.cells componentsJoinedByString:@" | "]];
    for (MHRow *r in _discRows) [s appendFormat:@"  disc idx=%ld %@\n", (long)r.param, [r.cells componentsJoinedByString:@" | "]];
    return s;
}

/* Commands: report, select-case N, select-disc N, drag-case <rowFrom> <rowTo> <after>,
 * drag-disc <discRow> <targetKind c|d> <targetRow> <after>, remove-disc, remove-case, shot <file>,
 * name-case TEXT, name-disc TEXT, busy 0|1. Output goes to stdout. */
- (void)testCommand:(NSString *)cmd {
    NSArray *a = [cmd componentsSeparatedByString:@" "];
    NSString *op = a[0];
    if ([op isEqualToString:@"report"]) {
        printf("%s", self.snapshotReport.UTF8String);
    } else if ([op isEqualToString:@"select-case"]) {
        _app.curCase = [a[1] intValue];
        _app.curDisc = -1;
        [self refreshCases];
        [self refreshFields];
    } else if ([op isEqualToString:@"select-disc"]) {
        _app.curDisc = [a[1] intValue];
        [self refreshDiscs];
        [self refreshFields];
    } else if ([op isEqualToString:@"drag-case"]) { /* from row, to row, after */
        NSInteger from = [a[1] integerValue], to = [a[2] integerValue];
        [self dragProgrammaticKind:1 fromRow:from toList:_tCases row:to after:[a[3] intValue]];
    } else if ([op isEqualToString:@"drag-disc"]) { /* from disc row, "d"/"c", row, after */
        NSInteger from = [a[1] integerValue], to = [a[3] integerValue];
        BOOL toCases = [a[2] isEqualToString:@"c"];
        [self dragProgrammaticKind:2 fromRow:from toList:toCases ? _tCases : _tDiscs row:to after:[a[4] intValue]];
    } else if ([op isEqualToString:@"remove-disc"]) {
        [self cmdRemoveDisc];
    } else if ([op isEqualToString:@"remove-case"]) {
        [self removeCase];
    } else if ([op isEqualToString:@"name-case"]) {
        _fCase.stringValue = [[a subarrayWithRange:NSMakeRange(1, a.count - 1)] componentsJoinedByString:@" "];
        [self applyCaseName];
    } else if ([op isEqualToString:@"name-disc"]) {
        _fDisc.stringValue = [[a subarrayWithRange:NSMakeRange(1, a.count - 1)] componentsJoinedByString:@" "];
        [self applyDiscName];
    } else if ([op isEqualToString:@"busy"]) {
        [self setBusy:[a[1] boolValue]];
    } else if ([op isEqualToString:@"shot"]) {
        [self writeShot:a[1]];
    } else if ([op isEqualToString:@"context"]) {
        printf("context menu for disc %d\n", _app.curDisc);
    } else if ([op isEqualToString:@"progress"]) {
        [self queueProgress:[a[1] longLongValue] total:100 text:"Teste de progresso"];
    }
    fflush(stdout);
}

/* Drives the same state machine as the mouse: start, move over the target row, drop. */
- (void)dragProgrammaticKind:(int)kind fromRow:(NSInteger)from toList:(MHListTable *)tl row:(NSInteger)to after:(int)after {
    MHListTable *src = kind == 1 ? _tCases : _tDiscs;
    NSArray *rows = [self rowsFor:src];
    if (from < 0 || from >= (NSInteger)rows.count) return;
    MHRow *r = rows[from];
    if (kind == 1) { _dragCase = (int)r.param; _dragDisc = -1; }
    else { _dragCase = _app.curCase; _dragDisc = (int)r.param; }
    [self dragStartKind:kind row:from];
    NSArray *trows = [self rowsFor:tl];
    NSPoint p;
    if (to >= (NSInteger)trows.count) {
        NSScrollView *sc = tl == _tCases ? _scCases : _scDiscs;
        p = NSMakePoint(NSMidX(sc.frame), NSMaxY(sc.frame) - 3);
    } else {
        NSRect rr = [self rootRectForRow:to inTable:tl];
        p = NSMakePoint(NSMidX(rr), after ? rr.origin.y + rr.size.height * 0.75 : rr.origin.y + rr.size.height * 0.25);
    }
    [self dragMoveTo:p];
    printf("drop target: case=%d disc=%d row=%d after=%d hi=%d\n", _dropCase, _dropDisc, _dropRow, _dropAfter, _dropHi);
    [self dragDropAt:p];
}

- (void)writeShot:(NSString *)path {
    [_root layoutSubtreeIfNeeded];
    NSBitmapImageRep *rep = [_root bitmapImageRepForCachingDisplayInRect:_root.bounds];
    [_root cacheDisplayInRect:_root.bounds toBitmapImageRep:rep];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}

@end
