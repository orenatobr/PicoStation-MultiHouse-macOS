#!/bin/bash
# UI smoke test: launches the built app many times with the --add / --cmd / --auto-answer / --quit
# test interface and fails when a launch crashes, hangs, prints an NSException or leaves a crash report.
#
#   tools/ui-smoke.sh [APP|BINARY]        default: $BUILD/PicoStation Multi House V3.app
#   env: GAMES_DIR   folder with game folders (default "/Volumes/Renato/PicoStation BR")
#        ART_DIR     MH_DATA_DIR that holds cover art (default "/Users/renato.pereira/Downloads/PICOSTATION BR/Managers")
#        TIMEOUT     seconds per scenario before it counts as a hang (default 120)
#        ONLY=regex  run only the scenarios whose name matches
#        FUZZ=1      also run the test-hook fuzz scenarios (missing or negative arguments)
#        STRICT=1    a scenario whose input folder is missing fails instead of being skipped
#        KEEP=1      keep the per-scenario logs (always printed for failures)
# Exit status: 0 all passed, 1 at least one failure. It never writes to a block device: no scenario uses
# write-device, and Save Image is not used. The app itself only reads disk metadata (refresh-sd).
set -eu

BUILD="${BUILD:-$HOME/.cache/picostation-mh-build}"
TARGET="${1:-$BUILD/PicoStation Multi House V3.app}"
GAMES_DIR="${GAMES_DIR:-/Volumes/Renato/PicoStation BR}"
ART_DIR="${ART_DIR:-/Users/renato.pereira/Downloads/PICOSTATION BR/Managers}"
TIMEOUT="${TIMEOUT:-120}"
ONLY="${ONLY:-}"
FUZZ="${FUZZ:-0}"
STRICT="${STRICT:-0}"
KEEP="${KEEP:-0}"

if [ -d "$TARGET" ]; then BIN="$TARGET/Contents/MacOS/PicoStationMultiHouse"; else BIN="$TARGET"; fi
[ -x "$BIN" ] || { echo "ui-smoke: no executable at $BIN (run: make app)" >&2; exit 2; }

DIAG="$HOME/Library/Logs/DiagnosticReports"
GUARD="$HOME/Library/Logs/PicoStation Multi House"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ui-smoke.XXXXXX")"
trap '[ "$KEEP" = 1 ] || rm -rf "$WORK"' EXIT
NSEXC='Terminating app due to uncaught exception|NSInvalidArgumentException|NSRangeException|NSInternalInconsistencyException|NSGenericException|\*\*\* -\['

pass=0; fail=0; skip=0

list_reports() {
  { ls "$DIAG" 2>/dev/null | grep -E '^PicoStationMultiHouse.*\.(ips|crash)$' || true
    ls "$GUARD" 2>/dev/null | grep -E '^crash-.*\.log$' || true; } | sort
}

# scenario NAME EXPECT_REGEX|- ENVSPEC -- args...
#   ENVSPEC is "nodata" (MH_DATA_DIR unset) or "art" (MH_DATA_DIR=$ART_DIR)
scenario() {
  local name="$1" expect="$2" envspec="$3"; shift 4
  if [ -n "$ONLY" ] && ! echo "$name" | grep -Eq "$ONLY"; then return 0; fi
  local out="$WORK/$name.log" rc=0 before after problems=""
  before="$(list_reports)"
  local envs=(MH_NO_ZOOM=1)
  [ "$envspec" = art ] && envs+=("MH_DATA_DIR=$ART_DIR")
  # A SIGTERM is never a crash of the app: another process (a pkill from a parallel build, a
  # terminal closing) sent it. Run again up to 3 times; real crashes are SIGTRAP/SEGV/ABRT.
  local attempt
  for attempt in 1 2 3; do
    rc=0; rm -f "$out.hang"
    env -u MH_DATA_DIR "${envs[@]}" "$BIN" "$@" >"$out" 2>&1 &
    local pid=$!
    ( sleep "$TIMEOUT"; kill -9 "$pid" 2>/dev/null && echo hang >"$out.hang" ) 2>/dev/null &
    local wd=$!
    { wait "$pid"; } 2>/dev/null || rc=$?
    pkill -P "$wd" 2>/dev/null || true
    kill "$wd" 2>/dev/null || true
    { wait "$wd"; } 2>/dev/null || true
    [ "$rc" -eq 143 ] && [ ! -e "$out.hang" ] || break
    echo "      (killed by an outside SIGTERM, running $name again)"
  done
  sleep 1
  after="$(list_reports)"
  if [ -e "$out.hang" ]; then problems="$problems hang(>${TIMEOUT}s);"
  elif [ "$rc" -ge 128 ]; then problems="$problems crash(signal $((rc - 128)));"
  elif [ "$rc" -ne 0 ]; then problems="$problems exit=$rc;"; fi
  if grep -Eq "$NSEXC" "$out"; then problems="$problems NSException printed;"; fi
  local newrep; newrep="$(comm -13 <(echo "$before") <(echo "$after") | tr '\n' ' ')"
  [ -z "${newrep// /}" ] || problems="$problems new crash report: $newrep;"
  if [ "$expect" != - ] && ! grep -Eq "$expect" "$out"; then problems="$problems output lacks /$expect/;"; fi
  if [ -z "$problems" ]; then
    pass=$((pass + 1)); echo "PASS  $name"
  else
    fail=$((fail + 1)); echo "FAIL  $name:$problems"
    grep -v 'linkd\|\[Connection\]' "$out" | head -25 | sed 's/^/      | /'
  fi
}

need() { # need PATH...: true when every path exists
  for p in "$@"; do [ -e "$p" ] || return 1; done
}

with_input() { # with_input NAME PATH... -- scenario args...  (skips when a PATH is missing)
  local name="$1"; shift
  local paths=()
  while [ "$1" != -- ]; do paths+=("$1"); shift; done
  shift
  if need "${paths[@]}"; then scenario "$name" "$@"
  elif [ "$STRICT" = 1 ]; then fail=$((fail + 1)); echo "FAIL  $name: input missing: ${paths[*]}"
  else skip=$((skip + 1)); echo "SKIP  $name: input missing: ${paths[*]}"; fi
}

XENO="$GAMES_DIR/Xenogears (USA)"
FF7="$GAMES_DIR/Final Fantasy VII (USA)"
ALU="$GAMES_DIR/Alundra (USA) (Rev 1).cue"
LONG="$(printf 'L%.0s' $(seq 1 600))"
UNI='Ação 日本語 😀 é ñ'
ADD_X=(--add "$XENO")
ADD_A=(--add "$ALU")
TAIL=(--auto-answer yes --quit)

echo "ui-smoke: $BIN"

scenario empty-project 'cases=0 discs=0' nodata -- --cmd report "${TAIL[@]}"
scenario empty-quit-only - nodata -- --quit
scenario empty-new-project 'cases=0' nodata -- --cmd new-project --cmd report "${TAIL[@]}"
scenario empty-remove-and-select - nodata -- --cmd remove-disc --cmd remove-case --cmd "select-case 0" --cmd "select-disc 0" --cmd move-up --cmd move-down --cmd report "${TAIL[@]}"
scenario empty-panes - nodata -- --cmd toggle-sidebar --cmd toggle-inspector --cmd toggle-sidebar --cmd toggle-inspector --cmd "appearance dark" --cmd "appearance light" --cmd "appearance system" "${TAIL[@]}"
scenario empty-sd-and-bad-open - nodata -- --cmd sd-list --cmd refresh-sd --cmd "open-image /nonexistent/file.img" --cmd wait-idle "${TAIL[@]}"

with_input case-without-art "$XENO" -- 'cases=1' nodata -- "${ADD_X[@]}" --cmd report --cmd "select-case 0" --cmd "select-disc 0" --cmd report "${TAIL[@]}"
with_input case-without-art-dark "$XENO" -- 'cases=1' nodata -- "${ADD_X[@]}" --cmd "appearance dark" --cmd "select-case 0" --cmd "toggle-inspector" --cmd "toggle-inspector" --cmd "appearance light" --cmd report "${TAIL[@]}"
with_input case-with-art "$ALU" "$ART_DIR" -- 'cases=1' art -- "${ADD_A[@]}" --cmd report --cmd "select-case 0" --cmd "select-disc 0" --cmd "appearance dark" --cmd report "${TAIL[@]}"
with_input case-with-art-nodata "$ALU" -- 'cases=1' nodata -- "${ADD_A[@]}" --cmd report "${TAIL[@]}"
with_input multi-disc-xenogears "$XENO" -- 'cases=1' nodata -- "${ADD_X[@]}" --cmd "select-case 0" --cmd "select-disc 1" --cmd "select-disc 0" --cmd move-down --cmd move-up --cmd report "${TAIL[@]}"
with_input multi-disc-ff7 "$FF7" "$ART_DIR" -- 'cases=1' art -- --add "$FF7" --cmd "select-case 0" --cmd "select-disc 2" --cmd "select-disc 0" --cmd report "${TAIL[@]}"
with_input two-cases-reorder "$XENO" "$ALU" -- 'cases=2' nodata -- "${ADD_X[@]}" "${ADD_A[@]}" --cmd report --cmd "drag-case 0 1 0" --cmd report --cmd "drag-case 1 0 1" --cmd "drag-case 0 99 1" --cmd "drag-case 5 0 0" --cmd move-down --cmd move-up --cmd report "${TAIL[@]}"
with_input drag-disc-between-cases "$XENO" "$ALU" -- 'cases=[12]' nodata -- "${ADD_X[@]}" "${ADD_A[@]}" --cmd "select-case 0" --cmd "drag-disc 0 c 1 0" --cmd report --cmd "drag-disc 0 d 0 1" --cmd "drag-disc 9 d 0 1" --cmd "drag-disc 0 c 9 0" --cmd report "${TAIL[@]}"
with_input odd-names "$ALU" -- 'cases=1' nodata -- "${ADD_A[@]}" --cmd "select-case 0" --cmd "name-case " --cmd "name-case $LONG" --cmd "name-case $UNI" --cmd "name-case    " --cmd "name-case \"quote\" 'q' %s %@ %n \\0" --cmd "select-disc 0" --cmd "name-disc " --cmd "name-disc $LONG" --cmd "name-disc $UNI" --cmd report "${TAIL[@]}"
with_input odd-selection "$ALU" -- 'cases=1' nodata -- "${ADD_A[@]}" --cmd "select-case -1" --cmd "select-case 9999" --cmd "select-disc -1" --cmd "select-disc 99" --cmd "select-case 0" --cmd "select-disc 0" --cmd report "${TAIL[@]}"
with_input remove-everything "$XENO" "$ALU" -- 'cases=0' nodata -- "${ADD_X[@]}" "${ADD_A[@]}" --cmd "select-case 0" --cmd "select-disc 0" --cmd remove-disc --cmd remove-case --cmd remove-case --cmd remove-disc --cmd report "${TAIL[@]}"
with_input remove-decline "$ALU" -- 'cases=1' nodata -- "${ADD_A[@]}" --cmd "select-case 0" --cmd remove-case --cmd remove-disc --cmd report --auto-answer no --quit
with_input add-twice-duplicate "$ALU" -- - nodata -- "${ADD_A[@]}" "${ADD_A[@]}" --cmd report "${TAIL[@]}"
with_input panes-and-sheets "$ALU" -- 'cases=1' nodata -- "${ADD_A[@]}" --cmd "select-case 0" --cmd toggle-sidebar --cmd toggle-inspector --cmd "focus cases" --cmd "focus discs" --cmd "focus case-name" --cmd "focus disc-name" --cmd toggle-sidebar --cmd toggle-inspector --cmd "demo-sheet progress" --cmd "demo-sheet-close" --cmd "demo-sheet success" --cmd "demo-sheet-close" --cmd "demo-sheet failure" --cmd "demo-sheet-close" --cmd "demo-sheet confirm" --cmd "demo-sheet menu" --cmd "demo-sheet alert" --cmd "demo-sheet error" --cmd "demo-sheet details" --cmd "drop-overlay on" --cmd "drop-overlay off" --cmd "resize 1000 640" --cmd "resize 1800 1000" --cmd "busy 1" --cmd "progress 50" --cmd "busy 0" --cmd "verify-on-add 0" --cmd "verify-on-add 1" --cmd "shot $WORK/shot.png" --cmd "ax-dump" --cmd report "${TAIL[@]}"
with_input appearance-matrix "$ALU" -- 'cases=1' nodata -- "${ADD_A[@]}" --cmd "appearance dark" --cmd "shot $WORK/dark.png" --cmd "appearance light" --cmd "shot $WORK/light.png" --cmd "appearance system" --cmd "appearance bogus" --cmd report "${TAIL[@]}"

# 10 games at once: the first ten game folders found. Verification on add is switched off first
# (verifying ten discs reads ~10 GB and takes minutes); one add-folder per game, each waited for.
if [ -d "$GAMES_DIR" ]; then
  ten=(--cmd "verify-on-add 0")
  n=0
  while IFS= read -r d; do ten+=(--cmd "add-folder $d" --cmd wait-idle); n=$((n + 1)); done < <(find "$GAMES_DIR" -mindepth 1 -maxdepth 1 -type d ! -name 'PicoStation-MultiHouse-macOS' | sort | head -10)
  if [ "$n" -ge 10 ]; then
    scenario ten-games-at-once 'cases=([5-9]|10)' nodata -- "${ten[@]}" --cmd "select-case 0" --cmd "select-case 9" --cmd "drag-case 0 9 1" --cmd "drag-case 9 0 0" --cmd report "${TAIL[@]}"
  else skip=$((skip + 1)); echo "SKIP  ten-games-at-once: fewer than 10 game folders in $GAMES_DIR"; fi
else skip=$((skip + 1)); echo "SKIP  ten-games-at-once: no $GAMES_DIR"; fi

if [ "$FUZZ" = 1 ]; then # test-hook argument fuzz: known to crash today, see docs/audit/ui-crash-audit.md
  scenario fuzz-select-case-no-arg - nodata -- --cmd "select-case" "${TAIL[@]}"
  scenario fuzz-select-disc-no-arg - nodata -- --cmd "select-disc" "${TAIL[@]}"
  scenario fuzz-drag-no-args - nodata -- --cmd "drag-case" --cmd "drag-disc" "${TAIL[@]}"
  scenario fuzz-open-image-empty - nodata -- --cmd "open-image " "${TAIL[@]}"
  scenario fuzz-save-image-empty - nodata -- --cmd "save-image " "${TAIL[@]}"
  scenario fuzz-add-empty-path - nodata -- --add "" "${TAIL[@]}"
  with_input fuzz-drag-negative "$XENO" "$ALU" -- - nodata -- "${ADD_X[@]}" "${ADD_A[@]}" --cmd "drag-case 0 -1 0" --cmd "drag-disc 0 d -1 0" "${TAIL[@]}"
fi

echo "ui-smoke: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
