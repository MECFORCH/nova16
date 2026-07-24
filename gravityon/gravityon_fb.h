/**
 * GRAVITYON — Framebuffer Backend
 * ================================
 * OS / bare-metal hedefler için: PPM/BMP dosyası yerine doğrudan
 * bellek adresine (linear framebuffer) piksel yazar.
 *
 * NOVA-64 OS'ta kullanım:
 *
 *   // Boot sırasında ekran belleğini kaydet
 *   GravFBTarget fb;
 *   gravFBInit(&fb, (void*)0xFB000000, 1920, 1080, GRAV_FB_RGBA8);
 *
 *   // Render edilen image'ı doğrudan ekrana kopyala
 *   gravFBPresent(&fb, colorImage);
 *
 * Bağımlılık: sadece gravityon.h — libc gerekmez.
 */

#ifndef GRAVITYON_FB_H
#define GRAVITYON_FB_H

#include "gravityon.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * FRAMEBUFFER FORMAT
 * ========================================================================= */

typedef enum GravFBFormat {
    GRAV_FB_RGBA8   = 0,   /* 4 bayt/piksel: R8 G8 B8 A8  (en yaygın)      */
    GRAV_FB_BGRA8   = 1,   /* 4 bayt/piksel: B8 G8 R8 A8  (Windows/VBE)    */
    GRAV_FB_RGB8    = 2,   /* 3 bayt/piksel: R8 G8 B8      (hizalama yok)   */
    GRAV_FB_BGR8    = 3,   /* 3 bayt/piksel: B8 G8 R8                       */
    GRAV_FB_RGB565  = 4,   /* 2 bayt/piksel: R5 G6 B5      (gömülü sistem)  */
    GRAV_FB_ARGB8   = 5,   /* 4 bayt/piksel: A8 R8 G8 B8   (bazı BIOS'lar) */
} GravFBFormat;

/* =========================================================================
 * FRAMEBUFFER HEDEFİ
 * ========================================================================= */

typedef struct GravFBTarget {
    void*        base;       /* Framebuffer başlangıç adresi (fiziksel/sanal) */
    uint32_t     width;      /* Piksel genişliği                               */
    uint32_t     height;     /* Piksel yüksekliği                              */
    uint32_t     pitch;      /* Bir satırın bayt cinsinden uzunluğu (stride)  */
    GravFBFormat format;     /* Piksel formatı                                 */
} GravFBTarget;

/* =========================================================================
 * NOVA-64 I/O PORT TANIMI
 * ========================================================================= */

/**
 * NOVA-64 mimarisinde framebuffer I/O port adresleri.
 * SPEC.md'deki bellek haritasıyla uyumlu.
 *
 * Port 0xF0 — FB_CTRL : Kontrol registeri
 *   [63:32] yükseklik  [31:16] genişlik  [15:0] format
 * Port 0xF1 — FB_ADDR : Framebuffer fiziksel başlangıç adresi
 * Port 0xF2 — FB_PITCH: Satır adımı (bayt)
 * Port 0xF3 — FB_FLIP : Yazmak → double buffering flip tetikler
 */
#define NOVA64_FB_PORT_CTRL  0xF0
#define NOVA64_FB_PORT_ADDR  0xF1
#define NOVA64_FB_PORT_PITCH 0xF2
#define NOVA64_FB_PORT_FLIP  0xF3

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * Framebuffer hedefini başlat.
 *
 * @param target  Doldurulacak GravFBTarget yapısı
 * @param base    Framebuffer bellek adresi (örn. 0xFB000000 veya BIOS'tan alınan)
 * @param width   Ekran piksel genişliği
 * @param height  Ekran piksel yüksekliği
 * @param format  Piksel formatı
 *
 * pitch otomatik hesaplanır; farklı bir değer gerekiyorsa yapıyı elle doldur.
 */
static inline void gravFBInit(GravFBTarget* target,
                               void* base,
                               uint32_t width, uint32_t height,
                               GravFBFormat format) {
    target->base   = base;
    target->width  = width;
    target->height = height;
    target->format = format;

    /* Varsayılan pitch hesapla */
    uint32_t bpp;
    switch (format) {
        case GRAV_FB_RGB565:             bpp = 2; break;
        case GRAV_FB_RGB8: case GRAV_FB_BGR8: bpp = 3; break;
        default:                         bpp = 4; break;
    }
    target->pitch = width * bpp;
}

/**
 * Gravityon image'ını framebuffer'a kopyala (present/blit).
 *
 * GravImage'ın formatı GRAV_FORMAT_R8G8B8A8_UNORM olmalıdır.
 * Gerekirse format dönüşümü otomatik yapılır.
 *
 * @param target    Hedef framebuffer
 * @param image     Kaynak GravImage (renk attachment)
 * @param srcX,srcY Kaynak başlangıç koordinatı
 * @param dstX,dstY Hedef başlangıç koordinatı
 * @param w,h       Kopyalanacak bölge boyutu (0 = tam image)
 */
GravResult gravFBPresent(GravDevice device,
                          const GravFBTarget* target,
                          GravImage image,
                          uint32_t srcX, uint32_t srcY,
                          uint32_t dstX, uint32_t dstY,
                          uint32_t w, uint32_t h);

/**
 * Framebuffer'ı tek bir renkle doldur (donanım clear).
 */
GravResult gravFBClear(const GravFBTarget* target, GravColorF color);

/**
 * Framebuffer'ın belirli bir pikselini oku (RGBA8).
 */
static inline void gravFBReadPixel(const GravFBTarget* target,
                                    uint32_t x, uint32_t y,
                                    uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (x >= target->width || y >= target->height) return;
    const uint8_t* row = (const uint8_t*)target->base + y * target->pitch;
    switch (target->format) {
        case GRAV_FB_RGBA8:
            *r = row[x*4+0]; *g = row[x*4+1]; *b = row[x*4+2]; *a = row[x*4+3]; break;
        case GRAV_FB_BGRA8:
            *b = row[x*4+0]; *g = row[x*4+1]; *r = row[x*4+2]; *a = row[x*4+3]; break;
        case GRAV_FB_ARGB8:
            *a = row[x*4+0]; *r = row[x*4+1]; *g = row[x*4+2]; *b = row[x*4+3]; break;
        case GRAV_FB_RGB8:
            *r = row[x*3+0]; *g = row[x*3+1]; *b = row[x*3+2]; *a = 0xFF; break;
        case GRAV_FB_BGR8:
            *b = row[x*3+0]; *g = row[x*3+1]; *r = row[x*3+2]; *a = 0xFF; break;
        case GRAV_FB_RGB565: {
            uint16_t px = ((const uint16_t*)row)[x];
            *r = (uint8_t)((px >> 11) & 0x1F) << 3;
            *g = (uint8_t)((px >>  5) & 0x3F) << 2;
            *b = (uint8_t)((px      ) & 0x1F) << 3;
            *a = 0xFF; break;
        }
        default: break;
    }
}

/**
 * Framebuffer'ın belirli bir pikselini yaz.
 */
static inline void gravFBWritePixel(const GravFBTarget* target,
                                     uint32_t x, uint32_t y,
                                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x >= target->width || y >= target->height) return;
    uint8_t* row = (uint8_t*)target->base + y * target->pitch;
    switch (target->format) {
        case GRAV_FB_RGBA8:
            row[x*4+0]=r; row[x*4+1]=g; row[x*4+2]=b; row[x*4+3]=a; break;
        case GRAV_FB_BGRA8:
            row[x*4+0]=b; row[x*4+1]=g; row[x*4+2]=r; row[x*4+3]=a; break;
        case GRAV_FB_ARGB8:
            row[x*4+0]=a; row[x*4+1]=r; row[x*4+2]=g; row[x*4+3]=b; break;
        case GRAV_FB_RGB8:
            row[x*3+0]=r; row[x*3+1]=g; row[x*3+2]=b; break;
        case GRAV_FB_BGR8:
            row[x*3+0]=b; row[x*3+1]=g; row[x*3+2]=r; break;
        case GRAV_FB_RGB565: {
            uint16_t px = (uint16_t)(((r>>3)<<11) | ((g>>2)<<5) | (b>>3));
            ((uint16_t*)row)[x] = px; break;
        }
        default: break;
    }
}

/* =========================================================================
 * NOVA-64 ASSEMBLY YARDIMCILARI
 * ========================================================================= */

/*
 * NOVA-64 mimarisinde donanım framebuffer kaydı.
 * Simülatörde çalışırken bu fonksiyonları kullanarak
 * sanal ekran donanımını (port 0xF0-0xF3) başlatabilirsin.
 *
 * NOT: Bu fonksiyonlar NOVA-64 simülatöründe (sim.c) çalışır.
 *      Gerçek donanımda asm OUT komutu kullanılır.
 */

#ifdef NOVA64_SIMULATOR
/* Simülatör I/O port erişimi (sim.c'nin sunduğu fonksiyon) */
extern void nova64_out(uint8_t port, uint64_t value);
extern uint64_t nova64_in(uint8_t port);

static inline void gravNova64FBRegister(const GravFBTarget* target) {
    uint64_t ctrl = ((uint64_t)target->height << 32) |
                    ((uint64_t)target->width  << 16) |
                    (uint64_t)target->format;
    nova64_out(NOVA64_FB_PORT_CTRL,  ctrl);
    nova64_out(NOVA64_FB_PORT_ADDR,  (uint64_t)(uintptr_t)target->base);
    nova64_out(NOVA64_FB_PORT_PITCH, (uint64_t)target->pitch);
}

static inline void gravNova64FBFlip(void) {
    nova64_out(NOVA64_FB_PORT_FLIP, 1);
}
#endif /* NOVA64_SIMULATOR */

#ifdef __cplusplus
}
#endif

#endif /* GRAVITYON_FB_H */
