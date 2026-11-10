# Audit: base, art, menu, disk

Scope: `core/src/base`, `core/src/art` (+ `platform/mac/mh_image_mac.m`), `core/src/menu`,
`core/src/blockdev` + `platform/mac/mh_disk_mac.*`, `tools/test-disk.sh`. Every function was read
side by side with the decompilation (`$RE/out/fn/<name>.c`), disassembly where Ghidra was
garbled (SIMD in `generate`, `cd_sector_check.part.0`), and checked against independent
implementations. Classes: BUG (fixed, with a test that failed first), DEVIATION-OK (differs from
the original on purpose or harmlessly), RISK (left as is, reason given).

## Independent verification performed

| What | How | Result |
|---|---|---|
| CRC-32, SHA-1, SHA-256 | 3000 random buffers (0..105 KB, chunked updates of 1..130 bytes) vs Python `zlib`/`hashlib` | identical. Original CRC is the plain reflected 0xEDB88320, init FFFFFFFF, final xor (bitwise loop in `crc32_update`), `crc_record` = CRC of len-4 bytes. |
| EDC/ECC Mode 2 Form 1 | Own Python ECMA-130 implementation (GF(2^8)/0x11D, RS parity from the syndrome equations, Q diagonals as words `(44*i+43*j) mod 1118`) vs 2857 real Form 1 sectors of Alundra | the Python code reproduces every real sector, so it is a valid reference |
| EDC/ECC Mode 1 | No Mode 1 sector exists in the library (440 BINs, all PS1 = Mode 2; 13,410 Form 1 and 4,314 Form 2 sectors sampled over all of them: 0 failures). The Python reference generated 200 Mode 1 + 100 Form 1 + 100 Form 2 sectors; `mh_cd_sector_check` accepted all, `recode` left them unchanged. Two known answers (SHA-1 of the full sector) are now in `test_base_cd.c::test_independent`. | OK |
| `cd_sector_check`, `recode`, `repair_sector`, `ecc` | Decompilation (offsets 0x810/0x818/0x92C, Form 2 EDC 0 = unused, ECC compare 0x81C..0x92F, Q at 0x8C8/0x8FC) | identical |
| `validate_track_sector` | decompilation + disassembly (CRC length 0x930 is not visible in Ghidra, confirmed `mov r8d,0x930`; "SBI\0" at 0xf14a78) | identical |
| `.art` encode | `generate` SIMD loop disassembled: `psubusb 0x1f` + `pcmpeqb 0` (alpha < 0x20 gives 0), `pmaxub 1`, R&E0 / G>>5<<2 / B>>6; palette loop (`(c*0xff/7|3)*0x1f+0x7f)/0xff`, `0x421` for 0); header `02 60 60 00 24`, palette size 0x200 | identical (3-3-2 claim confirmed) |
| 96x96 resize | decompiled magic-division by 95 = `floor(x/95)`, 16.16 split, row/col weight order; compared with a float bilinear in numpy for 7 shapes | port equals the original math; vs ideal float max error 1.98 (two truncations, as the original) |
| Case homography | constants read from `.rdata` (quad 80,20 / 590,31 / 592,539 / 81,583, 1e-12, 1e-6, 255.0, 0.5), matrix setup incl. `-0.0` entries, pivoting, determinant, inverse mapping and bilinear sum compared expression by expression; `FP_CONTRACT OFF` present. Independently solved with numpy `linalg` for 4 cover shapes: mapped region equals the ideal one (at most 2 border pixels of 273,000 differ), position error < 1 (8-bit gradient). `case_template.bgra` is byte identical to `.data` at 0x140040000. | identical |
| Disc mask | radius thresholds 0x90/0x22b2/0x2111/0x30f/0x121, ring 0xbe/0xc3/200 `>>1`, alpha 0xdc, rim 0x0808+8 (DAT_140f092f0 = 0808) | identical |
| Menu | `menu_defaults_read/patch/compatible`, `menu_user_sector`, `menu_find_root_file`, `v2_validate_menu_blob` (whole SYSTEM.CNF line parser), `v2_validate_menu_file`, `cmd_defaults`, `project_use_default_menu`; UTF-16 texts in `.rdata` (themes, languages, visuals, dialog strings) | identical |
| Disk | `sd_enumerate`, `system_disks`, `query_info`, `identity_matches`, `lock_volumes` against the port; real runs of `tools/test-disk.sh --images` (see below) | 2 BUGs, see below |

## Findings

### BUG (fixed)

1. **disk: volumes mounted through a synthesized APFS container were invisible.** An APFS card
   is a physical store `diskN` plus a container `diskM`; the volume mounts as `/dev/diskMsK`.
   `disk_mounts("diskN")` matched only `diskN`/`diskNsK`, so `mounted` was 0, `unmount_whole`
   said "already unmounted", and the raw open + write went ahead under a mounted APFS container.
   Fix (`mh_disk_mac.m`): mounts of whole IOMedia below the disk count; they are unmounted first
   (containers, then the disk itself); the post-unmount check and `dev_recheck` use the same
   list. Test: `test_apfs_image` in `platform/mac/tests/test_disk_mac.m` (failed on
   `mounted == 1`, then on the unmount, now passes). Commit `fix(disk)` 9560036.
2. **disk: unmount left the card unmounted when a later step failed** (busy second container,
   refused authorization dialog, allocation failure). The volumes already unmounted are now
   mounted again (`DADiskMount`, best effort). Same commit. Not covered by an automated test
   (needs a refused authopen dialog); reviewed by hand.
3. **disk: unmount callback context on the stack.** After the 20 s timeout the callback could
   still fire into a dead frame. The context is heap allocated (leaked on timeout on purpose).
4. **disk: raw descriptor without close-on-exec.** `open(2)` had no `O_CLOEXEC` and descriptors
   received from `authopen` never have it, so a spawned `hdiutil`/`diskutil`/`authopen` inherited
   an open writable raw disk. `FD_CLOEXEC` is now set in `mh_disk_open_raw` (test:
   `fcntl(F_GETFD)` on the write handle).
5. **base: a typed `/dev/rdiskN` reached a writer.** `mh_io_open` (RDWR/CREATE),
   `mh_write_entire_file` and `mh_copy_file_safe` opened any path, so the V2 image writer
   (path from a save panel), cue/art/cheat exporters could write raw to a disk that never went
   through `mh_disk_eligible` + identity. Writers now accept regular files only and truncate
   after the check (`ftruncate`, not `O_TRUNC`). Test with `/dev/null` in `test_base_io.c`. The
   code change is in c6d3cd1 (swept in by a concurrent commit of another engineer), the test in
   90df7ed.
6. **base: collector order differed from NTFS.** `FindFirstFile` on NTFS returns names in upper
   case order (`_` after letters); the port sorted by lower case (`_` before letters), which
   changes which CUEs survive the 256 limit and the duplicate/ignored order. `name_cmp` folds to
   upper case. Test `ord/` in `test_collect` (failed, passes). 1d999f2.
7. **menu: header comment pointed at the wrong record.** `menu.h` said sector 57 byte 1372, which
   is the plain data copy of the magic string; the real record is sector 60 byte 152
   (file offset 141272). Comment fixed (the code was right).

### DEVIATION-OK

* `base`: `normalize_ascii_title`, `clean_ascii` (n == 1 overflow fixed), `trim_line`,
  `normalize_serial`, `a_*`/`w_*` comparisons, `bytes_to_hex`, `wbuf_*` (growth 128, x2 to 32 KiB
  then +32 KiB, `wbuf_size` 1.5 KB format), `path_basename/dirname/join/stem/change_ext`,
  `path_equal_full`, natural compare + insertion sort, `read_entire_file` (+2 zero bytes,
  < 2 GiB), `write_entire_file`, `crc_file_region`, `read_raw_payload_at`, `v1_io_read/write`,
  `io_write_block_verified`, `handle_sync_blocks_verified` (4 attempts, sleeps 80..320 ms, final
  re-read of the whole region), `handle_compare_bytes`: equal to the decompilation. Known and
  accepted differences: UTF-8 instead of UTF-16 (caps in bytes), `/` instead of `\`, AppleDouble
  `._` names skipped by the collector, `mh_path_equal_full` now also compares device+inode (other
  engineer, c6d3cd1), `mh_ensure_dir` fails on a plain file, `mh_io_*` give up on a short
  `pread`/`pwrite` (like the original), no seek error branch (pread has no seek).
* `art`: the loader (`mh_image_mac.m`) goes through ImageIO + sRGB: images with another ICC
  profile (Display P3 screenshots, gray gamma) are colour managed, GDI+ ignored profiles; semi
  transparent pixels are un-premultiplied with rounding. Honest improvement, not a bug.
* `menu`: sector 16 and index bounds are checked (the original read out of range).
* `disk`: stricter than the original (block size 512, no synthesized, no optical, no internal
  fixed disk, `has_v2` alone no longer qualifies an internal disk, identity includes model,
  serial and IOMedia registry id, geometry from the fd).

### The `.art` palette question

* The Manager never builds an adaptive palette. The only art writer in the executable is
  `generate` (plain / case / disc), which emits the 3-3-2 palette byte for byte as the port does;
  there is no quantizer, median cut or palette function in the 372 functions of the callgraph.
* The 17,358 database files and the 222 `*-CASE.art`/`*-DISCO.art` of the library are not Manager
  output: entry 0 is 0 (transparent), every other used entry has bit 15 set, 224..256 colours are
  used, header is `02 60 60 00 24`. They come from the author's offline database build. So custom
  art made by the Manager is 3-3-2 (8 levels of R and G, 4 of B) exactly as in the original
  (visibly more posterized than database art). A better quantizer would be a deliberate deviation;
  nothing is missing from the port. Opaque black becomes index 1 (a dark blue, 0x421 never occurs),
  same as the original.

### RISK (left)

* `mh_authopen` is exported and accepts any absolute path; its only caller validates the
  device name. Defence in depth would be to restrict it to `/dev/rdisk<valid>` (the test harness
  uses a temp file, so not done).
* `mh_file_size64` opens with plain `O_RDONLY`: a FIFO named in a CUE would block.
* Identity: `media_id` is compared only when both records have one (0 on the portable stand-in).
  On a real disk `query_one` marks the disk as system when the IOMedia cannot be found, but a
  failing `IORegistryEntryGetRegistryEntryID` would silently weaken the check.
* `mh_blockdev_recheck` is called at open, not again before the first write; the window is the
  few ms between `open_dev` and the first `pwrite`. The fd geometry check means a swapped card of
  a different size is caught, an identical card is not distinguishable without the registry id.
* `unmount_whole` unmounts by BSD name before the open: if the card is swapped in that window an
  unrelated disk is unmounted (not written: the post-open recheck refuses it).
* Test mode (`MH_DISK_TEST_ALLOW_IMAGES=1`) skips the bus/internal rules for anything classified
  as an image; a USB device that reports the model "Disk Image" would be classified as one. Only
  reachable with the environment variable set by the user.
* `authopen` timeout path: `kill(SIGTERM)` then a blocking `waitpid`; a hung authopen that ignores
  SIGTERM would block the caller.
* ECC/EDC cannot be validated against a real Mode 1 dump (none available); covered by the
  independent Python reference only.

## Test harness safety (`tools/test-disk.sh`)

Without `--images` nothing is opened for writing (`mh_blockdev_open_write` on system and large
disks returns before any `open`, invalid names never become paths). With `--images` the only
writable devices are nodes returned by `hdiutil attach` for image files created by the test in a
fresh `mkdtemp` directory, matched by `^/dev/disk[0-9]+$`, cross-checked with `hdiutil info`
(image path equality), and detached by the same list (never by number guessed later). Run during
this audit (`--images`): passed; afterwards `hdiutil info` listed only other engineers' images,
`diskutil list` unchanged, disk0, disk3 and disk6 untouched (disk6 is the 2 TB external, refused
by the size rule on every run). Harness improvement: `cleanup()` closes leaked raw descriptors
before detaching (a failed CHECK with an open device left a stale image attached once).
Incident: while cleaning that stale image I detached `scratchpad/audit/t20.dmg` of another
engineer by mistake (read the wrong line of `hdiutil info`) and re-attached it with `-nomount`
immediately; use `hdiutil info -plist` when matching devices to images.
