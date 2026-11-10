/* cheat_ui.m: "Cheats do Disco", "Biblioteca de cheats" and "Editor de Cheats".
 * Original: cheat_ui_edit, ui_proc (+ ui_profile, ui_details, ui_new_custom, ui_remove, ui_import,
 * ui_export, ui_accept, ui_commit_edit, ui_move_entry, ui_select_index, ui_header_proc),
 * catalog_proc (+ catalog_refresh, catalog_details, catalog_add), editor_proc, editor_save,
 * run_editor. Layout numbers are the original WM_SIZE values in (flipped) points. */
#import "cheat_ui.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "mh/str.h"

#define MH_NONE 0xFFFFFFFFu
#define TITLE_CHEATS @"Cheats do Disco"
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

static NSFont *UIFont(void) { return [NSFont systemFontOfSize:12]; }

static NSString *S(const char *c) {
    if (!c) return @"";
    NSString *s = [NSString stringWithUTF8String:c];
    return s ?: [[NSString alloc] initWithBytes:c length:strlen(c) encoding:NSISOLatin1StringEncoding];
}

/* orig: MessageBoxW. style: NSAlertStyleCritical for 0x10, Informational for 0x40, Warning for
 * 0x30 and the question boxes. defIdx is the button answering Return. */
static NSInteger MsgBox(NSString *title, NSString *text, NSAlertStyle style, NSArray<NSString *> *buttons,
                        NSInteger defIdx) {
    if (MHCheatUIAlertHook) return MHCheatUIAlertHook(title, text, buttons);
    NSAlert *a = [[NSAlert alloc] init];
    a.alertStyle = style;
    a.messageText = title;
    a.informativeText = text;
    for (NSString *b in buttons) [a addButtonWithTitle:b];
    if (buttons.count > 1) {
        for (NSButton *b in a.buttons) b.keyEquivalent = @"";
        a.buttons[defIdx].keyEquivalent = @"\r";
        if (buttons.count == 3) a.buttons.lastObject.keyEquivalent = @"\033";
    }
    NSModalResponse r = [a runModal];
    return r - NSAlertFirstButtonReturn;
}
static void MsgErr(NSString *text) { MsgBox(TITLE_CHEATS, text, NSAlertStyleCritical, @[ @"OK" ], 0); }
static void MsgInfo(NSString *text) { MsgBox(TITLE_CHEATS, text, NSAlertStyleInformational, @[ @"OK" ], 0); }
/* MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 (0x124): YES when index 0. */
static BOOL AskYesNo(NSString *text) {
    return MsgBox(TITLE_CHEATS, text, NSAlertStyleWarning, @[ @"Sim", @"Nao" ], 1) == 0;
}

@interface MHFlipView : NSView
@property(copy) void (^onSize)(NSSize);
@end
@implementation MHFlipView
- (BOOL)isFlipped { return YES; }
- (void)setFrameSize:(NSSize)s {
    [super setFrameSize:s];
    if (self.onSize) self.onSize(s);
}
@end

@interface MHTable : NSTableView
@property(copy) void (^onDelete)(void);
@end
@implementation MHTable
- (void)keyDown:(NSEvent *)e {
    NSString *c = e.charactersIgnoringModifiers;
    if (c.length && ([c characterAtIndex:0] == NSDeleteFunctionKey || [c characterAtIndex:0] == NSDeleteCharacter) &&
        self.onDelete) {
        self.onDelete();
        return;
    }
    [super keyDown:e];
}
@end

/* orig: ui_header_proc, double click on the "Cheats" header sorts by name. */
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

static NSTextField *Label(NSString *s, BOOL wrap) {
    NSTextField *t = wrap ? [NSTextField wrappingLabelWithString:s] : [NSTextField labelWithString:s];
    t.font = UIFont();
    return t;
}
static NSButton *Btn(NSString *title, id target, SEL action) {
    NSButton *b = [NSButton buttonWithTitle:title target:target action:action];
    b.font = UIFont();
    return b;
}
static NSTextField *Edit(NSString *s, id delegate) {
    NSTextField *t = [[NSTextField alloc] init];
    t.font = UIFont();
    t.bezeled = YES;
    t.bezelStyle = NSTextFieldSquareBezel;
    t.editable = YES;
    t.stringValue = s ?: @"";
    t.delegate = delegate;
    t.usesSingleLineMode = YES;
    return t;
}
static NSPopUpButton *Popup(void) {
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    p.font = UIFont();
    return p;
}
/* multi line text in a bezel scroll view (EDIT with WS_VSCROLL and ES_MULTILINE). */
static NSScrollView *TextBox(BOOL readOnly, NSString *initial) {
    NSScrollView *sv = [[NSScrollView alloc] init];
    sv.hasVerticalScroller = YES;
    sv.borderType = NSBezelBorder;
    sv.autohidesScrollers = NO;
    NSTextView *t = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    t.minSize = NSMakeSize(0, 0);
    t.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    t.verticallyResizable = YES;
    t.horizontallyResizable = NO;
    t.autoresizingMask = NSViewWidthSizable;
    t.textContainer.widthTracksTextView = YES;
    t.font = UIFont();
    t.richText = NO;
    t.editable = !readOnly;
    t.selectable = YES;
    t.automaticQuoteSubstitutionEnabled = NO;
    t.automaticDashSubstitutionEnabled = NO;
    t.automaticTextReplacementEnabled = NO;
    t.automaticSpellingCorrectionEnabled = NO;
    t.continuousSpellCheckingEnabled = NO;
    t.allowsUndo = !readOnly;
    t.textContainerInset = NSMakeSize(2, 2);
    t.string = initial ?: @"";
    if (readOnly) t.backgroundColor = NSColor.controlBackgroundColor;
    sv.documentView = t;
    return sv;
}

/* cols: @[@[ident, title, @(width), @(center)]] */
static NSScrollView *MakeTable(NSArray<NSArray *> *cols, BOOL multi, id owner) {
    MHTable *t = [[MHTable alloc] init];
    t.dataSource = owner;
    t.delegate = owner;
    t.allowsMultipleSelection = multi;
    t.allowsColumnReordering = NO;
    t.allowsColumnSelection = NO;
    t.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
    t.rowHeight = 18;
    t.intercellSpacing = NSMakeSize(0, 1);
    t.usesAlternatingRowBackgroundColors = NO;
    t.gridStyleMask = NSTableViewGridNone;
    t.style = NSTableViewStylePlain;
    MHHeader *h = [[MHHeader alloc] initWithFrame:NSMakeRect(0, 0, 100, 22)];
    t.headerView = h;
    for (NSArray *c in cols) {
        NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier:c[0]];
        col.title = c[1];
        col.width = [c[2] doubleValue];
        col.minWidth = 20;
        col.resizingMask = NSTableColumnUserResizingMask;
        NSTextAlignment al = [c[3] boolValue] ? NSTextAlignmentCenter : NSTextAlignmentLeft;
        NSTextFieldCell *dc = [[NSTextFieldCell alloc] initTextCell:@""];
        dc.font = UIFont();
        dc.alignment = al;
        dc.lineBreakMode = NSLineBreakByTruncatingTail;
        col.dataCell = dc;
        col.headerCell.font = UIFont();
        col.headerCell.alignment = al;
        [t addTableColumn:col];
    }
    NSScrollView *sv = [[NSScrollView alloc] init];
    sv.documentView = t;
    sv.hasVerticalScroller = YES;
    sv.borderType = NSBezelBorder;
    return sv;
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

/* ---- Editor de Cheats (orig: editor_proc, editor_save, run_editor) ----------------------------- */

@interface MHCheatEditor : NSObject <NSWindowDelegate, MHCheatUITestable>
@property NSWindow *window, *parent;
@property NSTextField *nameField;
@property NSPopUpButton *version;
@property NSTextView *codes;
@property BOOL done, accepted;
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
}
- (NSWindow *)testWindow { return _window; }

- (instancetype)initWithParent:(NSWindow *)parent name:(const char *)name lines:(const uint8_t *)lines n:(uint16_t)n
                         flags:(uint8_t)flags {
    if ((self = [super init])) {
        _parent = parent;
        _initName = name ? S(name) : @"";
        _lines = lines;
        _n = n;
        _flags = flags;
        [self build];
    }
    return self;
}

- (NSString *)initialCodeText {
    if (!_lines || !_n) return @"";
    MhWbuf b;
    mh_wbuf_init(&b);
    if (!(_flags & 0x40)) {
        mh_cheat_codes_to_text(_lines, _n, &b);
    } else {
        /* classic origin: native 0x70/0x90 conditions are shown as D0/D1 */
        uint8_t *tmp = malloc((size_t)_n * 6);
        memcpy(tmp, _lines, (size_t)_n * 6);
        for (uint16_t i = 0; i < _n; i++) {
            if (tmp[i * 6] == 0x70) tmp[i * 6] = 0xD0;
            else if (tmp[i * 6] == 0x90) tmp[i * 6] = 0xD1;
        }
        mh_cheat_codes_to_text(tmp, _n, &b);
        free(tmp);
    }
    NSString *s = S(mh_wbuf_cstr(&b));
    mh_wbuf_free(&b);
    return [s stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
}

- (void)build {
    NSWindow *w = NewWindow(@"Editor de Cheats", NSMakeSize(704, 520), NSZeroSize, NO, NO);
    w.delegate = self;
    MHFlipView *root = [[MHFlipView alloc] initWithFrame:NSMakeRect(0, 0, 704, 520)];
    w.contentView = root;
    NSTextField *desc = Label(@"Escolha o formato de entrada. O Manager converte o classico para V5 no SD, mas preserva a "
                               "origem: ao editar novamente, USUARIO CLASSICO continua aparecendo como Classico.", YES);
    NSTextField *nameL = Label(@"Nome:", NO);
    _nameField = Edit(_initName, nil);
    NSTextField *verL = Label(@"Versao:", NO);
    _version = Popup();
    [_version addItemWithTitle:@"GameShark V5 / Xploder V4 (nativo)"];
    [_version addItemWithTitle:@"GameShark / Action Replay classico (converter)"];
    [_version selectItemAtIndex:_lines ? ((_flags >> 6) & 1) : 1];
    NSScrollView *codesSV = TextBox(NO, [self initialCodeText]);
    _codes = codesSV.documentView;
    NSButton *save = Btn(@"Salvar", self, @selector(save:));
    save.keyEquivalent = @"\r";
    NSButton *cancel = Btn(@"Cancelar", self, @selector(cancel:));
    cancel.keyEquivalent = @"\033";
    for (NSView *v in @[ desc, nameL, _nameField, verL, _version, codesSV, save, cancel ]) [root addSubview:v];
    root.onSize = ^(NSSize s) {
        CGFloat W = s.width, H = s.height;
        desc.frame = NSMakeRect(12, 10, W - 24, 42);
        nameL.frame = NSMakeRect(12, 59, 90, 18);
        self.nameField.frame = NSMakeRect(105, 55, W - 117, 24);
        verL.frame = NSMakeRect(12, 91, 90, 20);
        self.version.frame = NSMakeRect(105, 87, W - 117, 26);
        codesSV.frame = NSMakeRect(12, 123, W - 24, H - 177);
        save.frame = NSMakeRect(W - 202, H - 44, 90, 28);
        cancel.frame = NSMakeRect(W - 104, H - 44, 90, 28);
    };
    root.onSize(root.bounds.size);
    _window = w;
    gEditor = self;
}

- (void)windowWillClose:(NSNotification *)n {
    _done = YES;
    [NSApp stopModal];
}

- (void)cancel:(id)sender { [_window close]; }

/* orig: editor_save.isra.0 */
- (void)save:(id)sender {
    NSInteger sel = _version.indexOfSelectedItem;
    const char *dialect;
    if (sel == 0) dialect = DIALECT_V5;
    else if (sel == 1) dialect = DIALECT_CLASSIC;
    else { MsgErr(@"Escolha a versao dos codigos."); return; }

    char name[MH_CHEAT_NAME_MAX];
    NSString *nm = _nameField.stringValue;
    if (nm.length > 255) nm = [nm substringToIndex:255];
    mh_normalize_title(nm.UTF8String, name, sizeof name);
    if (!name[0]) { MsgErr(@"Informe o nome do cheat."); return; }

    NSString *text = [_codes.string stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
    text = [text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r\n"];
    if (text.length < 1 || text.length >= 0xFFFF) { MsgErr(@"Informe os codigos do cheat."); return; }

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
        if (mh_str_icmp(head, dialect) != 0) {
            MsgErr(@"A versao escrita no texto diverge da versao escolhida acima.");
            return;
        }
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
    if (!ok) { MsgErr(S(err)); return; }
    mh_str_copy(_outName, sizeof _outName, name);
    _outLines = lines;
    _outN = n;
    _outFlags = sel == 1 ? 0x40 : 0;
    _accepted = YES;
    [_window close];
}

- (void)testAction:(NSString *)a argument:(id)arg {
    if ([a isEqualToString:@"name"]) _nameField.stringValue = arg;
    else if ([a isEqualToString:@"codes"]) _codes.string = arg;
    else if ([a isEqualToString:@"version"]) [_version selectItemAtIndex:[arg integerValue]];
    else if ([a isEqualToString:@"save"]) [self save:nil];
    else if ([a isEqualToString:@"cancel"]) [self cancel:nil];
}

/* orig: run_editor. outLines is malloc'ed (mh_free) on YES. */
+ (BOOL)runWithParent:(NSWindow *)parent name:(const char *)name lines:(const uint8_t *)lines n:(uint16_t)n
                flags:(uint8_t)flags outName:(char *)outName outLines:(uint8_t **)outLines outN:(uint16_t *)outN
             outFlags:(uint8_t *)outFlags {
    MHCheatEditor *ed = [[MHCheatEditor alloc] initWithParent:parent name:name lines:lines n:n flags:flags];
    CenterOn(ed.window, parent);
    [NSApp runModalForWindow:ed.window];
    if (!ed->_accepted) { free(ed->_outLines); return NO; }
    mh_str_copy(outName, MH_CHEAT_NAME_MAX, ed->_outName);
    *outLines = ed->_outLines;
    *outN = ed->_outN;
    *outFlags = ed->_outFlags;
    return YES;
}
@end

/* Save panel format chooser: orig filters "Cheats (*.cht)" and "Texto (*.txt)", default extension cht. */
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

static NSURL *RunPanel(BOOL save, NSString *title) {
    if (MHCheatUIPanelHook) return MHCheatUIPanelHook(save);
    UTType *cht = [UTType typeWithFilenameExtension:@"cht"], *txt = [UTType typeWithFilenameExtension:@"txt"];
    if (save) {
        NSSavePanel *p = [NSSavePanel savePanel];
        p.title = title;
        p.canCreateDirectories = YES;
        p.nameFieldStringValue = @"cheats.cht";
        if (cht) p.allowedContentTypes = @[ cht ];
        MHFormatPicker *pick = [[MHFormatPicker alloc] init];
        pick.panel = p;
        NSPopUpButton *pop = Popup();
        [pop addItemsWithTitles:@[ @"Cheats (*.cht)", @"Texto (*.txt)" ]];
        pop.target = pick;
        pop.action = @selector(changed:);
        NSView *acc = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 40)];
        NSTextField *l = Label(@"Formato:", NO);
        l.frame = NSMakeRect(10, 10, 60, 20);
        pop.frame = NSMakeRect(72, 7, 220, 26);
        [acc addSubview:l];
        [acc addSubview:pop];
        p.accessoryView = acc;
        return [p runModal] == NSModalResponseOK ? p.URL : nil;
    }
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.title = title;
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    if (cht) [types addObject:cht];
    if (txt) [types addObject:txt];
    p.allowedContentTypes = types;
    p.canChooseDirectories = NO;
    p.allowsMultipleSelection = NO;
    return [p runModal] == NSModalResponseOK ? p.URL : nil;
}

/* ---- Cheats do Disco (orig: cheat_ui_edit + ui_proc) ------------------------------------------- */

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

static NSPasteboardType const kRowType = @"br.picostation.multihouse.cheatrow";

@interface MHCheatCatalog : NSObject
- (instancetype)initWithManager:(id)mgr;
- (void)run;
@end

@interface MHCheatManager : NSObject <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate,
                                      NSControlTextEditingDelegate, MHCheatUITestable>
@property NSWindow *window, *parent;
@property(nonatomic) const MhCheatDb *db;
@property MhCheatProfile *profile; /* the working clone (orig ctx+0x58) */
@property uint8_t scope;           /* orig ctx+0x74 */
@property uint32_t variant;        /* orig ctx+0x1f8 */
@property BOOL accepted, done;
@property NSMutableArray<NSNumber *> *rows;
@property MHTable *table;
@property NSScrollView *tableSV;
@property NSTextField *search, *status, *titleLabel;
@property NSTextView *details;
- (unsigned)budget;
- (void)refresh;
- (void)selectProfileIndex:(NSInteger)idx;
@end

@implementation MHCheatManager
- (NSWindow *)testWindow { return _window; }
- (unsigned)budget { return _scope == MH_CHEAT_UI_SCOPE_DISC ? MH_CHEAT_BUDGET_DISC : MH_CHEAT_BUDGET_CASE; }

- (void)dealloc { mh_cheat_profile_free(_profile); }

- (void)build:(NSString *)header {
    _rows = [NSMutableArray array];
    NSWindow *w = NewWindow(TITLE_CHEATS, NSMakeSize(944, 621), NSMakeSize(844, 521), YES, YES);
    w.delegate = self;
    MHFlipView *root = [[MHFlipView alloc] initWithFrame:NSMakeRect(0, 0, 944, 621)];
    w.contentView = root;
    _titleLabel = Label(header, YES);
    NSButton *choose = Btn(@"Escolher jogo / cheats", self, @selector(choose:));
    NSTextField *srchL = Label(@"Buscar:", NO);
    _search = Edit(@"", self);
    _tableSV = MakeTable(@[ @[ @"act", @"Ativo", @62, @YES ], @[ @"name", @"Cheats", @460, @NO ],
                                     @[ @"src", @"Origem", @132, @YES ], @[ @"lines", @"Linhas", @58, @YES ] ],
                         YES, self);
    _table = _tableSV.documentView;
    __weak MHCheatManager *ws = self;
    _table.onDelete = ^{ [ws remove:nil]; };
    ((MHHeader *)_table.headerView).onDouble = ^(NSInteger col) { if (col == 1) [ws sortByName]; };
    [_table registerForDraggedTypes:@[ kRowType ]];
    [_table setDraggingSourceOperationMask:NSDragOperationMove forLocal:YES];
    NSScrollView *detSV = TextBox(YES, @"Selecione um cheat para consultar as orientacoes disponiveis.");
    _details = detSV.documentView;
    _status = Label(@"", NO);
    _status.lineBreakMode = NSLineBreakByTruncatingTail;
    NSButton *bNew = Btn(@"Novo", self, @selector(newCheat:));
    NSButton *bEdit = Btn(@"Editar", self, @selector(editCheat:));
    NSButton *bTog = Btn(@"Ativar / Desativar", self, @selector(toggle:));
    NSButton *bDel = Btn(@"Excluir", self, @selector(remove:));
    NSButton *bImp = Btn(@"Importar", self, @selector(importCheats:));
    NSButton *bExp = Btn(@"Exportar", self, @selector(exportCheats:));
    NSButton *bOk = Btn(@"Aplicar", self, @selector(accept:));
    bOk.keyEquivalent = @"\r";
    NSButton *bCancel = Btn(@"Cancelar", self, @selector(cancel:));
    bCancel.keyEquivalent = @"\033";
    for (NSView *v in @[ _titleLabel, choose, srchL, _search, _tableSV, detSV, _status, bNew, bEdit, bTog, bDel, bImp,
                         bExp, bOk, bCancel ])
        [root addSubview:v];
    root.onSize = ^(NSSize s) {
        MHCheatManager *me = ws;
        CGFloat W = s.width, H = s.height, bw = W - 24, by = H - 48;
        me.titleLabel.frame = NSMakeRect(12, 12, W - 256, 38);
        choose.frame = NSMakeRect(W - 232, 12, 220, 28);
        srchL.frame = NSMakeRect(12, 62, 62, 20);
        me.search.frame = NSMakeRect(74, 58, W - 86, 25);
        me.tableSV.frame = NSMakeRect(12, 90, bw, H - 278);
        detSV.frame = NSMakeRect(12, H - 181, bw, 72);
        me.status.frame = NSMakeRect(12, H - 101, bw, 24);
        bNew.frame = NSMakeRect(12, by, 70, 28);
        bEdit.frame = NSMakeRect(88, by, 70, 28);
        bTog.frame = NSMakeRect(164, by, 124, 28);
        bDel.frame = NSMakeRect(294, by, 82, 28);
        bImp.frame = NSMakeRect(382, by, 82, 28);
        bExp.frame = NSMakeRect(470, by, 92, 28);
        bOk.frame = NSMakeRect(W - 212, by, 96, 28);
        bCancel.frame = NSMakeRect(W - 108, by, 96, 28);
        [me fitColumns];
    };
    root.onSize(root.bounds.size);
    _window = w;
    gManager = self;
    [self refresh];
}

- (void)fitColumns {
    CGFloat avail = _tableSV.contentSize.width - 260;
    NSArray *cols = _table.tableColumns;
    if (cols.count < 4) return;
    [cols[0] setWidth:62];
    [cols[1] setWidth:MAX(300, avail)];
    [cols[2] setWidth:132];
    [cols[3] setWidth:58];
}

- (void)windowWillClose:(NSNotification *)n {
    _done = YES;
    [NSApp stopModal];
}

- (const MhCheatEntry *)entryAtProfileIndex:(NSInteger)i {
    if (!_profile || i < 0 || i >= (NSInteger)_profile->count) return NULL;
    return &_profile->entries[i];
}

/* orig: ui_profile */
- (void)refresh {
    const char *flt = _search.stringValue.UTF8String ?: "";
    [_rows removeAllObjects];
    for (uint32_t i = 0; _profile && i < _profile->count; i++)
        if (mh_cheat_filter_match(_profile->entries[i].name, flt)) [_rows addObject:@(i)];
    [_table deselectAll:nil];
    [_table reloadData];
    [self fitColumns];
    uint32_t active = 0;
    for (uint32_t i = 0; _profile && i < _profile->count; i++)
        if (_profile->entries[i].flags & MH_CHEAT_F_ENABLED) active += _profile->entries[i].nlines;
    MhWbuf b;
    mh_wbuf_init(&b);
    mh_wbuf_puts(&b, "Catalogo: ");
    mh_wbuf_u32(&b, _profile ? _profile->count : 0);
    mh_wbuf_puts(&b, " cheats | Espaco: ");
    mh_wbuf_u32(&b, mh_cheat_profile_serialized_size(_profile));
    mh_wbuf_puts(&b, " / ");
    mh_wbuf_u32(&b, [self budget]);
    mh_wbuf_puts(&b, " bytes | Linhas ativas: ");
    mh_wbuf_u32(&b, active);
    mh_wbuf_puts(&b, " / 31");
    _status.stringValue = S(mh_wbuf_cstr(&b));
    mh_wbuf_free(&b);
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

/* orig: ui_details */
- (void)showDetails {
    NSInteger i = [self firstSelected];
    const MhCheatEntry *e = [self entryAtProfileIndex:i];
    _details.string = e ? S(mh_cheat_entry_hardware_note())
                        : @"Selecione um cheat para consultar as orientacoes disponiveis.";
}

#pragma mark table

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv { return (NSInteger)_rows.count; }
- (id)tableView:(NSTableView *)tv objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    const MhCheatEntry *e = [self entryAtProfileIndex:_rows[row].integerValue];
    if (!e) return @"";
    NSString *ident = col.identifier;
    if ([ident isEqualToString:@"act"]) return (e->flags & MH_CHEAT_F_ENABLED) ? @"SIM" : @"NAO";
    if ([ident isEqualToString:@"name"]) return S(e->name);
    if ([ident isEqualToString:@"src"]) {
        if (e->flags & MH_CHEAT_F_CUSTOM) return (e->flags & MH_CHEAT_F_SIMPLE) ? @"USUARIO CLASSICO" : @"USUARIO V5";
        return @"BIBLIOTECA";
    }
    return [NSString stringWithFormat:@"%u", e->nlines];
}
- (void)tableViewSelectionDidChange:(NSNotification *)n { [self showDetails]; }

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
    }
}

- (void)controlTextDidChange:(NSNotification *)n {
    if (n.object == _search) [self refresh];
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
        MsgErr(S(err));
        mh_cheat_profile_free(clone);
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
    if (![MHCheatEditor runWithParent:_window name:"NOVO CHEAT" lines:NULL n:0 flags:0 outName:name outLines:&lines
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
        return;
    }
    char err[256] = "";
    NSString *msg = @"O novo cheat ultrapassa o espaco ou o limite de 31 linhas ativas.";
    (void)mh_cheat_profile_validate(_profile, err, sizeof err); /* orig overwrote the message anyway */
    MsgErr(msg);
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
    if (!c) { free(lines); MsgErr(@"Memoria insuficiente."); return; }
    MhCheatEntry *x = &c->entries[i];
    free(x->lines);
    x->lines = lines;
    x->nlines = n;
    mh_str_copy(x->name, sizeof x->name, name);
    x->game = 0;
    x->variant = MH_NONE;
    x->id = MH_NONE;
    x->flags = (uint8_t)((x->flags & 0xA5) | oflags | 0x24);
    [self commit:c select:-1];
}

/* orig: Ativar / Desativar (0x13f1) */
- (void)toggle:(id)sender {
    NSInteger i = [self firstSelected];
    const MhCheatEntry *e = [self entryAtProfileIndex:i];
    if (!e) return;
    if ((e->flags & (MH_CHEAT_F_ENABLED | MH_CHEAT_F_MANDATORY)) == 3 &&
        !AskYesNo(@"Este codigo foi marcado como obrigatorio pela fonte. Deseja transforma-lo em personalizado e desativa-lo?"))
        return;
    MhCheatProfile *c = mh_cheat_profile_clone(_profile);
    if (!c) { MsgErr(@"Memoria insuficiente."); return; }
    MhCheatEntry *x = &c->entries[i];
    uint8_t f = x->flags;
    if ((f & 3) == 3) {
        x->game = 0;
        f = (uint8_t)((f & 0xE5) | 0x24);
        x->variant = MH_NONE;
        x->id = MH_NONE;
    }
    x->flags = f ^ 1;
    [self commit:c select:i];
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
    if (mandatory && !AskYesNo(@"A selecao inclui um codigo obrigatorio. Remover pode impedir outros cheats de funcionar. Remover os selecionados?"))
        return;
    for (NSUInteger i = idx.lastIndex; i != NSNotFound; i = [idx indexLessThanIndex:i]) mh_cheat_profile_remove(_profile, (uint32_t)i);
    [self refresh];
    if (_profile->count) [self selectProfileIndex:MIN((NSUInteger)first, (NSUInteger)_profile->count - 1)];
}

/* orig: ui_import */
- (void)importCheats:(id)sender {
    NSURL *u = RunPanel(NO, @"Importar cheats personalizados");
    if (!u) return;
    NSInteger a = MsgBox(@"Versao dos Cheats",
                         @"Qual versao dos codigos neste arquivo?\n\nSIM: GameShark / Action Replay classico (converter)\n"
                          "NAO: GameShark V5 / Xploder V4 (nativo)\nCANCELAR: nao importar",
                         NSAlertStyleWarning, @[ @"Sim", @"Nao", @"Cancelar" ], 0);
    if (a != 0 && a != 1) return;
    char err[256] = "";
    int imported = 0;
    if (!mh_cheat_profile_import_text_dialect(_profile, u.fileSystemRepresentation, a == 0 ? DIALECT_CLASSIC : DIALECT_V5,
                                              &imported, err, sizeof err)) {
        MsgErr(S(err));
        return;
    }
    [self refresh];
}

/* orig: ui_export */
- (void)exportCheats:(id)sender {
    if (!_profile || !_profile->count) { MsgInfo(@"A lista esta vazia."); return; }
    NSURL *u = RunPanel(YES, @"Exportar cheats do disco");
    if (!u) return;
    char err[256] = "";
    if (!mh_cheat_profile_export_text(_profile, u.fileSystemRepresentation, err, sizeof err)) { MsgErr(S(err)); return; }
    MsgInfo(@"Lista exportada com sucesso.");
}

/* orig: ui_accept */
- (void)accept:(id)sender {
    char err[256] = "";
    if (!mh_cheat_profile_validate_hardware(_profile, err, sizeof err)) { MsgErr(S(err)); return; }
    if (mh_cheat_profile_serialized_size(_profile) > [self budget]) {
        MsgErr(@"A selecao excede o espaco reservado no disco. Remova grupos antes de aplicar; nenhum grupo sera cortado.");
        return;
    }
    _accepted = YES;
    [_window close];
}
- (void)cancel:(id)sender { [_window close]; }

/* orig: Escolher jogo / cheats (0x13f7) */
- (void)choose:(id)sender {
    if (!_db || !_db->loaded) {
        MsgInfo(@"O banco GameShark V5 incorporado nao esta disponivel.");
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
    else if ([a isEqualToString:@"drag"]) {
        [self moveEntryFrom:[arg[0] unsignedIntValue] to:[arg[1] unsignedIntValue]];
        [self refresh];
        [self selectProfileIndex:[arg[1] integerValue]];
    }
}
@end

/* ---- Biblioteca de cheats (orig: catalog_proc, catalog_refresh, catalog_details, catalog_add) ---- */

@interface MHCheatCatalog () <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSControlTextEditingDelegate,
                              MHCheatUITestable>
@end

@implementation MHCheatCatalog {
    __weak MHCheatManager *_mgr;
    NSWindow *_window;
    NSPopUpButton *_combo;
    NSTextField *_search, *_info;
    MHTable *_table;
    NSScrollView *_tableSV;
    NSTextView *_details;
    NSMutableArray<NSNumber *> *_rows;
    NSMutableArray<NSString *> *_names, *_lineCounts;
}
- (NSWindow *)testWindow { return _window; }

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
    NSWindow *w = NewWindow(@"Biblioteca de cheats", NSMakeSize(784, 581), NSMakeSize(624, 401), YES, YES);
    w.delegate = self;
    MHFlipView *root = [[MHFlipView alloc] initWithFrame:NSMakeRect(0, 0, 784, 581)];
    w.contentView = root;
    NSTextField *gameL = Label(@"Jogo original:", NO);
    _combo = Popup();
    _combo.target = self;
    _combo.action = @selector(comboChanged:);
    NSMenuItem *first = [[NSMenuItem alloc] initWithTitle:@"-- Escolha manualmente o jogo GameShark V5 --" action:nil keyEquivalent:@""];
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
    NSTextField *srchL = Label(@"Buscar cheat:", NO);
    _search = Edit(@"", self);
    _tableSV = MakeTable(@[ @[ @"name", @"Grupo de cheats", @600, @NO ], @[ @"lines", @"Linhas", @70, @YES ] ], NO, self);
    _table = _tableSV.documentView;
    _table.doubleAction = @selector(add:);
    _table.target = self;
    NSScrollView *detSV = TextBox(YES, @"Selecione um cheat para consultar as orientacoes disponiveis.");
    _details = detSV.documentView;
    _info = Label(@"", NO);
    _info.lineBreakMode = NSLineBreakByTruncatingTail;
    NSButton *bAdd = Btn(@"Adicionar a selecao", self, @selector(add:));
    NSButton *bBack = Btn(@"Voltar", self, @selector(back:));
    bBack.keyEquivalent = @"\033";
    for (NSView *v in @[ gameL, _combo, srchL, _search, _tableSV, detSV, _info, bAdd, bBack ]) [root addSubview:v];
    __weak MHCheatCatalog *ws = self;
    root.onSize = ^(NSSize s) {
        MHCheatCatalog *me = ws;
        if (!me) return;
        CGFloat W = s.width, H = s.height, bw = W - 24;
        gameL.frame = NSMakeRect(12, 16, 94, 24);
        me->_combo.frame = NSMakeRect(108, 12, W - 120, 26);
        srchL.frame = NSMakeRect(12, 54, 86, 24);
        me->_search.frame = NSMakeRect(102, 50, W - 114, 26);
        me->_tableSV.frame = NSMakeRect(12, 86, bw, H - 254);
        detSV.frame = NSMakeRect(12, H - 160, bw, 72);
        me->_info.frame = NSMakeRect(12, H - 80, bw, 28);
        bAdd.frame = NSMakeRect(12, H - 44, 174, 28);
        bBack.frame = NSMakeRect(W - 112, H - 44, 100, 28);
        NSArray *cols = me->_table.tableColumns;
        if (cols.count == 2) {
            [cols[0] setWidth:MAX(100, me->_tableSV.contentSize.width - 70 - 8)];
            [cols[1] setWidth:70];
        }
    };
    root.onSize(root.bounds.size);
    _window = w;
    gCatalog = self;
    [self refresh];
}

- (void)run {
    CenterOn(_window, _mgr.window);
    [NSApp runModalForWindow:_window];
}
- (void)windowWillClose:(NSNotification *)n { [NSApp stopModal]; }
- (void)back:(id)sender { [_window close]; }

- (void)comboChanged:(id)sender {
    MHCheatManager *m = _mgr;
    m.variant = (uint32_t)[_combo.selectedItem.representedObject unsignedIntValue];
    [self refresh];
}

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
    if (m.variant == MH_NONE) _info.stringValue = @"Serial sem correspondencia automatica: escolha manualmente o jogo acima.";
    else _info.stringValue = [NSString stringWithFormat:@"%d grupos executaveis do GameShark V5 original. Maximo simultaneo: 31 linhas.", shown];
    [self details];
}

- (NSInteger)selectedEntry {
    NSInteger r = _table.selectedRow;
    return (r < 0 || r >= (NSInteger)_rows.count) ? -1 : _rows[r].integerValue;
}

/* orig: catalog_details. Prefix per text kind 0..4. */
- (void)details {
    NSInteger e = [self selectedEntry];
    if (e < 0) { _details.string = @"Selecione um cheat."; return; }
    static NSString *const pre[5] = {@"PT: ", @"\r\nEN: ", @"\r\nDescricao: ", @"\r\nDescription: ", @"\r\nOrigem / original: "};
    MHCheatManager *m = _mgr;
    NSMutableString *out = [NSMutableString string];
    static char buf[0x10000];
    for (int k = 0; k < 5; k++)
        if (mh_cheat_db_entry_text(m.db, m.variant, (uint32_t)e, k, buf, sizeof buf)) {
            [out appendString:pre[k]];
            [out appendString:S(buf)];
        }
    _details.string = [out stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
}

/* orig: catalog_add */
- (void)add:(id)sender {
    MHCheatManager *m = _mgr;
    NSInteger e = [self selectedEntry];
    if (e < 0) return;
    char err[256] = "";
    if (!mh_cheat_profile_add_catalog_entry(m.profile, m.db, m.variant, (uint32_t)e, [m budget], err, sizeof err)) {
        MsgErr(S(err));
        return;
    }
    [m refresh];
    MhWbuf b;
    mh_wbuf_init(&b);
    mh_wbuf_puts(&b, "Grupo preparado sem ativar. Uso: ");
    mh_wbuf_size(&b, mh_cheat_profile_serialized_size(m.profile));
    mh_wbuf_puts(&b, " de ");
    mh_wbuf_size(&b, [m budget]);
    mh_wbuf_putc(&b, '.');
    _details.string = S(mh_wbuf_cstr(&b));
    mh_wbuf_free(&b);
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tv { return (NSInteger)_rows.count; }
- (id)tableView:(NSTableView *)tv objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)row {
    return [col.identifier isEqualToString:@"name"] ? _names[row] : _lineCounts[row];
}
- (void)tableViewSelectionDidChange:(NSNotification *)n { [self details]; }
- (void)controlTextDidChange:(NSNotification *)n {
    if (n.object == _search) [self refresh];
}

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
