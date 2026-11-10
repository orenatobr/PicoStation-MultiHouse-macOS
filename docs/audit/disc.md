# Audit of module `disc`

Scope: `core/src/disc/*.c`, `core/include/mh/disc.h`, `docs/map/disc.md`. Method: every original function of the
table in `docs/map/disc.md` was read in the Ghidra output next to the port (constants, limits, offsets, text, integer
widths, order of side effects). Doubtful spots were confirmed in the machine code (constants of `cue_parse`, the
`.rdata` strings and tables, `DAT_` values, the root/unrolled `psserial_walk_directory.constprop.0`, the PVD checks
of `extract_mapped`). All 150 CUE sheets under `/Volumes/Renato/PicoStation BR` parse under ASan/UBSan, and each one
survives `cue_write_corrected` followed by a re-parse with identical tracks. Message strings of the port were
compared mechanically with `defstrings.tsv`: every one exists in the original.

Classes: BUG (differs without a documented reason), DEVIATION-OK (documented, sensible), RISK (unclear or accepted).

## Functions audited (77)

* Serial recovery: `canonical_serial`, `psserial_parse_token`, `psserial_parse_filename`, `psserial_add_candidate`,
  `psserial_finish_from_candidates`, `psserial_file_read`, `psserial_file_close`, `psserial_read_user_sector`,
  `psserial_read_iso_bytes`, `psserial_walk_directory` (and the unrolled `.constprop.0`),
  `psserial_find_system_cnf_visitor`, `psserial_find_entry_visitor`, `psserial_collect_exes_visitor`,
  `psserial_scan_executable`, `extract_mapped`, `extractSerialDetailed`, `extractSerialRaw`, `extractSerial`,
  `psserial_status_text`, `psserial_source_text`, `cue_scan_serial_file`, `recover_disc_serial_internal`,
  `disc_scan_serial`, `normalize_serial`, `normalize_ascii_title`, `game_directory`.
* CUE: `cue_parse`, `decode_cue`, `cue_count_files`, `msf`, `cue_rewrite_files`, `cue_write_corrected`, `obs_add`.
* Database: `db_load`, `db_free`, `db_record_serial`, `db_record_title`, `db_resolve_alias`, `db_find_art`,
  `db_find_original_cue`, `db_find_sbi`, `db_apply_disc`, `db_apply_saved_single_track`, `db_match_trackset`,
  `db_recover_track_map_safe`, `db_verify_redump`, `apply_trackset_title` (`db_record_title_active` and
  `db_match_trackset_active` are not ported, thin wrappers).
* Disc, case, project: `disc_free`, `disc_refresh_status`, `disc_status_text`, `disc_progress`, `disc_layout_valid`,
  `keep_disc_with_observations`, `kept_package`, `case_title_for_disc`, `short_disc_name`, `project_init`,
  `project_clear`, `project_recount`, `project_add_case`, `project_remove_case`, `project_move_case`,
  `project_reorder_case`, `project_reorder_case_index`, `project_move_disc`, `project_find_case_title`,
  `manager_contains_cue`, `manager_group_case`, `case_add_disc`, `case_remove_disc`, `case_move_disc`,
  `manager_parse`, `add_cue_path`, `project_use_default_menu`.

## Findings

### BUG (fixed, `d657e05`, tests in `core/tests/test_disc_audit.c`)

1. `decode_cue` / `mh_cue_read_text` accepted an empty file. `MultiByteToWideChar` returns 0 for 0 bytes, so the
   original failed and `cue_parse` said "Nao foi possivel abrir ou decodificar o CUE."; the port parsed the empty text
   and said "CUE sem FILE/TRACK validos.", and `cue_rewrite_files` succeeded writing an empty file. Fixed. The old test
   `test_disc.c` `e0.cue` encoded the wrong text and now uses a file with one line break (which does reach the
   "CUE sem FILE/TRACK validos." path).
2. `cue_rewrite_files`: the original loop is a do/while over "next line pointer != NULL". After a final CR/LF the
   pointer is not NULL but points at the terminator, so one more (empty) line is visited and written as `\r\n`. The
   port stopped at the end of text, so every rewritten CUE lacked that last empty line (visible in the temporary CUE
   of `manager_parse` and in the recoded CUE of the V2 writer). `next_line` now returns NULL for an empty remainder
   and the loop matches. Files without a final line break are unchanged.

### Hardening on untrusted input (deviations, documented in `docs/map/disc.md`)

3. CUE text cap lowered from 2 GiB to 64 MiB (`MH_CUE_MAX_BYTES`): a hostile oversized "CUE" would have been read
   whole and, for non UTF-8 text, expanded 3x.
4. Executable content scan: a crafted image can list up to 64 `PS-X EXE` files that each claim most of the volume; the
   original (and the first port) read all of them (tens of GB of reads on a 700 MB image). The total scanned content is
   now capped at 256 MiB per recovery (`SCAN_MAX_BYTES`); real executables are under 2 MiB and 64 of them stay far
   below the cap. Over the cap the recovery ends with ERRO_IO ("Falha durante a leitura de executavel PS-X EXE.").
   The 32 bit offset of that loop could also have wrapped on a 4 GB claim; the cap makes that unreachable.

Other memory safety checks that held (no change needed): record parsing of the ISO walk reads `r[0x20]` only after
`pos + rl <= chunk`; names and paths are NUL terminated and capped (255 / 511); the SYSTEM.CNF buffer is `size + 1`
with NUL; BOOT values are capped at 0x21f and paths at 511; `tt[]`, `used[]`, `pregap[]`, `start[]` are bounded by
the 99 file / 99 track checks (the 100th FILE and 100th TRACK stop the parse); sizes use `uint64_t` before multiplying
by 2352 / 2048 / 0xa00; the database blobs are fully validated by `db_load` (NUL terminated text fields, offsets,
counts, SBI blobs) before any lookup; recursion is limited to depth 17 and to 200000 records / 50000 directory reads.
A mutation fuzzer (CUE text and PVD / directory / SYSTEM.CNF bytes, truncated images, raw V2 slots; 8000 CUE and
4000 ISO iterations run by hand on the final code, 400 + 200 kept in the test) found nothing under ASan/UBSan.

### DEVIATION-OK

* `ps_walk` visited set of directory extents, record / read budget (see `docs/map/disc.md`). The original aborts with
  ERRO_IO on a directory loop (after 17 levels); the port skips the repeated extent. Fixture discs use at most 113
  distinct directories and 184 reads, far below the caps (2048 / 50000).
* Paths and names are UTF-8 bytes (1023) instead of 259 UTF-16 units; FILE names beginning with `/` are absolute;
  backslashes are converted; the case-insensitive BIN search is a `readdir` walk.
* `verify_redump` honours cancel (original ignored the callback result); `manager_parse` removes the temporary CUE
  also when the rewrite failed (the original left it); temporary file by `mkstemp`; `db_find_sbi` has no title
  argument (the original passed one and ignored it); `manager_contains_cue` skips a NULL disc pointer.
* `project_use_default_menu` zeroes `cache_tag` (project +0x960) before reading the file; the port zeroes it only when
  a valid record is installed. Nothing in the executable reads that field (only the two writers), so there is no
  observable difference.
* `psserial_walk_directory.constprop.0` is the original recursion unrolled twice; the port starts at depth 0 and fails
  at depth 17 exactly like the original (the unrolled copy calls the generic one with depth 2).

### RISK (accepted, not changed)

* A directory extent reachable under two names is walked once; a BOOT target spelled through the second name would
  not be found (original would find it). Not seen on any fixture disc.
* More than 2047 distinct directories in one image abort the walk with ERRO_IO (original has no limit).
* `db_verify_redump` reads exactly `files[i].size` bytes (the size seen at parse time); the original read to EOF. A BIN
  that changed on disk since parsing is reported as unreadable instead of being hashed whole.
* `apply_trackset_title`: the original converted the UTF-8 title into 160 UTF-16 units and skipped the title if it did
  not fit; the port truncates. Database titles are under 96 characters, so no effect.
* A `FILE` line naming a FIFO blocks in `open()` (`mh_file_size64`, module `base`); a local attacker needs write access
  next to the CUE. Not changed here.
* Notes are limited to 767 bytes (original 767 wide characters); only ASCII is ever appended, so identical.
* `manager_parse` writes FILE paths exactly as `d->files[].path` holds them; a relative CUE path would give a temporary
  CUE whose relative BIN paths no longer resolve (same weakness in the original; the UI passes absolute paths).
