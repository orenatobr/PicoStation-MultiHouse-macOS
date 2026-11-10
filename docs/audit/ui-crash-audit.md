# UI crash audit (app/, platform/mac/)

Scope: `app/*.m`, `app/*.h`, `app/cheat_ui.m`, `platform/mac/*.m`, plus the core calls they make with
Objective-C values. Date 2026-11-10, branch `feat/modern-ui`, macOS 27.2 arm64.
Line numbers refer to the working tree at the time of the audit (the UI files were being rewritten, so
they drift; the function names are stable).

## 1. The reported crash: confirmed

Reproduced with a snapshot of the tree in which the two thumbnail lines used
`r.artPath.fileSystemRepresentation` (the form in the user's report), launched as
`MH_NO_ZOOM=1 PicoStationMultiHouse --add "/Volumes/Renato/PicoStation BR/Xenogears (USA)" --auto-answer yes --quit`
with `MH_DATA_DIR` unset (Xenogears USA, serial SLUS-00669, has no default art file, so `art_path` is empty).
Exit status 133 (SIGTRAP) and a new `.ips` in DiagnosticReports. The exception, from the crash guard log:

```
Name: NSInvalidArgumentException
Reason: *** -[NSString fileSystemRepresentation]: Cannot form file system representation of empty string
 2 Foundation   -[NSFileManager fileSystemRepresentationWithPath:]
 3 PicoStationMultiHouse   -[MHDiscCellView configureWithRow:] + 312
 4 PicoStationMultiHouse   -[MHMainController tableView:viewForTableColumn:row:] + 332
 5 AppKit   -[NSTableView(NSTableViewViewBased) _delegate_viewForTableColumn:row:]
 ...
 12 AppKit   -[NSTableRowData _updateVisibleRowEntries]
 13 AppKit   -[NSTableView layout]
```

The `.ips` only shows the unwound thread (`+[NSApplication _crashOnException:]` then layout frames), the
reason text is not in it. lldb (`br set -n objc_exception_throw`, `po $x0`) printed the same reason.
Fix: commit 68e11a6 (`UTF8String` plus `MHTheme artImageAtPath:` which returns nil for empty paths). Both
`--add` of Xenogears and of Alundra pass on the fixed tree.

Side effect found while testing: on macOS 26 and later AppKit calls `+[NSApplication _crashOnException:]`
for exceptions raised during layout and event handling. That path traps (SIGTRAP) without calling the
function installed with `NSSetUncaughtExceptionHandler` and without `-[NSApplication reportException:]`.
The crash guard (section 4) hooks it explicitly.

## 2. Findings, by priority

Status values: FIXED (commit), OPEN-UI (sent to the UI lead, his files), OPEN (no owner yet), NOTE.

### P1: reproducible or realistic crashes

| # | Where | Why it crashes | Fix | Status |
|---|---|---|---|---|
| 1 | `MHViews.m` `-[MHCaseCellView configureWithRow:]`, `-[MHDiscCellView configureWithRow:]` (`setArtPath:r.artPath.fileSystemRepresentation`) | Empty string raises NSInvalidArgumentException during table layout. Any case or disc without art. | Pass `UTF8String` (NULL for nil) and let `artImageAtCPath:` ignore empty or NULL. | FIXED 68e11a6 (UI lead) |
| 2 | `MHMainController.m` `loadImageAtPath:` (about :1426) and `writeImageTo:` (about :1462) | `path.fileSystemRepresentation` runs inside the worker block. An empty path raises on a GCD queue where nothing catches it: abort. Reproduced with `--cmd "open-image "`. Real users reach it only with an empty path, but the same pattern guards 12 other call sites. | `if (!path.length) return;` first, or one helper `MHFS(NSString*)` returning `""` for empty and use it everywhere `fileSystemRepresentation` is used (`:1163 :1343 :1355 :1398 :1404 :1430 :1477 :1481 :1723 :1862 :1889 :1894`). | OPEN-UI |
| 3 | `MHMainController.m` `-recodeDisc` (about :1854) and `-makeCustomArtForCase:` (about :1881) | The sheet completion block captures raw `MhDisc *` / `MhCase *`. While the sheet is open, menu items (New Project, Remove, Open Image) stay available, free the structs and the block then writes through a dangling pointer. | Capture `slot` and `curDisc` ints, re-resolve with `[_app caseAt:slot]` and a `disc_count` check inside the completion, bail out when `_app.busy`. | OPEN-UI |
| 4 | `MHMainController.m` `-doWrite` (about :1563) | `_disks[di]` with `di == -1` reads before the array. `mh_v2_write_precheck` reports "no card" today so it is not reached, but nothing local guarantees it. | `if (di < 0) return;` right after `selectedDiskIndex`. | OPEN-UI |
| 5 | `MHMainController.m` `-testCommand:` (about :2014 to :2108) | `a[1]`..`a[4]` read without a count check: `--cmd select-case` (also select-disc, drag-case, drag-disc, busy, progress, select-sd, write-device) raises NSRangeException. Reproduced (`FUZZ=1 tools/ui-smoke.sh`). Test aid only. | `#define ARG(n) (a.count > (n) ? a[n] : @"")`. | OPEN-UI |
| 6 | `MHMainController.m` `-dragProgrammaticKind:` (about :2130) | Negative `to` gives `row = MIN(to, count - 1)` below zero and `trows[row]` raises. Test aid only. | `if (from < 0 \|\| to < 0) return;` | OPEN-UI |

### P2: robustness, no reproduction yet

| # | Where | Why it can crash | Fix | Status |
|---|---|---|---|---|
| 7 | `MHMainController.m` `tableView:pasteboardWriterForRow:` (:1014) | `[self rowsFor:t][row]` without a range check. AppKit asks for rows it just counted, but the list can be rebuilt between layout and drag start. | Return nil outside `0..<count`. | OPEN-UI |
| 8 | `cheat_ui.m` `tableView:pasteboardWriterForRow:` (:1540) | Same pattern on `_rows[row]`. | Same guard. | FIXED ab38345 |
| 9 | `MHMainController.m` `tableView:acceptDrop:` (:1051) | Case and disc numbers come from the pasteboard string and are cast to `uint8_t` (-1 becomes 255). The core checks `in_use`, so today it is a no-op, not a crash. | Range check against `MH_PROJECT_MAX_CASES` and 255 before the call. | OPEN-UI |
| 10 | `MHMainController.m` (:1146, :1332) and `MHAppDelegate.m` `application:openURLs:` | `[paths addObject:u.path]`, nil `path` raises. | `if (u.path)`. | OPEN-UI |
| 11 | `MHMainController.m` `-addCuePath:target:` (about :1210) | Worker thread writes `_app.curCase/_app.curDisc` while the main thread reads them (selection change, refresh). Benign data race. | Set them in the `done:` block. | OPEN-UI |
| 12 | `MHPanes.m` `loadView` (:288) | `typeof(self) s = weak; s->_detailsScroll...` dereferences nil (SIGSEGV) if the controller is gone. | `if (!s) return;` | OPEN-UI |
| 13 | `MHApp.m` `find_data_dir` (:149) | `@(env)` is nil for a non UTF-8 `MH_DATA_DIR`; `_dataDir` becomes nil, every consumer passes NULL to `mh_path_join` (it tolerates NULL) so the data folder is silently wrong. | `NSString *e = @(env); if (e.length) return e;`. | OPEN-UI |
| 14 | `MHTheme.m` `artImageAtPath:` | Decodes art (ImageIO plus `mh_art_decode`) on the main thread inside `viewForTableColumn`. A huge or corrupt `.art` stalls the UI (hang, not a crash). Cached by size and mtime after the first decode. | Decode on a background queue, set the image on main. | OPEN-UI |

### P3: notes, verified safe

* `NSString.UTF8String` returns NULL (not a replacement character) for strings with lone surrogates,
  for example a user typing half an emoji or `substringToIndex:255` splitting a surrogate pair
  (verified with a small program). Every `UTF8String` that reaches C in `app/` goes to a NULL tolerant
  function (`mh_normalize_title`, `mh_str_starts_i`, `mh_wbuf_puts`, `mh_path_join` all check NULL), or
  to `printf("%s")` (prints `(null)`). Exceptions: `strcmp(..., bsd.UTF8String)` in the `write-device`
  and `select-sd` test commands (finding 5 territory).
* `MHStr()` uses `stringWithUTF8String:` with a NULL and nil check: safe.
* `MHLibRow` cell arrays (`r.cells = @[...]`) hold only non-nil strings (`MHStr` never returns nil).
* Table delegates: `numberOfRowsInTableView:`, `viewForTableColumn:`, `didAddRowView:`,
  `tableViewSelectionDidChange:` (selectedRow -1), `selectRowOf:`, `canMoveKind:`, `moveKind:` all
  range check. `refreshDiscs` handles `curDisc = -1` the way the original did.
* Core move and remove functions (`mh_project_move_disc`, `mh_project_reorder_case_index`,
  `mh_case_remove_disc`, `mh_project_remove_case`) take `uint8_t` and check `in_use` and counts.
  `mh_project_remove_case(p, slot)` indexes `cases[slot]` and `MH_PROJECT_MAX_CASES` is 256, so every
  `uint8_t` is a valid index.
* No KVO and no `NSNotificationCenter` observers are registered in `app/` besides the new crash-guard
  launch observer (block based, process lifetime). Delegates are `weak` (`MHListTable.owner`,
  `MHDropRootView.dropDelegate`), `MHProgressSheet.onCancel` captures `weak`, the other blocks capture
  `self` strongly but `MHMainController` lives for the whole process, so no leak that matters.
* Main thread rule: the worker queue (`mh.worker`) touches UI only through `MHMain()` (dispatch_sync to
  main) or `dispatch_async(main)` (`queueProgress:`, `startJob:done:`, `refreshSD`). `MHMain` is
  reentrant for main. No path waits on the worker from main, so no dispatch_sync deadlock.
* `cheat_ui.m`: `SpinUntil` loops until a sheet reports done; if a sheet window is closed without its
  completion running the loop never ends (hang). No known trigger. `gManager/gCatalog/gEditor` are
  `__weak`. `MoveEntry` validates both indices. `compute:` copes with a NULL `UTF8String`.
* `platform/mac/mh_image_mac.m`: `strlen(path)` assumes non-NULL; every caller passes a checked C path.
  Allocation sizes are bounded (`iw * ih <= SIZE_MAX / 4`) and `calloc`/`CGBitmapContextCreate` failure
  paths free the buffer.
* `platform/mac/mh_disk_mac.m`: `WholeSet.name[4]` is filled at most 3 plus 1; all CF values are type
  checked before use (`CFGetTypeID`); `unmount_cb` context is heap allocated and intentionally leaked
  on timeout; `mount_cb` uses `__bridge_transfer` against `__bridge_retained`. No change needed.
* `MHAppDelegate` `applicationWillTerminate:` frees the database while a worker could still run, but
  `shouldQuit` refuses to quit while `busy`; the test `--quit` path uses `exit(0)` and skips it.

## 3. How findings were checked

* Source read of every `app/*.m`, `app/cheat_ui.m`, `platform/mac/*.m`; grep for `fileSystemRepresentation`,
  `UTF8String`, `stringWithUTF8String`, `URLWithString`, `fileURLWithPath`, `pathExtension`,
  `lastPathComponent`, NSFileManager, `initWithContentsOfFile`, array literals, `objectAtIndex`, `[0]`/`[1]`
  indexing, `selectedRow`, `clickedRow`, `dispatch_`, `__weak`, observers.
* Empirical: `fileSystemRepresentation` on empty string raises and on nil returns NULL; `UTF8String` is
  NULL for lone surrogates; array index out of range raises NSRangeException.
* `tools/ui-smoke.sh` (with `FUZZ=1` for the test-hook cases).

## 4. Crash guard

Files: `app/MHCrashGuard.h`, `app/MHCrashGuard.m`, hook in `app/main.m` (commit 922bf3b).

* `MHCrashGuardInstall()` runs first in `main()`. It builds the log path
  `~/Library/Logs/PicoStation Multi House/crash-<yyyy-MM-dd-HHmmss>-<pid>.log` (the file is created only
  when something crashes), warms up `backtrace()`, installs `NSSetUncaughtExceptionHandler`, sets the
  default `NSApplicationCrashOnExceptions = NO`, replaces `+[NSApplication _crashOnException:]` with a
  wrapper that logs first (best effort: skipped when the private selector does not exist) and installs
  `sigaction` handlers for SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP on an alternate stack.
* Exception log: name, reason, userInfo, `callStackSymbols`, breadcrumbs. Signal log: signal number,
  `backtrace_symbols_fd` stack (async-signal-safe path: `open`, `write`, `backtrace`), breadcrumbs. After
  logging, the previous behaviour is kept (default signal action), so macOS still writes the `.ips`.
* `MHBreadcrumb(const char *)` keeps the last 16 notes in a static ring (no allocation, safe from a
  handler). Nothing in the app calls it yet; the UI lead can call it from `runNextCommand` and
  `testCommand:`.
* `MHCrashGuardOfferLog()` runs from the `NSApplicationDidFinishLaunchingNotification` observer in
  `main()`. If a `crash-*.log` newer than the file `.last-seen` exists, it shows an alert with
  "Reveal Log" (Finder selects the file) and "Dismiss", and touches `.last-seen` so it asks once per
  crash. With `MH_NO_ZOOM` set (tests) it only prints `[crash-guard] previous crash log: <path>` to stderr.
* Verified: the snapshot with the old bug now writes a log with the exact reason and the full stack
  down to `-[MHDiscCellView configureWithRow:]`, and the process still exits 133.

## 5. UI smoke test

`tools/ui-smoke.sh [APP|BINARY]` (default `$BUILD/PicoStation Multi House V3.app`, `BUILD` defaults to
`~/.cache/picostation-mh-build`). Run it from anywhere:

```
make app                                   # or: BUILD=/some/dir make app
tools/ui-smoke.sh                          # all scenarios
ONLY='xeno|empty' tools/ui-smoke.sh        # name filter (regex)
FUZZ=1 tools/ui-smoke.sh                   # plus the test-hook fuzz cases (several fail today, see findings 5 and 6)
STRICT=1 tools/ui-smoke.sh                 # missing input folders fail instead of SKIP
GAMES_DIR=... ART_DIR=... TIMEOUT=120 TIMEOUT_BIG=300 tools/ui-smoke.sh
```

Each scenario is one launch with `MH_NO_ZOOM=1`, `MH_DATA_DIR` unset (`nodata`) or set to `ART_DIR` (`art`),
`--auto-answer yes --quit`. A scenario fails when: the exit status is 128+n (crash), it runs longer than
`TIMEOUT` seconds (hang, killed with SIGKILL), output contains an NSException marker
(`Terminating app due to uncaught exception`, `NSInvalidArgumentException`, `NSRangeException`, ...), a new
`PicoStationMultiHouse*.ips`/`.crash` appears in `~/Library/Logs/DiagnosticReports`, a new crash guard log
appears, or an expected `report` line is missing. SIGTERM (143) is retried up to 3 times, because it is
sent by something outside the app (for example another session running `pkill`) and is never a crash.
Exit status 0 only when every scenario passed. Nothing in the matrix writes to a block device
(`write-device` and `save-image` are not used; the app reads disk metadata at startup as always).

Matrix: empty project (report, quit only, new-project, remove/select/move on empty, panes and appearance
toggles, sd-list/refresh-sd/open-image of a missing file), case without art (Xenogears, light and dark),
case with art (Alundra with `MH_DATA_DIR=ART_DIR`, and with it unset), multi-disc (Xenogears, Final Fantasy VII),
two cases with reorder and cross-case disc drags (including out of range rows), unusual names (empty, blank,
600 characters, unicode, format specifiers), odd selections (-1, 9999), remove everything / decline removal,
duplicate add, panes + sheets + focus + busy/progress + `shot` + `ax-dump`, appearance matrix, and ten
games at once.
