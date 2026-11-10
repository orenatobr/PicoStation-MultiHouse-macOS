# Embedded data blobs

The original executable links several data blobs into `.data`; `tools/extract_embedded.py` carves them into
`resources/embedded/` (copied into the app bundle by the Makefile). Each module appends its section below.

## case_template.bgra (module `art`)

1,440,000 bytes (0x15F900) = a 600x600 BGRA image (B, G, R, A byte order, top row first, no header) of a PlayStation
jewel case seen at an angle. `make_case` copies it and projects the cover onto the front face; the result is shrunk to
96x96 for the .art file. Verified by rendering it (`core/tests/test_art_image.c` writes `case600.png` and
`template600.png` previews). Quad of the front face: (80,20) (590,31) (592,539) (81,583). See docs/map/art.md.

## picostation_menu.bin (module `menu`)

218,736 bytes = 93 raw CD sectors of 2352 bytes (Mode 2, 89 Form 1 and 4 Form 2): the default PSn00bSDK based
PicoStation boot menu disc. ISO 9660: PVD LBA 16, root dir LBA 22, SYSTEM.CNF LBA 23, `SCES_313.37` (PS-X EXE) LBA 24..71,
`PICO.DAT` LBA 72. The 64 byte defaults record (`PST_DEFAULTS_V1!`) is at file offset 141272 (sector 60, byte 152);
the same string also occurs at offset 135436 as plain data and is not a record. Defaults of the embedded copy:
320x240, theme PlayStation (Fat), Portugues, list view, favourites off, Umbrella music on, volume 68. Structure and
validator rules: docs/map/menu.md.

## umbrella_music.vag (used by the V2 writer, not by `menu`)

VAGp (big endian header) version 2, data size 0x00074E70 (478,832 bytes), sample rate 0x5622 (22050 Hz),
name `UMBRELLA`; 478,880 bytes in all. The V2 writer copies it after a `PSTCFG2` configuration sector (text `theme=umbrella`,
`music=embedded:umbrella.vag`, `music_loop=true`, `music_volume=68`) when the Umbrella theme with music is chosen. The
limit of the stored size is 0x22EC00 bytes.

## The disc database: aliases.bin, disc_index.bin, redump_index.bin, trackset_index.bin (module `disc`)

Four sorted, fixed-record tables, all little endian, loaded and validated by `db_load` @0x140015d40
(`mh_db_load`, `core/src/disc/db.c`). The original copied each blob from `.data` into a heap block and checked it
completely before use; a blob that fails any check is dropped (its lookups fail) and the others stay usable. The
program works with the embedded copies only: there is no integrity constant for them (the SHA-256 constant
`kExpectedSha256` @0x140f1d080 and CRC-32 0xA06CFC31 belong to the cheat database `cdx_cheats.bin`, checked in
`cheat_db_load`; docs of the `cheat` module). The only external data of the disc part is the folder given to `db_load`
(in the original: the folder of the executable): `database/art/default/<SERIAL>-CASE.art` and `<SERIAL>-CD.art`
(10240 bytes each, produced by the art module) and `database/cues/<SERIAL>.cue` (original CUE sheets, optional, not
shipped; `db_find_original_cue`). `art-manifest.json` is not referenced anywhere in the executable. Serial text
comparisons are ASCII case-insensitive (`a_icmp`); the tables are sorted that way.

Serial syntax (`canonical_serial` @0x1400030c0): 3 or 4 letters/digits, `-`, 5 or 6 digits, an optional letter; a
trailing `-` + the first 3 (or 4) prefix letters + dashes is tolerated. Database serials are matched through it.

### aliases.bin ("PSTALS2")

    0x00 char[8]  "PSTALS2\0"
    0x08 u32      version = 2
    0x0c u32      count
    0x10..0x1f    zero
    0x20 + 0x20*i record i: char key[16] (byte 15 is 0), char canonical[16] (byte 15 is 0)

File size must be exactly (count + 1) * 0x20; keys sorted ascending (case-insensitive), no duplicates in the shipped
blob (1504 records). `db_resolve_alias` and `db_find_original_cue` use a plain binary search (lo/hi exclusive, first
hit wins); `db_find_art` finds the lower bound and walks every record with that key. Example:
`LSP-01017001 -> LSP-010170`, `SLUS-90094 -> SLUS-90093`.

### disc_index.bin ("PSTDB2")

    0x00 char[8]  "PSTDB2\0\0"
    0x08 u32 version = 2        0x0c u32 count (10637)   0x10 u32 record size = 0x80   0x14 u32 entry size = 0x10
    0x18 u64 offset of the track entries = 0x40 + count * 0x80      0x20..0x3f zero
    0x40 + 0x80*i record i:
        +0x00 char serial[16]   (byte 15 = 0)       records are sorted by serial; one serial may have several records
        +0x10 char title[64]    (byte 63 = 0)       Redump style title, e.g. "Classic Card Games (Russia) (Unl)"
        +0x50 u32  first track entry index
        +0x54 u16  track count          (first + count must stay inside the entry table)
        +0x56 u16  zero
        +0x58 u32  total sectors of the disc (2352 byte sectors, all tracks, pregaps included)
        +0x5c..0x7f zero
    entries (at the offset in the header, 16 bytes each, (file size - offset) % 16 == 0):
        +0 u8 track number (1-based, consecutive inside a record)   +1 u8 kind: 1 audio, 2 data
        +2 u16 pregap sectors (INDEX 00 to INDEX 01)               +4 u32 length in sectors (pregap included)
        +8 u32 CRC-32 of that track's bytes (equals the matching redump_index record: size = length * 2352)
        +12 u32 zero

Used by `db_apply_disc` (score = 200000 same track count / 100000 otherwise, + 300 - |sector difference| if < 300,
+ 100 per track whose kind matches; strict maximum wins, equal scores with different titles are ambiguous),
`db_apply_saved_single_track`, `db_recover_track_map_safe` (rebuild: track i starts after the lengths of the
previous ones, INDEX 01 at start + pregap, lengths must add up to the total and the first track must be data
without pregap) and by title lookups (`db_record_title`).

### redump_index.bin ("PSTRDX2")

    0x00 char[8]  "PSTRDX2\0"
    0x08 u32 version = 2   0x0c u32 count (49470)   0x10 u32 record size = 0x80   0x14..0x3f zero
    0x40 + 0x80*i record i (sorted ascending by the first field):
        +0x00 u64 file size in bytes of one Redump track image (a multiple of 2352)
        +0x08 u32 CRC-32          +0x0c u8 sha1[20]
        +0x20 char name[96]       Redump file name, e.g. "Return Fire (USA) (En,Ja,...) (Track 02).bin"
        +0x80 end

File size must be exactly 0x40 + count * 0x80. `db_verify_redump` binary-searches the size, then compares CRC-32 and
SHA-1 of every BIN file of the disc; the first match supplies the game name (cut at ".bin" and at " (Track ", trailing
blanks removed), used as the disc title when no disc-index record is linked.

### trackset_index.bin ("PSTTS1")

Identifies a physical edition from the SHA-1 of every track, in order.

    0x00 char[8]  "PSTTS1\0\0"
    0x08 u32 version = 1   0x0c u32 count (10974)   0x10 u32 record size = 0xc0   0x14 u32 entry size = 0x14
    0x18 u64 offset of the SHA-1 table = 0x40 + count * 0xc0
    0x20 u64 offset of the SBI data   = SHA-1 table offset + entries * 0x14      (<= file size)
    0x28 u32 entries (49470)    0x2c u32 number of records that carry an SBI blob (235)   0x30..0x3f zero
    0x40 + 0xc0*i record i (sorted ascending by memcmp of the first 20 bytes; equal keys allowed):
        +0x00 u8 key[20]      SHA-1 of the first track
        +0x14 u32 first index into the SHA-1 table      +0x18 u16 track count (1..99, first + count <= entries)
        +0x1a u16 flags: bit 0 = has SBI (must agree with sbi length); bit 1 appears on 441 records and is unused
        +0x1c u32 disc_index.bin record of this edition (checked against the disc count before use)
        +0x20 u32 id (kept in the disc as ps.trackset_id; a Redump side id, meaning not verified)
        +0x24 u32 SBI offset (absolute in the file, >= the SBI data offset)   +0x28 u16 SBI length   +0x2a u16 zero
        +0x2c u32 zero
        +0x30 char serial[32]   (byte 31 = 0)           +0x50 char title[96] (byte 95 = 0)    +0xb0 16 bytes zero
    SHA-1 table (20 bytes per entry): the SHA-1 of each track of record i is table[first + k], k < track count.
    SBI blob: "SBI\0" then 14 byte entries (BCD min, sec, frame, type = 1, 10 bytes Q data); length in 18..744 with
    (length - 4) % 14 == 0; each time stamp must be valid BCD, sec <= 59, frame <= 74, at least 2 seconds (150 frames)
    and strictly increasing.

`db_match_trackset` finds the key range with a binary search and accepts only exactly one record whose track count
and complete ordered SHA-1 list equal the disc's; then it sets the serial (when empty), links the disc-index record,
applies the title and exposes the SBI blob (a pointer into the loaded blob, so the `MhDb` must outlive the discs).

## cdx_cheats.bin and the CDXLZ1 container (module `cheat`)

1,060,037 bytes, the GameShark V5 "original" database. Container `CDXLZ1`: bytes 0..7 `"CDXLZ1\0\0"`, u32 at 8 = raw size
(must be 0x34D836 = 3,463,222), then a plain LZ4 *block* stream from offset 12 (token: high nibble literal length,
low nibble match length minus 4, 15 extends with 255-terminated bytes; 2 byte little endian offset; the stream ends
with a literal-only token whose low nibble is 0 and the output must be exactly the raw size). Any violation is error
60001 (0xEA61). The decompressed image has SHA-256
`1b48391f53001a0b95b813a9739e6575007e8ba7f2cc7359fb26196a423cf1a3` (`kExpectedSha256` at VA 0x140f1d080) and CRC-32
`0xA06CFC31`; that CRC doubles as the "audited database" id stored in every audited profile (`MH_CHEAT_DB_CRC`).
The loader also knows CRC `0x3CC8BE42`, an older database whose audited entries are dropped by `deserialize`
(unreachable in practice: `validate` insists on the current CRC).

### MHGSF1 image (decompressed)

All integers little endian. Header 0x74 bytes:

    +0x00 "MHGSF1\0\0"   +0x08 u16 version = 2   +0x0A u16 header size = 0x74   +0x0C u32 flags (<= 7)
    +0x10 u32 n_games  +0x14 n_titles  +0x18 n_variants  +0x1C n_texts  +0x20 n_cheats  +0x24 n_programs (all non zero)
    +0x28 u32 n_lines (6 byte lines)   +0x2C u32 = sum of variant.stat_b
    +0x30 u32 offsets of: games(=0x74) titles variants texts cheats programs lines strings, each table starts where
          the previous one ends
    +0x50 32 bytes not checked   +0x70 u32 CRC-32 of bytes 0..0x6F

Embedded database: 1 game, 1004 titles, 1004 variants (exactly one per title), 10118 texts and cheats, 9891 programs,
17992 lines, 23160 strings (≈ 3.4 MB, mostly the JSON detail text).

| table | size | fields |
| --- | --- | --- |
| game | 0x54 | +0 u8 id (1..n, unique), +4 u32 long name string, +8 u32 short code string, +0xC u32 variants of the game, +0x10 u32 = sum of their stat_a |
| title | 0xC | +0 u32 name string, +4 u32 first variant (running), +8 u16 variant count (>0), +0xA u16 0 |
| variant | 0x38 | +0 u8 game id, +1 u8 aux, +2 u16 0, +4 u32 title, +8 u32 aux, +0xC u32 first cheat (running), +0x10 u32 cheat count, +0x14 u16 stat_a (>= count and >= stat_b), +0x16 u16 stat_b, +0x18 32 bytes hash |
| text (group) | 0x1C | +0 u32 name string, +4 u32 program, +8 u8 line count (= program's), +9 u8 (<8), +0xA u16 0, +0xC display name, +0x10 PT note, +0x14 EN note, +0x18 JSON details (all string ids) |
| cheat | 0x10 | +0 u32 variant, +4 u32 text, +8 u32 entry id, +0xC u8 (low 7 bits: line count or 0), +0xD u8 flags (bit0 = on by default, bit1 = mandatory), +0xE u16 0 |
| program | 8 | +0 u32 byte offset in the lines table (running), +4 u16 bytes (= 6 * count), +6 u8 count (>0), +7 u8 (<8) |
| lines | 6 | op, addr[3], value[2]; every program must pass `v5_program` (ops 20 21 30 31 00 80 70 90 C1) |
| strings | | u32 n; n * (u32 offset, u32 length) with offsets running from 0; then the bytes. No NUL, valid UTF-8 |

Validation order and error codes of `cheat_db_load` (the number is `load_error`):
60000 no memory; 60001 container/LZ4 problem; 2 header; 3 header CRC; 4..12 table bounds (games, titles, variants,
texts, cheats, programs, lines, strings, string count); 100+i string i bad; 900 strings do not end at the image end;
1000+i game i; 1200+i duplicate game id; 1400+i game counts; 2000+i title i; 2400+i variant of title; 2800 variants
not all used; 3000+i variant i; 4800+i cheats of variant; 4999 variant totals; 5000+i cheat i; 8000+i cheat line
count vs text; 22000+i program i; 26000+i program ops; 32000 program totals; 33000+i text i; 36000+i text vs program;
50000+256*k+b SHA-256 differs first at byte k (b is our byte); 59000 CRC-32 differs.

### Serial bindings and the V5 compact bank

`kCheatSerialBindings` (VA 0x140f1a9e0) is 618 sorted 16 byte records (12 byte NUL padded serial, u32 title index), ASCII
case-insensitive binary search; it lives in `core/src/cheat/serial_bindings.inc`. FF VII and Metal Gear Solid have no
entry (they are not in the database at all or only under other names).

`v5_compact_bank` (VA 0x140f1d220, 497,601 bytes, magic `V5REF01\0`, DB crc inside) is **not** carved: it is a pure
function of the image. Per title (title i = variant i) the groups are the cheats of the variant in order; group name =
`mh_normalize_title(name string)` (63 chars) and, if that collides with an earlier group of the same title, `"<name> <id>"`
(three cases: ZIDANE 20, UNLOCK PICTURES 37, UNLIMITED HEALTH 5); id = entry id (u16); lines "a" = the program lines
(decoded form) and "b" = the same with opcodes 30 and 80 replaced by 00 (the form the card stores for audited
entries). This was checked for all 10118 groups against the original bank bytes (a SHA-256 over names, ids, counts and
both line forms is in `core/tests/test_cheat.c`). Original layout, for reference: u32 title count 1004, group count
10118, title table offset 0x28 (u16 first, u16 count per title), group table 0xFD8 (16 bytes: name offset into the
string pool at 0x28838, a-lines offset, u16 count, u16 id, b-lines offset), line pool at 0x44CD9.

Database facts for the UI: 79 of the 10118 entries contain lines the runtime cannot take (opcodes 20, 21, C1, or a
condition 70/90 with nothing after it); `cheat_db_entry_info` refuses them and the catalog builder skips them.

## PSTCHT1: the cheat region on the card (module `cheat`)

10240 (0x2800) byte region, sectors written by `build_package_metadata` (offset 0xA00 of the package block). Header
0x40 bytes, common to all versions:

    +0x00 "PSTCHT1\0"  +0x08 u16 version  +0x0A u16 header size 0x40  +0x0C u32 0
    +0x10 u32 entries  +0x14 u32 enabled entries  +0x18 u32 total lines
    +0x1C u32 record size  +0x20 u32 records offset (0x40)  +0x24 u32 payload offset  +0x28 u32 total size (v6: u16 + u16 0)
    +0x2C u32 title  +0x30 u32 variant  +0x34 u32 database CRC  +0x38 u8 scope (1 Case, 2 disc)  +0x39 u8 game
    +0x3A u16 0  +0x3C u32 CRC-32 of the whole blob with these four bytes taken as zero

Entry flags (profile and blob): 01 enabled, 02 mandatory (needs 08 and 01), 04 custom, 08 audited (exactly one of 04/08),
10 on by default in the database (never on custom), 20 compact marker, 40 "simple/classic", 80 invalid.

* **version 4, full ("wire")**: 0x60 byte records at 0x40: name[0x40] (NUL padded), +0x40 u32 variant, +0x44 u32 id,
  +0x48 u32 absolute code offset, +0x4C u16 lines, +0x4E u8 game, +0x4F u8 flags & 0x3F, +0x50 u32 CRC-32 of the
  code, 12 zero bytes. Code follows the records, contiguous. Custom entries: game 0, variant = id = 0xFFFFFFFF. Written
  when the profile cannot be expressed against the bank (any audited entry that differs from the bank, game != 1, name
  over 47 characters, more than 256 positions). Bit 6 is not stored; the reader sets it again on custom entries whose
  lines are only 30/80 or 70/90 + 30/80. Header title/variant/CRC are the sentinels (0xFFFFFFFF x2, 0) when game is 0.
* **version 5, references only** (read only here): title (+0x2C) must equal variant (+0x30); 4 byte records
  (u16 bank position, u8 flags with 08 set and 04/40/80 clear, u8 0); size = 0x40 + 4n <= 0x440. Reads back with state
  `REFS_ONLY` so the caller can rebuild.
* **version 6, compact**: 8 byte records at 0x40: u16 name offset, u16 code offset, u16 bank position, u8 lines, u8 flags;
  then for each entry the NUL terminated name (A-Z 0-9 space & ' - . only, at most 47 chars) and the code. Audited entries
  carry the "b" form of the bank group at position `compact_position`; custom entries carry their own lines and position
  `groups(title) + running custom index`; their flag byte is `((flags >> 2) & 0x10) | (flags & 1) | 0x28`, and flag 0x20
  tells readers there is no bank group. Always game 1, title < 1004, positions unique (0..255), scope 1 or 2.
