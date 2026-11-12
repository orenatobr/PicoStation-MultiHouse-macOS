# Module `menu`: the PicoStation boot menu image and its defaults

> Note: the user-visible texts quoted here were translated to EN-US after the port. The Portuguese originals below are kept as the reverse-engineering record; docs/i18n/base-disc-art-menu-platform.md lists the strings deliberately left untranslated.

Header `core/include/mh/menu.h`, code `core/src/menu/{menu_iso.c,menu_defaults.c}`, tests `core/tests/test_menu.c`.
Buffers in, buffers out: no project struct is touched; the disc module owns `g` (the 0x40 byte record
copy at 0x140e7e5f0 and its "present" flag at 0x140e7e630).

## Function map

| original | port | note |
|---|---|---|
| `menu_user_sector` @0x140024900 | `mh_menu_user_sector` | adds the `index < nsectors` bound the original never checked |
| `menu_find_root_file` @0x140024680 | `mh_menu_find_root_file` | |
| `v2_validate_menu_blob` @0x140039a70 | `mh_menu_validate_blob` | also guards sector 16 against short images (the original read past the end) |
| `v2_validate_menu_file` @0x14003a240 | `mh_menu_validate_file` | the embedded menu is passed in |
| `menu_defaults_read` @0x1400242f0 | `mh_menu_defaults_read` | |
| `menu_defaults_patch` @0x140024090 | `mh_menu_defaults_patch` | uses `mh_cd_repair_sector` (original `repair_sector`) |
| `menu_defaults_compatible` @0x140023b20 | `mh_menu_defaults_compatible` | |
| `menu_defaults_dialog` @0x140023c80 + `window_proc`, `populate`, `choices` | `mh_menu_defaults_valid/parse/build/seal/factory`, `MH_MENU_THEMES/LANGUAGES/VISUALS` | UI excluded; the data rules are here |
| `project_use_default_menu.constprop.0` @0x140029090 | `mh_menu_defaults_load` | reads `menu-defaults.dat` |
| `cmd_defaults` save step @0x140011130 | `mh_menu_defaults_save` | writes `menu-defaults.tmp`, then renames over `menu-defaults.dat` (original MoveFileExW REPLACE_EXISTING|WRITE_THROUGH) |
| `repair_sector` @0x14002d880 | `mh_cd_repair_sector` (base) | |

## Defaults record (64 bytes, `menu-defaults.dat` is exactly this)

    0x00 "PST_DEFAULTS_V1!"
    0x10 u32 0x00400001
    0x14 u16 width  0x140 | 0x170 | 0x200
    0x16 u16 height 0xF0 | 0x100          (check is ((h - 0xF0) & 0xFFEF) == 0)
    0x18 u8  theme    0..2  PS One (Slim), PlayStation (Fat), Umbrella
    0x19 u8  language 0..2  Portugues, English, Espanol
    0x1A u8  visual   0..1  Lista, Carrossel ("Visual" combo)
    0x1B u8  start in the favourites list 0/1
    0x1C u8  Umbrella theme music 0/1
    0x1D u8  volume 0..100
    0x1E..0x3B zero
    0x3C u32 CRC-32 (IEEE) of 0x00..0x3B

"Restaurar padroes" (factory): 320x240, theme 1, language 0, visual 0, favourites off, music on, volume 68.
The embedded menu carries exactly that. Dialog texts (for the UI): title "Padroes do menu", note "Estes padroes
serao gravados no menu.bin.\nUm save valido no Memory Card tem prioridade.", buttons "Restaurar padroes",
"Salvar no BIN", "Cancelar", checkboxes "Iniciar na lista de favoritos" and "Musica do tema Umbrella", "Volume
(0 a 100)", error "Informe um volume de 0 a 100." (volume field accepts at most 7 digits, parsed decimal, > 100
rejected), "Configuracao invalida." when the sealed record fails validation, "Padroes incorporados invalidos."
when the embedded record cannot be read, "Nao foi possivel salvar os padroes ao lado do Manager." on a write
failure, status "Padroes salvos. Aplique ao SD ou imagem. O save do Memory Card tem prioridade.".

## Where the record lives inside the image

`menu_defaults_read` needs the image size to be a multiple of 0x930 and at least 0x40, scans every byte offset for
`PST_DEFAULTS_V1!` followed by a valid record, and requires for each hit: offset inside the sector >= 0x18 and
(offset + 0x40) <= 0x818 (so inside the 2048 user bytes of a Mode 2 Form 1 sector), mode byte 2, submode bit
0x20 clear, subheader copies equal. A hit that breaks a placement rule fails the whole read; zero or more than one
valid hits also fail. In the embedded image the string also appears as plain data at offset 135436 (the code's
magic constant, not a valid record) and the real record is at 141272 (sector 60, byte 152 of the raw sector).
`patch` copies the 64 bytes and runs `mh_cd_repair_sector` on that sector (EDC + P/Q parity). `compatible(a, b)` is
true when both have the record at the same place, all other sectors are byte identical, a's record sector
already has a correct EDC/ECC and b's sector equals a's sector with b's record patched in.

## The menu image (validator rules, in order)

Size 1..93 sectors of 0x930 (or 0xA00 "PSXRAW" slots whose last 0xD0 bytes are zero), otherwise "O menu deve possuir
de 1 a 93 setores RAW completos para este firmware.". Every sector: sync `00 FF*10 00`, mode 1, or mode 2 with
equal subheader copies ("O menu possui setor CD-ROM RAW invalido."; "O padding PSXRAW do menu nao esta zerado.").
EDC/ECC are NOT checked. Sector 16 = PVD (`01 "CD001" 01`) else "O menu nao possui PVD ISO9660 valido no setor 16.".
Root record at PVD+0x9C: length >= 0x22 ("O diretorio raiz ISO9660 do menu e invalido."), extent in range and size
non-zero ("... aponta para fora do menu."), 1..8 sectors that fit ("... excede o limite seguro do menu."). Then
`SYSTEM.CNF` is looked up (case-insensitive, `;version` ignored) ("SYSTEM.CNF nao foi encontrado na raiz do menu.");
only the first sector's worth of the file, capped by its size, is parsed. Lines end at NUL, CR or LF; leading
blanks (space, tab) skipped; a line starting with `BOOT` (any case, optional `2`), optional blanks, `=`,
blanks, value; the value (trailing blanks trimmed) must start with `cdrom:` (any case), then leading `\` or `/`
skipped and the last path component taken (1..127 printable ASCII characters). Lines not of that shape are
skipped; a `BOOT` line that starts with `cdrom:` but has no usable name ends the search with "SYSTEM.CNF do menu nao
possui BOOT=cdrom valido." (so does reaching the end without one). The name is then looked up in the root
directory WITHOUT version stripping, so it must carry the `;1` exactly as in the directory (the embedded menu uses
`BOOT=cdrom:\SCES_313.37;1`); it must be >= 8 bytes with `PS-X EXE` at the start, else "O executavel indicado pelo
SYSTEM.CNF nao e um PS-X EXE valido.". `menu_find_root_file` also rejects entries with a zero or out of range LBA,
zero size or an extent past the image.
`validate_file` adds "Nao foi possivel ler o menu selecionado." (unreadable file) and, when asked to match the
embedded menu, "O menu selecionado nao corresponde ao menu PSn00bSDK incorporado neste Manager." (via `compatible`).

## The embedded menu (`resources/embedded/picostation_menu.bin`)

218,736 bytes = 93 raw Mode 2 sectors of 2352 bytes (89 Form 1, 4 Form 2). PVD at LBA 16
(volume id PICO), root directory LBA 22; SYSTEM.CNF at LBA 23 (`BOOT=cdrom:\SCES_313.37;1 TCB=4 EVENT=10
STACK=801FFFF0`), `SCES_313.37` (the PSn00bSDK menu executable, `PS-X EXE`) LBA 24..71 (98304 bytes), `PICO.DAT`
LBA 72 (924 bytes). The V2 image writer (not part of this module) stores the menu with 0xA00 byte slots, patches the
defaults with `menu_defaults_patch`, and for the Umbrella theme appends a `PSTCFG2` config sector followed by
`umbrella_music.vag` (config text `theme=umbrella\nmusic=embedded:umbrella.vag\nmusic_loop=true\nmusic_volume=68\n`).
This module does not use the VAG file itself.

## Memory Card compatibility patch (Manager 0.5.1, 0.5.2 and 0.5.3, deliberate deviation from the original)

The embedded menu differs from the original in seven instruction words, applied by `tools/patch_menu.py` (idempotent,
with an EDC/ECC self test over the 89 Form 1 sectors) and pinned by `test_memcard_timing_patch` in `core/tests/test_menu.c`.
The menu reads and writes the Memory Card through SIO0 itself, with two kinds of bounded waits that assume a card as
fast as an original one.

1. **ACK wait of the detection routine** (text address `0x800d480c`): 0x78 units (about 180 microseconds) per byte, while the
   frame routines wait 2000 units (about 2.4 ms). A device that acknowledges slower passed the transfers but was reported
   as "no Memory Card found". `addiu $a0, $zero, 0x78` at `0x800d48b4` and `0x800d48fc` is now `0x7d0` (0.5.1).
2. **Settle time after /CS goes low** (minimum waits, 0.5.2): the first byte is sent 0x96 (detection, `0x800d484c`), 0x64
   (frame read, `0x800dea94`) or 0x50 (frame write, `0x800d4b94`) units after the device is selected (about 1 microsecond
   per unit). A device that is not listening yet (a microcontroller waking up) loses the byte. All three are now 1000
   (about 1 ms). Every frame operation takes about 0.9 ms longer.

3. **Retries of a failed frame** (0.5.3): in the save state machine (`FUN_800d4c50`) a data frame that fails to write or to read
   back is retried 7 times, the directory frame 9 times, one attempt per video frame, which only covers a device that stays
   busy about 100 ms after a write (simulated: a device ignoring /CS for 50 ms after a write already breaks the first
   read back). `sltiu $v0, $v0, 7` at `0x800d51b4` and `sltiu $v0, $v0, 9` at `0x800d5244` now compare with 30. Measured by
   running the state machine against a card that never answers: 7 and 9 attempts before, 30 and 30 after. A save that really
   fails now reports the error after about 2 seconds instead of about 0.1 seconds.

The third `addiu $a0, $zero, 0x78` in the program (`0x800dece0`) belongs to the pad routine and is not touched. Not tested
on a console or a real Memory Card device.

`tools/sim_menu_memcard.py` runs the real MIPS code of the blob (Unicorn emulator, `pip install unicorn`) against a modelled
card (the same frame protocol as a PS1 card; the read and write routines are byte exact on it). `--sweep` tries device
profiles (CPU time is one cycle per instruction, so figures are approximate):

| Device profile | original menu | patched menu |
|---|---|---|
| normal card | detect, read, write ok | detect, read, write ok |
| not listening for the first 100 us after /CS | write fails | ok |
| not listening for 200 us to 1 ms after /CS | detect, read, write fail | ok |
| not listening for 2 ms | fail | fail |
| ACK 100 us to 1.5 ms on every byte | detect fails (read, write ok) | ok |
| first ACK 150 us to 1 ms | detect fails | ok |
| first ACK 2.5 ms and more | detect fails | detect fails |

Time on a normal card: detection 0.55 to 1.4 ms, frame read 14.7 to 15.6 ms, frame write 56.6 to 57.5 ms (before and after).
This is a model, not a device.

## Game ID and cards that switch (SD2PSX family)

When a game is launched the menu probes each slot with `81 53` and `81 20` (a MemCard PRO or SD2PSX answers `27 FF`) and then
sends `81 21 00 len+1 <boot file name> 00` without waiting for ACKs (these devices do not ACK the Game ID bytes). Such a device
switches to the virtual card of that game. The menu never sends an id for itself, so its settings and favorites file
(`BASCES_31337PICO_CFG`) is read from and saved to whichever card is active, which after a game is that game's card. With an
original card there is one card and this cannot happen. Workaround: turn off the PS1 Game ID switching in the device (or
use a fixed channel). A menu change that sends the menu's own id (`SCES_313.37`) before reading and saving was not made: it
cannot be tested here and it would move existing favorites. Hypothesis, not verified on a device.

## Deviations

* Sector 16 and sector index bounds are checked (the original trusted its callers).
* `mh_menu_defaults_save` uses rename(2) instead of MoveFileExW.
