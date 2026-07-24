/**
 * GRAVITYON GPU API — Uygulama
 * ============================
 * Software rasterizer backend: tam pipeline, z-buffer, perspektif-doğru
 * interpolasyon, scissor, viewport transform, back-face culling.
 */

#define _POSIX_C_SOURCE 199309L
#include "gravityon.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* =========================================================================
 * YARDIMCI MAKROLAR
 * ========================================================================= */

#define GRAV_CHECK_NULL(ptr)  if (!(ptr)) return GRAV_ERROR_INVALID_ARGUMENT
#define GRAV_CHECK_HANDLE(h)  if (!(h))   return GRAV_ERROR_INVALID_HANDLE
#define GRAV_CLAMP(v,lo,hi)   ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define GRAV_MAX(a,b)         ((a)>(b)?(a):(b))
#define GRAV_MIN(a,b)         ((a)<(b)?(a):(b))
#define GRAV_ABS(x)           ((x)<0?-(x):(x))

/* =========================================================================
 * İÇ YAPILARI
 * ========================================================================= */

/* Komut türleri */
typedef enum CmdType {
    CMD_BEGIN_RENDER_PASS,
    CMD_END_RENDER_PASS,
    CMD_BIND_PIPELINE,
    CMD_BIND_VERTEX_BUFFER,
    CMD_BIND_INDEX_BUFFER,
    CMD_SET_VIEWPORT,
    CMD_SET_SCISSOR,
    CMD_SET_UNIFORMS,
    CMD_DRAW,
    CMD_DRAW_INDEXED,
    CMD_CLEAR_COLOR_IMAGE,
} CmdType;

/* Tek komut */
typedef struct Command {
    CmdType type;
    union {
        struct { GravRenderPassBeginInfo info; }        beginRenderPass;
        struct { GravPipeline pipeline; }               bindPipeline;
        struct { GravBuffer buffer; uint64_t offset; }  bindVertex;
        struct { GravBuffer buffer; uint64_t offset; }  bindIndex;
        struct { GravViewport vp; }                     setViewport;
        struct { GravRect2D sc; }                       setScissor;
        struct {
            uint8_t  data[256];
            size_t   size;
        }                                               setUniforms;
        struct {
            uint32_t count;
            uint32_t first;
        }                                               draw;
        struct {
            uint32_t count;
            uint32_t firstIndex;
            int32_t  vertexOffset;
        }                                               drawIndexed;
        struct { GravImage image; GravColorF color; }   clearColor;
    };
} Command;

/* Instance */
struct GravInstance_T {
    uint32_t appVersion;
    char     appName[128];
};

/* Device */
struct GravDevice_T {
    GravInstance parent;
    uint64_t     lastSubmitNs;
};

/* Buffer */
struct GravBuffer_T {
    void*          data;
    uint64_t       size;
    GravBufferUsage usage;
    int            mapped;
};

/* Image */
struct GravImage_T {
    uint32_t       width, height;
    GravFormat     format;
    GravImageUsage usage;
    void*          pixels;      /* RGBA8: uint8_t[w*h*4] | D32: float[w*h] */
    size_t         sizeBytes;
};

/* Shader Module */
struct GravShaderModule_T {
    GravVertFn vertFn;
    GravFragFn fragFn;
};

/* Render Pass */
struct GravRenderPass_T {
    GravAttachmentDesc color;
    GravAttachmentDesc depth;
    int                hasDepth;
};

/* Framebuffer */
struct GravFramebuffer_T {
    GravRenderPass renderPass;
    GravImage      colorImage;
    GravImage      depthImage;
    GravExtent2D   extent;
};

/* Pipeline */
struct GravPipeline_T {
    GravShaderModule      shader;
    GravRenderPass        renderPass;
    uint32_t              vertexStride;
    uint32_t              attributeCount;
    GravVertexAttribute   attributes[GRAV_MAX_VERTEX_ATTRIBUTES];
    uint32_t              varyingCount;
    GravPrimitiveTopology topology;
    GravCullMode          cullMode;
    GravFillMode          fillMode;
    int                   depthTestEnable;
    int                   depthWriteEnable;
    GravDepthCompare      depthCompare;
    GravViewport          viewport;
    GravRect2D            scissor;
    const void*           uniforms;
    size_t                uniformSize;
};

/* Command Buffer */
struct GravCommandBuffer_T {
    Command* cmds;
    uint32_t count;
    uint32_t capacity;
    int      recording;
};

/* =========================================================================
 * SONUÇ STRİNG
 * ========================================================================= */

const char* gravResultString(GravResult r) {
    switch (r) {
        case GRAV_SUCCESS:                      return "GRAV_SUCCESS";
        case GRAV_NOT_READY:                    return "GRAV_NOT_READY";
        case GRAV_TIMEOUT:                      return "GRAV_TIMEOUT";
        case GRAV_ERROR_OUT_OF_MEMORY:          return "GRAV_ERROR_OUT_OF_MEMORY";
        case GRAV_ERROR_INVALID_HANDLE:         return "GRAV_ERROR_INVALID_HANDLE";
        case GRAV_ERROR_INVALID_ARGUMENT:       return "GRAV_ERROR_INVALID_ARGUMENT";
        case GRAV_ERROR_OUT_OF_RANGE:           return "GRAV_ERROR_OUT_OF_RANGE";
        case GRAV_ERROR_COMMAND_BUFFER_FULL:    return "GRAV_ERROR_COMMAND_BUFFER_FULL";
        case GRAV_ERROR_NOT_RECORDING:          return "GRAV_ERROR_NOT_RECORDING";
        case GRAV_ERROR_ALREADY_RECORDING:      return "GRAV_ERROR_ALREADY_RECORDING";
        case GRAV_ERROR_RENDER_PASS_NOT_BEGUN:  return "GRAV_ERROR_RENDER_PASS_NOT_BEGUN";
        case GRAV_ERROR_NO_PIPELINE_BOUND:      return "GRAV_ERROR_NO_PIPELINE_BOUND";
        case GRAV_ERROR_NO_VERTEX_BUFFER_BOUND: return "GRAV_ERROR_NO_VERTEX_BUFFER_BOUND";
        case GRAV_ERROR_IO:                     return "GRAV_ERROR_IO";
        default:                                return "GRAV_ERROR_UNKNOWN";
    }
}

/* =========================================================================
 * INSTANCE
 * ========================================================================= */

GravResult gravCreateInstance(const GravInstanceCreateInfo* pInfo, GravInstance* pInstance) {
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pInstance);
    struct GravInstance_T* inst = calloc(1, sizeof(*inst));
    if (!inst) return GRAV_ERROR_OUT_OF_MEMORY;
    inst->appVersion = pInfo->appVersion;
    if (pInfo->appName)
        strncpy(inst->appName, pInfo->appName, sizeof(inst->appName)-1);
    *pInstance = inst;
    return GRAV_SUCCESS;
}

GravResult gravDestroyInstance(GravInstance instance) {
    GRAV_CHECK_HANDLE(instance);
    free(instance);
    return GRAV_SUCCESS;
}

GravResult gravEnumerateDeviceFeatures(GravInstance instance, GravDeviceFeatures* pFeatures) {
    GRAV_CHECK_HANDLE(instance);
    GRAV_CHECK_NULL(pFeatures);
    pFeatures->wireframeSupport  = 1;
    pFeatures->depthClampSupport = 1;
    pFeatures->maxRenderWidth    = 16384;
    pFeatures->maxRenderHeight   = 16384;
    pFeatures->maxVaryings       = GRAV_MAX_VARYINGS;
    return GRAV_SUCCESS;
}

/* =========================================================================
 * DEVICE
 * ========================================================================= */

GravResult gravCreateDevice(GravInstance instance, const GravDeviceCreateInfo* pInfo, GravDevice* pDevice) {
    GRAV_CHECK_HANDLE(instance);
    GRAV_CHECK_NULL(pDevice);
    struct GravDevice_T* dev = calloc(1, sizeof(*dev));
    if (!dev) return GRAV_ERROR_OUT_OF_MEMORY;
    dev->parent = instance;
    *pDevice = dev;
    return GRAV_SUCCESS;
}

GravResult gravDestroyDevice(GravDevice device) {
    GRAV_CHECK_HANDLE(device);
    free(device);
    return GRAV_SUCCESS;
}

/* =========================================================================
 * BUFFER
 * ========================================================================= */

GravResult gravCreateBuffer(GravDevice device, const GravBufferCreateInfo* pInfo, GravBuffer* pBuffer) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pBuffer);
    if (pInfo->size == 0) return GRAV_ERROR_INVALID_ARGUMENT;
    struct GravBuffer_T* buf = calloc(1, sizeof(*buf));
    if (!buf) return GRAV_ERROR_OUT_OF_MEMORY;
    buf->data = calloc(1, (size_t)pInfo->size);
    if (!buf->data) { free(buf); return GRAV_ERROR_OUT_OF_MEMORY; }
    buf->size  = pInfo->size;
    buf->usage = pInfo->usage;
    *pBuffer = buf;
    return GRAV_SUCCESS;
}

GravResult gravDestroyBuffer(GravDevice device, GravBuffer buffer) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(buffer);
    free(buffer->data);
    free(buffer);
    return GRAV_SUCCESS;
}

GravResult gravMapBuffer(GravDevice device, GravBuffer buffer, void** ppData) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(buffer);
    GRAV_CHECK_NULL(ppData);
    buffer->mapped = 1;
    *ppData = buffer->data;
    return GRAV_SUCCESS;
}

GravResult gravUnmapBuffer(GravDevice device, GravBuffer buffer) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(buffer);
    buffer->mapped = 0;
    return GRAV_SUCCESS;
}

GravResult gravBufferSize(GravDevice device, GravBuffer buffer, uint64_t* pSize) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(buffer);
    GRAV_CHECK_NULL(pSize);
    *pSize = buffer->size;
    return GRAV_SUCCESS;
}

/* =========================================================================
 * IMAGE
 * ========================================================================= */

static size_t image_pixel_size(GravFormat fmt) {
    switch (fmt) {
        case GRAV_FORMAT_R8G8B8A8_UNORM:   return 4;
        case GRAV_FORMAT_R32G32B32A32_F:   return 16;
        case GRAV_FORMAT_D32_SFLOAT:       return 4;
        default:                           return 4;
    }
}

GravResult gravCreateImage(GravDevice device, const GravImageCreateInfo* pInfo, GravImage* pImage) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pImage);
    if (pInfo->extent.width == 0 || pInfo->extent.height == 0)
        return GRAV_ERROR_INVALID_ARGUMENT;
    struct GravImage_T* img = calloc(1, sizeof(*img));
    if (!img) return GRAV_ERROR_OUT_OF_MEMORY;
    img->width     = pInfo->extent.width;
    img->height    = pInfo->extent.height;
    img->format    = pInfo->format;
    img->usage     = pInfo->usage;
    img->sizeBytes = (size_t)pInfo->extent.width * pInfo->extent.height * image_pixel_size(pInfo->format);
    img->pixels    = calloc(1, img->sizeBytes);
    if (!img->pixels) { free(img); return GRAV_ERROR_OUT_OF_MEMORY; }
    /* Derinlik buffer'ını +∞ ile doldur */
    if (pInfo->format == GRAV_FORMAT_D32_SFLOAT) {
        float* fp = (float*)img->pixels;
        for (size_t i = 0; i < (size_t)img->width * img->height; i++) fp[i] = 1.0f;
    }
    *pImage = img;
    return GRAV_SUCCESS;
}

GravResult gravDestroyImage(GravDevice device, GravImage image) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(image);
    free(image->pixels);
    free(image);
    return GRAV_SUCCESS;
}

GravResult gravGetImageData(GravDevice device, GravImage image, void** ppPixels, size_t* pSizeBytes) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(image);
    if (ppPixels)   *ppPixels   = image->pixels;
    if (pSizeBytes) *pSizeBytes = image->sizeBytes;
    return GRAV_SUCCESS;
}

GravResult gravGetImageExtent(GravDevice device, GravImage image, uint32_t* pWidth, uint32_t* pHeight) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(image);
    if (pWidth)  *pWidth  = image->width;
    if (pHeight) *pHeight = image->height;
    return GRAV_SUCCESS;
}

/* =========================================================================
 * SHADER MODULE
 * ========================================================================= */

GravResult gravCreateShaderModule(GravDevice device, const GravShaderModuleCreateInfo* pInfo, GravShaderModule* pModule) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pModule);
    if (!pInfo->vertFn || !pInfo->fragFn) return GRAV_ERROR_INVALID_ARGUMENT;
    struct GravShaderModule_T* m = calloc(1, sizeof(*m));
    if (!m) return GRAV_ERROR_OUT_OF_MEMORY;
    m->vertFn = pInfo->vertFn;
    m->fragFn = pInfo->fragFn;
    *pModule = m;
    return GRAV_SUCCESS;
}

GravResult gravDestroyShaderModule(GravDevice device, GravShaderModule module) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(module);
    free(module);
    return GRAV_SUCCESS;
}

/* =========================================================================
 * RENDER PASS
 * ========================================================================= */

GravResult gravCreateRenderPass(GravDevice device, const GravRenderPassCreateInfo* pInfo, GravRenderPass* pRenderPass) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pRenderPass);
    struct GravRenderPass_T* rp = calloc(1, sizeof(*rp));
    if (!rp) return GRAV_ERROR_OUT_OF_MEMORY;
    rp->color    = pInfo->colorAttachment;
    rp->depth    = pInfo->depthAttachment;
    rp->hasDepth = pInfo->hasDepthAttachment;
    *pRenderPass = rp;
    return GRAV_SUCCESS;
}

GravResult gravDestroyRenderPass(GravDevice device, GravRenderPass renderPass) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(renderPass);
    free(renderPass);
    return GRAV_SUCCESS;
}

/* =========================================================================
 * FRAMEBUFFER
 * ========================================================================= */

GravResult gravCreateFramebuffer(GravDevice device, const GravFramebufferCreateInfo* pInfo, GravFramebuffer* pFramebuffer) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pFramebuffer);
    struct GravFramebuffer_T* fb = calloc(1, sizeof(*fb));
    if (!fb) return GRAV_ERROR_OUT_OF_MEMORY;
    fb->renderPass  = pInfo->renderPass;
    fb->colorImage  = pInfo->colorImage;
    fb->depthImage  = pInfo->depthImage;
    fb->extent      = pInfo->extent;
    *pFramebuffer = fb;
    return GRAV_SUCCESS;
}

GravResult gravDestroyFramebuffer(GravDevice device, GravFramebuffer framebuffer) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(framebuffer);
    free(framebuffer);
    return GRAV_SUCCESS;
}

/* =========================================================================
 * PIPELINE
 * ========================================================================= */

GravResult gravCreatePipeline(GravDevice device, const GravPipelineCreateInfo* pInfo, GravPipeline* pPipeline) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pInfo);
    GRAV_CHECK_NULL(pPipeline);
    if (!pInfo->shaderModule) return GRAV_ERROR_INVALID_ARGUMENT;
    struct GravPipeline_T* pl = calloc(1, sizeof(*pl));
    if (!pl) return GRAV_ERROR_OUT_OF_MEMORY;
    pl->shader         = pInfo->shaderModule;
    pl->renderPass     = pInfo->renderPass;
    pl->vertexStride   = pInfo->vertexStride;
    pl->attributeCount = pInfo->attributeCount;
    pl->varyingCount   = pInfo->varyingCount;
    pl->topology       = pInfo->topology;
    pl->cullMode       = pInfo->cullMode;
    pl->fillMode       = pInfo->fillMode;
    pl->depthTestEnable  = pInfo->depthTestEnable;
    pl->depthWriteEnable = pInfo->depthWriteEnable;
    pl->depthCompare     = pInfo->depthCompare;
    pl->viewport         = pInfo->viewport;
    pl->scissor          = pInfo->scissor;
    pl->uniforms         = pInfo->uniforms;
    pl->uniformSize      = pInfo->uniformSize;
    if (pInfo->attributeCount > 0)
        memcpy(pl->attributes, pInfo->attributes,
               pInfo->attributeCount * sizeof(GravVertexAttribute));
    *pPipeline = pl;
    return GRAV_SUCCESS;
}

GravResult gravDestroyPipeline(GravDevice device, GravPipeline pipeline) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(pipeline);
    free(pipeline);
    return GRAV_SUCCESS;
}

/* =========================================================================
 * COMMAND BUFFER
 * ========================================================================= */

GravResult gravAllocateCommandBuffer(GravDevice device, const GravCommandBufferAllocInfo* pInfo, GravCommandBuffer* pCmdBuf) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_NULL(pCmdBuf);
    uint32_t cap = (pInfo && pInfo->maxCommands > 0) ? pInfo->maxCommands : GRAV_MAX_COMMANDS;
    struct GravCommandBuffer_T* cb = calloc(1, sizeof(*cb));
    if (!cb) return GRAV_ERROR_OUT_OF_MEMORY;
    cb->cmds = calloc(cap, sizeof(Command));
    if (!cb->cmds) { free(cb); return GRAV_ERROR_OUT_OF_MEMORY; }
    cb->capacity  = cap;
    cb->count     = 0;
    cb->recording = 0;
    *pCmdBuf = cb;
    return GRAV_SUCCESS;
}

GravResult gravFreeCommandBuffer(GravDevice device, GravCommandBuffer cmdBuf) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(cmdBuf);
    free(cmdBuf->cmds);
    free(cmdBuf);
    return GRAV_SUCCESS;
}

GravResult gravBeginCommandBuffer(GravCommandBuffer cmdBuf) {
    GRAV_CHECK_HANDLE(cmdBuf);
    if (cmdBuf->recording) return GRAV_ERROR_ALREADY_RECORDING;
    cmdBuf->count     = 0;
    cmdBuf->recording = 1;
    return GRAV_SUCCESS;
}

GravResult gravEndCommandBuffer(GravCommandBuffer cmdBuf) {
    GRAV_CHECK_HANDLE(cmdBuf);
    if (!cmdBuf->recording) return GRAV_ERROR_NOT_RECORDING;
    cmdBuf->recording = 0;
    return GRAV_SUCCESS;
}

GravResult gravResetCommandBuffer(GravCommandBuffer cmdBuf) {
    GRAV_CHECK_HANDLE(cmdBuf);
    cmdBuf->count     = 0;
    cmdBuf->recording = 0;
    return GRAV_SUCCESS;
}

/* Komut ekle yardımcısı */
static Command* push_cmd(GravCommandBuffer cb) {
    if (cb->count >= cb->capacity) return NULL;
    return &cb->cmds[cb->count++];
}

#define REQUIRE_RECORDING(cb) if (!(cb)->recording) return GRAV_ERROR_NOT_RECORDING

GravResult gravCmdBeginRenderPass(GravCommandBuffer cb, const GravRenderPassBeginInfo* pInfo) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_NULL(pInfo); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_BEGIN_RENDER_PASS;
    c->beginRenderPass.info = *pInfo;
    return GRAV_SUCCESS;
}

GravResult gravCmdEndRenderPass(GravCommandBuffer cb) {
    GRAV_CHECK_HANDLE(cb); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_END_RENDER_PASS;
    return GRAV_SUCCESS;
}

GravResult gravCmdBindPipeline(GravCommandBuffer cb, GravPipeline pipeline) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_HANDLE(pipeline); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_BIND_PIPELINE;
    c->bindPipeline.pipeline = pipeline;
    return GRAV_SUCCESS;
}

GravResult gravCmdBindVertexBuffer(GravCommandBuffer cb, GravBuffer buffer, uint64_t offset) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_HANDLE(buffer); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_BIND_VERTEX_BUFFER;
    c->bindVertex.buffer = buffer;
    c->bindVertex.offset = offset;
    return GRAV_SUCCESS;
}

GravResult gravCmdBindIndexBuffer(GravCommandBuffer cb, GravBuffer buffer, uint64_t offset) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_HANDLE(buffer); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_BIND_INDEX_BUFFER;
    c->bindIndex.buffer = buffer;
    c->bindIndex.offset = offset;
    return GRAV_SUCCESS;
}

GravResult gravCmdSetViewport(GravCommandBuffer cb, const GravViewport* pViewport) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_NULL(pViewport); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_SET_VIEWPORT;
    c->setViewport.vp = *pViewport;
    return GRAV_SUCCESS;
}

GravResult gravCmdSetScissor(GravCommandBuffer cb, const GravRect2D* pScissor) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_NULL(pScissor); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_SET_SCISSOR;
    c->setScissor.sc = *pScissor;
    return GRAV_SUCCESS;
}

GravResult gravCmdSetUniforms(GravCommandBuffer cb, const void* pUniforms, size_t size) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_NULL(pUniforms); REQUIRE_RECORDING(cb);
    if (size > 256) return GRAV_ERROR_OUT_OF_RANGE;
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_SET_UNIFORMS;
    memcpy(c->setUniforms.data, pUniforms, size);
    c->setUniforms.size = size;
    return GRAV_SUCCESS;
}

GravResult gravCmdDraw(GravCommandBuffer cb, uint32_t vertexCount, uint32_t firstVertex) {
    GRAV_CHECK_HANDLE(cb); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_DRAW;
    c->draw.count = vertexCount;
    c->draw.first = firstVertex;
    return GRAV_SUCCESS;
}

GravResult gravCmdDrawIndexed(GravCommandBuffer cb, uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) {
    GRAV_CHECK_HANDLE(cb); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_DRAW_INDEXED;
    c->drawIndexed.count        = indexCount;
    c->drawIndexed.firstIndex   = firstIndex;
    c->drawIndexed.vertexOffset = vertexOffset;
    return GRAV_SUCCESS;
}

GravResult gravCmdClearColorImage(GravCommandBuffer cb, GravImage image, GravColorF color) {
    GRAV_CHECK_HANDLE(cb); GRAV_CHECK_HANDLE(image); REQUIRE_RECORDING(cb);
    Command* c = push_cmd(cb); if (!c) return GRAV_ERROR_COMMAND_BUFFER_FULL;
    c->type = CMD_CLEAR_COLOR_IMAGE;
    c->clearColor.image = image;
    c->clearColor.color = color;
    return GRAV_SUCCESS;
}

/* =========================================================================
 * SOFTWARE RASTERIZER — KALBİ
 * ========================================================================= */

/* Çalışma zamanı durumu */
typedef struct RastState {
    GravPipeline      pipeline;
    GravFramebuffer   framebuffer;
    GravBuffer        vertexBuffer;
    uint64_t          vertexOffset;
    GravBuffer        indexBuffer;
    uint64_t          indexOffset;
    GravViewport      viewport;
    GravRect2D        scissor;
    const void*       uniforms;
    size_t            uniformSize;
    uint8_t           uniformData[256];
    int               inRenderPass;
} RastState;

/* Rengi RGBA8 piksel yazma */
static inline void write_pixel_rgba8(GravImage img, int x, int y, float r, float g, float b, float a) {
    if (x < 0 || y < 0 || (uint32_t)x >= img->width || (uint32_t)y >= img->height) return;
    uint8_t* px = (uint8_t*)img->pixels + (y * img->width + x) * 4;
    px[0] = (uint8_t)(GRAV_CLAMP(r, 0.0f, 1.0f) * 255.0f);
    px[1] = (uint8_t)(GRAV_CLAMP(g, 0.0f, 1.0f) * 255.0f);
    px[2] = (uint8_t)(GRAV_CLAMP(b, 0.0f, 1.0f) * 255.0f);
    px[3] = (uint8_t)(GRAV_CLAMP(a, 0.0f, 1.0f) * 255.0f);
}

/* Derinlik okuma/yazma */
static inline float read_depth(GravImage dep, int x, int y) {
    if (!dep) return 1.0f;
    return ((float*)dep->pixels)[y * dep->width + x];
}
static inline void write_depth(GravImage dep, int x, int y, float d) {
    if (!dep) return;
    ((float*)dep->pixels)[y * dep->width + x] = d;
}

/* Derinlik karşılaştırması */
static inline int depth_test(GravDepthCompare cmp, float z, float zBuf) {
    switch (cmp) {
        case GRAV_COMPARE_LESS:    return z < zBuf;
        case GRAV_COMPARE_LEQUAL:  return z <= zBuf;
        case GRAV_COMPARE_GREATER: return z > zBuf;
        case GRAV_COMPARE_ALWAYS:  return 1;
        default:                   return z < zBuf;
    }
}

/* Scissor kontrolü */
static inline int in_scissor(RastState* s, int x, int y) {
    return x >= s->scissor.x && y >= s->scissor.y &&
           x <  s->scissor.x + (int)s->scissor.width &&
           y <  s->scissor.y + (int)s->scissor.height;
}

/* NDC → Ekran koordinatları */
static inline void ndc_to_screen(RastState* s, float nx, float ny, float nz, float* sx, float* sy, float* sz) {
    GravViewport* vp = &s->viewport;
    *sx = (nx * 0.5f + 0.5f) * vp->width  + vp->x;
    *sy = (1.0f - (ny * 0.5f + 0.5f)) * vp->height + vp->y;  /* Y flip */
    *sz = nz * (vp->maxDepth - vp->minDepth) * 0.5f + (vp->maxDepth + vp->minDepth) * 0.5f;
}

/* Barycentric üçgen rasterizasyonu */
static void rasterize_triangle(
    RastState*  s,
    GravImage   color,
    GravImage   depth,
    /* 3 vertex clip-space pozisyonu */
    float       clip[3][4],
    /* 3 vertex varyingları */
    float       varyings[3][GRAV_MAX_VARYINGS],
    uint32_t    varyingCount
) {
    GravPipeline pl = s->pipeline;
    const void*  uni = s->uniforms;

    /* Perspektif bölme → NDC */
    float ndc[3][3];
    float invW[3];
    for (int i = 0; i < 3; i++) {
        float w = clip[i][3];
        if (fabsf(w) < 1e-7f) w = 1e-7f;
        invW[i]   = 1.0f / w;
        ndc[i][0] = clip[i][0] * invW[i];
        ndc[i][1] = clip[i][1] * invW[i];
        ndc[i][2] = clip[i][2] * invW[i];
    }

    /* Ekran koordinatları */
    float sx[3], sy[3], sz[3];
    for (int i = 0; i < 3; i++)
        ndc_to_screen(s, ndc[i][0], ndc[i][1], ndc[i][2], &sx[i], &sy[i], &sz[i]);

    /* Back-face culling (2D çapraz çarpım işareti) */
    if (pl->cullMode != GRAV_CULL_NONE) {
        float ex1 = sx[1]-sx[0], ey1 = sy[1]-sy[0];
        float ex2 = sx[2]-sx[0], ey2 = sy[2]-sy[0];
        float cross = ex1*ey2 - ey1*ex2;
        if (pl->cullMode == GRAV_CULL_BACK  && cross >= 0) return;
        if (pl->cullMode == GRAV_CULL_FRONT && cross <= 0) return;
    }

    /* Bounding box */
    int minX = (int)floorf(GRAV_MIN(sx[0], GRAV_MIN(sx[1], sx[2])));
    int minY = (int)floorf(GRAV_MIN(sy[0], GRAV_MIN(sy[1], sy[2])));
    int maxX = (int) ceilf(GRAV_MAX(sx[0], GRAV_MAX(sx[1], sx[2])));
    int maxY = (int) ceilf(GRAV_MAX(sy[0], GRAV_MAX(sy[1], sy[2])));

    /* Scissor kırpma */
    minX = GRAV_MAX(minX, s->scissor.x);
    minY = GRAV_MAX(minY, s->scissor.y);
    maxX = GRAV_MIN(maxX, s->scissor.x + (int)s->scissor.width  - 1);
    maxY = GRAV_MIN(maxY, s->scissor.y + (int)s->scissor.height - 1);

    /* Ekran sınırları */
    minX = GRAV_MAX(minX, 0);
    minY = GRAV_MAX(minY, 0);
    maxX = GRAV_MIN(maxX, (int)color->width  - 1);
    maxY = GRAV_MIN(maxY, (int)color->height - 1);

    /* Kenar sabitlerini hazırla */
    float dx01 = sx[1]-sx[0], dy01 = sy[1]-sy[0];
    float dx12 = sx[2]-sx[1], dy12 = sy[2]-sy[1];
    float dx20 = sx[0]-sx[2], dy20 = sy[0]-sy[2];

    float triArea = dx01*dy20 - dy01*dx20;
    if (fabsf(triArea) < 1e-6f) return;
    float invArea = 1.0f / triArea;

    /* Perspektif-doğru varying interpolasyonu için 1/w dizisi */
    float pw[3]; for (int i=0;i<3;i++) pw[i] = invW[i];

    /* Varying vektörleri */
    float frag_varying[GRAV_MAX_VARYINGS];
    float outColor[4];

    for (int py = minY; py <= maxY; py++) {
        for (int px = minX; px <= maxX; px++) {
            float fpx = (float)px + 0.5f, fpy = (float)py + 0.5f;

            /* Barycentric koordinatlar */
            float w0 = ((fpx - sx[1]) * dy12 - (fpy - sy[1]) * dx12) * invArea;
            float w1 = ((fpx - sx[2]) * dy20 - (fpy - sy[2]) * dx20) * invArea;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            /* Derinlik interpole */
            float z = w0*sz[0] + w1*sz[1] + w2*sz[2];
            if (z < 0.0f || z > 1.0f) continue;

            /* Derinlik testi */
            if (pl->depthTestEnable) {
                float zBuf = read_depth(depth, px, py);
                if (!depth_test(pl->depthCompare, z, zBuf)) continue;
                if (pl->depthWriteEnable) write_depth(depth, px, py, z);
            }

            /* Perspektif-doğru interpolasyon */
            float wInterp = w0*pw[0] + w1*pw[1] + w2*pw[2];
            float invWInterp = (fabsf(wInterp) > 1e-8f) ? 1.0f / wInterp : 0.0f;

            for (uint32_t v = 0; v < varyingCount && v < GRAV_MAX_VARYINGS; v++) {
                frag_varying[v] = (w0*pw[0]*varyings[0][v] +
                                   w1*pw[1]*varyings[1][v] +
                                   w2*pw[2]*varyings[2][v]) * invWInterp;
            }

            /* Fragment shader */
            pl->shader->fragFn(frag_varying, uni, outColor);

            /* Framebuffer'a yaz */
            write_pixel_rgba8(color, px, py,
                              outColor[0], outColor[1], outColor[2], outColor[3]);
        }
    }
}

/* Çizgi (wireframe) çizen Bresenham */
static void draw_line(GravImage img, int x0, int y0, int x1, int y1,
                      float r, float g, float b) {
    int dx = GRAV_ABS(x1-x0), dy = GRAV_ABS(y1-y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        write_pixel_rgba8(img, x0, y0, r, g, b, 1.0f);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* Tek üçgeni rasterize et (wireframe veya fill) */
static void draw_triangle(
    RastState* s,
    GravImage color,
    GravImage depth,
    float clip[3][4],
    float var[3][GRAV_MAX_VARYINGS],
    uint32_t varyingCount
) {
    if (s->pipeline->fillMode == GRAV_FILL_WIREFRAME) {
        /* NDC → ekran dönüşümü */
        float sx[3], sy[3], sz[3];
        for (int i = 0; i < 3; i++) {
            float w = fabsf(clip[i][3]) > 1e-7f ? clip[i][3] : 1e-7f;
            float nx = clip[i][0]/w, ny = clip[i][1]/w, nz = clip[i][2]/w;
            ndc_to_screen(s, nx, ny, nz, &sx[i], &sy[i], &sz[i]);
        }
        draw_line(color, (int)sx[0],(int)sy[0], (int)sx[1],(int)sy[1], 1,1,1);
        draw_line(color, (int)sx[1],(int)sy[1], (int)sx[2],(int)sy[2], 1,1,1);
        draw_line(color, (int)sx[2],(int)sy[2], (int)sx[0],(int)sy[0], 1,1,1);
    } else {
        rasterize_triangle(s, color, depth, clip, var, varyingCount);
    }
}

/* Vertex işleme ve çizim fonksiyonu */
static void execute_draw(
    RastState* s,
    const uint8_t* vertBase,
    uint32_t       vertexCount,
    const uint32_t* indices,    /* NULL = dizinlenmemiş */
    uint32_t        indexCount
) {
    GravPipeline pl = s->pipeline;
    if (!pl || !s->framebuffer) return;
    GravImage color = s->framebuffer->colorImage;
    GravImage depth = s->framebuffer->depthImage;
    if (!color) return;

    uint32_t stride  = pl->vertexStride;
    uint32_t nvCount = indices ? indexCount : vertexCount;
    if (nvCount < 3) return;

    /* Maksimum 3 vertex için geçici depolama */
    float clip[3][4];
    float var[3][GRAV_MAX_VARYINGS];
    uint32_t vc = pl->varyingCount;

    uint32_t triVertices = (pl->topology == GRAV_TOPOLOGY_TRIANGLE_STRIP) ? 1 : 3;

    uint32_t i = 0;
    uint32_t triIdx = 0;

    while (i + 2 < nvCount) {
        uint32_t idxs[3];
        if (pl->topology == GRAV_TOPOLOGY_TRIANGLE_STRIP) {
            idxs[0] = (indices ? indices[i]   : i);
            idxs[1] = (indices ? indices[i+1] : i+1);
            idxs[2] = (indices ? indices[i+2] : i+2);
            /* Strip'te çift indexli üçgenleri ters çevir */
            if (triIdx & 1) { uint32_t tmp = idxs[1]; idxs[1]=idxs[2]; idxs[2]=tmp; }
            i++;
        } else {
            idxs[0] = (indices ? indices[i]   : i);
            idxs[1] = (indices ? indices[i+1] : i+1);
            idxs[2] = (indices ? indices[i+2] : i+2);
            i += 3;
        }
        triIdx++;

        /* Her vertex için vert shader çağır */
        for (int v = 0; v < 3; v++) {
            const void* vdata = vertBase + idxs[v] * stride;
            pl->shader->vertFn(vdata, s->uniforms, clip[v], var[v]);
        }

        draw_triangle(s, color, depth, clip, var, vc);
    }
    (void)triVertices;
}

/* =========================================================================
 * SUBMIT
 * ========================================================================= */

GravResult gravSubmitCommandBuffer(GravDevice device, GravCommandBuffer cmdBuf) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(cmdBuf);
    if (cmdBuf->recording) return GRAV_ERROR_ALREADY_RECORDING;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    RastState state;
    memset(&state, 0, sizeof(state));

    for (uint32_t i = 0; i < cmdBuf->count; i++) {
        Command* c = &cmdBuf->cmds[i];
        switch (c->type) {

        case CMD_BEGIN_RENDER_PASS: {
            GravRenderPassBeginInfo* bi = &c->beginRenderPass.info;
            state.framebuffer = bi->framebuffer;
            state.inRenderPass = 1;
            /* Viewport ve scissor'ı render alanından başlat */
            state.viewport = (GravViewport){
                .x = (float)bi->renderArea.x,
                .y = (float)bi->renderArea.y,
                .width  = (float)bi->renderArea.width,
                .height = (float)bi->renderArea.height,
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            };
            state.scissor = bi->renderArea;

            GravFramebuffer fb = bi->framebuffer;
            if (!fb) break;
            GravRenderPass rp = bi->renderPass;

            /* Color clear */
            if (rp && rp->color.loadOp == GRAV_LOAD_OP_CLEAR && fb->colorImage) {
                GravColorF cc = bi->clearColor;
                uint8_t r = (uint8_t)(GRAV_CLAMP(cc.r,0,1)*255);
                uint8_t g = (uint8_t)(GRAV_CLAMP(cc.g,0,1)*255);
                uint8_t b = (uint8_t)(GRAV_CLAMP(cc.b,0,1)*255);
                uint8_t a = (uint8_t)(GRAV_CLAMP(cc.a,0,1)*255);
                uint8_t* px = (uint8_t*)fb->colorImage->pixels;
                size_t n = (size_t)fb->colorImage->width * fb->colorImage->height;
                for (size_t p = 0; p < n; p++) {
                    px[p*4+0]=r; px[p*4+1]=g; px[p*4+2]=b; px[p*4+3]=a;
                }
            }
            /* Depth clear */
            if (rp && rp->hasDepth && rp->depth.loadOp == GRAV_LOAD_OP_CLEAR && fb->depthImage) {
                float* dp = (float*)fb->depthImage->pixels;
                size_t n = (size_t)fb->depthImage->width * fb->depthImage->height;
                for (size_t p = 0; p < n; p++) dp[p] = bi->clearDepth;
            }
            break;
        }

        case CMD_END_RENDER_PASS:
            state.inRenderPass = 0;
            break;

        case CMD_BIND_PIPELINE:
            state.pipeline = c->bindPipeline.pipeline;
            /* Pipeline'ın varsayılan viewport/scissor'ını uygula */
            if (state.pipeline) {
                state.viewport = state.pipeline->viewport;
                state.scissor  = state.pipeline->scissor;
                state.uniforms = state.pipeline->uniforms;
            }
            break;

        case CMD_BIND_VERTEX_BUFFER:
            state.vertexBuffer = c->bindVertex.buffer;
            state.vertexOffset = c->bindVertex.offset;
            break;

        case CMD_BIND_INDEX_BUFFER:
            state.indexBuffer = c->bindIndex.buffer;
            state.indexOffset = c->bindIndex.offset;
            break;

        case CMD_SET_VIEWPORT:
            state.viewport = c->setViewport.vp;
            break;

        case CMD_SET_SCISSOR:
            state.scissor = c->setScissor.sc;
            break;

        case CMD_SET_UNIFORMS:
            memcpy(state.uniformData, c->setUniforms.data, c->setUniforms.size);
            state.uniforms = state.uniformData;
            state.uniformSize = c->setUniforms.size;
            break;

        case CMD_DRAW: {
            if (!state.vertexBuffer) break;
            const uint8_t* vbase = (const uint8_t*)state.vertexBuffer->data + state.vertexOffset;
            execute_draw(&state, vbase, c->draw.count, NULL, 0);
            break;
        }

        case CMD_DRAW_INDEXED: {
            if (!state.vertexBuffer || !state.indexBuffer) break;
            const uint8_t*  vbase  = (const uint8_t*)state.vertexBuffer->data + state.vertexOffset;
            const uint32_t* ibase  = (const uint32_t*)((uint8_t*)state.indexBuffer->data + state.indexOffset);
            execute_draw(&state, vbase, 0, ibase + c->drawIndexed.firstIndex, c->drawIndexed.count);
            break;
        }

        case CMD_CLEAR_COLOR_IMAGE: {
            GravImage img = c->clearColor.image;
            if (!img) break;
            GravColorF cc = c->clearColor.color;
            uint8_t r = (uint8_t)(GRAV_CLAMP(cc.r,0,1)*255);
            uint8_t g = (uint8_t)(GRAV_CLAMP(cc.g,0,1)*255);
            uint8_t b2 = (uint8_t)(GRAV_CLAMP(cc.b,0,1)*255);
            uint8_t a = (uint8_t)(GRAV_CLAMP(cc.a,0,1)*255);
            uint8_t* px = (uint8_t*)img->pixels;
            size_t n = (size_t)img->width * img->height;
            for (size_t p = 0; p < n; p++) {
                px[p*4+0]=r; px[p*4+1]=g; px[p*4+2]=b2; px[p*4+3]=a;
            }
            break;
        }

        } /* switch */
    } /* for */

    clock_gettime(CLOCK_MONOTONIC, &t1);
    device->lastSubmitNs = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
                         + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
    return GRAV_SUCCESS;
}

uint64_t gravGetLastSubmitTimeNs(GravDevice device) {
    if (!device) return 0;
    return device->lastSubmitNs;
}

/* =========================================================================
 * ÇIKTI: PPM & BMP
 * ========================================================================= */

GravResult gravSaveImagePPM(GravDevice device, GravImage image, const char* path) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(image);
    GRAV_CHECK_NULL(path);
    if (image->format == GRAV_FORMAT_D32_SFLOAT) return GRAV_ERROR_INVALID_ARGUMENT;

    FILE* f = fopen(path, "wb");
    if (!f) return GRAV_ERROR_IO;
    fprintf(f, "P6\n%u %u\n255\n", image->width, image->height);
    uint8_t* px = (uint8_t*)image->pixels;
    for (size_t i = 0; i < (size_t)image->width * image->height; i++) {
        fwrite(px + i*4, 1, 3, f);   /* sadece RGB yaz (alpha atla) */
    }
    fclose(f);
    return GRAV_SUCCESS;
}

GravResult gravSaveImageBMP(GravDevice device, GravImage image, const char* path) {
    GRAV_CHECK_HANDLE(device);
    GRAV_CHECK_HANDLE(image);
    GRAV_CHECK_NULL(path);
    if (image->format == GRAV_FORMAT_D32_SFLOAT) return GRAV_ERROR_INVALID_ARGUMENT;

    uint32_t w = image->width, h = image->height;
    uint32_t rowSize   = (w * 3 + 3) & ~3u;
    uint32_t imageSize = rowSize * h;
    uint32_t fileSize  = 54 + imageSize;

    FILE* f = fopen(path, "wb");
    if (!f) return GRAV_ERROR_IO;

    /* BMP dosya başlığı (14 bayt) */
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    *(uint32_t*)(hdr+ 2) = fileSize;
    *(uint32_t*)(hdr+10) = 54;
    /* DIB başlığı (40 bayt) */
    *(uint32_t*)(hdr+14) = 40;
    *(int32_t*) (hdr+18) = (int32_t)w;
    *(int32_t*) (hdr+22) = -(int32_t)h;  /* negatif = üstten-aşağı */
    *(uint16_t*)(hdr+26) = 1;
    *(uint16_t*)(hdr+28) = 24;
    *(uint32_t*)(hdr+34) = imageSize;
    fwrite(hdr, 1, 54, f);

    uint8_t* px = (uint8_t*)image->pixels;
    uint8_t  pad[4] = {0};
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t* p = px + (y*w+x)*4;
            uint8_t bgr[3] = {p[2], p[1], p[0]};   /* BMP: BGR sırası */
            fwrite(bgr, 1, 3, f);
        }
        uint32_t padBytes = rowSize - w*3;
        if (padBytes) fwrite(pad, 1, padBytes, f);
    }
    fclose(f);
    return GRAV_SUCCESS;
}
