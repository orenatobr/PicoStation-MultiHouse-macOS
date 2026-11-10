# Audit: V2 part A (format, validation, loading, SD front ends, recode)

Scope: `core/include/mh/v2format.h`, `v2.h`, `core/src/v2/{v2_format,v2_util,v2_validate,v2_load,v2_hydrate,v2_sd,v2_recode}.c`
and the part A table of `docs/map/v2.md`. Method: every function was read side by side with the Ghidra output
(`$RE/out/fn/<name>.c`), stack-offset-heavy spots were checked against `objdump -d`, constants carved from `.rdata`
with `xxd` (the header constants block at 0x140f1a688, the track map head 0x1001, the image tag 0xFFFF...). The
classification is BUG (wrong or unsafe, fixed), DEVIATION-OK (differs from the original on purpose or because of the
platform), RISK (same as the original, worth knowing).

## Functions audited

| Original | Port | Result |
|---|---|---|
| v2_header_valid, v2_header_raw_valid, crc_record | `mh_v2_header_valid`, `mh_v2_header_raw_valid`, `mh_crc_record` | match (offsets, widths, order, CRC over 0x92C bytes, MBR rule only when flag bit 1) |
| disc_layout_valid | `mh_disc_layout_valid` | match (flags < 4, +0x64 == base+5, EXT: size <= 0x2800 and extra == base+0x19, else 0x1400 and base+0xF, extra size 0xA00) |
| v2_validate_image | `mh_v2_validate_image` | match: check order, 0x930 byte header reads, CRC of Case 0x5000 / Disc 0x10000 / art 0x280000, per record rules, 0xD0 byte padding reads, cheat blob scope 2, RAW read in 0x800 slot chunks with progress after the padding test, menu, PSTCFG2, music, final CRC-32/SHA-1 on a second open; every error text byte for byte; silent `false` on I/O failures where the original had no text |
| v2_load_source | `mh_v2_load_io` (+ `load_disc`, `tracks_from_sector`) | match: header slot reads (0xA00, padding), the whole A/B selection tree (checked branch by branch, including the signed generation difference and the repair flags), menu extraction, Case/Disc record order and rules, track map native / rebuilt (SBI included) / damaged, resume marker (magic, CRC at 0x1FC, record compare, PSTVFY1), disc and case art, cheat load and its three notes, dirty flag, all result texts. One fix (below) |
| v2_load_image, v2_load_physical, v2_probe_recoverable_physical | same names with `mh_` | match |
| recover_v2_header_from_body | `mh_v2_recover_header_from_body` | match: PSTWIP1 refusal, 5 MiB minimum, PSTCFG2 read twice, Case/Disc rules, `base >= 0x2800`, overflow guard on `raw_blocks + raw_lba`, size check, header bytes (flags 1, generation 1, only the two table CRCs) |
| hydrate_loaded_project | `mh_v2_hydrate_project` | match: state 2 -> 0 with the compact firmware note, state 1 rebuild + restore, no-profile binding with its three failure texts, note append of the "banco indisponivel" text once (CRLF separator verified in `.rdata`), art lookup order case then disc, dirty rules, `cache_tag = 0` |
| cache_name, cache_region | statics of v2_load.c | match (0x2800 byte regions, `<prefix><n><ext>`) |
| v2_invalidate_header_slot, v2_publish_header_slot | same | match (blocks 1..4 then block 0, write/flush/read/compare each, final 0xA00 compare) |
| sd_invalidate_v2_header, sd_publish_v2_header, sd_write_block_verified, sd_v2_header_raw_valid | same | match (8 attempts, sleeps 5..75 ms for reads, 30..275 ms for writes, read-back retry inside the write attempt) |
| clear_backup_gpt_tail | `mh_clear_backup_gpt_tail` | match (last block "EFI PART", start = max(size-1 MiB, used rounded up), zero only non-zero blocks, three texts) |
| compare_region | `mh_v2_compare_region` | match (256 KiB pieces, a-side then b-side read, 8 tries, three texts) |
| verify_committed_headers_after_reopen | `mh_v2_verify_committed_headers` | match (250 ms, A then B read with retry, both `sd_v2_header_raw_valid`, A == B, A == expected) |
| sd_verify_image, sd_write_image | same | match: order identity, open image, size checks (`< 0x1400`, larger than the card), header pair test, previous header kept, invalidate A then B (restore on B failure), body from 0x1400 in 256 KiB pieces with 8 tries, flush, compare, GPT tail, publish B then A, reopen check. Header A is the last write |
| sd_quick_health | `mh_sd_quick_health` | match (four regions, two reads, cancel text, ok rule) |
| sd_write_project_direct | `mh_sd_write_project_direct` | match (order: identity, estimate, open, prepare, resume target, GPT tail, writer, header A read, commit texts, reopen check); the "full verify" flag is never read by the writer in the original either |
| confirm_session_marker, v2_confirm_resume_target | `mh_v2_confirm_session_marker`, `mh_v2_confirm_resume_target` | match (the five unrolled disc slots became a loop; image / card / empty path matching rules checked) |
| manager_recode_copy | `mh_manager_recode_copy` | match (preconditions, CREATE_NEW semantics, folder, 0x80 sector chunks, track cursor kept across files, `r & 1` / mode compare failure, SHA-1 of the source against the verified one, nothing changed text, CUE rewrite, cleanup of created files, folder and CUE) |

## Findings

### BUG (fixed)

* **B1 `Disc.nfiles` of a loaded disc is not bounded** (`v2_load.c`, `load_disc`). The Disc record byte +0x61
  (`bin_count`) was copied to `MhDisc.nfiles` unchecked. A hostile card or image with valid CRCs can set it up to 255
  while `MhDisc.files` has 99 entries, and `dbmatch.c`, `write_image.c`, `stream_disc.c`, `manager.c` and the recode
  code index `files[]` up to `nfiles`: out-of-bounds heap read/write. Found by the mutation fuzzer (assertion on the
  loaded project) and fixed by clamping to `MH_DISC_MAX_FILES`. A valid library never exceeds 99 (the CUE parser
  refuses more). Test: `test_v2_audit.c` `test_bin_count` (failed before the fix, passes after).

### DEVIATION-OK

* The progress callback "kind" argument of the original (5 for the write, 6 for validate / compare / recode) is not
  passed; `MhProgress` has no such field. Texts and totals are the original ones.
* Recovered header with a menu shorter than `menu_sectors * 0x930`: the original `memcpy`s past the embedded menu; the
  port fails with "Memoria insuficiente para extrair o menu V2." (cannot happen with the embedded menu, the sector
  count comes from its size).
* `sd_quick_health` measures with a monotonic clock instead of QueryPerformanceCounter (same formula, MiB/s x 100).
* The loader reports `(erro Win32 N)` for a failed cheat catalog read using the first error recorded in the `MhIo`
  (the original used `GetLastError()`); the code is informative only.
* Volume locking (`lock_volumes`, `DeviceIoControl 0x9001C`) became `mh_blockdev_open_write` / `_read`; share modes and
  `FILE_FLAG_*` have no equivalent.
* `verification_issue_text` is a static copy in `v2_load.c` (documented in `docs/map/v2.md`); the text was compared
  with `mh_v2_verification_issue_text` and the original, identical.

### RISK (same as the original)

* **R1 Validator and loader disagree on the header slot padding.** `v2_validate_image` reads 0x930 bytes of each slot,
  `v2_load_source` reads 0xA00 and requires bytes 0x930..0xA00 zero ("Nao foi possivel ler os headers V2."). An image
  with dirty padding validates but does not load. Writers always zero it. Kept as is; the fuzzer allows exactly this
  disagreement.
* **R2 No `base >= 0x2800` and no overlap test in the validator and the loader** (only the recovery path has the
  first one). A hostile record can point a package into the fixed zone or over another package; nothing is read out of
  bounds because every read goes through `mh_io_read` (range checked, offsets above INT64_MAX fail), but such a card
  lists as a valid library. Rewriting it is refused later by `mh_v2_extent_insert` / the estimate
  ("extents RAW sao invalidos").
* **R3 64-bit wrap in LBA arithmetic.** `base << 9` (validator) and `raw_lba + raw_blocks` (loader) wrap for LBAs
  above 2^55 / near 2^64. The wrapped offsets only select other bytes of the same file, or fail the range check; the
  loader then ends in "Falha ao ler o estado do disco" (marker read at `(raw_lba - 1) * 512`).
* **R4 `sd_write_image` trusts the image.** It checks the header pair and the size only (as the original); the caller
  must run `mh_v2_validate_image` first, otherwise an image with valid headers and a damaged body is committed.
* **R5 The cheat blob rules depend on the active embedded cheat bank** (global in `mh_cheat_blob_validate`), as in the
  original: without the bank, v6 blobs are refused by the validator.
* **R6 Retry sleeps.** A failed positional read in the SD paths costs 8 attempts and 320 ms before giving up (original
  schedule); `compare_region` against a vanished image therefore takes that long once, not per piece.

## Hostile data checks

Every count, offset and length that comes from the card or image was followed to its use:

* Case: `disc_count` is forced to 1..5 before the loop, `disc_ids` index 256 byte maps, `art_id` is the index or
  0xFFFF; strings are NUL terminated by decode and copied with caps that stay inside the 0x50 record.
* Disc: `track_count` 1..99 (bounds `ntracks` in the damaged case), `sectors` > 0 with `raw_blocks == sectors * 5`,
  `cheat_size <= 0x2800` through `disc_layout_valid` before any allocation, string caps (0x5F bytes from +0x1C) stay
  inside the 0x100 record, `bin_count` clamped (B1).
* Track map: `mh_track_sector_valid` bounds the entry count (<= 99, entries end at 0x650 < 0x930) and the SBI length
  (4..0x2E8, so `0x648 + len <= 0x930`); the rebuilt map copies at most `len + 8` bytes from a length tested before
  use; unsigned compare of `len - 4` verified in the disassembly.
* Menu: sector count 1..93 gates the allocation, `mh_menu_defaults_read` reads at most `size - 0x40` bytes.
* Reads: all sizes are fixed or bounded constants (0x930, 0xA00, 0x2800, <= 0x2800 rounded, 5 MiB RAW chunks), every
  offset goes through `mh_io_read` / `range_ok`.
* Recursion: none. Allocation failures are handled on every path; `leaks --atExit` over the audit test and 400 fuzz
  iterations reports 0 leaks.

## Tests added

* `core/tests/test_v2_audit.c` (58 checks): B1 regression; the header selection tree (A bad, B bad, equal generation
  refused, newer generation with signed wrap, stale A, stale B, stale in only one table, nothing matches, padding,
  count mismatch); loader failure texts (menu padding, case and disc art id, a disc referenced twice, sectors past the
  end of the file); card source; multi-track damaged map; damaged cheat blob; SBI extraction; resume marker with a
  verification stamp (and without it, and with a different record); a cancelled `sd_write_image` leaves both header
  slots invalid and the first body chunk written.
* `core/tests/test_v2_fuzz.c`: mutation fuzzer on the builder image of `test_v2_image.c` (3 discs, cheat blob, case and
  disc art, resume markers). In place mutation of the first 5 MiB (raw bit flips, field writes with boundary values in
  header / Case / Disc / track map / cheat / PSTCFG2 / menu / art / marker zones, optional CRC fixes of every layer
  so that the deep checks are reached, forced header loss to hit `recover_v2_header_from_body`, random truncation).
  Each iteration runs `mh_v2_validate_image`, `mh_v2_load_image`, `mh_v2_hydrate_project`, checks the loaded project
  invariants (disc counts, track and file counts, string termination), requires validate-ok implies load-ok (except
  R1) and arms a 60 s alarm against hangs. Default 1000 iterations in `make test-v2`; `MH_FUZZ_ITERS`, `MH_FUZZ_SEED`,
  `MH_FUZZ_HIST=1` (outcome histogram) for longer runs.

## Fuzz results

Build: `make test-v2` flags (clang, `-O1`, ASan + UBSan). Fixed seeds, in place mutation of a 5.4 MiB builder image.

| Seed | Iterations | Valid images | Loaded | Recovered from the body | Result |
|---|---|---|---|---|---|
| 7 | 4000 | 1090 | 3264 | 430 | clean |
| 11 | 12000 | 3590 | 7877 | 1678 | clean |
| 12 | 12000 | 3603 | 7731 | 1632 | clean |
| 13 | 12000 | 3669 | 7827 | 1606 | clean |
| 14 | 12000 | 3716 | 7886 | 1708 | clean |

Two earlier runs stopped on purpose-built assertions and led to changes: the first one on the loaded project
invariant `nfiles <= 99` (B1, fixed), the second on "validate ok but load failed" for a header slot with dirty padding
(R1, original behaviour, the invariant now allows exactly that case). Total 52000 completed iterations without a
crash, sanitizer report, hang (60 s alarm per iteration) or invariant failure.
`MH_FUZZ_HIST=1` shows 35 or 36 distinct validator and loader outcomes per run, i.e. almost every error text of
both functions is reached (the ones not reached are the I/O failure texts that need a failing device and the
`Falha ao salvar o menu V2 no cache` family). Per iteration cost is about 45 ms (the final whole-file CRC-32 / SHA-1
of valid images dominates).

Memory: `leaks --atExit` (non sanitized build) over `test_v2_audit` and over 400 fuzz iterations: 0 leaks.
