/**
 * GRAVITYON GPU — I/O Port Haritası
 * ===================================
 * NOVA-64 mimarisinde CPU ↔ GPU iletişim arayüzü.
 *
 * CPU şu komutu kullanır: OUT port, değer
 * CPU şu komutu kullanır: IN  port → değer
 *
 * Mimari:
 *   CPU, ring buffer'ı RAM'de hazırlar.
 *   Doorbell portuna yazar → GPU uyandırılır.
 *   GPU, DMA ile ring buffer'ı okur ve çalıştırır.
 *   İş bitince IRQ fırlatır (opsiyonel).
 */

#ifndef GPU_IO_H
#define GPU_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * PORT ADRESLERİ (0xE0 – 0xFF)
 * ========================================================================= */

/* ─── Kimlik & Versiyon ───────────────────────────────────────────────── */
#define GPU_PORT_ID          0xE0  /* RO: 0x47505500 = "GPU\0"            */
#define GPU_PORT_VERSION     0xE1  /* RO: [31:16]=major [15:8]=minor [7:0]=patch */

/* ─── Durum & Kontrol ─────────────────────────────────────────────────── */
#define GPU_PORT_STATUS      0xE2  /* RO: GPU durum bayrakları (aşağıda)   */
#define GPU_PORT_CTRL        0xE3  /* WO: GPU kontrol (aşağıda)            */

/* ─── Ring Buffer (Komut Kuyruğu) ─────────────────────────────────────── */
#define GPU_PORT_RING_BASE   0xE4  /* RW: Ring buffer fiziksel adresi       */
#define GPU_PORT_RING_SIZE   0xE5  /* RW: Ring buffer boyutu (bayt)         */
#define GPU_PORT_RING_HEAD   0xE6  /* RW: CPU yazma pozisyonu (head)        */
#define GPU_PORT_RING_TAIL   0xE7  /* RO: GPU okuma pozisyonu (tail)        */
#define GPU_PORT_DOORBELL    0xE8  /* WO: Herhangi bir değer yaz → GPU uyan */

/* ─── DMA ─────────────────────────────────────────────────────────────── */
#define GPU_PORT_DMA_SRC     0xE9  /* RW: DMA kaynak adresi                 */
#define GPU_PORT_DMA_DST     0xEA  /* RW: DMA hedef adresi (VRAM ofseti)    */
#define GPU_PORT_DMA_SIZE    0xEB  /* RW: DMA boyutu (bayt)                 */
#define GPU_PORT_DMA_CTRL    0xEC  /* WO: DMA başlat (1=başlat)             */
#define GPU_PORT_DMA_STATUS  0xED  /* RO: DMA durum (0=hazır,1=meşgul)      */

/* ─── Framebuffer ─────────────────────────────────────────────────────── */
#define GPU_PORT_FB_ADDR     0xEE  /* RW: Framebuffer fiziksel adresi       */
#define GPU_PORT_FB_WIDTH    0xEF  /* RW: Genişlik (piksel)                 */
#define GPU_PORT_FB_HEIGHT   0xF0  /* RW: Yükseklik (piksel)                */
#define GPU_PORT_FB_PITCH    0xF1  /* RW: Satır adımı (bayt)               */
#define GPU_PORT_FB_FORMAT   0xF2  /* RW: Piksel formatı (aşağıda)          */
#define GPU_PORT_FB_FLIP     0xF3  /* WO: Yaz → double buffer flip          */

/* ─── VRAM ────────────────────────────────────────────────────────────── */
#define GPU_PORT_VRAM_SIZE   0xF4  /* RO: Toplam VRAM boyutu (bayt)         */
#define GPU_PORT_VRAM_FREE   0xF5  /* RO: Kullanılabilir VRAM (bayt)        */

/* ─── Texture ─────────────────────────────────────────────────────────── */
#define GPU_PORT_TEX_ADDR    0xF6  /* RW: Doku VRAM ofseti                  */
#define GPU_PORT_TEX_WIDTH   0xF7  /* RW: Doku genişliği                    */
#define GPU_PORT_TEX_HEIGHT  0xF8  /* RW: Doku yüksekliği                   */
#define GPU_PORT_TEX_FORMAT  0xF9  /* RW: Doku formatı                      */
#define GPU_PORT_TEX_SLOT    0xFA  /* RW: Hangi doku slotuna bağla (0-15)   */
#define GPU_PORT_TEX_COMMIT  0xFB  /* WO: Yaz → dokuyu GPU'ya yükle         */

/* ─── IRQ ─────────────────────────────────────────────────────────────── */
#define GPU_PORT_IRQ_STATUS  0xFC  /* RO: Bekleyen IRQ bayrakları            */
#define GPU_PORT_IRQ_MASK    0xFD  /* RW: IRQ maske (1=etkin)               */
#define GPU_PORT_IRQ_CLEAR   0xFE  /* WO: IRQ temizle (bit mask)             */

/* ─── Hata Ayıklama ───────────────────────────────────────────────────── */
#define GPU_PORT_DEBUG       0xFF  /* RW: Debug / serbest kullanım           */

/* =========================================================================
 * DURUM BAYRAKLARI  (GPU_PORT_STATUS)
 * ========================================================================= */
#define GPU_STATUS_IDLE        (1u << 0)   /* GPU hazır, bekliyor          */
#define GPU_STATUS_BUSY        (1u << 1)   /* GPU çalışıyor                */
#define GPU_STATUS_RING_EMPTY  (1u << 2)   /* Ring buffer boş              */
#define GPU_STATUS_RING_FULL   (1u << 3)   /* Ring buffer dolu             */
#define GPU_STATUS_DMA_BUSY    (1u << 4)   /* DMA meşgul                   */
#define GPU_STATUS_FAULT       (1u << 5)   /* Hata oluştu                  */
#define GPU_STATUS_FB_READY    (1u << 6)   /* Framebuffer hazır            */
#define GPU_STATUS_VSYNC       (1u << 7)   /* VSync gerçekleşti            */

/* =========================================================================
 * KONTROL BİTLERİ  (GPU_PORT_CTRL)
 * ========================================================================= */
#define GPU_CTRL_RESET         (1u << 0)   /* GPU'yu sıfırla               */
#define GPU_CTRL_FLUSH         (1u << 1)   /* Ring buffer'ı zorla boşalt   */
#define GPU_CTRL_IRQ_ENABLE    (1u << 2)   /* IRQ sistemini etkinleştir    */
#define GPU_CTRL_VSYNC_ENABLE  (1u << 3)   /* VSync IRQ etkinleştir        */

/* =========================================================================
 * IRQ BAYRAKLARI  (GPU_PORT_IRQ_STATUS / IRQ_CLEAR)
 * ========================================================================= */
#define GPU_IRQ_FRAME_DONE     (1u << 0)   /* Frame tamamlandı             */
#define GPU_IRQ_RING_EMPTY     (1u << 1)   /* Ring buffer boşaldı          */
#define GPU_IRQ_VSYNC          (1u << 2)   /* VSync                        */
#define GPU_IRQ_FAULT          (1u << 3)   /* Hata                         */
#define GPU_IRQ_DMA_DONE       (1u << 4)   /* DMA tamamlandı               */

/* =========================================================================
 * FRAMEBUFFER FORMAT  (GPU_PORT_FB_FORMAT / TEX_FORMAT)
 * ========================================================================= */
#define GPU_FMT_RGBA8    0x00   /* 4 bayt: R8 G8 B8 A8                     */
#define GPU_FMT_BGRA8    0x01   /* 4 bayt: B8 G8 R8 A8                     */
#define GPU_FMT_RGB565   0x02   /* 2 bayt: R5 G6 B5                        */
#define GPU_FMT_RGBA16F  0x03   /* 8 bayt: R16F G16F B16F A16F (HDR)       */
#define GPU_FMT_D32F     0x04   /* 4 bayt: derinlik float                  */

/* =========================================================================
 * NOVA-64 I/O PORT ERİŞİM YARDIMCILARI
 * ========================================================================= */

#ifdef NOVA64_SIMULATOR
/* Simülatör bağlantısı — sim.c tarafından sağlanır */
extern void     nova64_out(uint8_t port, uint64_t value);
extern uint64_t nova64_in (uint8_t port);

static inline void     gpu_write(uint8_t port, uint64_t val) { nova64_out(port, val); }
static inline uint64_t gpu_read (uint8_t port)               { return nova64_in(port); }

#elif defined(NOVA64_NATIVE)
/* Gerçek NOVA-64 donanımında: OUT/IN assembly komutları */
/* Derleyici bu fonksiyonları asm ile sağlar             */
extern void     nova64_out(uint8_t port, uint64_t value);
extern uint64_t nova64_in (uint8_t port);

static inline void     gpu_write(uint8_t port, uint64_t val) { nova64_out(port, val); }
static inline uint64_t gpu_read (uint8_t port)               { return nova64_in(port); }

#else
/* Host test modu: I/O portları bellek dizisine maplanır */
extern uint64_t _gpu_port_regs[256];
static inline void     gpu_write(uint8_t port, uint64_t val) { _gpu_port_regs[port] = val; }
static inline uint64_t gpu_read (uint8_t port)               { return _gpu_port_regs[port]; }
#endif

/* =========================================================================
 * YÜKSEK SEVİYE YARDIMCI FONKSİYONLAR
 * ========================================================================= */

/** GPU'nun hazır olup olmadığını kontrol et */
static inline int gpu_is_idle(void) {
    return (gpu_read(GPU_PORT_STATUS) & GPU_STATUS_IDLE) != 0;
}

/** GPU'yu sıfırla */
static inline void gpu_reset(void) {
    gpu_write(GPU_PORT_CTRL, GPU_CTRL_RESET);
}

/** Doorbell — GPU'yu uyandır */
static inline void gpu_doorbell(void) {
    gpu_write(GPU_PORT_DOORBELL, 1);
}

/** Framebuffer'ı ayarla */
static inline void gpu_setup_framebuffer(uint64_t addr, uint32_t w,
                                          uint32_t h, uint32_t pitch,
                                          uint8_t fmt) {
    gpu_write(GPU_PORT_FB_ADDR,   addr);
    gpu_write(GPU_PORT_FB_WIDTH,  w);
    gpu_write(GPU_PORT_FB_HEIGHT, h);
    gpu_write(GPU_PORT_FB_PITCH,  pitch);
    gpu_write(GPU_PORT_FB_FORMAT, fmt);
}

/** Ring buffer'ı ayarla */
static inline void gpu_setup_ring(uint64_t base, uint32_t size) {
    gpu_write(GPU_PORT_RING_BASE, base);
    gpu_write(GPU_PORT_RING_SIZE, size);
    gpu_write(GPU_PORT_RING_HEAD, 0);
}

/** IRQ'yu etkinleştir */
static inline void gpu_enable_irq(uint32_t mask) {
    gpu_write(GPU_PORT_IRQ_MASK, mask);
    gpu_write(GPU_PORT_CTRL, GPU_CTRL_IRQ_ENABLE);
}

/** IRQ temizle */
static inline void gpu_clear_irq(uint32_t mask) {
    gpu_write(GPU_PORT_IRQ_CLEAR, mask);
}

/** GPU versiyon bilgisi */
static inline uint64_t gpu_get_version(void) {
    return gpu_read(GPU_PORT_VERSION);
}

/** GPU kimlik kontrolü (0x47505500 = "GPU\0") */
static inline int gpu_is_present(void) {
    return gpu_read(GPU_PORT_ID) == 0x47505500ULL;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_IO_H */
