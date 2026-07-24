/**
 * GRAVITYON GPU API
 * =================
 * NOVA-64 ekosistemi için sıfırdan tasarlanmış, Vulkan ilhamlı yazılım GPU API'si.
 *
 * Mimari:
 *   Instance → Device → CommandBuffer → Submit
 *
 * Shader modeli: C fonksiyon işaretçileri (vert + frag çifti)
 * Backend     : Software rasterizer (barycentric, z-buffer, MSAA yok)
 * Çıktı       : RGBA8 framebuffer → PPM / ham piksel erişimi
 *
 * (c) NOVA Project — MIT Lisansı
 */

#ifndef GRAVITYON_H
#define GRAVITYON_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * VERSİYON
 * ========================================================================= */

#define GRAV_VERSION_MAJOR 1
#define GRAV_VERSION_MINOR 0
#define GRAV_VERSION_PATCH 0
#define GRAV_MAKE_VERSION(maj, min, pat) \
    (((uint32_t)(maj) << 22) | ((uint32_t)(min) << 12) | (uint32_t)(pat))
#define GRAV_API_VERSION GRAV_MAKE_VERSION(1, 0, 0)

/* =========================================================================
 * SONUÇ KODLARI
 * ========================================================================= */

typedef enum GravResult {
    GRAV_SUCCESS                        =  0,
    GRAV_NOT_READY                      =  1,
    GRAV_TIMEOUT                        =  2,
    GRAV_ERROR_OUT_OF_MEMORY            = -1,
    GRAV_ERROR_INVALID_HANDLE           = -2,
    GRAV_ERROR_INVALID_ARGUMENT         = -3,
    GRAV_ERROR_OUT_OF_RANGE             = -4,
    GRAV_ERROR_COMMAND_BUFFER_FULL      = -5,
    GRAV_ERROR_NOT_RECORDING            = -6,
    GRAV_ERROR_ALREADY_RECORDING        = -7,
    GRAV_ERROR_RENDER_PASS_NOT_BEGUN    = -8,
    GRAV_ERROR_NO_PIPELINE_BOUND        = -9,
    GRAV_ERROR_NO_VERTEX_BUFFER_BOUND   = -10,
    GRAV_ERROR_IO                       = -11,
} GravResult;

const char* gravResultString(GravResult result);

/* =========================================================================
 * HANDLE TİPLERİ (opak işaretçiler)
 * ========================================================================= */

typedef struct GravInstance_T*       GravInstance;
typedef struct GravDevice_T*         GravDevice;
typedef struct GravBuffer_T*         GravBuffer;
typedef struct GravImage_T*          GravImage;
typedef struct GravShaderModule_T*   GravShaderModule;
typedef struct GravRenderPass_T*     GravRenderPass;
typedef struct GravFramebuffer_T*    GravFramebuffer;
typedef struct GravPipeline_T*       GravPipeline;
typedef struct GravCommandBuffer_T*  GravCommandBuffer;

/* =========================================================================
 * TEMEL VERİ TİPLERİ
 * ========================================================================= */

/** RGBA renk (0.0–1.0 float) */
typedef struct GravColorF {
    float r, g, b, a;
} GravColorF;

/** 2D boyut */
typedef struct GravExtent2D {
    uint32_t width;
    uint32_t height;
} GravExtent2D;

/** 3D boyut */
typedef struct GravExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} GravExtent3D;

/** Viewport tanımı */
typedef struct GravViewport {
    float x, y;
    float width, height;
    float minDepth, maxDepth;   /* genellikle 0.0 – 1.0 */
} GravViewport;

/** Kırpma dikdörtgeni */
typedef struct GravRect2D {
    int32_t  x, y;
    uint32_t width, height;
} GravRect2D;

/* =========================================================================
 * FORMAT
 * ========================================================================= */

typedef enum GravFormat {
    GRAV_FORMAT_UNDEFINED        = 0,
    GRAV_FORMAT_R8G8B8A8_UNORM   = 1,   /* 4 bayt/piksel, ana renk format */
    GRAV_FORMAT_R32G32B32A32_F   = 2,   /* 16 bayt/piksel, float HDR        */
    GRAV_FORMAT_D32_SFLOAT       = 3,   /* 32-bit derinlik                  */
} GravFormat;

/* =========================================================================
 * BUFFER
 * ========================================================================= */

typedef enum GravBufferUsage {
    GRAV_BUFFER_USAGE_VERTEX  = 0x01,
    GRAV_BUFFER_USAGE_INDEX   = 0x02,
    GRAV_BUFFER_USAGE_UNIFORM = 0x04,
    GRAV_BUFFER_USAGE_STORAGE = 0x08,
} GravBufferUsage;

typedef struct GravBufferCreateInfo {
    uint64_t        size;       /* bayt cinsinden boyut */
    GravBufferUsage usage;
} GravBufferCreateInfo;

/* =========================================================================
 * IMAGE
 * ========================================================================= */

typedef enum GravImageUsage {
    GRAV_IMAGE_USAGE_COLOR_ATTACHMENT   = 0x01,
    GRAV_IMAGE_USAGE_DEPTH_ATTACHMENT   = 0x02,
    GRAV_IMAGE_USAGE_SAMPLED            = 0x04,
    GRAV_IMAGE_USAGE_TRANSFER_SRC       = 0x08,
    GRAV_IMAGE_USAGE_TRANSFER_DST       = 0x10,
} GravImageUsage;

typedef struct GravImageCreateInfo {
    GravExtent2D  extent;
    GravFormat    format;
    GravImageUsage usage;
} GravImageCreateInfo;

/* =========================================================================
 * SHADER
 * ========================================================================= */

/**
 * Vertex shader fonksiyon imzası.
 *
 * @param vertexData   Ham vertex verisi (stride, layout pipeline'da tanımlı)
 * @param uniforms     Uniform bloğu işaretçisi (NULL olabilir)
 * @param outPos       Clip-space çıkış pozisyonu [x, y, z, w]
 * @param outVaryings  Vertex→Fragment arası interpolasyon değerleri
 *                     (boyut pipeline'daki varyingCount'a eşit)
 */
typedef void (*GravVertFn)(
    const void* vertexData,
    const void* uniforms,
    float       outPos[4],
    float*      outVaryings
);

/**
 * Fragment shader fonksiyon imzası.
 *
 * @param varyings     Perspektif-doğru interpole edilmiş varyinglar
 * @param uniforms     Uniform bloğu işaretçisi (NULL olabilir)
 * @param outColor     Çıkış rengi [r, g, b, a]
 */
typedef void (*GravFragFn)(
    const float* varyings,
    const void*  uniforms,
    float        outColor[4]
);

typedef struct GravShaderModuleCreateInfo {
    GravVertFn vertFn;   /* vertex shader  (NULL → geçemez) */
    GravFragFn fragFn;   /* fragment shader (NULL → geçemez) */
} GravShaderModuleCreateInfo;

/* =========================================================================
 * RENDER PASS
 * ========================================================================= */

typedef enum GravLoadOp {
    GRAV_LOAD_OP_LOAD       = 0,   /* önceki içeriği koru */
    GRAV_LOAD_OP_CLEAR      = 1,   /* clear değeriyle doldur */
    GRAV_LOAD_OP_DONT_CARE  = 2,
} GravLoadOp;

typedef enum GravStoreOp {
    GRAV_STORE_OP_STORE     = 0,   /* sonucu yaz */
    GRAV_STORE_OP_DONT_CARE = 1,
} GravStoreOp;

typedef struct GravAttachmentDesc {
    GravFormat  format;
    GravLoadOp  loadOp;
    GravStoreOp storeOp;
} GravAttachmentDesc;

typedef struct GravRenderPassCreateInfo {
    GravAttachmentDesc colorAttachment;
    int                hasDepthAttachment;  /* 0 = yok, 1 = var */
    GravAttachmentDesc depthAttachment;
} GravRenderPassCreateInfo;

/* =========================================================================
 * FRAMEBUFFER
 * ========================================================================= */

typedef struct GravFramebufferCreateInfo {
    GravRenderPass renderPass;
    GravImage      colorImage;
    GravImage      depthImage;   /* NULL → derinlik testi yok */
    GravExtent2D   extent;
} GravFramebufferCreateInfo;

/* =========================================================================
 * PIPELINE
 * ========================================================================= */

typedef enum GravPrimitiveTopology {
    GRAV_TOPOLOGY_TRIANGLE_LIST  = 0,   /* her 3 vertex → 1 üçgen */
    GRAV_TOPOLOGY_TRIANGLE_STRIP = 1,   /* her yeni vertex → yeni üçgen */
    GRAV_TOPOLOGY_LINE_LIST      = 2,
    GRAV_TOPOLOGY_POINT_LIST     = 3,
} GravPrimitiveTopology;

typedef enum GravCullMode {
    GRAV_CULL_NONE  = 0,
    GRAV_CULL_BACK  = 1,
    GRAV_CULL_FRONT = 2,
} GravCullMode;

typedef enum GravFillMode {
    GRAV_FILL_SOLID      = 0,
    GRAV_FILL_WIREFRAME  = 1,
} GravFillMode;

typedef enum GravDepthCompare {
    GRAV_COMPARE_LESS    = 0,
    GRAV_COMPARE_LEQUAL  = 1,
    GRAV_COMPARE_GREATER = 2,
    GRAV_COMPARE_ALWAYS  = 3,
} GravDepthCompare;

/** Vertex attribute açıklaması */
typedef struct GravVertexAttribute {
    uint32_t   offset;   /* struct içinde byte offset */
    GravFormat format;   /* veri formatı              */
} GravVertexAttribute;

#define GRAV_MAX_VERTEX_ATTRIBUTES 16
#define GRAV_MAX_VARYINGS          16

typedef struct GravPipelineCreateInfo {
    GravShaderModule       shaderModule;
    GravRenderPass         renderPass;

    /* Vertex layout */
    uint32_t               vertexStride;                           /* bayt/vertex */
    uint32_t               attributeCount;
    GravVertexAttribute    attributes[GRAV_MAX_VERTEX_ATTRIBUTES];

    /* Varying sayısı (vert→frag arası) */
    uint32_t               varyingCount;

    /* Rasterizasyon */
    GravPrimitiveTopology  topology;
    GravCullMode           cullMode;
    GravFillMode           fillMode;

    /* Derinlik */
    int                    depthTestEnable;
    int                    depthWriteEnable;
    GravDepthCompare       depthCompare;

    /* Viewport (pipeline'a sabitlenebilir veya cmd ile değiştirilebilir) */
    GravViewport           viewport;
    GravRect2D             scissor;

    /* Uniform veri (işaretçi + boyut — pipeline yaşadığı sürece geçerli olmalı) */
    const void*            uniforms;
    size_t                 uniformSize;
} GravPipelineCreateInfo;

/* =========================================================================
 * COMMAND BUFFER
 * ========================================================================= */

#define GRAV_MAX_COMMANDS 4096

typedef struct GravCommandBufferAllocInfo {
    uint32_t maxCommands;   /* 0 → GRAV_MAX_COMMANDS kullanılır */
} GravCommandBufferAllocInfo;

/* =========================================================================
 * RENDER PASS BEGIN
 * ========================================================================= */

typedef struct GravRenderPassBeginInfo {
    GravRenderPass  renderPass;
    GravFramebuffer framebuffer;
    GravRect2D      renderArea;
    GravColorF      clearColor;
    float           clearDepth;
} GravRenderPassBeginInfo;

/* =========================================================================
 * INSTANCE
 * ========================================================================= */

typedef struct GravInstanceCreateInfo {
    const char* appName;
    uint32_t    appVersion;
    uint32_t    apiVersion;      /* GRAV_API_VERSION önerilir */
} GravInstanceCreateInfo;

/* =========================================================================
 * DEVICE
 * ========================================================================= */

typedef struct GravDeviceFeatures {
    int wireframeSupport;    /* her zaman 1 (software) */
    int depthClampSupport;   /* her zaman 1 (software) */
    int maxRenderWidth;
    int maxRenderHeight;
    int maxVaryings;
} GravDeviceFeatures;

typedef struct GravDeviceCreateInfo {
    uint32_t threadCount;    /* gelecek: paralel rasterizasyon iş parçacıkları */
} GravDeviceCreateInfo;

/* =========================================================================
 * PUBLIC API FONKSİYONLARI
 * ========================================================================= */

/* --- Instance --- */
GravResult gravCreateInstance (const GravInstanceCreateInfo* pInfo, GravInstance* pInstance);
GravResult gravDestroyInstance(GravInstance instance);
GravResult gravEnumerateDeviceFeatures(GravInstance instance, GravDeviceFeatures* pFeatures);

/* --- Device --- */
GravResult gravCreateDevice (GravInstance instance, const GravDeviceCreateInfo* pInfo, GravDevice* pDevice);
GravResult gravDestroyDevice(GravDevice device);

/* --- Buffer --- */
GravResult gravCreateBuffer (GravDevice device, const GravBufferCreateInfo* pInfo, GravBuffer* pBuffer);
GravResult gravDestroyBuffer(GravDevice device, GravBuffer buffer);
GravResult gravMapBuffer    (GravDevice device, GravBuffer buffer, void** ppData);
GravResult gravUnmapBuffer  (GravDevice device, GravBuffer buffer);
GravResult gravBufferSize   (GravDevice device, GravBuffer buffer, uint64_t* pSize);

/* --- Image --- */
GravResult gravCreateImage (GravDevice device, const GravImageCreateInfo* pInfo, GravImage* pImage);
GravResult gravDestroyImage(GravDevice device, GravImage image);
/** Ham piksel verisini al (RGBA8 veya D32 float dizisi) */
GravResult gravGetImageData  (GravDevice device, GravImage image, void** ppPixels, size_t* pSizeBytes);
/** Image boyutunu al */
GravResult gravGetImageExtent(GravDevice device, GravImage image, uint32_t* pWidth, uint32_t* pHeight);

/* --- Shader Module --- */
GravResult gravCreateShaderModule (GravDevice device, const GravShaderModuleCreateInfo* pInfo, GravShaderModule* pModule);
GravResult gravDestroyShaderModule(GravDevice device, GravShaderModule module);

/* --- Render Pass --- */
GravResult gravCreateRenderPass (GravDevice device, const GravRenderPassCreateInfo* pInfo, GravRenderPass* pRenderPass);
GravResult gravDestroyRenderPass(GravDevice device, GravRenderPass renderPass);

/* --- Framebuffer --- */
GravResult gravCreateFramebuffer (GravDevice device, const GravFramebufferCreateInfo* pInfo, GravFramebuffer* pFramebuffer);
GravResult gravDestroyFramebuffer(GravDevice device, GravFramebuffer framebuffer);

/* --- Pipeline --- */
GravResult gravCreatePipeline (GravDevice device, const GravPipelineCreateInfo* pInfo, GravPipeline* pPipeline);
GravResult gravDestroyPipeline(GravDevice device, GravPipeline pipeline);

/* --- Command Buffer --- */
GravResult gravAllocateCommandBuffer(GravDevice device, const GravCommandBufferAllocInfo* pInfo, GravCommandBuffer* pCmdBuf);
GravResult gravFreeCommandBuffer    (GravDevice device, GravCommandBuffer cmdBuf);

GravResult gravBeginCommandBuffer(GravCommandBuffer cmdBuf);
GravResult gravEndCommandBuffer  (GravCommandBuffer cmdBuf);
GravResult gravResetCommandBuffer (GravCommandBuffer cmdBuf);

/* Kayıt komutları (Begin/End arasında çağrılmalı) */
GravResult gravCmdBeginRenderPass(GravCommandBuffer cmdBuf, const GravRenderPassBeginInfo* pBeginInfo);
GravResult gravCmdEndRenderPass  (GravCommandBuffer cmdBuf);
GravResult gravCmdBindPipeline   (GravCommandBuffer cmdBuf, GravPipeline pipeline);
GravResult gravCmdBindVertexBuffer(GravCommandBuffer cmdBuf, GravBuffer buffer, uint64_t offset);
GravResult gravCmdBindIndexBuffer (GravCommandBuffer cmdBuf, GravBuffer buffer, uint64_t offset);
GravResult gravCmdSetViewport    (GravCommandBuffer cmdBuf, const GravViewport* pViewport);
GravResult gravCmdSetScissor     (GravCommandBuffer cmdBuf, const GravRect2D* pScissor);
GravResult gravCmdSetUniforms    (GravCommandBuffer cmdBuf, const void* pUniforms, size_t size);
GravResult gravCmdDraw           (GravCommandBuffer cmdBuf, uint32_t vertexCount, uint32_t firstVertex);
GravResult gravCmdDrawIndexed    (GravCommandBuffer cmdBuf, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset);
GravResult gravCmdClearColorImage(GravCommandBuffer cmdBuf, GravImage image, GravColorF color);

/* --- Submit & Yürütme --- */
GravResult gravSubmitCommandBuffer(GravDevice device, GravCommandBuffer cmdBuf);

/* --- Çıktı / Yardımcılar --- */
/** Renk image'ını PPM dosyasına kaydet */
GravResult gravSaveImagePPM(GravDevice device, GravImage image, const char* path);
/** Renk image'ını BMP dosyasına kaydet */
GravResult gravSaveImageBMP(GravDevice device, GravImage image, const char* path);

/** Profil: son submit'in nanosaniye cinsinden süresi */
uint64_t gravGetLastSubmitTimeNs(GravDevice device);

#ifdef __cplusplus
}
#endif

#endif /* GRAVITYON_H */
