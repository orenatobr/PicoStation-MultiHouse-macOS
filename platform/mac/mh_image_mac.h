/* platform/mac/mh_image_mac.h: ImageIO implementation of MhImageLoadFn (see mh/art.h). */
#ifndef MH_IMAGE_MAC_H
#define MH_IMAGE_MAC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes PNG, JPEG, BMP, GIF (first frame), TIFF and anything else ImageIO reads. Output is
 * 32 bit BGRA, straight alpha, top row first, malloc'ed. Returns 1 on success, 0 on failure.
 * Like GDI+ in the original it ignores colour profiles' orientation tags. */
int mh_image_load_imageio(const char *path, uint8_t **bgra, int *w, int *h);

/* Writes a w*h BGRA bitmap as PNG (used by tests and by "export preview"). Returns 1 on
 * success. */
int mh_image_save_png(const char *path, const uint8_t *bgra, int w, int h);

#ifdef __cplusplus
}
#endif
#endif
