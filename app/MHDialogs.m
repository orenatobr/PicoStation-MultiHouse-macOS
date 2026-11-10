#import "MHDialogs.h"
#import "MHApp.h"
#import "MHViews.h"

static NSFont *ui_font(void) { return [NSFont systemFontOfSize:12]; }

static NSTextField *label(NSString *t, NSRect f) {
    NSTextField *l = [NSTextField labelWithString:t];
    l.frame = f;
    l.font = ui_font();
    l.lineBreakMode = NSLineBreakByWordWrapping;
    l.usesSingleLineMode = NO;
    return l;
}

static NSButton *button(NSString *t, NSRect f, id target, SEL sel, BOOL def) {
    NSButton *b = [NSButton buttonWithTitle:t target:target action:sel];
    b.frame = f;
    b.font = ui_font();
    b.bezelStyle = NSBezelStyleRounded;
    if (def) b.keyEquivalent = @"\r";
    return b;
}

static NSWindow *make_window(NSString *title, NSSize content, NSWindow *parent) {
    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, content.width, content.height)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.title = title;
    w.releasedWhenClosed = NO;
    MHRootView *root = [[MHRootView alloc] initWithFrame:NSMakeRect(0, 0, content.width, content.height)];
    w.contentView = root;
    if (parent) {
        NSRect pf = parent.frame;
        NSRect wf = w.frame;
        [w setFrameOrigin:NSMakePoint(NSMidX(pf) - wf.size.width / 2, NSMidY(pf) - wf.size.height / 2)];
    } else {
        [w center];
    }
    return w;
}

/* ================================================================ confirm_proc */

@interface MHConfirmController : NSObject <NSWindowDelegate>
@property(strong) NSWindow *window;
@property(strong) NSButton *check, *apply;
@property BOOL approved;
@end
@implementation MHConfirmController
- (void)toggled:(id)s { self.apply.enabled = self.check.state == NSControlStateValueOn; }
- (void)accept:(id)s {
    if (self.check.state != NSControlStateValueOn) return;
    self.approved = YES;
    [NSApp stopModal];
    [self.window orderOut:nil];
}
- (void)cancel:(id)s {
    self.approved = NO;
    [NSApp stopModal];
    [self.window orderOut:nil];
}
- (BOOL)windowShouldClose:(NSWindow *)w { [self cancel:nil]; return NO; }
@end

BOOL MHConfirmWrite(NSWindow *parent, NSString *text) {
    /* Outer size 720x430 in the original; client 704x391 here. */
    MHConfirmController *c = [[MHConfirmController alloc] init];
    NSWindow *w = make_window(@"Confirmar alteracoes no SD", NSMakeSize(704, 391), parent);
    c.window = w;
    w.delegate = c;
    w.level = NSFloatingWindowLevel; /* orig: WS_EX_TOPMOST */
    NSView *root = w.contentView;
    NSTextField *body = label(MHDisplay(text), NSMakeRect(20, 18, 660, 235));
    [root addSubview:body];
    c.check = [NSButton checkboxWithTitle:@"Confirmo que selecionei o cartao correto e autorizo a gravacao RAW."
                                   target:c action:@selector(toggled:)];
    c.check.frame = NSMakeRect(20, 265, 660, 48);
    c.check.font = ui_font();
    [c.check setAccessibilityLabel:@"Confirmo que selecionei o cartao correto e autorizo a gravacao RAW."];
    [root addSubview:c.check];
    c.apply = button(@"Aplicar alteracoes", NSMakeRect(425, 335, 155, 32), c, @selector(accept:), YES);
    c.apply.enabled = NO;
    [root addSubview:c.apply];
    NSButton *cancel = button(@"Cancelar", NSMakeRect(590, 335, 90, 32), c, @selector(cancel:), NO);
    cancel.keyEquivalent = @"\033";
    [root addSubview:cancel];
    [w makeFirstResponder:c.check];
    if (MHAlertHook) { /* tests: first button of a Sim/Nao hook means "approved" */
        return MHAlertHook(@"Confirmar alteracoes no SD", text, @[ @"Sim", @"Nao" ]) == 0;
    }
    [NSApp runModalForWindow:w];
    return c.approved;
}

/* ================================================================ menu_defaults_dialog */

@interface MHMenuDefaultsController : NSObject <NSWindowDelegate>
@property(strong) NSWindow *window;
@property(strong) NSPopUpButton *theme, *language, *visual;
@property(strong) NSButton *favorites, *music;
@property(strong) NSTextField *volume;
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
    self.volume.stringValue = [NSString stringWithFormat:@"%u", self.d.volume];
}
- (void)restore:(id)s {
    uint8_t r[MH_MENU_DEFAULTS_SIZE];
    mh_menu_defaults_factory(r);
    MhMenuDefaults d;
    if (mh_menu_defaults_parse(r, &d)) { self.d = d; [self populate]; }
}
/* orig: window_proc, id 0x1bc2. Up to 3 ASCII digits are required (anything else is ignored
 * silently), then 0..100. */
- (void)save:(id)s {
    NSString *t = self.volume.stringValue;
    if (t.length == 0 || t.length > 3) return;
    for (NSUInteger i = 0; i < t.length; i++) {
        unichar ch = [t characterAtIndex:i];
        if (ch < '0' || ch > '9') return;
    }
    int v = t.intValue;
    if (v > 100) {
        MHMessage(@"Padroes do menu", @"Informe um volume de 0 a 100.", MHIconWarning);
        return;
    }
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
    [self finish];
}
- (void)cancel:(id)s { [self finish]; }
- (void)finish { [NSApp stopModal]; [self.window orderOut:nil]; }
- (BOOL)windowShouldClose:(NSWindow *)w { [self finish]; return NO; }
@end

static NSPopUpButton *choices(NSView *root, NSString *name, CGFloat y, const char *const *items, int n,
                              int sel) {
    [root addSubview:label(name, NSMakeRect(18, y + 4, 140, 24))];
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(168, y, 250, 26) pullsDown:NO];
    p.font = ui_font();
    for (int i = 0; i < n; i++) [p addItemWithTitle:MHStr(items[i])];
    [p selectItemAtIndex:sel];
    [p setAccessibilityLabel:name];
    [root addSubview:p];
    return p;
}

BOOL MHMenuDefaultsDialog(NSWindow *parent, uint8_t rec[MH_MENU_DEFAULTS_SIZE]) {
    MhMenuDefaults d;
    if (!mh_menu_defaults_parse(rec, &d)) return NO; /* orig: invalid record: return 0 */
    MHMenuDefaultsController *c = [[MHMenuDefaultsController alloc] init];
    c.d = d;
    c.rec = rec;
    NSWindow *w = make_window(@"Padroes do menu", NSMakeSize(440, 371), parent);
    c.window = w;
    w.delegate = c;
    NSView *root = w.contentView;
    [root addSubview:label(@"Estes padroes serao gravados no menu.bin.\nUm save valido no Memory Card tem prioridade.",
                           NSMakeRect(18, 14, 410, 46))];
    c.theme = choices(root, @"Tema", 70, MH_MENU_THEMES, 3, d.theme);
    c.language = choices(root, @"Idioma", 110, MH_MENU_LANGUAGES, 3, d.language);
    c.visual = choices(root, @"Visual", 150, MH_MENU_VISUALS, 2, d.visual);
    c.favorites = [NSButton checkboxWithTitle:@"Iniciar na lista de favoritos" target:nil action:nil];
    c.favorites.frame = NSMakeRect(18, 200, 390, 25);
    c.favorites.font = ui_font();
    [root addSubview:c.favorites];
    c.music = [NSButton checkboxWithTitle:@"Musica do tema Umbrella" target:nil action:nil];
    c.music.frame = NSMakeRect(18, 235, 390, 25);
    c.music.font = ui_font();
    [root addSubview:c.music];
    [root addSubview:label(@"Volume (0 a 100)", NSMakeRect(18, 275, 140, 24))];
    c.volume = [[NSTextField alloc] initWithFrame:NSMakeRect(168, 271, 80, 27)];
    c.volume.font = ui_font();
    [c.volume setAccessibilityLabel:@"Volume (0 a 100)"];
    [root addSubview:c.volume];
    [root addSubview:button(@"Restaurar padroes", NSMakeRect(18, 322, 138, 32), c, @selector(restore:), NO)];
    [root addSubview:button(@"Salvar no BIN", NSMakeRect(178, 322, 122, 32), c, @selector(save:), YES)];
    NSButton *cancel = button(@"Cancelar", NSMakeRect(312, 322, 106, 32), c, @selector(cancel:), NO);
    cancel.keyEquivalent = @"\033";
    [root addSubview:cancel];
    [c populate];
    if (MHAlertHook) { /* tests: hook index 0 = "Salvar no BIN" keeping the shown values */
        if (MHAlertHook(@"Padroes do menu", @"", @[ @"Salvar no BIN", @"Cancelar" ]) == 0) {
            [c save:nil];
            return c.saved;
        }
        return NO;
    }
    [NSApp runModalForWindow:w];
    return c.saved;
}
