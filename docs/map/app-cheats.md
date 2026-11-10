# app/cheat_ui: the cheat windows

Files: `app/cheat_ui.h` (public API), `app/cheat_ui.m` (three windows), `app/demo/cheat_ui_demo.m`
(standalone harness, `make cheat-demo`, kept out of `app/*.m` so the product target does not pick up its `main`).
Logic is all in `core/include/mh/cheat.h`; the UI only calls it.

## Entry point for the main window

```objc
#import "cheat_ui.h"
BOOL MHCheatEditProfile(NSWindow *parent, const MhCheatDb *db, NSString *headerText,
                        const char *serial, uint8_t scope, MhCheatProfile **profile);
```

Mirrors `cheat_ui_edit @0x140010300` (called by `cmd_cheats @0x140010f80`):

| orig param | here | notes |
|---|---|---|
| hwnd, hinst, font | `parent` | modal: runs `[NSApp runModalForWindow:]`, blocks until closed, centred on `parent` |
| param_4 db | `db` | the loaded embedded database (global `&DAT_140e7cc40` there). nil or `!db->loaded` makes "Escolher jogo / cheats" say "O banco GameShark V5 incorporado nao esta disponivel." |
| param_5 text | `headerText` | the 2 line label above the list; nil or "" gives "Cheats do Disco". `cmd_cheats` builds it as `<disc name>  |  Serial: <serial>` + newline + "Vinculo por serial. Use o botao ao lado para escolher outro perfil." (or "Serial ausente: e possivel editar uma lista personalizada, mas o menu nao podera fazer vinculo automatico por serial.") |
| param_6 serial | `serial` | ASCII "SCUS-94221" style; looks up the title (`mh_cheat_db_find_title_for_disc`, first variant), stored in the profile (`mh_cheat_profile_prepare_hardware`). nil or "" is fine |
| param_7 scope | `scope` | `MH_CHEAT_UI_SCOPE_DISC` (2): budget 0x2800 bytes; anything else (Case): 0x1400 |
| param_8 `profile**` | `profile` | in/out. `*profile` may be NULL. The window edits a clone (`mh_cheat_profile_clone`). On "Aplicar" the old `*profile` is freed and replaced by the clone. On Cancelar/close it is untouched |
| return int | `BOOL` | YES when applied (orig 1: caller then sets the disc dirty and calls `refresh_fields`), NO otherwise (also when the clone fails) |

If the original variant stored in the profile (`profile->variant`) is the only hint (serial not bound), it is used as the
catalog's current game. The header text of the original (`cmd_cheats`) needs the disc name and serial: the caller owns that.

## Original function map

| original | here |
|---|---|
| cheat_ui_register (3 window classes) | not needed (windows are built in code) |
| cheat_ui_edit | `MHCheatEditProfile` |
| ui_proc, WM_CREATE/WM_SIZE | `MHCheatManager -build:` (flipped layout block `onSize`, same numbers, client 944x621, min 844x521) |
| ui_profile | `-refresh` (rows filtered by `mh_cheat_filter_match(name, search)`; status line "Catalogo: n cheats | Espaco: x / budget bytes | Linhas ativas: a / 31") |
| ui_select_index / ui_details | `-selectProfileIndex:` / `-showDetails` (text = `mh_cheat_entry_hardware_note`) |
| ui_new_custom / run_editor | `-newCheat:` / `+[MHCheatEditor runWithParent:...]` (new cheat starts as "NOVO CHEAT", classic version preselected, inserted after the selection) |
| Editar (0x13f0) | `-editCheat:` (entry becomes custom: `flags = (flags & 0xA5) | classic(0x40) | 0x24`, game 0, variant/id -1) |
| Ativar / Desativar (0x13f1) | `-toggle:` (mandatory entry: YES/NO question, becomes custom and disabled) |
| ui_remove | `-remove:` (multi selection, mandatory warning, Delete key) |
| ui_import / ui_export | `-importCheats:` / `-exportCheats:` (NSOpenPanel *.cht, *.txt; question "Versao dos Cheats" Sim/Nao/Cancelar; NSSavePanel with a Cheats (*.cht) / Texto (*.txt) popup) |
| ui_accept / ui_commit_edit | `-accept:` / `-commit:select:` |
| ui_move_entry + LVN_BEGINDRAG/WM_LBUTTONUP | NSTableView drag and drop (drop ON a row moves the dragged entry to that row's index), `MoveEntry` |
| ui_header_proc | `MHHeader`: double click on the "Cheats" column header sorts by name (insertion sort, `mh_str_icmp`) |
| catalog_proc/refresh/details/add | `MHCheatCatalog` ("Biblioteca de cheats", 784x581 client, min 624x401) |
| editor_proc / editor_save | `MHCheatEditor` ("Editor de Cheats", fixed 704x520 client, no close button like WS_CAPTION only) |
| MessageBoxW | `MsgBox` (NSAlert, title = messageText, body = informativeText) |

`window_proc`, `choices.constprop.0` and `populate` are NOT cheat code: they are the "Padroes do menu" dialog
(menu defaults) and belong to the menu UI.

## Behaviours worth knowing

* The original list has no custom draw: no row colours in the cheat windows. Columns: Ativo (SIM/NAO), Cheats, Origem
  (BIBLIOTECA, USUARIO V5, USUARIO CLASSICO, from flags CUSTOM and SIMPLE), Linhas.
* "Linhas ativas: n / 31" is only a label in the original too (sum of lines of enabled entries).
* After "Novo" fails (space or active lines) the original always shows "O novo cheat ultrapassa o espaco ou o limite de
  31 linhas ativas." (it overwrites the validate message); reproduced.
* The editor saves with the dialect chosen in the combo; a pasted "MHDialect=" first line must match it ("A versao
  escrita no texto diverge da versao escolhida acima."). Name goes through `mh_normalize_title` (upper case, 63 chars).
* Catalog: double click or "Adicionar a selecao" calls `mh_cheat_profile_add_catalog_entry` on the manager's own profile
  (not a clone); the entry is prepared, not enabled.

## Deviations

* After Ativar/Desativar the toggled row stays selected (the original cleared the selection by rebuilding the list).
* The import panel offers only the Cheats filter (orig also had "Todos os arquivos"); the export panel's second format
  (.txt) only changes the file extension, the text written is the same.
* Messagebox icons are NSAlert styles, button captions "Sim", "Nao", "Cancelar", "OK".
* The embedded database of this repo has no Final Fantasy VII entry; the demo uses Final Fantasy Tactics (SCUS-94221).

## Test hooks (harness only)

`MHCheatUIAlertHook`, `MHCheatUIPanelHook` (replace NSAlert and the file panels) and `MHCheatUITestTarget(@"manager" |
@"catalog" | @"editor")` with `-testAction:argument:`. `make cheat-demo` builds `$(BUILD)/cheat-demo`; run it from the repo root
as `cheat-demo <shot-dir> [serial] [title substring]` for a scripted session that writes PNGs (via cacheDisplay, since
`screencapture -l` needs the Screen Recording permission), or without arguments to use the window by hand.
