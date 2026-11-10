/* mh/art.h: PicoStation menu artwork (.art files): decoding, encoding and generation.
 *
 * .art file format (always exactly MH_ART_FILE_SIZE = 10240 bytes), little endian:
 *   0x0000  u8  format, always 2
 *   0x0001  u8  width  (96 for everything the Manager writes)
 *   0x0002  u8  height (96)
 *   0x0003  u16 pixel count, must equal width * height (0x2400)
 *   0x0005  u8[count] palette indices, row major, top row first
 *   0x2405  u16 palette size in bytes (0x200)
 *   0x2407  u16[256] palette, PS1 15 bit colour: R | G << 5 | B << 10 (R in the low bits),
 *           bit 15 unused. Entry 0 is 0 and means transparent, any other entry is opaque.
 *   0x2607  zero padding up to 10240 bytes
 * The generator quantises to 3-3-2 bits: index = (R & 0xE0) | (G >> 5) << 2 | (B >> 6);
 * a pixel with alpha < 0x20 gets index 0, an opaque pixel that would get index 0 gets 1, and
 * palette entry i holds the 5 bit rescaling of the 3-3-2 components (the original also
 * mapped a zero colour to 0x0421, but that cannot happen for i >= 1).
 * The 17,358 files of the shipped database/art/default use the same layout with an adaptive
 * palette instead (and sometimes bit 15 set); mh_art_decode reads both.
 *
 * Image decoding (PNG, JPG, BMP, GIF, TIFF) is NOT in the core: the original loaded gdiplus.dll
 * at run time, here the platform layer supplies an MhImageLoadFn (platform/mac/mh_image_mac.m
 * implements it with ImageIO). Pixel buffers in this API are 32 bit BGRA in memory order
 * B, G, R, A (the GDI+ PixelFormat32bppARGB layout the original worked with), straight
 * (non-premultiplied) alpha, top row first, no row padding. */
#ifndef MH_ART_H
#define MH_ART_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MH_ART_FILE_SIZE 10240u
#define MH_ART_DIM 96u                 /* width and height of the images the Manager writes */
#define MH_ART_PIXELS (96u * 96u)
#define MH_ART_BGRA_SIZE (96u * 96u * 4u) /* a decoded 96x96 bitmap */
#define MH_CASE_TEMPLATE_DIM 600u
#define MH_CASE_TEMPLATE_SIZE (600u * 600u * 4u) /* 1,440,000 bytes: resources/embedded/case_template.bgra */
#define MH_ART_MAX_SRC_DIM 0x2000u     /* source images must be 1..8192 pixels each way */

/* Loads an image file. On success returns non-zero and sets *bgra (malloc'ed, w*h*4 bytes,
 * layout above, release with free()) and *w, *h. Returns 0 on failure. Parameter names follow
 * the lead's interface; the byte order is BGRA, see the header comment. */
typedef int (*MhImageLoadFn)(const char *path, uint8_t **rgba, int *w, int *h);

typedef enum {
    MH_ART_PLAIN = 0, /* orig art_generate_plain: the whole image scaled to 96x96 */
    MH_ART_CASE = 1,  /* orig art_generate_case: image composed into the case template */
    MH_ART_DISC = 2   /* orig art_generate_disc: centre square, circular disc mask */
} MhArtKind;

/* Replacement for the gdiplus state kept by art_init/art_shutdown. The case template is
 * needed for MH_ART_CASE only. */
typedef struct MhArt {
    MhImageLoadFn load;
    const uint8_t *case_template; /* MH_CASE_TEMPLATE_SIZE bytes of BGRA, or NULL */
    size_t case_template_size;
    uint8_t *owned_template;      /* set by mh_art_load_case_template */
} MhArt;

/* orig: art_init. False if no loader was supplied (the original returned 0 when gdiplus.dll
 * could not be loaded). */
bool mh_art_init(MhArt *a, MhImageLoadFn load);
/* orig: art_shutdown. Idempotent. */
void mh_art_shutdown(MhArt *a);
/* Borrowed template (must outlive a). */
void mh_art_set_case_template(MhArt *a, const uint8_t *data, size_t size);
/* Reads the template file (resources/embedded/case_template.bgra) and owns it. */
bool mh_art_load_case_template(MhArt *a, const char *path);

/* orig: art_decode. Reads a .art file and returns a malloc'ed w*h*4 BGRA bitmap (alpha 255
 * for palette entries other than 0, 0 for entry 0). Same acceptance rules as the original:
 * file longer than 0x206 bytes, format byte 2, w and h non-zero, w*h == stored count,
 * count + 0x207 <= file size. Used by the UI preview. */
bool mh_art_decode(const char *path, uint8_t **bgra, uint32_t *w, uint32_t *h);
/* Same on a memory buffer (art_decode minus the file read). */
bool mh_art_decode_mem(const uint8_t *data, size_t size, uint8_t **bgra, uint32_t *w,
                       uint32_t *h);
/* Encoder: quantises a 96x96 BGRA bitmap into a complete 10240 byte .art image (the tail of
 * generate). */
void mh_art_encode(const uint8_t bgra[MH_ART_BGRA_SIZE], uint8_t out[MH_ART_FILE_SIZE]);

/* ---- the building blocks of generate (all pure, no I/O) ----------------------------------- */

/* orig: generate, plain branch. Bilinear (16.16 fixed point) resize of w*h BGRA to 96x96;
 * corner aligned: output i samples source (w-1)*i/95. out is MH_ART_BGRA_SIZE bytes. */
void mh_art_resize96(const uint8_t *bgra, uint32_t w, uint32_t h, uint8_t *out);
/* orig: generate, disc branch. Centre square crop sampled nearest neighbour onto 96x96, then
 * the disc mask: outside radius ~96, the hub hole and its translucent ring, dark rim. */
bool mh_art_make_disc96(const uint8_t *bgra, uint32_t w, uint32_t h, uint8_t *out);
/* orig: make_case.constprop.0. Copies the template and projects the w*h cover through the
 * perspective transform that maps its corners to the front face quad of the case, bilinear
 * sampling. Returns a malloc'ed 600x600 BGRA image, or NULL (template too small, singular
 * transform, no memory). */
uint8_t *mh_art_make_case600(const uint8_t *tpl, size_t tpl_size, const uint8_t *bgra,
                             uint32_t w, uint32_t h);

/* Whole pipeline from an in-memory source image to a .art image. False on failure with the
 * original Portuguese message in err ("Falha ao montar a imagem na case padrao.", ...). */
bool mh_art_build(const MhArt *a, MhArtKind kind, const uint8_t *bgra, uint32_t w, uint32_t h,
                  uint8_t out[MH_ART_FILE_SIZE], char *err, size_t err_n);

/* orig: generate (and art_generate_plain/case/disc). Loads src with the loader, builds the
 * art and writes it to dst. Returns false and fills err with the original text on failure
 * ("Nao foi possivel carregar PNG, JPG, BMP, GIF ou TIFF pelo GDI+.", "Memoria
 * insuficiente.", "Falha ao montar a imagem na case padrao.", "Falha ao montar o disco
 * padrao.", "Falha ao escrever o arquivo ART."). err may be NULL. */
bool mh_art_generate(const MhArt *a, MhArtKind kind, const char *src, const char *dst,
                     char *err, size_t err_n);
static inline bool mh_art_generate_case(const MhArt *a, const char *s, const char *d, char *e,
                                        size_t n) { return mh_art_generate(a, MH_ART_CASE, s, d, e, n); }
static inline bool mh_art_generate_disc(const MhArt *a, const char *s, const char *d, char *e,
                                        size_t n) { return mh_art_generate(a, MH_ART_DISC, s, d, e, n); }
static inline bool mh_art_generate_plain(const MhArt *a, const char *s, const char *d, char *e,
                                         size_t n) { return mh_art_generate(a, MH_ART_PLAIN, s, d, e, n); }

/* ---- custom art (non-UI part of make_custom_art) ------------------------------------------- */

/* Name of the custom art file in <data>/custom/art: disc art "<serial>-CD.art" (serial is
 * the game id text, at most 47 bytes, the original copied 0x30 wide chars) or case art
 * "CASE-<n>.art" (n is the project case number). is_case selects which. */
void mh_art_custom_name(bool is_case, const char *serial, unsigned case_no, char *out, size_t n);
/* orig: make_custom_art, after the file picker. src is the picked file, dst the target path.
 * A .art source (case-insensitive extension) must be exactly 10240 bytes and is copied
 * (mh_copy_file_safe); anything else goes through mh_art_generate_disc/case. Failure texts:
 * "O arquivo ART precisa ter exatamente 10.240 bytes." or the generate error. */
bool mh_art_import(const MhArt *a, bool is_case, const char *src, const char *dst, char *err,
                   size_t err_n);

#ifdef __cplusplus
}
#endif
#endif
