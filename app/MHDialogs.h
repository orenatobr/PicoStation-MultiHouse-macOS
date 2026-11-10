/* MHDialogs.h: the two modal windows the original built itself (confirm_proc and
 * menu_defaults_dialog + window_proc). */
#ifndef MH_APP_DIALOGS_H
#define MH_APP_DIALOGS_H

#import <Cocoa/Cocoa.h>
#include "mh/menu.h"

/* orig: cmd_write, the "Confirmar alteracoes no SD" window. YES when "Aplicar alteracoes" was
 * pressed (only possible after ticking the confirmation checkbox). */
BOOL MHConfirmWrite(NSWindow *parent, NSString *text);

/* orig: menu_defaults_dialog "Padroes do menu". rec is the 64 byte record, edited in place;
 * YES when "Salvar no BIN" produced a valid sealed record. */
BOOL MHMenuDefaultsDialog(NSWindow *parent, uint8_t rec[MH_MENU_DEFAULTS_SIZE]);

#endif
