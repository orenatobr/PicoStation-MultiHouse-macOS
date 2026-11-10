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
