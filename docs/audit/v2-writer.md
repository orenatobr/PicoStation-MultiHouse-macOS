# Audit: V2 writer (part B)

Scope: `core/include/mh/v2write.h`, `core/src/v2/{write_handle,write_image,write_package,write_extent,write_preflight,
estimate_project,stream_disc,admit_disc}.c`, `core/tests/test_v2_write*.c`, plus the I/O helpers the writer stands on
(`core/src/base/io.c`, `path.c`, `cdsector.c`). Method: every function was re-read against the Ghidra output
(`out/fn/*.c`) and, where the decompiler garbled the stack frame, against `objdump -d` of `mh64.exe`; constants were
dumped from `.rdata` (not taken from `v2format.h`); images produced by the port were then dumped field by field by an
independent Python script (`zlib`/`hashlib`, no port code) and by new C tests whose expected bytes come from the
decompilation. Classification: BUG (fixed, with a test that failed before), DEVIATION-OK, RISK, UNVERIFIED.

Tests: `make test-v2` passes under ASan/UBSan (`test_v2_write` 100025 checks, `test_v2_write_audit` 141k checks).

## Byte ranges of the produced image and their evidence

| Range | Original evidence | Verdict |
|---|---|---|
| Header A / B (0x0, 0xA00), 0x930 bytes + zero pad | stores `0x14003ca0f..0x14003cc47` of `v2_write_handle_direct`; constants `.rdata 0x140f1a690..0x140f1a6fc` (version/flags `02 03`, `5`, `10`, `0x32`, `0xB4`, `0x14B4`, `0x1685`, `0x2800`, `0x800`); CRC by `crc_record(h,0x930)` = CRC-32 of 0x92C bytes | match, byte for byte (golden test) |
| counts (+8, +10) | case count and disc count of the validation loop (`iVar39`, `uVar38` saved at `0xd8/0xdc(%rsp)`) | match |
| generation (+12) | `proj+4==0 && proj+8==0 ? max(proj+0xc,1) : (proj+0xc==-1 ? 1 : proj+0xc+1)` | match (`generation_of`) |
| table CRCs (+0x50..+0x5C), menu SHA-1 (+0x60) | CRC of `zone+0x1400` (0x5000), `zone+0x6400` (0x10000), `zone+0x16800` (0x280000); menu CRC/SHA-1 over 0x930 bytes of each sector | match |
| hybrid MBR (+0x1B8..+0x1FF) | `gen ^ 0x32545350`, entry `00 00 02 00 DA FE FF FF` (`DAT_140f1a6f0`), start LBA 1, count `blocks-1` or `0xFFFFFFFF` (only if `capacity/512 > 1`), `55 AA` (`DAT_140f1a6f8`) | match; the count uses the capacity passed in (card size, or image target size) as the original |
| Case table (0x1400, 0x50 each) | `0x14003b0e6..0x14003ba02`: `CASE`, 1, disc count, slot, byte 7 = `case+0x11AD`, name 0x20, serial 0x10, art id (slot or `0xFFFF`), disc ids at +0x3A, CRC-32 of 0x4C at +0x4C | match. Byte 7 is the loader's `rec[7]` (`v2_load_source` stores `CONCAT11(rec[7],slot)` at case+4), so cards round trip; new cases write 0 |
| Disc table (0x6400, 0x100 each) | `0x14003b25c..0x14003b7f2`: `DISC`, 1, global index, case, slot, flags `(f>>6&1)|2`, serial 0x10, name 0x20, LBAs at +0x3C/+0x44/+0x4C = L, L, L+0x100, +0x54 = sectors*5, +0x5C sectors, +0x60 tracks, +0x61 BIN count, +0x62 art id (= global index or `0xFFFF`), +0x64 = L+5, +0x6C cheat size, +0x70 = L+0x19, +0x78 = 0xA00, +0x7C CRC-32, +0x80 SHA-1, +0xFC `crc_record` | match; bytes 0x68, 0x74, 0x94..0xFB stay zero as in the original |
| Case art (0x16800 + slot*0x2800) | `read_entire_file`, must be 0x2800 bytes, copied at `0x2800*slot` | match |
| Menu (0x296800 + i*0xA00) | menu copy (patched by `menu_defaults_patch` when the project has a valid 64 byte record), 0x930 bytes per slot | match; embedded blob verified equal to `[0x14019f900,0x1401d4f70)` of the exe |
| PSTCFG2 (0x2D0A00) | `0x14003c928..0x14003c9e3`: `PSTCFG2`, `.rdata 0x140f1a6e8` = `01 00 01 00` at +8, music LBA `0x168A` at +0x1C, size at +0x24, CRC-32 of the music at +0x28, 0x4C text bytes (`.rdata 0x140f1c1a0`, includes the NUL) at +0x2C, CRC-32 of 0x92C at +0x92C | match, byte for byte |
| Music (0x2D1400) | copy of `umbrella_music`, length 1..0x22EC00, always written | match; blob verified equal to the exe range |
| Package head (`L`, 0x6400 bytes) | `build_package_metadata.constprop.0 @0x140002a70`: `PSTTRK1` + entry size 0x10, entries `[begin, INDEX01, length, type, kind_bits, flag_bits]`, SBI at +0x640 (u16 len 4..0x2E8, CRC, data), cheats at +0xA00 (scope 2, <= 0x2800), Disc art at +0x3C00 (exactly 0x2800) | match; acceptance rules checked against a model written from the decompilation over 30000 random maps |
| gap `[L+0x32, L+0x100)` | `seek64 + write_exact` of zeroes (1 MiB pieces), before streaming, only for discs that are neither in place nor resumed; also clears the marker block | match |
| Resume marker (`L+0xFF`) | `0x14003c5f1..0x14003c6bb`: `PSTRSM1\0`, QPC stamp at +8, 0x100 byte record copy at +0x40 (after `crc_record`), optional `PSTVFY1\0` + `u64 {issues,accepted}` at +0x140/+0x148 (when `verified != 0`), CRC-32 of 0x1FC at +0x1FC; written with `io_write_block_verified` | match (stamp differs by design) |
| `PSTWIP1` (block 0) | `0x14003bea9..0x14003bed0`: 512 bytes, 7 byte magic, rest zero, after both headers were invalidated | match |
| RAW data (`L+0x100`, 0xA00 per sector) | `stream_disc`: 0x930 bytes of sector + 208 zero bytes, CRC-32 and SHA-1 over the 0x930 parts | match; CRC/SHA of Tactics Ogre (34267 sectors, several 0x800 sector chunks) re-derived with `zlib`/`hashlib` |
| Extent policy | `extent_allocate @0x1400198e0`, `extent_insert @0x1400199a0`: start 0x2800, align 256 blocks, first fit, 256 entries, overflow guards | match; property test against a brute force first fit |
| Size of an image | `v2_write_image_direct @0x14003d0a0`: `max(0x500000, current size)` then `max(estimate)`, `SetEndOfFile` before; `need` cut at the end | match |
| Order of writes | `0x14003be3d` invalidate A (0) then B (0xA00); `PSTWIP1`; per disc zero gap, stream, metadata sync, record CRC, flush, read back, marker; flush; body sync (0x1400..0x500000), compare; publish B (`edx=0xA00`, `0x14003cd67`), publish A (`edx=0`, `0x14003cdd6`, source = zone), compare 0..0x1400; truncate | match; header A is the last block written |

## Functions audited

`v2_write_handle_direct`, `v2_write_image_direct`, `v2_build_image`, `v2_estimate_project_bytes` (+ `_for_target`, constprop),
`extent_allocate`, `extent_insert`, `kept_package` (all combinations of kind, keep, phys, path, slot compared by hand
against the three nests of the original: the validation loop, the table loop and the estimate), `build_package_metadata`,
`v2_prepare_write`, `preflight_serials.constprop.0` (unrolled 5 discs), `serial_preflight_error`, `stream_disc`,
`v2_check_disc_source`, `v2_verify_disc_source`, `v2_admit_disc_without_full_check`, `manager_admit`,
`verification_issue_text`, `auto_attach_cheats` (cheat module owns the catalog), `all_ready`, the checks of `cmd_write`
and `write_image_direct_to`, `write_failure`, `result_msg`, `result_io_msg`; underneath: `handle_sync_blocks_verified`,
`io_write_block_verified`, `cd_sector_check` (+ `.part.0`, `ecc`), `crc_record`.

## Findings

### BUG (fixed)

1. **B1 An output path that aliases a source file destroyed the source.** `mh_path_equal_full` compared paths
   lexically, so a symlink or hard link to a BIN, CUE, SBI or art file passed the source clash test of
   `mh_v2_write_image_direct`; `CREATE` (O_TRUNC) then emptied the user's file and the write failed with
   "Gravacao interrompida". The test (`test_audit_alias_clash`) showed the BIN truncated before the fix. Fix: equal
   device and non-zero inode count as the same file (c6d3cd1). Same helper also protects duplicate detection and the
   resume path match.
2. **B2 `mh_io_size` returned 0 for `/dev/rdiskN`.** On macOS `lseek(SEEK_END)` on a raw disk yields 0 (checked on an
   attached disk image: `seek_end=0`, `DKIOCGETBLOCKCOUNT=40960`). The writer only saves the previous header (to restore
   header A when invalidating header B fails) if the target is at least 0x1400 bytes, so on every real card the restore
   path was dead code. `mh_io_size` now asks `DKIOCGETBLOCKSIZE/COUNT` (c6d3cd1). Test `test_audit_device_size` attaches a
   throw-away raw disk image, opens it read only and checks the size (skips when `hdiutil` is not usable).
3. **B3 An in place disc could lose its RAW data.** `mh_v2w_classify` treats a kind 5 disc whose `source_path` equals
   the target as "in place" (nothing is copied), but `mh_v2_write_image_direct` only opened the file as is when
   `image_loaded`/`image_path` or a kept package path matched. A project whose `image_loaded` flag was lost then got
   `CREATE_ALWAYS`: the file was truncated and the Disc table described RAW slots that no longer existed (no read back
   for in place discs, so the result looked like a valid library). Fix: such a disc also selects the open as is mode.
   Test `test_audit_source_is_target` (failed before: data gone, validator refused the image).

### DEVIATION-OK

* Marker stamp: monotonic nanoseconds instead of `QueryPerformanceCounter` (`MhV2Writer.stamp` overrides it); the only
  time dependent bytes of an image, and they are covered by the CRC at +0x1FC.
* `GetLastError()` after a failed stream is the recorded I/O error code here (0 when the failure was a cancel).
* `a_copy` vs `mh_str_copy`: identical unless a Latin-1 name longer than 31 bytes ends in 0x80..0xBF (never, names are
  at most 31 characters).
* Error texts keep "Erro Windows"/"Win32" with errno numbers; `/dev/rdisk<N>` replaces `\\.\PhysicalDrive<N>`.
* The "Disco N: name" tail of a failure message uses the record name (0x20 bytes), the original used the project name
  (0x21 bytes); identical for names of at most 31 characters.
* `fcntl(F_FULLFSYNC)` fails with ENOTTY on a raw disk and `fsync` succeeds (probed read only on a throw-away disk
  image); the writer therefore never records a bogus "Sincronizacao do destino" error on a card.
* `build_package_metadata` now also refuses a map that `mh_track_sector_valid` rejects (INDEX 01 outside its track),
  added by the firmware cross-check, stricter than the original; the audit model applies the same rule.

### RISK (not changed)

* **No sector validation while writing.** `stream_disc` in write mode only copies, hashes and compares with the CRC/SHA-1
  recorded at admission (when the disc was fully checked). The analysis pass checks only data tracks from INDEX 01:
  sync bytes, header zero bytes, mode byte equal to the CUE, the two subheader copies, EDC and ECC (Form 2 with EDC 0
  accepted). It does NOT check MSF address continuity, and never looks at audio tracks or at the pregap of a data track.
  A BIN with wrong MSF headers is accepted, as in the original. A disc admitted with "Verificacao completa desativada"
  (`verified_aux`) is only size checked; the stream then hashes whatever the BIN holds now.
* **Copy from a card or image (kind 4/5) is not compared with the source record** unless the disc was verified: the
  CRC/SHA-1 written are those of the bytes read. If `/dev/rdisk<N>` was reassigned to another device since the library
  was loaded, a different card would be copied without notice. The app checks identity before writes to the loaded
  card, not before reading a source.
* Kind 4 sources and `mh_v2_check_disc_source` open `/dev/rdisk<N>` with plain `open(2)`: it works for members of
  group `operator` (mode 0640) and fails with EACCES for others.
* A library with 99 tracks and an SBI block overlaps the last entry with the SBI header (entry 99 ends at 0x650, SBI at
  0x640). Same layout as the original; unreachable with real CUEs.
* Performance: every changed control block is written, `F_FULLFSYNC` + `fsync`, read back (about 2500 blocks for a first
  image, tens of seconds on APFS). Same pattern as the original (`FlushFileBuffers` per block). An image write is not durable-sensitive,
  a card write is.
* `MBR` sector count is the capacity (card size, or the image target size when the file is created) and an image is cut
  to `need` afterwards, as in the original.
* The cancel of a body sync is reported as "Falha persistente ao sincronizar a zona fixa ..." (original text for any
  `handle_sync_blocks_verified` failure).

### UNVERIFIED (outside what the decompilation or the tests can prove)

* Acceptance of every field by the console firmware (cross-check engineer; `FIRMWARE_CROSSCHECK.md`).
* Serialized cheat blob format (cheat module audit), menu defaults patch (menu module audit).
* Behaviour on a physical SD card: only the size ioctl and `fsync` were probed, read only, on an attached disk image;
  no write to any block device was made.

## Safety properties checked

* Header A is the last block written. Before the commit the card has no valid header: header A (block 0) is zeroed
  and verified, then header B, then `PSTWIP1` replaces block 0. A cancel or error at any later point (tested at six
  points of `test_cancel_and_resume`, plus truncated BIN, tampered BIN, damaged marker) leaves `PSTWIP1` at block 0 and the validator refuses the
  target.
* Publishing a header slot writes blocks 1..4 first and block 0 last, each verified; B before A.
* Resume only trusts a package whose marker block on the target equals the copy kept in memory; a card of another size
  or a damaged marker is refused before anything is touched.
* In place editing: the packages of kept discs (head, gap, marker, RAW) are byte identical before and after; a new
  disc takes the hole of a removed one (first fit); an interrupted edit leaves the kept discs intact
  (`test_audit_edit_inplace`).
* Overflow: all sector, LBA and size products are 64 bit (`(uint64_t)sectors * 0xA00`); a hostile preserved LBA near
  2^64 is refused by `extent_insert` (`end <= start`) and by the capacity test.

## Tests added (`core/tests/test_v2_write_audit.c`)

`golden` (fixed zone, Case/Disc record, package head and marker against constants dumped from the exe), `track` and
`model` (track map flags, SBI limits, refusals, 30000 random maps against a model of the decompiled loop), `extent`
(3000 random extent sets against a brute force first fit), `edit` (in place edit, kept packages byte identical, hole
reuse, interrupted edit and resume), `alias` (symlink, hard link, `..` path), `source` (B3), `devsize` (B2).
Run one with `MH_V2A_ONLY=alias`.
