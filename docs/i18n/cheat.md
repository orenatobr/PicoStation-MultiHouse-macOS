# i18n: module `cheat`

All user-visible texts returned by the `cheat` core (`core/src/cheat/*`) are EN-US: code line and dialect
errors, TXT/CHT import and export errors, profile limits, PSTCHT1 validation, classic GameShark to V5 conversion
errors, database errors, the hardware note and the database description. `core/tests/test_cheat*.c` assert the
new texts byte for byte.

## Deliberate exceptions (not translated)

| text | where | reason |
| --- | --- | --- |
| `MHDialect=V5ORIG_BANK_V01`, `MHDialect=GAMESHARK_CLASSIC_V1`, `MHDialect=` prefix | `cheat_text.c`, export header | file format keyword: the parser matches it and the exporter writes it |
| `#activation=`, `#type=`, `#group=` | `cheat_text.c` import | file format: the parser matches these DuckStation metadata prefixes |
| `[NAME]` header syntax | `cheat_text.c` | file format; only the hint in the error text changed from `[NOME]` to `[NAME]` (the parser accepts any name inside the brackets) |
| `PSTCHT1\0`, `CDXLZ1`, `MHGSF1`, `V5REF01` magics | `cheat_blob.c`, `cheat_db.c`, `cheat_bank.c` | binary format identifiers, stored on the card / compared by the validator |
| `AUDITED`, `CUSTOM`, `ENABLED`, `C0`, `WRITE16`, opcode names, `XXXXXXXX XXXX` | error texts | technical tokens, kept exact |
| cheat names, group names, PT/EN notes, JSON details, titles | embedded database (`cdx_cheats.bin`) | database content is data (e.g. the group name `Banco original V5` checked in `test_cheat.c`) |
| `serial_bindings.inc` | table | serials and title indices only, no text |
| the words `Case`, `Disc`, `RAW`, `PT/EN` | error texts | glossary terms (docs/UI_DESIGN.md) or names of database fields |

Origin and column labels ("USUARIO V5", "BIBLIOTECA", "SIM"/"NAO", "Ativo") are not produced by the core; they
live in `app/cheat_ui.m` (UI engineer).
