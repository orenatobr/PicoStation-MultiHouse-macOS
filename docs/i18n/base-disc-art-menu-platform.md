# i18n: base, disc, art, menu, blockdev and platform/mac

All user-visible texts returned by these modules are EN-US. Windows wording was removed
("Explorador" became "Finder", "erro Win32 N" became "error N", "Erro Windows" became "Error code").

Decisions about the status and source tokens:

* `mh_disc_status_text`: OK, Review (was OBS), Fixable, Error, Empty.
* `mh_psserial_status_text` / `mh_psserial_source_text` are diagnostic labels, never stored in a
  file, so they are English upper-case tokens (STRONG_INTERNAL, NOT_FOUND, SYSTEM_CNF, ...).
* `MH_NEW_CASE_TITLE` is now "NEW GAME" (default title of a new Case; the user renames it).
* Menu language and visual names (`MH_MENU_LANGUAGES`, `MH_MENU_VISUALS`) are shown by the UI
  pickers and are labels only (the stored value is the index), so they are translated.

## Deliberately untranslated data

| String | Where | Reason |
|---|---|---|
| `"DISC "`, `"DISCO "` | `core/src/base/path.c` (game folder name detection) | Matches folder names found on the user's disk (Portuguese dumps use "Disco 2"); input data, not a message. |
| `PSTTRK1`, `SBI`, `PSXRAW`, `PS-X EXE`, `CD001`, `PST_DEFAULTS_V1!` | cdsector.c, psserial.c, menu_*.c, mh_disk_mac.m | Magic strings and card/file headers. |
| `AUDIO`, `MODE1/2352`, `MODE2/2352`, `FILE`, `TRACK`, `INDEX`, `PREGAP`, `BINARY` | disc/cue.c | CUE keywords, parsed and written. |
| `SYSTEM.CNF`, `BOOT`, `BOOT2`, `.EXE` | disc/psserial.c | PlayStation disc file names and keys. |
| Serial prefixes (`SLUS`, `SCES`, ...) | disc/psserial.c | Serial formats. |
| Theme names `PS One (Slim)`, `PlayStation (Fat)`, `Umbrella` | menu_defaults.c | Product and theme names used by the menu itself. |
| IORegistry keys (`BSD Name`, `USB Serial Number`, `Secure Digital`, `Disk Image`, ...) | mh_disk_mac.m | Matched against system data. |
| `Redump`, `DAT`, `SHA-1`, `ISO9660`, `PVD`, `CUE`, `BIN`, `RAW` | notes and errors | Proper names and format names kept inside translated sentences. |
| `Offset`, `LBA512` | io.c report | Technical labels already English. |
