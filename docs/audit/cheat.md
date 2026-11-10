# Audit of module `cheat`

Scope: `core/include/mh/cheat.h`, `core/src/cheat/*`, tests `core/tests/test_cheat*.c`. Method: every function side by
side with the Ghidra output (`out/fn/*.c`), disassembly where the decompiler was ambiguous, then hostile input and seeded
fuzzing under ASan/UBSan. Classes: BUG (behaviour differs from the original in a way that matters), DEVIATION-OK
(differs on purpose or harmlessly), RISK (works, but depends on something outside the module or inherits an original
quirk).

Run `make test-cheat`. With `MH_EXE=/path/to/mh64.exe` it also runs `test_cheat_exe` (the byte comparison against the
executable; it prints SKIP otherwise). `MH_FUZZ_SCALE=n` multiplies the fuzz iterations (n=6 was run once: 830k checks,
no crash).

## Verified against the executable (permanent test `core/tests/test_cheat_exe.c`)

* **Derived bank = embedded `v5_compact_bank`, for all 10118 groups.** The test parses the 497,601 bytes at file offset
  0xF1C220 (VA 0x140f1d220) with its own code, independent of the port's pinned digest in `test_cheat.c`, and compares
  per title the first/count table, per group the name, id, line count and both line forms ("a" decoded, "b" with 30/80
  turned into 00). It also proves that nothing in the original bank is left unexplained: the name pool is allocated
  sequentially with first-use sharing, the line pool likewise (a form then b form per group, identical strings shared),
  every byte of both pools is covered by some group, header counts/offsets/size/CRC agree. About 450k checks. Flipping one
  byte in a copy of the exe makes it fail. The longest bank name has 45 characters and the alphabet is
  `A-Z 0-9 space ' - .`, so the 47 character and charset limits of the compact format never touch audited groups.
* `resources/embedded/cdx_cheats.bin` occurs exactly once in the exe, byte for byte (1,060,037 bytes, file offset
  0x1DFB90), and `kExpectedSha256` (VA 0x140f1d080) equals the database SHA-256 computed by the port.
* `kCheatSerialBindings` has exactly 618 records (the table ends where the SHA-256 constant starts); every record equals
  `serial_bindings.inc`, the serials are strictly ordered and each one resolves to its title through
  `mh_cheat_db_find_title_for_disc`.
* An independent Python LZ4 decode of the container gives the same image (SHA-256 1b48...f1a3, CRC A06CFC31), and the
  claim "79 unusable database entries" (ops outside 30/31/70/80/90, or a trailing 70/90) is exact.
* All 131 wide string literals of the cheat functions in the decompilation appear byte for byte in the port (error
  texts, ASCII without accents).

## Functions audited

Database: `cheat_db_load` (container checks, LZ4 decode loop, each table bound, string run check, game/title/variant/
cheat/program/text loops with their error codes, SHA-256 and CRC-32 order), `cheat_db_free`, `cheat_db_last_error`,
`mh_string[_w]`, `cheat_db_title_w`, `title_variant_count/_at`, `variant_info`, `entry_info`, `entry_text`, `copy_entry`,
`find_title_for_disc`, `find_title_normalized`, `title_matches` (control flow of the three comparisons; the vectorised
copy loops were read, not re-derived), `filter_match`, `v5_program`, `v5_ref_group`, `compact_position`,
`profile_needs_wire`, and the opcode scan that the original repeats inline.

Profile: `new`, `free`, `clone`, `remove` (+ `.part.0`), `find_audited`, `enabled_count`, `total_lines`,
`active_c0_count`, `prepare_hardware`, `validate` (== `validate_hardware`), `serialized_size`,
`profiles_validate_combined`, `profile_active_stats.part.0`, `add_entry`, `add_audited`, `add_catalog_entry`,
`add_custom[_disabled]`, `restore_user_state`, `build_exact_catalog` (variant choice by group count, budget estimate,
skipping of unusable entries), `build_for_disc_active`.

PSTCHT1: `cheat_blob_scope`, `cheat_blob_validate` (v4/v5/v6 dispatch, header, provenance, record loop, every message),
`disc_bank_valid` (v6, including the 0xF8000001FC009F3E charset bit test, the 47 character cut, sequential offsets, the
second pass against the bank), `cheat_profile_serialize` (wire and compact, sizes, flag bytes, CRC, final
`disc_bank_valid`), `cheat_profile_deserialize` (v4/v5/v6, state byte, simple-flag rebuild, legacy branch). Cross-checked
with the V2 writer (`build_package_metadata`: scope 2, `len > 0x2800` rejected, blob at +0xA00, size at +0x6C) and the V2
validator (`cheat_blob_validate` plus scope 2).

Text: `parse_code_line`, `cheat_parse_codes_w` (line splitting, dialect rules, 255 line cap, V5 structure check, classic
loop and every message), `classic_expand_slide`, `cheat_codes_to_text`, `import_flush`, `cheat_profile_import_text[_dialect]`
(file reading and BOM rules, 511 unit lines, 127 unit names, `#activation=` family, "NAME CODE" split, hex-run
heuristics, flush order, atomic commit), `cheat_profile_export_text`, `normalize_ascii_title` (through base).

Not audited here: the cheat UI (`app/cheat_ui.m`), `mh_sha256` and `mh_crc32` themselves (base module; the exe test and
the known database digest exercise them).

## Findings

| # | class | where | finding | resolution |
| - | - | - | - | - |
| 1 | BUG | `mh_cheat_parse_code_line` | The wrapper copied the text to a C string and converted it up to the first NUL, so an embedded NUL ended the text. The original works on UTF-16 with an explicit length and rejects a NUL that it reaches before `;` `#` `//` or a backslash (a NUL is not a hex digit, separator or comment start). `"8005E9CC 03E8\0 junk"` was accepted. | Fixed (`utf8n_to_w` keeps NUL units). Regression tests in `test_cheat_text.c` failed before and pass after. |
| 2 | BUG (hardening) | `mh_cheat_profile_active_stats` | The public function dereferenced a NULL profile; the original guards NULL in its public wrapper and only the `.part.0` body does not. | Fixed, regression test in `test_cheat_profile.c`. |
| 3 | DEVIATION-OK | decision (a), derived bank | Verified true, see above. The only residual difference: the original read the bank from `.rdata` even when the database had failed its SHA-256; the port then has no bank. The database is an embedded constant, so a failure means a damaged resource file. | Permanent test added. |
| 4 | DEVIATION-OK | decision (b), v5/v6 without a database | With no active database v5/v6 blobs are rejected, v4 still works, `profile_needs_wire` is 1 for audited profiles and `validate` uses the wire size. Consistent everywhere (validate, serialize, serialized_size, deserialize). | See R1. |
| 5 | DEVIATION-OK | legacy CRC 3CC8BE42 | Confirmed unreachable. `cheat_db_load` only accepts A06CFC31; `cheat_blob_validate` for a v4 blob with game != 0 requires `+0x34 == A06CFC31` ("Proveniencia ..." otherwise) and runs before `deserialize`; v5/v6 also require it. The legacy branch of `deserialize` is dead code kept for fidelity (exercised by `test_legacy_and_refs` up to the validator). | None. |
| 6 | DEVIATION-OK | UTF-8 versus UTF-16 | Database strings and export are UTF-8 in memory; the importer keeps UTF-16 internally, so the 511 unit line and 127 unit name limits match. Strict UTF-8 reading equals `MB_ERR_INVALID_CHARS`; lone surrogates from UTF-16 files end up as separators in names like in the original. `mh_string_w` fallback is truncation instead of widening bytes. | Documented in `docs/map/cheat.md`. |
| 7 | DEVIATION-OK | `import_flush`, `codebuf` | `codebuf` holds 513 units, the original 512. A code part is at most 510 units, so nothing observable. | None. |
| 8 | RISK | R1, global bank | The bank is global state published by `mh_cheat_db_load`. Loading a second `MhCheatDb` displaces the first, freeing the active one leaves none, and results of `validate`/`serialize` then change (wire size instead of compact size). Same shape as the original `g_active_cheat_db`, but the original bank itself was constant. The app loads once at start (`MHApp.m`) and frees at exit. A V2 card with a v6 blob read while no database is loaded fails with "Catalogo do disco invalido ou divergente do banco original." | Callers must keep the database alive for the session. |
| 9 | RISK | R2, custom name charset | `profile_needs_wire` checks only the length (47) of a custom name, but `disc_bank_valid` accepts only `A-Z 0-9 space & ' - .`. A custom name such as "my code" is accepted by `add_custom` and then `serialize` fails with "Catalogo original invalido ou divergente." instead of falling back to the wire format (reproduced). Same in the original. `app/cheat_ui.m` and the importer normalise names with `mh_normalize_title`, so it cannot happen from the UI. | Kept for fidelity. |
| 10 | RISK | R3, API misuse | `mh_cheat_profile_deserialize` with a NULL `out`, and `mh_cheat_db_load` into a struct that is already loaded (leaks the old image, as the original), are not guarded. `mh_cheat_profile_import_text*` replaces the contents of `*p` on success, so pointers into `p->entries` held by a UI go stale. | Documented. |
| 11 | RISK | R4, lower-case audited names | An audited entry whose name equals a bank name only up to case (possible through a hand edited v4 blob) matches `compact_position` (case-insensitive) but fails the v6 charset test at serialize. Original behaviour. | Kept. |
| 12 | RISK | R5, title search | `cheat_db_title_matches` and `find_title_normalized` were compared on control flow (the Ghidra output is heavily vectorised) and fuzzed for memory safety, but their results were not compared exhaustively against the original. | Open. |

Everything else matched in constants, limits, flag semantics, order of checks and integer widths. Examples checked in
detail: the bit tests `0xF8000001FC009F3E`, `0xFF81FFFFFF817C00`, `0x400200100000200`, `0x2400200000000200`; the
`& 0xffdf` and `& 0xffef` tricks; the u16 wrap of the slide value; the `0xfe`/`0xff` line caps; `n * 8 + 0x40` and
`n * 0x60 + 0x40` offsets; the u16 total at +0x28 in v6 versus u32 in v4/v5; the state byte `(ver == 5) * 2`; wire header
word `0x00400004` and compact `0x00400006`; the flag byte of custom entries in v6 `((f >> 2) & 0x10) | (f & 1) | 0x28`.

## Hostile input (permanent, `core/tests/test_cheat_fuzz.c`, seeded, deterministic)

* LZ4/CDXLZ1: random streams behind a correct header (6000), truncations, extensions and bit flips of the real container
  (150), all with exact-size heap copies so ASan sees every over-read. Every attempt ends in error 0xEA61 without touching
  memory outside the buffers.
* Database image: the real image re-encoded as a literal-only stream (proves the harness), then 700 mutated images
  (header, tables, string table; header CRC repaired in half of them) all fail with a code, never load, and do not
  displace the good database. Table, count and offset checks use 64 bit arithmetic.
* PSTCHT1: 60,000 mutants per run of v4, v5 and v6 seeds (header-biased byte/word writes with 0, 0x100, 0x101, 0x1ff,
  0x2800, 0x2801, 0xffffffff, ..., truncation, extension, repaired CRC and length). `validate`, `scope`, `disc_bank_valid`
  and `deserialize` never read outside the buffer; whatever `deserialize` accepts also validates as a profile and
  re-serializes into a blob that validates. Counts and offsets are bounded by `len` before use; code offsets must equal
  the running offset, so references can neither overlap nor skip.
* Importers and parsers: 14,000 mutated TXT/CHT files per run (UTF-8, UTF-8 BOM, UTF-16LE with random high bytes,
  duplicated chunks) against random dialect arguments, with the code parsers run on the same bytes; plus a 512 unit line
  (the 511 message), a file of 0x400001 bytes (unreadable) and one of exactly 4 MiB (read), 270k code lines under one
  header (255 line cap) and thousands of one-line blocks (256 entry cap) with no quadratic behaviour. Accepted imports
  always leave a profile that validates.
* Database queries with arbitrary indexes, buffer sizes and strings; `copy_entry`, `add_audited`,
  `add_catalog_entry`, `build_exact_catalog` on random arguments.
* Leak check: `leaks --atExit` on a non-sanitized build of the cheat tests including the fuzz reports 0 leaks.
* The harness itself was checked: deleting the `len - code_off < clen` bound from `cheat_blob_validate` in a scratch copy
  makes ASan report the over-read at once.
