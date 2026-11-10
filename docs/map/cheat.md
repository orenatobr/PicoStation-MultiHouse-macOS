# Module `cheat`

Header `core/include/mh/cheat.h`, sources `core/src/cheat/`, tests `core/tests/test_cheat*.c` (`make test-cheat`).
Data formats: docs/EMBEDDED_DATA.md (CDXLZ1, MHGSF1, serial bindings, PSTCHT1).

| file | content |
| --- | --- |
| `cheat_db.c` | CDXLZ1 decoder, `mh_cheat_db_load`, string table, title/variant/entry queries, title search, serial bindings |
| `cheat_bank.c` | derived V5 compact bank, `v5_program`, opcode scan, `compact_position` |
| `cheat_profile.c` | profile container, limits, validation, add/remove, catalog building, restore of user choices |
| `cheat_blob.c` | PSTCHT1 serialize, deserialize, `blob_validate`, `disc_bank_valid` |
| `cheat_text.c` | code line parser, MHDialect, classic GameShark conversion, TXT/CHT import, export |

## Function map

Database (`mh_cheat_db_*`, `MhCheatDb` = the 0x850 byte block):

| original | port |
| --- | --- |
| cheat_db_load @0x1400070f0 | `mh_cheat_db_load(db, blob, len)` (the blob comes from the caller instead of `pst_cdx_cheats_start/end`) |
| cheat_db_free @0x140007080, cheat_db_last_error @0x1400070e0 | `mh_cheat_db_free`, `mh_cheat_db_last_error(db)` |
| g_active_cheat_db | `mh_cheat_db_active()` |
| mh_string @0x1400249e0, mh_string_w @0x140024a70 | `ch_db_string_raw`, `mh_cheat_db_string` (UTF-8 out) |
| cheat_db_title_w @0x140008bc0, cheat_db_title_variant_count @0x140008b90, _at @0x140008b50 | `mh_cheat_db_title`, `..._title_variant_count`, `..._title_variant_at` |
| cheat_db_variant_info @0x140008e60 (0xfc byte struct) | `mh_cheat_db_variant_info` / `MhCheatVariantInfo` |
| cheat_db_entry_info @0x140006490 (0x114 byte struct) | `mh_cheat_db_entry_info` / `MhCheatEntryInfo` |
| cheat_db_entry_text @0x140006680 | `mh_cheat_db_entry_text` |
| cheat_db_copy_entry @0x140006240 | `mh_cheat_db_copy_entry` |
| cheat_db_title_matches @0x1400082c0 | `mh_cheat_db_title_matches` |
| cheat_db_find_title_normalized @0x140006830 | `mh_cheat_db_find_title_normalized` |
| cheat_db_find_title_for_disc @0x140006770 | `mh_cheat_db_find_title_for_disc` |
| filter_match @0x14001b0c0 | `mh_cheat_filter_match` |
| cheat_entry_clear @0x140008f90 | `mh_cheat_entry_clear` |
| cheat_entry_hardware_note/exclusion | `mh_cheat_entry_hardware_note/_exclusion` |
| v5_ref_group @0x14003d760, compact_position @0x140012520 | `ch_bank_group`, `ch_compact_position` |
| v5_program @0x14003d6e0 | `ch_v5_program` |

Profile (`mh_cheat_profile_*`; struct `MhCheatProfile` = the 0x38 byte block, `MhCheatEntry` = the 0x58 byte record):
new, free, clone, add_entry, add_audited, add_catalog_entry, add_custom, add_custom_disabled, remove, find_audited,
enabled_count, total_lines, active_c0_count, prepare_hardware, validate, validate_hardware, restore_user_state,
build_exact_catalog, serialized_size, serialize, deserialize keep their names. Renamed or merged:
`cheat_profiles_validate_combined` -> `mh_cheat_profiles_validate_combined`; `cheat_profile_build_for_disc_active` ->
`mh_cheat_profile_build_for_disc(db, ...)`; `profile_active_stats` -> `mh_cheat_profile_active_stats`;
`profile_needs_wire` -> `mh_cheat_profile_needs_wire`; `cheat_blob_validate/scope`, `disc_bank_valid` ->
`mh_cheat_blob_validate/_scope`, `mh_cheat_disc_bank_valid`.
Text: `parse_code_line` -> `mh_cheat_parse_code_line`; `cheat_parse_codes_w` -> `mh_cheat_parse_codes`;
`classic_expand_slide` -> static `expand_slide` (+ `classic_to_v5`, the loop that was inline in parse_codes_w);
`cheat_codes_to_text` -> `mh_cheat_codes_to_text` (MhWbuf); `import_flush` -> static `import_flush`;
`cheat_profile_import_text[_dialect]`, `cheat_profile_export_text` keep their names. `trim_line` and
`normalize_ascii_title` are `mh_trim_line` / `mh_normalize_title` in base.

Not ported (UI, a later engineer): cheat_ui_*, editor_*, catalog_*, ui_*, run_editor, choices, populate,
list_custom_draw. `auto_attach_cheats` and the cheat part of `hydrate_loaded_project` belong to disc/v2
(`mh_v2_auto_attach_cheats`), they use `mh_cheat_db_find_title_for_disc` and `mh_cheat_profile_build_exact_catalog`.

## Profile struct, field by field (from every user in the original)

| offset | field | meaning |
| --- | --- | --- |
| +0x00 | title | database title index; 0xFFFFFFFF for a custom-only profile (`DAT_140f0dd70` = eight 0xFF bytes initialise title+variant) |
| +0x04 | variant | database variant index (== title in this database) |
| +0x08 | game | database game id 1..3 (embedded database: 1), 0 = custom only |
| +0x09 | state | 0 normal; 1 = read from a full v4 blob of the current database (`hydrate_loaded_project` rebuilds the catalog and calls `restore_user_state`); 2 = read from a v5 references-only blob (hydrate re-prepares it as compact) |
| +0x0C/+0x10 | count / cap | entries in use (max 256) / allocated (16, doubling, max 256) |
| +0x14 | db_crc | 0xA06CFC31 when audited, else 0 |
| +0x18 | entries | array of 0x58 byte entries |
| +0x20 | serial[16] | disc serial copied by `prepare_hardware` for the loader |
| +0x30 | removed | count of cheats dropped for "incompatibilidade documentada"; v2_load_source prints a note when non zero. Nothing in the original code paths ported here sets it |

Entry: name[0x40], +0x40 flags, +0x41 game, +0x42 u16 lines, +0x44 u32 variant, +0x48 u32 id, +0x50 pointer to
6*lines bytes. Flag bits: docs/EMBEDDED_DATA.md.

## Limits and error texts

256 entries, 255 lines per entry, 510 lines in total (also the enabled lines: unreachable second limit), 10240 bytes
of serialised profile (`MH_CHEAT_BLOB_MAX`), names 1..63 printable ASCII without `[` `]`, 47 characters in the
compact format. Case profile budget in `add_catalog_entry` 0x1400, disc 0x2800 (what catalog_add passes). All message
texts are copied byte for byte (ASCII, no accents) and tested.

## Deviations from the original

* **Compact bank derived, not carved.** See docs/EMBEDDED_DATA.md. `mh_cheat_db_load` publishes it together with the
  database (as the original published g_active_cheat_db). Without a loaded, verified database `profile_needs_wire`
  is always true for audited profiles, v6/v5 blobs are refused, and serialize writes the full format. The original had
  the bank in `.rdata` and could read v6 blobs even if the database failed its SHA-256 check.
* `load_error` is a field of the database block, the original kept it in a global.
* Strings: UTF-8 instead of UTF-16. Buffer caps are in bytes at the same numbers; they differ only for non-ASCII
  strings longer than the cap. The importer keeps UTF-16 internally so the 511 character line limit and the 127
  character name limit count code units exactly as before. `mh_string_w`'s fallback that widened bytes one to one when
  a string did not fit is replaced by truncation without splitting a UTF-8 sequence (all database text is ASCII or
  valid UTF-8; titles top out at 255 bytes).
* `title_matches` and `find_title_normalized` normalise with `mh_normalize_title` (same table as the original).
* Export writes UTF-16LE with a BOM exactly like the original; import also accepts UTF-8 (BOM or not) and UTF-16LE with
  BOM, strictly validated, no NUL bytes, 1..4 MiB.
* `validate_hardware` is the same code as `validate` in the original (a 5 byte jump); both names are kept.
* `cheat_profile_active_c0_count` and `cheat_entry_hardware_exclusion` are constant 0 stubs in the original; kept.

## Findings / oddities kept on purpose

* `profile_needs_wire` and the compact format only work for game 1 and title < 1004; custom-only profiles always use the
  full format.
* `cheat_db_find_title_for_disc` requires the audited CRC; only 618 serials (594 titles) are bound, FF VII and MGS discs are
  not (the database has no FF VII, MGS exists as "- GH", "- V1.0", "- V1.1", "VR Missions" and is matched by name only).
* 79 database entries are unusable (ops 20/21/C1 or a trailing condition). `build_exact_catalog` skips them; a title whose
  other entries do not fit 10240 bytes in the compact form is abandoned (the largest catalog needs 8054 bytes, all 1004
  titles build).
* The estimate in `build_exact_catalog` and `add_catalog_entry` (`count*4 + 0x44`) is the size of the old v5 format; the
  real limit is enforced by `validate` through `serialized_size`, so catalogs that would exceed 10240 bytes as v6 fail
  at `add_entry`.
* The rule "marcado como auditado e personalizado ao mesmo tempo" in `cheat_blob_validate` can never fire, the xor rule
  before it catches that case. "Os cheats ativos excedem o limite interno" can never fire either (enabled lines <= total
  lines <= 510).
* The MHDialect line of an import file may follow comment lines; a second one, or one after the first cheat, is an error.
  A file that gives the dialect on the command line (`dialect` argument) may still repeat it once, with the same value.
* "Name Code" lines: the name is cut at the first hex digit that starts a valid 12 digit code and is preceded by
  space/tab/`-`/`:`/`=`; trailing `-` `:` `=` are stripped from the name; a name whose first token is a run of 8 or more
  hex-like characters is treated as an unrecognised instruction instead.
