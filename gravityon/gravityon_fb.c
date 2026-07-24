/**
 * GRAVITYON — Framebuffer Backend Uygulama
 * =========================================
 * OS / bare-metal için doğrudan bellek framebuffer erişimi.
 */

#include "gravityon_fb.h"
#include <string.h>

/* =========================================================================
 * gravFBPresent — GravImage → Framebuffer kopyala
 * ========================================================================= */

GravResult gravFBPresent(GravDevice device,
                          const GravFBTarget* target,
                          GravImage image,
                          uint32_t srcX, uint32_t srcY,
                          uint32_t dstX, uint32_t dstY,
                          uint32_t w, uint32_t h) {
    if (!target || !target->base) return GRAV_ERROR_INVALID_ARGUMENT;
    if (!image)                   return GRAV_ERROR_INVALID_HANDLE;

    /* Ham piksel verisini al */
    void*  pixels    = NULL;
    size_t sizeBytes = 0;
    GravResult r = gravGetImageData(device, image, &pixels, &sizeBytes);
    if (r != GRAV_SUCCESS) return r;

    uint32_t imgW = 0, imgH = 0;
    gravGetImageExtent(device, image, &imgW, &imgH);

    /* Boyut belirle */
    if (w == 0) w = imgW > srcX ? imgW - srcX : 0;
    if (h == 0) h = imgH > srcY ? imgH - srcY : 0;

    /* Sınır kırp */
    if (dstX + w > target->width)  w = target->width  > dstX ? target->width  - dstX : 0;
    if (dstY + h > target->height) h = target->height > dstY ? target->height - dstY : 0;
    if (w == 0 || h == 0) return GRAV_SUCCESS;

    const uint8_t* src = (const uint8_t*)pixels;

    /* Format dönüşümü ile kopyala */
    for (uint32_t row = 0; row < h; row++) {
        uint32_t sy = srcY + row;
        uint32_t dy = dstY + row;

        for (uint32_t col = 0; col < w; col++) {
            uint32_t sx = srcX + col;
            uint32_t dx = dstX + col;

            /* Kaynak: her zaman RGBA8 */
            const uint8_t* sp = src + (sy * imgW + sx) * 4;
            uint8_t rv = sp[0], gv = sp[1], bv = sp[2], av = sp[3];

            gravFBWritePixel(target, dx, dy, rv, gv, bv, av);
        }
    }

    return GRAV_SUCCESS;
}

/* =========================================================================
 * gravFBClear — Framebuffer'ı tek renkle doldur
 * ========================================================================= */

GravResult gravFBClear(const GravFBTarget* target, GravColorF color) {
    if (!target || !target->base) return GRAV_ERROR_INVALID_ARGUMENT;

    uint8_t r = (uint8_t)(color.r > 1.f ? 255 : color.r < 0 ? 0 : color.r * 255.f);
    uint8_t g = (uint8_t)(color.g > 1.f ? 255 : color.g < 0 ? 0 : color.g * 255.f);
    uint8_t b = (uint8_t)(color.b > 1.f ? 255 : color.b < 0 ? 0 : color.b * 255.f);
    uint8_t a = (uint8_t)(color.a > 1.f ? 255 : color.a < 0 ? 0 : color.a * 255.f);

    /* RGBA8 için hızlı memset-benzeri doldurma */
    if (target->format == GRAV_FB_RGBA8) {
        uint8_t* dst = (uint8_t*)target->base;
        for (uint32_t y = 0; y < target->height; y++) {
            uint8_t* row = dst + y * target->pitch;
            for (uint32_t x = 0; x < target->width; x++) {
                row[x*4+0]=r; row[x*4+1]=g; row[x*4+2]=b; row[x*4+3]=a;
            }
        }
    } else {
        /* Genel yol: her pikseli ayrı yaz */
        for (uint32_t y = 0; y < target->height; y++)
            for (uint32_t x = 0; x < target->width; x++)
                gravFBWritePixel(target, x, y, r, g, b, a);
    }

    return GRAV_SUCCESS;
}
