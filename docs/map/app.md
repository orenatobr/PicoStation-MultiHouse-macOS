# Module `app`: main window (AppKit)

Code in `app/`. Cheat windows are `app/cheat_ui.*` (other engineer). Original -> port:

| original | port |
|---|---|
| app_main, WinMainCRTStartup | `main.m`, `MHAppDelegate`, `MHApp -startup`, `MHMainController -start` |
| main_proc WM_CREATE | `MHMainController -buildWindow` (ids kept as control tags/identifiers) |
| main_proc WM_SIZE, WM_GETMINMAXINFO | `-layoutAll` (same arithmetic), `contentMinSize` 1104x661 (outer 0x460x0x2bc) |
| main_proc WM_CLOSE / WM_DESTROY | `-windowShouldClose:`, `-shouldQuit`, `applicationShouldTerminate:` |
| WM_NOTIFY (selection, key, dblclick, begin drag) | table delegate, `MHListOwner` (`MHViews.m`) |
| list_custom_draw | `-tableView:willDisplayCell:` (blue 0x0021b4 written, red 0xc40000 "OBS") |
| preview_proc / PSTPreview | `MHPreviewView` (alpha ignored like StretchDIBits: transparent art pixels are black) |
| drag_start, drag_list_row, drag_show_line, drag_cancel, drag_ghost_proc, drop_line_proc, item_rect_in_main, lv_hit | `-list:beginDragRow:event:`, `-dragRowIn:`, `-showLineIn:`, `-dragCancel`, `MHGhostPanel`, `MHDropLine` |
| lv_param, lv_select_param | `MHRow.param`, `-selectRowOf:param:` |
| refresh_cases/discs/fields/footer | `-refreshCases/-refreshDiscs/-refreshFields/-refreshFooter` (rows cached so workers never race the lists) |
| refresh_sd, load_selected_v2, WM_DEVICECHANGE | `-refreshSD`, `-applyDisks:`, `-loadSelectedV2:`, DiskArbitration watch |
| set_busy, set_status, progress_cb, pump_messages | `-setBusy:`, `-setStatus:`, `-queueProgress:`, `-startJob:done:` (worker queue) |
| add_dialog_files, add_cue_path, process_drop, finish_batch, admit_cue_disc, common_ask/locate | `-addDialogFilesTarget:`, `-addCuePath:`, `-importPaths:target:`, `-finishBatchOK:`, hooks |
| apply_case_name/apply_disc_name | `-applyCaseName/-applyDiscName` (on end editing) |
| cmd_remove_disc, project removal by Delete | `-cmdRemoveDisc`, `-removeCase` |
| cmd_defaults + menu_defaults_dialog/window_proc/populate/choices | `-cmdDefaults:`, `MHMenuDefaultsDialog` |
| cmd_image_open/save_as, Imagem popup, write_image_direct_to | `-cmdImageMenu:` and friends |
| cmd_write, confirm_proc, all_ready, write_failure | `-doWrite`, `MHConfirmWrite`, `-allReady:`, `-writeFailure:` |
| recode_disc, manager_recode_dialog | `-recodeDisc` |
| recover_disc_serial_internal | `-recoverSerial:silent:` (uses mh_psserial_extract*) |
| show_technical_details, cmd_cheats, make_custom_art | `-showTechnicalDetails`, `-cmdCheats`, `-makeCustomArtForCase:` |
| center_window_on_owner | modal windows are centred on the parent in `MHDialogs.m` |

## Deviations
* Operations run on a serial worker queue; the whole batch holds busy state (original toggled it per disc).
* Context menu of discs: double click as in the original, plus right click.
* Confirmation shows the BSD name (`disk6`) where the original printed `PhysicalDriveN`; other texts kept verbatim, including the Windows wording.
* Forced light appearance (original is light themed). Initial state is zoomed.
* `MHBridge.m` holds placeholders for v2 load/write, manager_admit, recode, hydrate until those modules land.
* Testing aids: `--add`, `--cmd`, `--auto-answer`, `--quit`, env `MH_NO_ZOOM`, `MH_DATA_DIR`.
