# Module `art`: menu artwork (.art)

> Note: the user-visible texts quoted here were translated to EN-US after the port. The Portuguese originals below are kept as the reverse-engineering record; docs/i18n/base-disc-art-menu-platform.md lists the strings deliberately left untranslated.

Header `core/include/mh/art.h`, code `core/src/art/{art.c,art_case.c}`, ImageIO loader
`platform/mac/mh_image_mac.{h,m}`, tests `core/tests/test_art.c` (core, fixtures) and
`core/tests/test_art_image.c` (ImageIO plus PNG previews, linked with `core/tests/art.ldflags`).

## Function map

| original | port | note |
|---|---|---|
| `art_init` @0x140002600 | `mh_art_init` | loaded gdiplus.dll with LoadLibrary; now only stores the `MhImageLoadFn`. Returns false when none is given, like the original when gdiplus was missing |
| `art_shutdown` @0x1400027d0 | `mh_art_shutdown` | frees the optional owned case template |
| `art_decode` @0x1400023b0 | `mh_art_decode`, `mh_art_decode_mem` | BGRA bitmap for the preview (`refresh_fields`, `preview_proc` only draw it with StretchDIBits) |
| `art_generate_case/disc/plain` | `mh_art_generate_case/disc/plain` | one-line wrappers of `generate` with kind 1, 2, 0 |
| `generate` @0x14001b3d0 | `mh_art_generate` (file in, file out), `mh_art_build` (memory), `mh_art_resize96`, `mh_art_make_disc96`, `mh_art_encode` | |
| `make_case.constprop.0` @0x140021400 | `mh_art_make_case600` | |
| `make_custom_art.isra.0` @0x1400223a0 | `mh_art_import`, `mh_art_custom_name` | the file picker and the project/refresh calls are UI/disc module work |
| `db_find_art` | not here | disc module |

## The .art format

Exactly 10240 bytes (the importer rejects other sizes with "O arquivo ART precisa ter exatamente 10.240 bytes."):

    0x0000 u8   format = 2
    0x0001 u8   width  (96)
    0x0002 u8   height (96)
    0x0003 u16  pixel count = width*height (0x2400), little endian
    0x0005 u8[] palette index per pixel, row major, top row first
    0x2405 u16  palette size in bytes = 0x200
    0x2407 u16[256] palette, PS1 15 bit colour: R | G<<5 | B<<10 (red in the LOW bits); 0 = transparent
    0x2607 zero padding to 0x2800

`art_decode` rule: file longer than 0x206, byte0 == 2, w and h non-zero, w*h == count,
count+0x207 <= file size. RGB = 5 bit component * 255 / 31, alpha = 255 unless the palette word is 0
(bit 15 is ignored). Output pixels are BGRA (0xAARRGGBB little endian).

### Two kinds of files exist
* Files written by this program (`generate`): fixed 3-3-2 palette. Index =
  `(R & 0xE0) | (G >> 5) << 2 | (B >> 6)`; alpha < 0x20 gives 0, an opaque pixel whose index would be 0
  gets 1. Palette entry i (1..255): R5 = ((i>>5)*255/7*31+127)/255, G5 from (i>>2)&7, B5 from i&3
  with /3; entry 0 stays 0 (the original had a `0 -> 0x421` fix-up which can never trigger for i>=1).
  No file in the available fixtures was produced this way (see below), so this part is verified by
  reading the code and by the property that decode(encode(x)) re-encodes to the same bytes.
* The 17,358 files of `database/art/default` (and the 222 CASE/DISCO files of the user library, which use
  the same adaptive style) have the same header and padding but an adaptive 256 colour palette (entries
  may carry bit 15). All of them decode with `mh_art_decode`; `test_art` checks header, zero tail and
  that the transparency mask survives decode + encode.

## generate in detail

1. The image is loaded as 32 bit ARGB (BGRA in memory, straight alpha, top row first). Width and height
   must be 1..0x2000 or it fails with "Nao foi possivel carregar PNG, JPG, BMP, GIF ou TIFF pelo GDI+.".
2. 96x96 BGRA working image (`0x9000` bytes):
   * kind 0 (plain): bilinear resize in 16.16 fixed point, corner aligned: output i samples
     `i*(size-1)/95`, `out = ((bot*fy + top*(65536-fy)) >> 16)` per channel with `top/bot =
     (p1*fx + p0*(65536-fx)) >> 16`. The original divided by 95 with a multiply-high trick; it is plain
     `x/95` (checked on 200k values).
   * kind 1 (case): `make_case` renders a 600x600 image, then the same resize (600 -> 96).
   * kind 2 (disc): centre square crop (side = min(w,h)), nearest neighbour `i*(side-1)/95`, then a mask
     over the 96x96 grid using d2 = dx^2+dy^2 with dx,dy in -95,-93..95 (so the grid is centred):
     d2 < 144 or d2 >= 9026: transparent; 144..288: transparent (hub hole); 289..783: colour averaged with
     (190,195,200) (B,G,R), alpha 220; 784..8464: picture, alpha 255; 8465..9025: dark rim (8,8,8) alpha 255.
3. Quantise with the 3-3-2 palette above and write the 10240 bytes (`write_entire_file`).
   Error texts: "Memoria insuficiente.", "Falha ao montar a imagem na case padrao.",
   "Falha ao montar o disco padrao.", "Falha ao escrever o arquivo ART.".

## make_case (case template)

`resources/embedded/case_template.bgra` is a 600x600 BGRA image of a jewel case seen at an angle (size
0x15F900, the original rejects smaller blobs). The cover (w x h) is projected onto the front face with a
perspective transform: the corners (0,0) (w,0) (w,h) (0,h) go to (80,20) (590,31) (592,539) (81,583).
The original solves the 8 unknowns of the homography with Gauss-Jordan (partial pivoting, first maximum
wins, pivot < 1e-12 fails), requires |det| >= 1e-12, then maps every template pixel back with the inverse
(adjugate) matrix: skip when the projective denominator <= 1e-6 or the source point is outside
[0,w) x [0,h); otherwise bilinear sample (double precision, +0.5 rounding, clamp), alpha 255.
Pixels outside the quad keep the template. The port keeps the exact order of the floating point
operations and sets `#pragma STDC FP_CONTRACT OFF` so arm64 does not fuse multiply-adds (that would change
bits compared with the x86-64 original).

## Image loading and the macOS platform layer

`MhImageLoadFn(path, &bgra, &w, &h)` returns malloc'ed top-down straight-alpha BGRA (the signature uses
the name `rgba` in the lead's brief but the byte order is the GDI+ one, documented in art.h).
`mh_image_load_imageio` draws the first frame of any ImageIO image into an sRGB premultiplied BGRA context and
un-premultiplies. EXIF orientation is ignored like GDI+. `mh_image_save_png` is used by tests and by an
optional "export preview".
`MhArt` also holds the template pointer: the app loads `case_template.bgra` from the bundle with
`mh_art_load_case_template` (without it only kinds 0 and 2 work).

## make_custom_art (non UI part)

* Disc art target: `<custom/art>/<serial>-CD.art` (serial: ASCII id of the disc, 47 chars at most; the
  original used the entry's first non-empty id text, offset 0xC or else 0x1C of the project record).
  Case art target: `<custom/art>/CASE-<n>.art` with the one byte case number of the project entry.
  `mh_art_custom_name` builds both names.
* If the picked file ends in `.art` (case-insensitive) it must be exactly 10240 bytes and is copied with
  `mh_copy_file_safe`; otherwise it is generated (disc or case). The caller then stores the destination
  path in the project, marks it dirty and refreshes (`mh_art_import` returns only success or the text).
* The file dialog title ("Escolher imagem para a Case" / "Escolher imagem para o Disco") and filter
  ("Imagens (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.art)") belong to the UI.

## Deviations

* No global gdiplus state; the loader lives in `MhArt`.
* Generation is split into pure steps so tests and the UI can run without files.
* A `.art` import that fails the copy also reports the 10,240 byte message (as in the original).
