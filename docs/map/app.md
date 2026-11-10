# Module `app`: main window (AppKit)

Code in `app/`. Cheat windows are `app/cheat_ui.*` (other engineer). The design brief is `docs/UI_DESIGN.md`;
all user-visible text is EN-US. The UI is built in code (no nibs), ARC, macOS 12+ with `@available` checks for newer APIs.

## Class structure

| file | classes | role |
|---|---|---|
| `main.m`, `MHAppDelegate.*` | `MHAppDelegate` | startup, menu bar (File, Edit, Library, View, Window, Help), launch arguments, test command runner |
| `MHApp.*` | `MHApp`, `MHMessage`, `MHAsk`, `MHConfirm`, `MHRunSheet` | shared state (project, db, env, data dirs), alerts as sheets on the main window (nested modal loop keeps callers synchronous), test hook `MHAlertHook` |
| `MHTheme.*` | `MHTheme`, `MHStatusChip`, `MHThumbnailView` | design tokens: dynamic colors (light, dark, Increase Contrast), fonts, 8 pt spacing, radii, SF Symbol helper, motion that honors Reduce Motion, cached `.art` decoder (`mh_art_decode`) |
| `MHViews.*` | `MHLibRow`, `MHListTable`, `MHCaseCellView`, `MHDiscCellView`, `MHDiscRowView`, `MHPillView`, `MHEmptyStateView`, `MHDropOverlay`, `MHPrimaryButton`, `MHBottomBar`, `MHDropRootView` | row model (cached, workers never race the lists), sidebar cell, disc card, empty state, "Drop to add" overlay, 52 pt bottom bar, Finder drop root |
| `MHPanes.*` | `MHSidebarViewController`, `MHContentViewController`, `MHInspectorViewController` | the three split view panes; views only, MHMainController is data source, delegate and action target |
| `MHDialogs.*` | `MHConfirmWrite`, `MHMenuDefaultsDialog`, `MHProgressSheet`, `MHShowDetails` | write confirmation sheet (raw write authorization checkbox), menu settings sheet, progress sheet with phases and result states, text details sheet |
| `MHMainController.*` | `MHMainController`, `MHShellViewController` | window, `NSSplitViewController` (sidebar, content, inspector), unified toolbar, jobs, project commands, test hooks |

Window: titled, full size content view, unified toolbar, minimum content 1000 x 640, default 1280 x 800, frame autosaved
(`MHMainWindow`), split divider autosaved, sidebar and inspector visibility kept in `NSUserDefaults`
(`MHSidebarHidden`, `MHInspectorHidden`). Tests (`MH_NO_ZOOM` set) neither read nor write the saved state.

## Original -> port

| original | port |
|---|---|
| app_main, WinMainCRTStartup | `main.m`, `MHAppDelegate`, `MHApp -startup`, `MHMainController -start` |
| main_proc WM_CREATE | `MHMainController -buildWindow` (control ids kept as `identifier`/`tag`) |
| main_proc WM_SIZE, WM_GETMINMAXINFO | Auto Layout, split view item thicknesses, `contentMinSize` |
| main_proc WM_CLOSE / WM_DESTROY | `-windowShouldClose:`, `-shouldQuit`, `applicationShouldTerminate:` |
| WM_NOTIFY (selection, key, dblclick) | table delegate, `MHListOwner`, `doubleAction` (rename) |
| list_custom_draw | `MHLibRow -statusKind`, `MHStatusChip` (icon plus word, never color alone) |
| preview_proc / PSTPreview | `MHThumbnailView` (alpha honored, placeholder symbol) |
| drag_start ... drop_line_proc | native `NSTableView` drag and drop (`MHRowPboardType`), `-moveCaseSlot:toInsertion:`, `-moveDisc:...` (same arithmetic as the original drop); `-moveItemUp:/Down:` for menu and keyboard |
| WM_DROPFILES, DragAcceptFiles | `MHDropRootView` + `MHDropDelegate`; sidebar groups by game, content adds discs to the selected case |
| refresh_cases/discs/fields/footer | `-refreshCases/-refreshDiscs/-refreshFields/-refreshFooter` |
| refresh_sd, load_selected_v2, WM_DEVICECHANGE | `-refreshSD`, `-applyDisks:`, `-loadSelectedV2:`, DiskArbitration watch |
| set_busy, set_status, progress_cb | `-setBusy:`, `-setStatus:`, `-queueProgress:`, `-applyProgress`, `-startJob:done:`; phase, step and steps come from `MhProgress` (`MH_V2_PHASE_*`) instead of parsing text |
| cmd_write, confirm_proc, write_failure | `-doWrite`, `MHConfirmWrite`, `MHProgressSheet`, `-writeFailure:` |
| cmd_defaults + menu_defaults_dialog | `-cmdDefaults:`, `MHMenuDefaultsDialog` (sheet, switches, slider) |
| cmd_image_open/save_as, Imagem popup | File menu and toolbar "Image" menu: `-imageOpen:`, `-imageSaveAs:`, `-imageApply:`, `-imageNew:` |
| recode_disc, recover_disc_serial_internal, show_technical_details, cmd_cheats, make_custom_art | `-recodeDisc`, `-recoverSerial:silent:`, `-showTechnicalDetails`, `-cmdCheats`, `-makeCustomArtForCase:` (all also in the Library menu and the disc context menu) |

## Behavior notes
* Progress: the write runs behind a progress sheet (phase title, "Step n of 6", determinate bar, current item, Cancel). From `MH_V2_PHASE_UPDATING_TABLES` on Cancel is disabled with the caption "Finishing safely". Rows turn "Written" when the phase leaves `WRITING_DISCS` or `step` changes.
* Every destructive action (remove case or disc, discard changes, erase the card) is a confirmation sheet with a red button.
* Keyboard: Option-Command-Up/Down reorder, Return or F2 rename, Delete removes, Command-Return creates or applies, Command-R refreshes cards, Option-Command-S and Option-Command-I toggle the panes.
* The primary button is custom drawn (`MHPrimaryButton`) so the brand fill is exact in every appearance.

## Testing aids
`--add <path>`, `--cmd "<command>"` (repeatable, in order), `--auto-answer yes|no` (confirmations are `[confirm, cancel]`, the hook logs `[alert] title: text` to stderr), `--quit`, env `MH_NO_ZOOM` (deterministic window, no saved state), `MH_DATA_DIR`.
Commands: `report`, `select-case N`, `select-disc N`, `drag-case from to after`, `drag-disc from d|c row after`, `remove-case`, `remove-disc`, `name-case TEXT`, `name-disc TEXT`, `shot <file>` (offscreen render), `wait-idle`, `sleep S`, `add-folder P`, `save-image P`, `open-image P`, `new-project`, `refresh-sd`, `sd-list`, `select-sd BSD`, `load-sd`, `write-device BSD`, `busy 0|1`, `progress N`.
New: `appearance light|dark|system`, `toggle-inspector`, `toggle-sidebar`, `ax-dump` (accessibility tree: role, label, help, value, identifier, plus toolbar items), `verify-on-add 0|1`, `drop-overlay on|off`, `resize W H`, `about`, `focus cases|discs|case-name|disc-name`, `move-up`, `move-down`, `demo-sheet confirm|progress|progress-late|success|failure|menu|alert|error|details` and `demo-sheet-close`.

Safety of `write-device`: only a disk offered by `mh_disk_enumerate`, never disk0/disk3/disk6, and only a disk image (`is_image`, use `MH_DISK_TEST_ALLOW_IMAGES=1` with `hdiutil attach -nomount`) unless `MH_TEST_ALLOW_REAL_DEVICE=1` is set. The normal user flow is unchanged.

## Deviations
* Operations run on a serial worker queue; the whole batch holds busy state (original toggled it per disc).
* Double click on a row renames it; the context menu is on right click and in the Library menu.
* Confirmation shows the BSD name (`disk6`) where the original printed `PhysicalDriveN`.
* Appearance follows the system (the original was light only).

## Screenshots
`docs/screenshots/`: `library-light.png`, `library-dark.png` (11 real games, a case and a disc selected), `empty-light.png`, `empty-dark.png`, `write-confirmation-light.png`, `write-progress-dark.png`.
