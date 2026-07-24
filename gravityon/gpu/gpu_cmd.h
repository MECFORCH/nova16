/**
 * GRAVITYON GPU — Ring Buffer & Komut Paketleri
 * ===============================================
 * CPU bu komutları RAM'deki ring buffer'a yazar.
 * Doorbell sonrası GPU DMA ile okur ve çalıştırır.
 *
 * Ring Buffer Yapısı:
 *   [BASE + 0]          : header (magic, size, flags)
 *   [BASE + HEAD..TAIL] : komut paketleri
 *
 * Paket Yapısı:
 *   [0] CMD_TYPE (32-bit)
 *   [1] PAYLOAD_SIZE (32-bit, bayt)
 *   [2..N] payload
 */

#ifndef GPU_CMD_H
#define GPU_CMD_H

#include <stdint.h>
#include <string.h>
#include "gpu_bytecode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * KOMUT TÜRLERİ
 * ========================================================================= */

typedef enum GPUCmdType {
    GPU_CMD_NOP            = 0x0000,  /* Boş komut                         */
    GPU_CMD_CLEAR          = 0x0001,  /* Framebuffer'ı temizle             */
    GPU_CMD_DRAW           = 0x0002,  /* Üçgen çiz (indexed veya doğrudan) */
    GPU_CMD_DRAW_INDEXED   = 0x0003,  /* İndeksli üçgen çiz                */
    GPU_CMD_UPLOAD_SHADER  = 0x0004,  /* Shader bytecode'u GPU'ya yükle    */
    GPU_CMD_BIND_SHADER    = 0x0005,  /* Yüklü shader'ı aktif et           */
    GPU_CMD_UPLOAD_VB      = 0x0006,  /* Vertex buffer yükle               */
    GPU_CMD_UPLOAD_IB      = 0x0007,  /* Index buffer yükle                */
    GPU_CMD_UPLOAD_UB      = 0x0008,  /* Uniform buffer yükle              */
    GPU_CMD_UPLOAD_TEX     = 0x0009,  /* Doku yükle                        */
    GPU_CMD_BIND_TEX       = 0x000A,  /* Dokuyu slota bağla                */
    GPU_CMD_SET_VIEWPORT   = 0x000B,  /* Viewport ayarla                   */
    GPU_CMD_SET_SCISSOR    = 0x000C,  /* Scissor dikdörtgeni ayarla        */
    GPU_CMD_SET_RASTER     = 0x000D,  /* Rasterizasyon durumu              */
    GPU_CMD_SET_DEPTH      = 0x000E,  /* Derinlik testi durumu             */
    GPU_CMD_PRESENT        = 0x000F,  /* Framebuffer'ı ekrana gönder       */
    GPU_CMD_FENCE          = 0x0010,  /* Senkronizasyon çiti               */
    GPU_CMD_IRQ            = 0x0011,  /* IRQ tetikle                       */
    GPU_CMD_MEMCPY         = 0x0012,  /* VRAM içi kopyalama                */
    GPU_CMD_FILL_RECT      = 0x0013,  /* Hızlı dikdörtgen doldur           */
    GPU_CMD_BLIT           = 0x0014,  /* Image kopyala (scale/flip)        */
    GPU_CMD_COMPUTE        = 0x0015,  /* Compute shader başlat             */
    GPU_CMD_TIMESTAMP      = 0x0016,  /* Zaman damgası al                  */
} GPUCmdType;

/* =========================================================================
 * PAKET BAŞLIĞI (her komutun başında)
 * ========================================================================= */

typedef struct GPUCmdHeader {
    uint32_t type;          /* GPUCmdType                              */
    uint32_t payloadBytes;  /* bu başlıktan sonraki payload bayt sayısı */
} GPUCmdHeader;

/* =========================================================================
 * KOMUT PAYLOADLARı
 * ========================================================================= */

/* GPU_CMD_CLEAR */
typedef struct GPUCmdClear {
    uint32_t flags;         /* bit0=color, bit1=depth, bit2=stencil    */
    float    r, g, b, a;   /* renk                                     */
    float    depth;         /* derinlik (1.0 = uzak)                   */
    uint32_t stencil;
} GPUCmdClear;

/* GPU_CMD_DRAW */
typedef struct GPUCmdDraw {
    uint32_t vertexCount;   /* çizilecek vertex sayısı                 */
    uint32_t firstVertex;   /* başlangıç vertex indeksi                */
    uint32_t instanceCount; /* örnek sayısı (1 = normal)               */
    uint32_t firstInstance;
    uint64_t vbOffset;      /* vertex buffer VRAM ofseti               */
    uint32_t vertexStride;  /* bayt/vertex                             */
    uint32_t topology;      /* 0=tri_list, 1=tri_strip, 2=line         */
} GPUCmdDraw;

/* GPU_CMD_DRAW_INDEXED */
typedef struct GPUCmdDrawIndexed {
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t instanceCount;
    uint64_t vbOffset;      /* vertex buffer VRAM ofseti               */
    uint64_t ibOffset;      /* index buffer VRAM ofseti                */
    uint32_t vertexStride;
    uint32_t indexType;     /* 0=uint16, 1=uint32                      */
    uint32_t topology;
} GPUCmdDrawIndexed;

/* GPU_CMD_UPLOAD_SHADER */
typedef struct GPUCmdUploadShader {
    uint32_t shaderSlot;    /* 0-15: hangi slota yükle                 */
    uint32_t shaderType;    /* GBYTShaderType                          */
    uint32_t instrCount;    /* komut sayısı                            */
    uint32_t varyingCount;  /* varying sayısı                          */
    /* GBYTInstr instrs[instrCount] payload'da devam eder */
} GPUCmdUploadShader;

/* GPU_CMD_BIND_SHADER */
typedef struct GPUCmdBindShader {
    uint32_t vertSlot;      /* vertex shader slot                      */
    uint32_t fragSlot;      /* fragment shader slot                    */
} GPUCmdBindShader;

/* GPU_CMD_UPLOAD_VB / IB / UB */
typedef struct GPUCmdUploadBuffer {
    uint64_t vramOffset;    /* VRAM'de nereye yükle                    */
    uint32_t sizeBytes;     /* veri boyutu                             */
    uint32_t flags;
    /* uint8_t data[sizeBytes] payload'da devam eder */
} GPUCmdUploadBuffer;

/* GPU_CMD_UPLOAD_TEX */
typedef struct GPUCmdUploadTex {
    uint64_t vramOffset;    /* doku verisi VRAM ofseti                 */
    uint32_t width;
    uint32_t height;
    uint32_t format;        /* GPU_FMT_*                               */
    uint32_t slot;          /* doku slotu (0-15)                       */
    uint32_t sizeBytes;
    /* uint8_t texData[sizeBytes] devam eder */
} GPUCmdUploadTex;

/* GPU_CMD_BIND_TEX */
typedef struct GPUCmdBindTex {
    uint32_t slot;
    uint64_t vramOffset;
    uint32_t width, height, format;
} GPUCmdBindTex;

/* GPU_CMD_SET_VIEWPORT */
typedef struct GPUCmdViewport {
    float x, y, width, height;
    float minDepth, maxDepth;
} GPUCmdViewport;

/* GPU_CMD_SET_SCISSOR */
typedef struct GPUCmdScissor {
    int32_t  x, y;
    uint32_t width, height;
} GPUCmdScissor;

/* GPU_CMD_SET_RASTER */
typedef struct GPUCmdRaster {
    uint32_t cullMode;      /* 0=none, 1=back, 2=front                 */
    uint32_t fillMode;      /* 0=solid, 1=wireframe                    */
    uint32_t frontFace;     /* 0=CCW, 1=CW                             */
} GPUCmdRaster;

/* GPU_CMD_SET_DEPTH */
typedef struct GPUCmdDepth {
    uint32_t testEnable;
    uint32_t writeEnable;
    uint32_t compareOp;    /* 0=less,1=lequal,2=greater,3=always       */
} GPUCmdDepth;

/* GPU_CMD_FILL_RECT */
typedef struct GPUCmdFillRect {
    int32_t  x, y;
    uint32_t width, height;
    float    r, g, b, a;
} GPUCmdFillRect;

/* GPU_CMD_FENCE */
typedef struct GPUCmdFence {
    uint32_t fenceId;       /* CPU'nun beklediği çit kimliği           */
    uint32_t flags;
} GPUCmdFence;

/* GPU_CMD_UPLOAD_UB (uniform buffer) */
typedef struct GPUCmdUploadUB {
    uint32_t slot;          /* uniform buffer slotu (0-7)              */
    uint32_t sizeBytes;
    /* float data[] devam eder */
} GPUCmdUploadUB;

/* =========================================================================
 * RING BUFFER YÖNETİCİSİ
 * ========================================================================= */

#define GPU_RING_MAGIC  0x47525547U  /* "GRUG" */
#define GPU_RING_MAX    (1 << 20)    /* 1 MB maksimum ring boyutu         */

typedef struct GPURingBuffer {
    uint8_t* base;          /* ring buffer başlangıcı (CPU erişimi)    */
    uint32_t size;          /* toplam boyut (bayt)                     */
    uint32_t head;          /* CPU yazma pozisyonu                     */
    uint32_t tail;          /* GPU okuma pozisyonu (GPU günceller)     */
} GPURingBuffer;

/** Ring buffer başlat */
static inline void gpu_ring_init(GPURingBuffer* rb, void* mem, uint32_t size) {
    rb->base = (uint8_t*)mem;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    memset(mem, 0, size);
}

/** Boş alan var mı? */
static inline uint32_t gpu_ring_free(const GPURingBuffer* rb) {
    uint32_t used = (rb->head >= rb->tail)
                  ? rb->head - rb->tail
                  : rb->size - rb->tail + rb->head;
    return rb->size - used - 1;
}

/** Komut yaz (header + payload) */
static inline int gpu_ring_write(GPURingBuffer* rb,
                                  GPUCmdType type,
                                  const void* payload,
                                  uint32_t payloadBytes) {
    uint32_t total = sizeof(GPUCmdHeader) + payloadBytes;
    if (gpu_ring_free(rb) < total) return -1; /* doldu */

    GPUCmdHeader hdr = { (uint32_t)type, payloadBytes };
    uint8_t* dst;

    /* Header yaz */
    uint32_t remaining = rb->size - rb->head;
    if (remaining >= sizeof(hdr)) {
        memcpy(rb->base + rb->head, &hdr, sizeof(hdr));
        rb->head = (rb->head + (uint32_t)sizeof(hdr)) % rb->size;
    } else {
        /* Wrap-around */
        memcpy(rb->base + rb->head, &hdr, remaining);
        memcpy(rb->base, (uint8_t*)&hdr + remaining, sizeof(hdr) - remaining);
        rb->head = (uint32_t)(sizeof(hdr) - remaining);
    }

    /* Payload yaz */
    if (payloadBytes > 0 && payload) {
        remaining = rb->size - rb->head;
        if (remaining >= payloadBytes) {
            memcpy(rb->base + rb->head, payload, payloadBytes);
            rb->head = (rb->head + payloadBytes) % rb->size;
        } else {
            memcpy(rb->base + rb->head, payload, remaining);
            memcpy(rb->base, (uint8_t*)payload + remaining, payloadBytes - remaining);
            rb->head = payloadBytes - remaining;
        }
    }
    return 0;
}

/** Ring buffer'dan oku (GPU tarafı) */
static inline int gpu_ring_read_header(GPURingBuffer* rb, GPUCmdHeader* out) {
    uint32_t avail = (rb->head >= rb->tail)
                   ? rb->head - rb->tail
                   : rb->size - rb->tail + rb->head;
    if (avail < sizeof(GPUCmdHeader)) return -1;

    uint32_t remaining = rb->size - rb->tail;
    if (remaining >= sizeof(*out)) {
        memcpy(out, rb->base + rb->tail, sizeof(*out));
        rb->tail = (rb->tail + (uint32_t)sizeof(*out)) % rb->size;
    } else {
        memcpy(out, rb->base + rb->tail, remaining);
        memcpy((uint8_t*)out + remaining, rb->base, sizeof(*out) - remaining);
        rb->tail = (uint32_t)(sizeof(*out) - remaining);
    }
    return 0;
}

static inline int gpu_ring_read_payload(GPURingBuffer* rb,
                                         void* buf, uint32_t size) {
    uint32_t remaining = rb->size - rb->tail;
    if (remaining >= size) {
        memcpy(buf, rb->base + rb->tail, size);
        rb->tail = (rb->tail + size) % rb->size;
    } else {
        memcpy(buf, rb->base + rb->tail, remaining);
        memcpy((uint8_t*)buf + remaining, rb->base, size - remaining);
        rb->tail = size - remaining;
    }
    return 0;
}

static inline int gpu_ring_skip(GPURingBuffer* rb, uint32_t size) {
    rb->tail = (rb->tail + size) % rb->size;
    return 0;
}

/** Ring boş mu? */
static inline int gpu_ring_empty(const GPURingBuffer* rb) {
    return rb->head == rb->tail;
}

/* =========================================================================
 * YÜKSEK SEVİYE KOMUT GÖNDERİCİLERİ
 * ========================================================================= */

static inline int gpu_cmd_clear(GPURingBuffer* rb,
                                 float r, float g, float b, float a,
                                 float depth) {
    GPUCmdClear c = { .flags=3, .r=r,.g=g,.b=b,.a=a, .depth=depth };
    return gpu_ring_write(rb, GPU_CMD_CLEAR, &c, sizeof(c));
}

static inline int gpu_cmd_present(GPURingBuffer* rb) {
    return gpu_ring_write(rb, GPU_CMD_PRESENT, NULL, 0);
}

static inline int gpu_cmd_viewport(GPURingBuffer* rb,
                                    float x, float y, float w, float h) {
    GPUCmdViewport v = {x,y,w,h,0.0f,1.0f};
    return gpu_ring_write(rb, GPU_CMD_SET_VIEWPORT, &v, sizeof(v));
}

static inline int gpu_cmd_scissor(GPURingBuffer* rb,
                                   int32_t x, int32_t y,
                                   uint32_t w, uint32_t h) {
    GPUCmdScissor s = {x,y,w,h};
    return gpu_ring_write(rb, GPU_CMD_SET_SCISSOR, &s, sizeof(s));
}

static inline int gpu_cmd_raster(GPURingBuffer* rb,
                                  uint32_t cull, uint32_t fill) {
    GPUCmdRaster r = {cull, fill, 0};
    return gpu_ring_write(rb, GPU_CMD_SET_RASTER, &r, sizeof(r));
}

static inline int gpu_cmd_depth(GPURingBuffer* rb,
                                 int test, int write, uint32_t cmp) {
    GPUCmdDepth d = {(uint32_t)test,(uint32_t)write,cmp};
    return gpu_ring_write(rb, GPU_CMD_SET_DEPTH, &d, sizeof(d));
}

static inline int gpu_cmd_bind_shader(GPURingBuffer* rb,
                                       uint32_t vslot, uint32_t fslot) {
    GPUCmdBindShader b = {vslot, fslot};
    return gpu_ring_write(rb, GPU_CMD_BIND_SHADER, &b, sizeof(b));
}

static inline int gpu_cmd_draw(GPURingBuffer* rb,
                                uint32_t count, uint32_t first,
                                uint64_t vbOfs, uint32_t stride,
                                uint32_t topo) {
    GPUCmdDraw d = {count,first,1,0,vbOfs,stride,topo};
    return gpu_ring_write(rb, GPU_CMD_DRAW, &d, sizeof(d));
}

static inline int gpu_cmd_draw_indexed(GPURingBuffer* rb,
                                        uint32_t idxCount, uint32_t firstIdx,
                                        int32_t vtxOfs, uint64_t vbOfs,
                                        uint64_t ibOfs, uint32_t stride,
                                        uint32_t topo) {
    GPUCmdDrawIndexed d = {idxCount,firstIdx,vtxOfs,1,vbOfs,ibOfs,stride,1,topo};
    return gpu_ring_write(rb, GPU_CMD_DRAW_INDEXED, &d, sizeof(d));
}

static inline int gpu_cmd_fill_rect(GPURingBuffer* rb,
                                     int32_t x, int32_t y,
                                     uint32_t w, uint32_t h,
                                     float r, float g, float b, float a) {
    GPUCmdFillRect fr = {x,y,w,h,r,g,b,a};
    return gpu_ring_write(rb, GPU_CMD_FILL_RECT, &fr, sizeof(fr));
}

static inline int gpu_cmd_fence(GPURingBuffer* rb, uint32_t fenceId) {
    GPUCmdFence f = {fenceId, 0};
    return gpu_ring_write(rb, GPU_CMD_FENCE, &f, sizeof(f));
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_CMD_H */
