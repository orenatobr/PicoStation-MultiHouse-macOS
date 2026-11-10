/* cheat_ui.h: the cheat windows of the original ("Cheats do Disco", "Biblioteca de cheats",
 * "Editor de Cheats"). Contract and map of original functions: docs/map/app-cheats.md. */
#ifndef MH_APP_CHEAT_UI_H
#define MH_APP_CHEAT_UI_H

#import <Cocoa/Cocoa.h>
#include "mh/cheat.h"

/* Scope values of the original (cheat_ui_edit param_7). Disc profiles get a 0x2800 byte budget,
 * everything else the 0x1400 byte Case budget. */
#define MH_CHEAT_UI_SCOPE_CASE 1
#define MH_CHEAT_UI_SCOPE_DISC 2

/* orig: cheat_ui_edit @0x140010300. Opens the modal "Cheats do Disco" window and runs it until
 * it is closed (it blocks like the original message loop).
 *
 *  parent      window to disable and centre on (may be nil)
 *  db          loaded embedded database (mh_cheat_db_load succeeded); nil or not loaded makes
 *              "Escolher jogo / cheats" answer "O banco GameShark V5 incorporado nao esta
 *              disponivel."
 *  headerText  the two line text above the list (orig param_5); nil or "" gives
 *              "Cheats do Disco". The window title is always "Cheats do Disco".
 *  serial      disc serial (orig param_6, ASCII like "SCUS-94163"); used to find the game in the
 *              database and stored in the profile (mh_cheat_profile_prepare_hardware). May be
 *              nil or empty.
 *  scope       MH_CHEAT_UI_SCOPE_DISC (budget 0x2800 bytes) or anything else (0x1400)
 *  profile     in/out. *profile may be NULL (empty start). The window works on a clone; only
 *              when "Aplicar" succeeds is *profile freed (mh_cheat_profile_free) and replaced
 *              by the edited clone. On cancel or close it is left untouched.
 *
 * Returns YES when the user applied (the original returned 1 and the caller marked the disc
 * dirty), NO otherwise (also when the clone could not be made, orig returned 0). */
BOOL MHCheatEditProfile(NSWindow *parent, const MhCheatDb *db, NSString *headerText,
                        const char *serial, uint8_t scope, MhCheatProfile **profile);

/* ---- test hooks (used by app/demo/cheat_ui_demo.m; leave NULL in the product) ---------------
 * If set, every message box goes through this block instead of NSAlert. title/text are the
 * original strings, buttons the captions ("OK" for single button boxes, "Sim","Nao" or
 * "Sim","Nao","Cancelar"). Return the 0 based index of the pressed button. */
extern NSInteger (^MHCheatUIAlertHook)(NSString *title, NSString *text, NSArray<NSString *> *buttons);
/* If set, replaces NSOpenPanel (save == NO) and NSSavePanel (save == YES); nil cancels. */
extern NSURL *(^MHCheatUIPanelHook)(BOOL save);

@protocol MHCheatUITestable <NSObject>
@property(readonly) NSWindow *testWindow;
/* actions: manager: select (NSArray of rows), search (NSString), toggle, edit, new, remove,
 * import, export, accept, cancel, choose, drag (NSArray @[from,to] profile indexes), sort.
 * catalog: select (NSArray of rows), search (NSString), combo (NSNumber item index), add,
 * back. editor: name, codes (NSString), version (NSNumber), save, cancel. */
- (void)testAction:(NSString *)action argument:(id)arg;
@end
/* Currently open window of the kind "manager", "catalog" or "editor", else nil. */
id<MHCheatUITestable> MHCheatUITestTarget(NSString *kind);

#endif
