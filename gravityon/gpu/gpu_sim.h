/**
 * GRAVITYON GPU Simülatörü — Public API
 * ======================================
 * NOVA-64 sim.c ile entegre çalışan GPU simülatörü.
 * GBYT bytecode çalıştırır, ring buffer'ı işler,
 * gerçek FPGA'ya 1:1 davranış sergiler.
 */

#ifndef GPU_SIM_H
#define GPU_SIM_H

#include "gpu_bytecode.h"
#include "gpu_io.h"
#include "gpu_cmd.h"
#include "../gravityon.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * SABITLER
 * ========================================================================= */

#define GPU_SIM_VRAM_SIZE      (64 * 1024 * 1024)  /* 64 MB VRAM           */
#define GPU_SIM_MAX_SHADERS    16
#define GPU_SIM_MAX_TEX_SLOTS  16
#define GPU_SIM_MAX_UB_SLOTS   8
#define GPU_SIM_MAX_VARYINGS   GRAV_MAX_VARYINGS
#define GPU_SIM_RING_SIZE      (1 * 1024 * 1024)   /* 1 MB ring buffer     */
#define GPU_SIM_MAX_UNIFORMS   256                  /* float sayısı         */

/* =========================================================================
 * DOKU SLOT
 * ========================================================================= */

typedef struct GPUTexSlot {
    uint8_t* data;         /* VRAM işaretçisi                           */
    uint32_t width;
    uint32_t height;
    uint32_t format;       /* GPU_FMT_*                                 */
    int      valid;
} GPUTexSlot;

/* =========================================================================
 * GPU SİMÜLATÖR DURUMU
 * ========================================================================= */

typedef struct GPUSim {
    /* VRAM */
    uint8_t* vram;
    uint64_t vramSize;

    /* Framebuffer */
    uint64_t fbOffset;     /* VRAM içinde framebuffer başlangıcı        */
    uint64_t dbOffset;     /* VRAM içinde derinlik buffer başlangıcı    */
    uint32_t fbWidth;
    uint32_t fbHeight;
    uint32_t fbPitch;
    uint32_t fbFormat;

    /* Shader slotları */
    GBYTShader shaders[GPU_SIM_MAX_SHADERS];
    int        shaderValid[GPU_SIM_MAX_SHADERS];
    uint32_t   activeVertSlot;
    uint32_t   activeFragSlot;
    uint32_t   activeVaryingCount;

    /* Uniform buffer */
    float    uniforms[GPU_SIM_MAX_UB_SLOTS][GPU_SIM_MAX_UNIFORMS];
    uint32_t uniformCount[GPU_SIM_MAX_UB_SLOTS];

    /* Doku slotları */
    GPUTexSlot texSlots[GPU_SIM_MAX_TEX_SLOTS];

    /* Rasterizasyon durumu */
    uint32_t cullMode;
    uint32_t fillMode;
    uint32_t topology;

    /* Derinlik durumu */
    int      depthTestEnable;
    int      depthWriteEnable;
    uint32_t depthCompareOp;

    /* Viewport & Scissor */
    float    vpX, vpY, vpW, vpH;
    float    vpMinDepth, vpMaxDepth;
    int32_t  scX, scY;
    uint32_t scW, scH;

    /* Ring buffer */
    GPURingBuffer ring;
    uint8_t       ringMem[GPU_SIM_RING_SIZE];

    /* I/O port kayıtları (host test modu) */
    uint64_t portRegs[256];

    /* İstatistikler */
    uint64_t framesRendered;
    uint64_t trianglesDrawn;
    uint64_t pixelsFilled;
    uint64_t shaderInvocations;
    uint64_t totalNs;

    /* IRQ durumu */
    uint32_t irqStatus;
    uint32_t irqMask;
    void   (*irqCallback)(uint32_t flags, void* userdata);
    void*    irqUserdata;

    /* Durum */
    int      running;
    int      initialized;
} GPUSim;

/* =========================================================================
 * API
 * ========================================================================= */

/** GPU simülatörünü başlat */
int  gpusim_init(GPUSim* gpu);

/** GPU simülatörünü kapat */
void gpusim_destroy(GPUSim* gpu);

/** I/O port yaz (NOVA-64 OUT komutu simülasyonu) */
void gpusim_port_write(GPUSim* gpu, uint8_t port, uint64_t value);

/** I/O port oku (NOVA-64 IN komutu simülasyonu) */
uint64_t gpusim_port_read(GPUSim* gpu, uint8_t port);

/** Ring buffer'daki bekleyen komutları işle */
int  gpusim_process(GPUSim* gpu);

/** Tek komut paketi işle */
int  gpusim_exec_cmd(GPUSim* gpu, GPUCmdType type, const void* payload, uint32_t size);

/** IRQ callback'i kaydet */
void gpusim_set_irq_callback(GPUSim* gpu,
                              void (*cb)(uint32_t flags, void* ud),
                              void* userdata);

/** Framebuffer'ı GravImage'a kopyala */
GravResult gpusim_readback_color(GPUSim* gpu, GravImage image);

/** İstatistikleri sıfırla */
void gpusim_reset_stats(GPUSim* gpu);

/** İstatistikleri yazdır */
void gpusim_print_stats(GPUSim* gpu);

/* =========================================================================
 * YÜKSELTİLMİŞ GRAVİTYON API — GPU backend
 * =========================================================================
 * Bu fonksiyonlar Gravityon'un submit akışını GPU üzerinde çalıştırır.
 * Gravityon command buffer → GPU ring buffer → GBYT shader çalıştırma
 */

/** Gravityon device'ına GPU simülatörü bağla */
GravResult gravGPUAttach(GravDevice device, GPUSim* gpu);

/** GPU backend ile command buffer submit et */
GravResult gravGPUSubmit(GravDevice device, GravCommandBuffer cmdBuf);

/** GPU backend aktif mi? */
int gravGPUIsAttached(GravDevice device);

#ifdef __cplusplus
}
#endif

#endif /* GPU_SIM_H */
