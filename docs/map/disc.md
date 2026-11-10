# Module `disc`: disc model, serial recovery, CUE, database, project

Header `core/include/mh/disc.h`, code `core/src/disc/{psserial,cue,disc,db,dbmatch,project,manager}.c`
(+ `disc_internal.h`), tests `core/tests/test_disc.c` (unit, synthetic ISO images, fixtures) and
`core/tests/test_disc_redump.c` (slow: Redump verification of two real discs and a track map recovery),
helpers `core/tests/disc_util.inc`. `make test-disc`. Depends on `base` only (the cheat module is needed at link time
for `mh_cheat_profile_free`; the tests carry a stub). Binary formats of the four blobs: docs/EMBEDDED_DATA.md.

Addresses are VAs in `mh64.exe`. All text is UTF-8; all messages are the original ASCII Portuguese strings.

## Function map

### Serial recovery (ISO 9660 walk)

| original | port | notes |
|---|---|---|
| `canonical_serial.constprop.0` @0x1400030c0 | `mh_canonical_serial` | the decompiler lost the success paths; ported from the machine code. 7 or more digits are always rejected |
| `psserial_parse_token.constprop.0` @0x140029e40 | static `parse_token` | 24 prefixes (`psserial_prefixes` @0x140f1d0e0: ESPM PAPX PBPX PCPD PCPX PDPX PEPX PTPX PUPX SCED SCES SCKA SCPM SCPS SCUS SCZS SIPS SLED SLES SLKA SLPM SLPS SLUS SPUS), `_ddd.dd` (11 chars) or `-ddddd` (10) |
| `psserial_parse_filename.constprop.0` @0x140029cd0 | `mh_psserial_parse_filename` | cuts `;version`, drops `.EXE`, the whole name must be the token |
| `psserial_add_candidate` @0x1400292f0, `psserial_finish_from_candidates.isra.0` @0x140029b80 | static `cand_add`, `cand_finish` | dedupe case-insensitively, at most 16 kept, count keeps growing and sets `overflow` |
| `psserial_file_read.constprop.0` @0x1400297b0, `psserial_file_close` @0x140029770 | static `ps_read`, `ps_close` (`PsFile`) | raw V2 mode reads 0xA00 byte slots (`sector * 0xA00 + base`), only 2352 bytes meaningful |
| `psserial_read_user_sector` @0x14002a3c0, `psserial_read_iso_bytes.constprop.0` @0x14002a240 | static `ps_read_sector`, `ps_read_bytes` | |
| `psserial_walk_directory` @0x14002a840 and `.constprop.0` @0x14002ab60 | static `ps_walk` | the constprop copy is the same recursion unrolled twice; depth limit 17 |
| `psserial_find_system_cnf_visitor` @0x1400299f0, `psserial_find_entry_visitor` @0x140029910, `psserial_collect_exes_visitor` @0x140029430 | static `v_system_cnf`, `v_find_entry`, `v_collect_exes` | up to 64 executables are stored |
| `psserial_scan_executable` @0x14002a450 | static `scan_executable` | scans the file in 0x800 chunks with a 16 byte carry; a token must not touch letters, digits, `-`, `.`, `_` |
| `extract_mapped.part.0.constprop.0` @0x140019ca0 | static `extract_mapped` | PVD at 0x9318 (mode 2), 0x9310 (mode 1) or 0x8000 (2048 byte ISO); then SYSTEM.CNF `BOOT`/`BOOT2`, then executables |
| `extractSerialDetailed` @0x140019b70, `extractSerialRaw` @0x140019c00, `extractSerial` @0x140019aa0 | `mh_psserial_extract`, `mh_psserial_extract_raw`, `mh_psserial_extract_serial` | |
| `psserial_status_text` @0x14002a7c0, `psserial_source_text` @0x14002a770 | `mh_psserial_status_text`, `mh_psserial_source_text` | |
| `cue_scan_serial_file` @0x140014c60 | `mh_cue_scan_serial_file` | |
| `recover_disc_serial_internal.isra.0` @0x14002bb40 | `mh_disc_recover_serial` | window and message box code removed, the texts are returned |
| `disc_scan_serial` @0x140018220 | `mh_disc_scan_serial` | |
| `normalize_serial` @0x140024f30, `normalize_ascii_title` @0x140024e50, `game_directory` @0x14001b2e0 | `mh_normalize_serial`, `mh_normalize_title`, `mh_game_directory` (base) | |

### CUE

| original | port | notes |
|---|---|---|
| `cue_parse` @0x140013160 | `mh_cue_parse` | see "CUE parser behaviour" |
| `decode_cue.constprop.0` @0x140017a60 | `mh_cue_read_text` | strict UTF-8 (`MB_ERR_INVALID_CHARS`), else the ANSI code page, assumed Windows-1252; the result is UTF-8 |
| `cue_count_files` @0x140012fe0 | `mh_cue_count_files` | |
| `msf` @0x140024cf0 | `mh_cue_msf` | `mm:ss:ff`, ss < 60, ff < 75, trailing text ignored, overflow checked like the original |
| `cue_rewrite_files` @0x140014820 | `mh_cue_rewrite_files` | output UTF-8 without BOM, CRLF, blank lines dropped, leading blanks of a line kept, trailing trimmed |
| `cue_write_corrected` @0x140014ce0 | `mh_cue_write_corrected` | `FILE "<basename of the resolved path>" BINARY`; INDEX 00 only when the pregap is not 0 |
| `obs_add` @0x1400251b0 | `mh_disc_note` | |

### Database

| original | port | notes |
|---|---|---|
| `db_load` @0x140015d40, `db_free` @0x140015cd0 | `mh_db_load`, `mh_db_load_dir`, `mh_db_free` | blob memory is a parameter; `MhDb` is a struct the caller owns (original: global `DAT_140e7c3b8`, 0x888 bytes, plus a global "active database" pointer) |
| `db_record_serial` @0x140016ad0, `db_record_title` @0x140016b10 | `mh_db_record_serial`, `mh_db_record_title` | |
| `db_record_title_active` @0x140016b50, `db_match_trackset_active` @0x140016ab0 | not ported | thin wrappers over the global active database; the only caller is `stream_disc`, which must now be given the `MhDb *` |
| `db_resolve_alias` @0x140017120 | `mh_db_resolve_alias` | |
| `db_find_art` @0x1400157a0, `db_find_original_cue` @0x140015a90, `db_find_sbi` @0x140015ca0 | `mh_db_find_art`, `mh_db_find_original_cue`, `mh_db_find_sbi` | path pieces `database\art\default`, `database\cues` became `database/art/default`, `database/cues` |
| `db_apply_disc` @0x1400151b0 | `mh_db_apply_disc` | |
| `db_apply_saved_single_track` @0x1400155b0 | `mh_db_apply_saved_single_track` | |
| `db_match_trackset` @0x140016680 | `mh_db_match_trackset` | |
| `db_recover_track_map_safe` @0x140016b90 | `mh_db_recover_track_map` | works on a copy of the disc; only on success the tracks, track count, flags, `ps`, `db_index` and SBI pointer are copied back |
| `db_verify_redump` @0x1400171e0 | `mh_db_verify_redump` | |
| `apply_trackset_title` @0x140002310 | `mh_apply_trackset_title` | |

### Disc, case, project

| original | port |
|---|---|
| `disc_free` @0x140018000 | `mh_disc_free` |
| `disc_refresh_status` @0x140018150 | `mh_disc_refresh_status` |
| `disc_status_text` @0x1400183c0 | `mh_disc_status_text` |
| `disc_progress.part.0` @0x1400180b0 | `mh_disc_progress` (the original callback had a phase argument, `MhProgress` has none) |
| `disc_layout_valid` @0x140018040 | `mh_disc_layout_valid` (takes the V2 header bytes; it validates a V2 header, not a disc) |
| `keep_disc_with_observations` @0x14001d660 | `mh_keep_disc_with_observations` (the "add anyway?" answer, always yes) |
| `kept_package` @0x14001d670 | `mh_kept_package` |
| `case_title_for_disc` @0x140003f00, `short_disc_name` @0x140030f50 | `mh_case_title_for_disc`, `mh_short_disc_name` |
| `project_init` @0x140027210, `project_clear` @0x1400270c0, `project_recount` @0x140027bb0 | `mh_project_init`, `mh_project_clear`, `mh_project_recount` |
| `project_add_case` @0x140026c50, `project_remove_case` @0x140027f80, `project_move_case` @0x140027240 | `mh_project_add_case`, `mh_project_remove_case`, `mh_project_move_case` |
| `project_reorder_case` @0x140028430, `project_reorder_case_index` @0x140028a70, `project_move_disc` @0x140027300 | `mh_project_reorder_case`, `mh_project_reorder_case_index`, `mh_project_move_disc` |
| `project_find_case_title` @0x1400271a0, `manager_contains_cue` @0x1400228c0, `manager_group_case` @0x140022950 | `mh_project_find_case_title`, `mh_project_contains_cue`, `mh_project_group_case` |
| `case_add_disc` @0x140003390, `case_remove_disc` @0x1400039a0, `case_move_disc` @0x1400037f0 | `mh_case_add_disc`, `mh_case_remove_disc`, `mh_case_move_disc` |
| `manager_parse` @0x140022a60 | `mh_manager_parse` |
| `add_cue_path.constprop.0` @0x1400012d0 | `mh_project_add_cue` (no window text; the message boxes became `err`) |
| `project_use_default_menu.constprop.0` @0x140029090 | `mh_project_set_menu_defaults` (the file read and validation is `mh_menu_defaults_load` of the menu module) |

### Not ported here

* `disc_bank_valid` @0x140017be0 validates a GameShark "PSTCHT1" bank blob; it belongs to the cheat module.
* `admit_cue_disc` @0x1400017f0, `manager_admit`, `v2_check_disc_source`: V2/UI engineers. `mh_project_add_cue` calls
  them through `MhCueHooks.admit`. Without that hook every parsed CUE is accepted, even a broken one; the original
  rejected discs whose structure was incomplete in `v2_check_disc_source`, so the app must install the hook.
* `auto_attach_cheats` @0x140002910 (`MhCueHooks.attach_cheats`), `common_ask`, `common_locate` (`ask`, `locate`).
* After `mh_disc_recover_serial` succeeds the caller must call `cheat_profile_prepare_hardware` when the disc has cheats.

## Struct layouts (original offsets)

The original kept discs, cases and the project as raw blocks. The port keeps the same fields as named members, not at the
same offsets (paths are UTF-8 `char[1024]` instead of `WCHAR[0x400]`). Offsets below are the original ones; V2 code
that is read next to the decompilation can map them with this table.

### Disc (0x43988 bytes, `MhDisc`)

| orig | field | notes |
|---|---|---|
| +0x0000 | `kind` | 1 CUE/BIN, 4 V2 device, 5 V2 image file (3 and others unused) |
| +0x0004 | `status` | 0 empty, 1 OK, 2 observation, 3 fixable, 4 error |
| +0x0008 | `flags` | `MH_DF_*`, see the header |
| +0x000c | `serial[16]` | |
| +0x001c | `name[0x21]` | short name; user edits set `MH_DF_USER_NAMED` (0x10000, byte +0xa bit 0) |
| +0x003d | `title[0x60]` | |
| +0x009e / +0x089e / +0x109e / +0x189e | `cue_path`, `source_path`, `case_art_path`, `cd_art_path` | WCHAR[0x400] each; the CD art is the "Disc Art" the package builder reads (exactly 0x2800 bytes) |
| +0x20a0 | `cheats` | profile pointer, freed by `disc_free` |
| +0x20a8 | `sbi_path` | WCHAR[0x400], external SBI used by `build_package_metadata` |
| +0x28a8 | `notes` | WCHAR[0x300] |
| +0x2ea8, +0x32a8, +0x32ac, +0x32b0, +0x32b4, +0x32b8, +0x32bc | `verify_text`, `verify_issues`, `verify_accepted`, `verified`, `verified_aux`, `verify_crc32`, `verify_sha1` | written by `verification_issue_text`, `v2_check_disc_source`, `v2_load_source`; `verified` + `verified_aux` are the two words of `DAT_140f1a6e0` |
| +0x32d0 / +0x32d4 | `cheat_regenerated`, `cheat_note` | `hydrate_loaded_project`, `auto_attach_cheats` (WCHAR[0x100]) |
| +0x34d4 / +0x34d6 / +0x34d8 / +0x34e0 | `nfiles`, `ntracks`, `total_sectors`, `total_bytes` | |
| +0x34e8, stride 0xa20, 99 | `files[]`: +0 path (WCHAR[0x400]), +0x800 name as written in the CUE (WCHAR[0x104]), +0xa08 size, +0xa10 sectors, +0xa14 start sector | |
| +0x41f48, stride 0x1c, 99 | `tracks[]` (`MhTrack`) | number, kind (1 audio, 2 data), mode (0, 1, 2), flags (1 DCP, 2 ISRC, 4 has INDEX 00), file (u16), pad, pregap (+8), index0 (+0xc), index1 (+0x10), start (+0x14), length (+0x18) |
| +0x42a1c / +0x42a20 / +0x42a34 | `crc32`, `sha1[20]`, `v2_image_tag` | whole-disc CRC-32 and SHA-1 (`db_verify_redump`; for V2 discs the values stored on the card); the tag is a u64 the V2 loader writes |
| +0x42a38 | `db_index` | 0xFFFFFFFF = none |
| +0x42a3c | `ps` (`MhPsSerial`, 0x504 bytes) | see below |
| +0x42f40 / +0x42f48 / +0x42f4a | `sbi_data`, `sbi_len`, `card_index` | SBI pointer into the trackset blob; `card_index` is 0xFFFF for CUE discs |
| +0x42f50 .. +0x42f70 | `v2_drive`, `v2_pkg_a`, `v2_raw_lba512`, `v2_raw_blocks`, `v2_slot` | V2 source descriptor (`kept_package` returns `v2_pkg_a` for a drive match) |
| +0x42f78 / +0x42f80 / +0x42f88 / +0x43788 | `v2_card_blocks`, `v2_pkg_b`, `v2_pkg_path` (WCHAR[0x400]), `v2_marker[0x200]` | `confirm_session_marker`, `kept_package` |

The region +0x99c8 .. +0x41f47 of the original block is never referenced by any function in the executable.

`MhPsSerial` (orig 0x504 bytes at disc+0x42a3c): +0 status, +4 source, +8/+0xc copies saved by `disc_scan_serial`
(restored by `db_match_trackset`), +0x10 serial[16], +0x20 token[32], +0x40 path[512], +0x240 candidate count,
+0x244 candidates[16][16], +0x344 SYSTEM.CNF found, +0x348 boot target found, +0x34c boot target is PS-X EXE,
+0x350 "needs hash" (1 unless a trackset settled it), +0x354 ISO volume sectors, +0x358 sectors in the image,
+0x35c truncated, +0x360 trackset serial[32], +0x380 trackset id, +0x384 message[192], +0x444 saved message[192].

### Case (0x868 bytes, `MhCase`)

+0 in_use (u32), +4 slot (u8), +6 serial[16], +0x16 title[0x21], +0x38 art path (WCHAR[0x400]), +0x838 disc count (u8),
+0x840 discs[5] (pointers, owned).

### Project (0x879a8 bytes, `MhProject`, global `DAT_140e7d490`)

+0 dirty, +4 card loaded, +8 image loaded, +0xc (initialised to 1, never read), +0x10 u16 case count, +0x12 u16 disc count,
+0x14 device (-1), +0x20 device name (WCHAR[0x60]), +0xe0 device label (WCHAR[0x40]), +0x160 image path (WCHAR[0x400]),
+0x960 u64 cache tag (zeroed by `hydrate_loaded_project` and `project_use_default_menu`), +0x1160 menu defaults record
(64 bytes), +0x11a0 "has menu defaults", +0x11a8 cases[256] (stride 0x868, up to +0x879a8).

## Limits and edge cases kept from the original

* 256 case slots, 5 discs per case, 256 discs in the whole project. `case_add_disc` tests the project counter cached by
  the last `project_recount` (it is recomputed by every operation that changes the counts).
* `project_add_case(from)` scans 256 slots starting at `from` and wraps; the new case is titled `NOVO JOGO`.
* `project_move_case` swaps two slots (the target may be empty); `project_reorder_case(_index)` compacts all used cases
  to slots 0..n-1 (the swap and the reorder are different operations); a move that changes nothing returns false.
* `project_move_disc` between cases needs room in the target (5 is full) and clears the source case when it becomes empty
  (the slot byte of the cleared case becomes 0, as in the original); inside one case the destination is clamped to the
  last place; outputs keep the source place when the move fails.
* `case_move_disc` does not set the dirty flag; `case_add_disc`, `case_remove_disc`, `project_move_case`, `project_move_disc` and the reorder functions do.
* `case_remove_disc` does not update the case serial; the serial of a case is set once from the first disc that has one.

## CUE parser behaviour (details worth knowing)

* Lines end at CR or LF; the whole following run of CR/LF is skipped; leading/trailing blanks (tab, LF, CR, space) are
  trimmed. Keywords are matched case-insensitively and need the blank: `REM `, `FILE `, `TRACK `, `INDEX `, `PREGAP `,
  `FLAGS `, `ISRC `, `POSTGAP `. Every other line (TITLE, PERFORMER, CATALOG...) is ignored.
* Errors that stop reading the sheet (no flag set, only the note): `Linha FILE invalida.`, `Linha TRACK invalida ou fora de
  ordem.`, `INDEX sem TRACK valido.`, `Tempo INDEX invalido.`, `PREGAP invalido.`, `FLAGS sem TRACK valido.`,
  `ISRC sem TRACK valido.`. The validation after the loop still runs on what was read, so more notes can follow.
  `Mais de 99 arquivos BIN no CUE.` (the 100th FILE) and the `BINARY`/mode errors set `MH_DF_INVALID` (0x80).
* After the sheet: unused FILE, duplicate BIN, first track not data (all 0x80); missing BIN (0x2, status "CORRIGIVEL",
  the UI can ask for the BIN); size 0 or not a multiple of 2352 (0x4); more than 450000 sectors in total (0x80).
  When none of 0x2, 0x4, 0x80 is set the tracks are validated (contiguous numbers from 1, INDEX 01 inside its BIN, PREGAP
  only together with INDEX 00 and consistent, first track at sector 0 without pregap, strictly increasing starts) and the
  serial scan runs; otherwise it is skipped.
* `POSTGAP` sets 0x100 (status ERRO through `disc_refresh_status`) but the structure still validates.
* A BIN that does not exist is searched case-insensitively by base name next to the CUE (the original did the same with
  FindFirstFile); Windows absolute names (`C:\...`, `\\...`) keep their name only for the fallback search.

## Deviations and additions

* Paths are UTF-8, 1024 bytes. A CUE name beginning with `/` is taken as an absolute path (the corrected temporary CUE
  that `manager_parse` writes holds absolute paths, which on Windows start with a drive letter). CP1252 is assumed for
  the "ANSI" fallback of `decode_cue`.
* `db_verify_redump` honours the progress cancel flag (the original ignored the callback result) and returns false
  without touching the disc on cancel.
* `mh_db_load` copies the blobs from caller memory (`MhDbBlobs`); `mh_db_load_dir` reads the four `.bin` files. The SBI
  pointer that `db_match_trackset` hands to a disc points into the database copy: keep the `MhDb` alive while discs are
  in use.
* No global "active database" and no global project; the `_active` wrappers are dropped (see above).
* `manager_parse`'s temporary corrected CUE is made with `mkstemp` in `$TMPDIR` (`GetTempFileNameW` prefix `PST`).
* `mh_project_add_cue` never calls `admit`/`attach_cheats` when the hooks are NULL; it returns `MH_ADD_NOT_CUE` and
  `MH_ADD_DUPLICATE` where the original returned 0 silently.

## Findings in the original

* The serial of a CUE disc comes only from FORTE_INTERNO (a BOOT target whose file name is a serial); PROVAVEL (guess from
  the executable content or the only serial-named executable) is shown but not stored in `serial` by `disc_scan_serial`.
* Several messages in the database code start with `\r\n` and are appended with `w_cat` even when the notes are empty
  (the notes then begin with a blank line); `obs_add` does not.
* `db_apply_disc` calls two editions "the same" only when their titles are equal; equal best scores with different titles
  are "ambiguous" and nothing is applied.
* `db_recover_track_map_safe` hashes every track of the single BIN with SHA-1 in 64 sector reads and accepts the rebuilt
  map only if `db_match_trackset` finds exactly one record with that complete ordered SHA-1 list.
* The ISO walker treats `SYSTEM.CNF` found in the root as the only candidate when it exists; a nested one is used only when no
  root one exists. Two `BOOT`/`BOOT2` lines with different targets make the whole SYSTEM.CNF count as unusable.
* The raw V2 serial path reads 2560 byte slots, so `extractSerialRaw` on a device needs the 512-byte block offset of the
  slot, not a byte offset.
* `kExpectedSha256` @0x140f1d080 is the cheat database's integrity constant, not part of the disc database.
