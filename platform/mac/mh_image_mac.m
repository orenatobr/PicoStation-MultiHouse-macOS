/* platform/mac/mh_image_mac.m: image loading and PNG export on top of ImageIO. */
#include "mh_image_mac.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdlib.h>
#include <string.h>

#define BGRA_INFO ((CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little)

int mh_image_load_imageio(const char *path, uint8_t **bgra, int *w, int *h) {
    int ok = 0;
    uint8_t *buf = NULL;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path,
                                                           (CFIndex)strlen(path), false);
    if (!url) return 0;
    CGImageSourceRef src = CGImageSourceCreateWithURL(url, NULL);
    CFRelease(url);
    if (!src) return 0;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if (!img) return 0;
    size_t iw = CGImageGetWidth(img), ih = CGImageGetHeight(img);
    if (iw && ih && iw <= 0x7fffffff && ih <= 0x7fffffff && iw * ih <= (SIZE_MAX / 4)) {
        buf = calloc(iw * ih, 4);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = (buf && cs) ? CGBitmapContextCreate(buf, iw, ih, 8, iw * 4, cs, BGRA_INFO)
                                       : NULL;
        if (cs) CFRelease(cs);
        if (ctx) {
            CGContextSetBlendMode(ctx, kCGBlendModeCopy);
            CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)iw, (CGFloat)ih), img);
            CGContextRelease(ctx);
            /* un-premultiply: GDI+ PixelFormat32bppARGB is straight alpha */
            for (size_t i = 0; i < iw * ih; i++) {
                uint8_t *p = buf + i * 4;
                uint32_t a = p[3];
                if (a != 0 && a != 255) {
                    for (int c = 0; c < 3; c++) {
                        uint32_t v = (p[c] * 255u + a / 2) / a;
                        p[c] = (uint8_t)(v > 255 ? 255 : v);
                    }
                }
            }
            *bgra = buf;
            *w = (int)iw;
            *h = (int)ih;
            ok = 1;
            buf = NULL;
        }
    }
    free(buf);
    CGImageRelease(img);
    return ok;
}

int mh_image_save_png(const char *path, const uint8_t *bgra, int w, int h) {
    int ok = 0;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    size_t n = (size_t)w * (size_t)h * 4;
    uint8_t *tmp = malloc(n);
    if (!cs || !tmp) { if (cs) CFRelease(cs); free(tmp); return 0; }
    /* premultiply for CoreGraphics */
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t a = bgra[i * 4 + 3];
        for (int c = 0; c < 3; c++) tmp[i * 4 + c] = (uint8_t)((bgra[i * 4 + c] * a + 127) / 255);
        tmp[i * 4 + 3] = (uint8_t)a;
    }
    CGContextRef ctx = CGBitmapContextCreate(tmp, (size_t)w, (size_t)h, 8, (size_t)w * 4, cs, BGRA_INFO);
    CFRelease(cs);
    if (ctx) {
        CGImageRef img = CGBitmapContextCreateImage(ctx);
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path,
                                                               (CFIndex)strlen(path), false);
        CGImageDestinationRef dst = url ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL) : NULL;
        if (img && dst) {
            CGImageDestinationAddImage(dst, img, NULL);
            ok = CGImageDestinationFinalize(dst) ? 1 : 0;
        }
        if (dst) CFRelease(dst);
        if (url) CFRelease(url);
        if (img) CGImageRelease(img);
        CGContextRelease(ctx);
    }
    free(tmp);
    return ok;
}
