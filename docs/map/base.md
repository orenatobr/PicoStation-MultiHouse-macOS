# base module (wave 0)

Headers: `core/include/mh/base.h` (umbrella) including `common.h`, `str.h`, `path.h`, `hash.h`,
`cdsector.h`, `io.h`. Sources: `core/src/base/*.c`. Tests: `core/tests/test_base*.c`
(`make test-base`, ASan/UBSan). All addresses are virtual addresses in `mh64.exe`.

## Original function to port function

### Memory and arithmetic

| Original (VA) | Port | Notes |
|---|---|---|
| m_memset 14001e3e0, m_memcpy 14001e3d0, m_memcmp 14001e3c0 | libc `memset`, `memmove`/`memcpy`, `memcmp` | byte loops, no behaviour of their own. m_memcmp returned the byte difference, callers only test for 0. |
| m_strlen 14001e3f0, w_strlen 14003e0f0 | `mh_strlen` | NULL gives 0. |
| p_alloc 140025200, p_calloc 140025230, p_realloc 1400252a0, p_free 140025260 | `mh_alloc`, `mh_calloc`, `mh_realloc`, `mh_free` | size 0 allocates 1 byte; realloc(NULL) allocates. |
| u64_div 1400324d0, u64_mod 140032520, u64_mul_u32 140032560 | `mh_u64_div`, `mh_u64_mod`, `mh_u64_mul_u32` | software helpers; divide by zero gives 0 as in the original. |

### Strings (UTF-8 `char *`; a_* and w_* collapse into one family)

| Original (VA) | Port | Notes |
|---|---|---|
| a_copy 1400010f0 / w_copy 14003ddf0 | `mh_str_copy` | truncates at n-1 bytes; never leaves half a UTF-8 sequence. |
| a_cat 140001060 / w_cat 14003dce0 | `mh_str_cat` | full destination is left unchanged. |
| w_cmp 14003dd90 | `mh_str_cmp` | -1/0/1; NULL is "". |
| a_icmp 140001150 / w_icmp 14003dfb0 | `mh_str_icmp` | ASCII fold only. a_icmp compared signed chars, here unsigned (same for ASCII). |
| a_starts_i 140001220 / w_starts_i 14003e080 | `mh_str_starts_i` | NULL gives false. |
| w_ends_i 14003de60 | `mh_str_ends_i` | NULL or empty suffix gives true. |
| a_upper 140001290 | `mh_str_upper` | |
| ascii_find_i 140002820 | `mh_str_find_i` | returns -1 for an empty needle. |
| ascii_from_w 1400028c0 | `mh_ascii_from_utf8` | non-ASCII UTF-16 unit becomes '?' (astral characters give two). |
| wide_from_ascii 14003ea60 | `mh_utf8_from_latin1` | widened bytes one to one, so bytes >= 0x80 are Latin-1. |
| bytes_to_hex 140002f50 | `mh_bytes_to_hex` (+ `_lower`, `mh_hex_decode`) | upper-case; the 64 character string it indexes at +0x40 is the same string `normalize_ascii_title` uses as its fold table (0x140f1a800). |
| clean_ascii 140010c70 | `mh_clean_ascii` | the original overran the buffer for n == 1; fixed. |
| normalize_ascii_title 140024e50 | `mh_normalize_title` | input is UTF-8 here (was UTF-16); table at 0x140f1a800 reproduced. Kept characters: A-Z a-z 0-9 `'` `-` `&` `.`. |
| normalize_serial 140024f30 | `mh_normalize_serial` | in place, max 15 characters. |
| trim_line 140032420 | `mh_trim_line` | leading space/tab; trailing space, tab, CR, LF only (mask 0xfffffffeffffd9ff). |
| wbuf_init 14003e290, wbuf_free 14003e110, wbuf_reserve 14003e420, wbuf_putc 14003e2a0, wbuf_puts 14003e390, wbuf_u32 14003e700, wbuf_u64 14003e710, wbuf_hex32 14003e150, wbuf_size 14003e4f0 | `mh_wbuf_*` on `MhWbuf` | growth 128, doubling to 32768 then +32768, like the original. `mh_wbuf_size` prints "1.5 KB" (suffixes B, KB, MB, GB, one truncated decimal). `mh_wbuf_printf`/`putn`/`cstr` are additions. |
| result_io_msg.part.0 14002dcb0 | `mh_err_fail_code` | message plus " (erro Win32 N)". The wording is kept (rule 4); the code is an errno on macOS. |
| progress_cb 140026b40, pump_messages 14002b100 | `mh_progress_report`, `mh_progress_yield` | only the "report, then test the cancel flag" part; the window updates belong to the UI. |

### Paths and files

| Original (VA) | Port | Notes |
|---|---|---|
| path_basename 140025480, path_dirname 1400258b0, path_join 140025bc0, path_stem 140025df0, path_change_ext 140025550 | `mh_path_basename/dirname/join/stem/change_ext` | both `/` and `\` are separators on input (CUE files); built paths use `/` (original used `\`; the constants are at 0x140f1392e "." and 0x140f13932 "\\"). path_dirname of a path with no separator is "." and of "/x" is "". |
| (GetFullPathNameW calls in path_equal_full, collect, game_directory) | `mh_path_full` | lexical only: cwd + path, collapses `//`, `.`, `..`. |
| path_equal_full 1400259a0 | `mh_path_equal_full` | compares full paths with ASCII case folding like the original. |
| game_directory 14001b2e0 | `mh_game_directory` | generic enough to live here. Parent folder is used when the folder name normalises to start with "DISC ", "DISCO " or "CD " ("CD2" does not match). |
| path_exists 140025ba0, dir_exists 140017bb0, file_exists 14001b010 | `mh_path_exists/dir_exists/file_exists` | |
| ensure_dir 140019780 | `mh_ensure_dir` | mkdir -p, 1023 byte cap. Deviation: also returned true when a plain file had that name (ERROR_ALREADY_EXISTS); not here. |
| file_size64 14001b040 | `mh_file_size64` | |
| read_entire_file 14002b700 | `mh_read_entire_file` | < 2 GiB, +2 zero bytes. |
| write_entire_file 14003f620 | `mh_write_entire_file` | in place, no temp file, not atomic; flush (F_FULLFSYNC) only after a successful write, result of the flush ignored. A failure half way leaves the partial file (as the original). |
| copy_file_safe 140012e10 | `mh_copy_file_safe` | CopyFileW with overwrite. Added: refuses src == dst, removes a partial dst on failure. |
| read_exact 14002b8a0, write_exact 14003f720, seek64 14002fff0 | `mh_read_exact`, `mh_write_exact`, `mh_seek64` | on an `int fd`; EOF counts as failure. |
| collect 1400120a0, import_collect 14001cd00 | `mh_import_collect` (static `collect`) on `MhCollect` | error strings byte for byte (`"Nao foi possivel acessar:"`, `"O lote excede 256 CUEs. Divida a importacao em lotes menores."`, `"Limite de subpastas excedido:"`, `"Caminho muito longo:"`, `"Caminho invalido:"`, `"Nao foi possivel listar a pasta:"`, `"A pasta foi desconectada ou mudou durante a leitura:"`), each followed by `\r\n` and the path. Limits 256 CUEs, depth 32, path 1024 bytes. Reparse points became symlinks (skipped, `ignored`++). Directory entries are processed in ASCII-case-insensitive name order (NTFS order). Addition: names starting `._` are skipped. |
| import_sort 14001d270 | `mh_import_sort` | stable insertion sort. |
| import_natural_compare 14001cff0 | `mh_import_natural_compare` | digit runs by value, letters folded to upper case, other characters raw. |

### Hashes

| Original (VA) | Port | Notes |
|---|---|---|
| crc32_begin 140012e20, crc32_update 140012e40, crc32_end 140012e30 | `mh_crc32_begin/update/end`, `mh_crc32` | IEEE CRC-32, table driven. |
| crc_record 140012fb0 | `mh_crc_record` | CRC of all but the last 4 bytes. |
| crc_file_region 140012ef0 | `mh_io_crc_region` | takes an `MhIo`. |
| sha1_init 1400309f0, sha1_update 140030a20, sha1_final 140030770, sha1_block 140030330 | `mh_sha1_*`, `mh_sha1` | context was 0x68 bytes: h[5] at 0, total length at 0x18, buffer at 0x20, fill at 0x60. Standard SHA-1. |
| sha256_block 140030ac0 (+ inline code in cheat_db_load 1400070f0) | `mh_sha256_*`, `mh_sha256` | the original only had the block function; init (IV at 0x140f0dd80), buffering and padding were inlined in `cheat_db_load`. Standard SHA-256: the test decompresses the embedded cheat database and confirms its SHA-256 equals `kExpectedSha256` (0x140f1d080) and its CRC-32 equals 0xA06CFC31, the constants the original checks. |

### CD sectors

| Original (VA) | Port | Notes |
|---|---|---|
| ecc 140018ab0 | `mh_cd_ecc` | P (86x24, stride 86) and Q (52x43, stride 88) parity at 0x81C and 0x8C8; for mode byte 2 the 4 header bytes are zeroed while computing. |
| cd_sector_check 140005050 + .part.0 1400050f0 | `mh_cd_sector_check` | returns 1 (malformed), or 0 / 2 (EDC) / 4 (ECC) / 6. Form 2 sectors with a stored EDC of 0 are accepted and have no ECC check. |
| cd_sector_recode 1400055d0 | `mh_cd_sector_recode` | returns the check result from before the repair. |
| repair_sector 14002d880 | `mh_cd_repair_sector` | Mode 2 Form 1 only, unconditional. |
| (tables, filled lazily at first use inside cd_sector_check.part.0) | `mh_cd_edc_table`, `mh_cd_ecc_forward_table`, `mh_cd_ecc_backward_table`, `mh_cd_edc` | symbol addresses: backward 0x140e7ad80 (256), edc_table 0x140e7ae80 (1024), forward 0x140e7b280 (256). These are all zero in the file on disk (initialised at run time) so they cannot be compared with exe bytes; the test regenerates them with the original loop (xtime with 0x11d, EDC polynomial 0xD8018001) and, more convincingly, every sampled sector of real dumps passes. |
| validate_track_sector 14003d7a0 | `mh_track_sector_valid` | it is the V2 "PSTTRK1" track map sector (CRC over all 2352 bytes). Lives here because the brief listed it; the V2 engineer can call it directly. Layout in `cdsector.h`. |

### I/O

| Original (VA) | Port | Notes |
|---|---|---|
| v1_io_read 140034da0, v1_io_write 140034f60, v1_io_flush.part.0 140034d40 | `mh_io_read`, `mh_io_write`, `mh_io_flush` on `MhIo` | pread/pwrite, 256 KiB pieces, `MhIoErr` replaces the global record at 0x140f08ed0 (set flag, Win32 code, label, offset, requested, transferred; first error wins, never cleared by the code). Labels kept in Portuguese. `is_dest` selects "Leitura do destino" vs "Leitura da origem". The caller formats the report with `mh_ioerr_append` (text taken from the SD writer at 0x14003ac70). |
| io_write_block_verified 14001d3f0 | `mh_io_write_block_verified` | write, flush, read back, compare; alignment error code 0x57, compare error 0x17 keep the original numbers. |
| handle_sync_blocks_verified 14001c360 | `mh_io_sync_blocks_verified` | 4 attempts, sleeps 80/160/240/320 ms; errors from the diff pass and the cancel are not retried. The progress callback phase code (2) is dropped; the UI passes `text`. |
| handle_compare_bytes 14001c1f0 | `mh_io_compare_bytes` | phase code 6 dropped. |
| read_raw_payload_at 14002b940 | `mh_io_read_raw_payload` | 2560 byte slot = 2352 bytes + 208 zero bytes. |

## Left to other modules

* `mh_string` 1400249e0 and `mh_string_w` 140024a70: accessors of the embedded database blob
  (string table at the offset stored at +0x4c); only called by the cheat database code. `mh_string_w`
  uses `MultiByteToWideChar(CP_UTF8)`, the port needs UTF-8 to UTF-8 so it reduces to a bounds checked copy.
* `msf` 140024cf0: parses "MM:SS:FF" for the CUE parser (returns frames, rejects overflow, sec < 60, frame < 75).
* `sd_write_block_verified`, `sd_*`, `lock_volumes`, `system_disks`, `confirm_session_marker`,
  `cache_region`: SD/device specific, built on `MhIo` by their own modules.
* The CRT stubs `memcpy`/`memset`/`memcmp` (compiler support), `FUN_140001000` (stack probe).

## Findings

* docs/PORTING.md gives `.rdata` file offset 0xF0F000; the PE section table says 0xF08000
  (`.rdata` VA 0x140f09000 maps to file offset 0xF08000, size 0x8DA00). `.data` 0x3F000 and `.text` 0x400 are right.
* The EDC/ECC tables in the exe image are zero (built at first use), see above.
* `normalize_serial` has no callers in the exe, nor have `a_upper`, `a_starts_i`, `bytes_to_hex`,
  `ascii_from_w`, `path_exists`, `dir_exists`; they are ported anyway.
* `write_entire_file` is neither atomic nor locked against readers (share mode 0 on Windows made
  the file exclusive while writing); callers that need atomic replacement must write a temp file and rename.
