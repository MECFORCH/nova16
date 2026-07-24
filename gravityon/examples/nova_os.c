/**
 * GRAVITYON — NOVA-64 OS Bare-Metal Örneği
 * ==========================================
 * Gerçek bir OS ortamında Gravityon'un nasıl kullanılacağını gösterir.
 *
 * Senaryo:
 *   - OS boot sırasında fiziksel framebuffer adresini öğrenir
 *   - Gravityon'u başlatır (malloc yerine statik bellek)
 *   - Render eder → doğrudan ekrana yazar
 *   - NOVA-64 I/O portlarıyla ekran donanımını kaydeder
 *
 * Host'ta test etmek için (simülasyon modunda):
 *   cd nova16/gravityon && make examples && ./nova_os
 *
 * Gerçek OS'ta:
 *   - FRAMEBUFFER_ADDR = BIOS/UEFI'den alınan fiziksel adres
 *   - malloc → statik dizi veya OS'un kendi heap'i
 *   - gravSaveImagePPM satırları kaldırılır (dosya sistemi yok)
 */

#include "../gravityon.h"
#include "../gravityon_fb.h"
#include "../gravityon_math.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * OS BOOT ORTAMI SİMÜLASYONU
 *
 * Gerçek OS'ta bunlar BIOS/UEFI boot tablosundan okunur.
 * Simülasyon için heap'ten ayırıyoruz.
 * ========================================================================= */

#define SCREEN_W  1280
#define SCREEN_H  720
#define FB_FORMAT GRAV_FB_RGBA8   /* çoğu modern BIOS RGBA8 verir */

/* Gerçek OS'ta: fiziksel adres (örn. 0xFD000000) */
static uint8_t* get_framebuffer_address(void) {
    /* Simülasyon: heap'ten al */
    static uint8_t* fb = NULL;
    if (!fb) {
        fb = (uint8_t*)calloc(SCREEN_W * SCREEN_H * 4, 1);
    }
    return fb;
}

/* =========================================================================
 * SHADER: Renkli dönen üçgen
 * ========================================================================= */

typedef struct { float pos[2]; float col[3]; } V2;

static void os_vert(const void* vd, const void* uni, float pos[4], float* ov) {
    const V2* v = (const V2*)vd;
    /* Uniform: 2x float = döndürme matrisi [cos, sin] */
    const float* rot = (const float*)uni;
    float c = rot[0], s = rot[1];
    float x = v->pos[0] * c - v->pos[1] * s;
    float y = v->pos[0] * s + v->pos[1] * c;
    pos[0] = x; pos[1] = y; pos[2] = 0.5f; pos[3] = 1.0f;
    ov[0] = v->col[0]; ov[1] = v->col[1]; ov[2] = v->col[2];
}

static void os_frag(const float* iv, const void* uni, float col[4]) {
    (void)uni;
    col[0] = iv[0]; col[1] = iv[1]; col[2] = iv[2]; col[3] = 1.0f;
}

/* =========================================================================
 * NOVA-64 OS GPU BAŞLATMA
 * ========================================================================= */

typedef struct {
    GravInstance     instance;
    GravDevice       device;
    GravImage        colorImg;
    GravImage        depthImg;
    GravRenderPass   renderPass;
    GravFramebuffer  framebuffer;
    GravShaderModule shader;
    GravPipeline     pipeline;
    GravBuffer       vertexBuf;
    GravCommandBuffer cmdBuf;
    GravFBTarget     fb;
} GravOSContext;

static GravResult os_init_gpu(GravOSContext* ctx, void* fbAddr) {
    GravResult r;

    /* --- Instance --- */
    r = gravCreateInstance(&(GravInstanceCreateInfo){
        .appName    = "NOVA-64 OS",
        .appVersion = GRAV_MAKE_VERSION(0, 1, 0),
        .apiVersion = GRAV_API_VERSION
    }, &ctx->instance);
    if (r) return r;

    /* --- Device --- */
    r = gravCreateDevice(ctx->instance, &(GravDeviceCreateInfo){0}, &ctx->device);
    if (r) return r;

    /* --- Image'lar --- */
    r = gravCreateImage(ctx->device, &(GravImageCreateInfo){
        .extent = {SCREEN_W, SCREEN_H},
        .format = GRAV_FORMAT_R8G8B8A8_UNORM,
        .usage  = GRAV_IMAGE_USAGE_COLOR_ATTACHMENT
    }, &ctx->colorImg);
    if (r) return r;

    r = gravCreateImage(ctx->device, &(GravImageCreateInfo){
        .extent = {SCREEN_W, SCREEN_H},
        .format = GRAV_FORMAT_D32_SFLOAT,
        .usage  = GRAV_IMAGE_USAGE_DEPTH_ATTACHMENT
    }, &ctx->depthImg);
    if (r) return r;

    /* --- Render Pass --- */
    r = gravCreateRenderPass(ctx->device, &(GravRenderPassCreateInfo){
        .colorAttachment = {GRAV_FORMAT_R8G8B8A8_UNORM, GRAV_LOAD_OP_CLEAR, GRAV_STORE_OP_STORE},
        .hasDepthAttachment = 1,
        .depthAttachment = {GRAV_FORMAT_D32_SFLOAT, GRAV_LOAD_OP_CLEAR, GRAV_STORE_OP_STORE}
    }, &ctx->renderPass);
    if (r) return r;

    /* --- Framebuffer --- */
    r = gravCreateFramebuffer(ctx->device, &(GravFramebufferCreateInfo){
        .renderPass = ctx->renderPass,
        .colorImage = ctx->colorImg,
        .depthImage = ctx->depthImg,
        .extent = {SCREEN_W, SCREEN_H}
    }, &ctx->framebuffer);
    if (r) return r;

    /* --- Shader --- */
    r = gravCreateShaderModule(ctx->device, &(GravShaderModuleCreateInfo){
        .vertFn = os_vert, .fragFn = os_frag
    }, &ctx->shader);
    if (r) return r;

    /* --- Pipeline --- */
    r = gravCreatePipeline(ctx->device, &(GravPipelineCreateInfo){
        .shaderModule  = ctx->shader,
        .renderPass    = ctx->renderPass,
        .vertexStride  = sizeof(V2),
        .attributeCount = 2,
        .attributes = {
            {offsetof(V2,pos), GRAV_FORMAT_R32G32B32A32_F},
            {offsetof(V2,col), GRAV_FORMAT_R32G32B32A32_F},
        },
        .varyingCount    = 3,
        .topology        = GRAV_TOPOLOGY_TRIANGLE_LIST,
        .cullMode        = GRAV_CULL_NONE,
        .fillMode        = GRAV_FILL_SOLID,
        .depthTestEnable = 1, .depthWriteEnable = 1,
        .depthCompare    = GRAV_COMPARE_LESS,
        .viewport = {0,0,(float)SCREEN_W,(float)SCREEN_H,0,1},
        .scissor  = {0,0,SCREEN_W,SCREEN_H}
    }, &ctx->pipeline);
    if (r) return r;

    /* --- Vertex Buffer --- */
    V2 verts[] = {
        {{ 0.0f,  0.6f}, {1.0f, 0.2f, 0.2f}},
        {{-0.6f, -0.4f}, {0.2f, 1.0f, 0.2f}},
        {{ 0.6f, -0.4f}, {0.2f, 0.4f, 1.0f}},
    };
    r = gravCreateBuffer(ctx->device, &(GravBufferCreateInfo){
        .size=sizeof(verts), .usage=GRAV_BUFFER_USAGE_VERTEX
    }, &ctx->vertexBuf);
    if (r) return r;

    void* mp; gravMapBuffer(ctx->device, ctx->vertexBuf, &mp);
    memcpy(mp, verts, sizeof(verts));
    gravUnmapBuffer(ctx->device, ctx->vertexBuf);

    /* --- Command Buffer --- */
    r = gravAllocateCommandBuffer(ctx->device, NULL, &ctx->cmdBuf);
    if (r) return r;

    /* --- Framebuffer Target (fiziksel ekran) --- */
    gravFBInit(&ctx->fb, fbAddr, SCREEN_W, SCREEN_H, FB_FORMAT);

    /* NOVA-64 simülatöründe ekran donanımını kaydet */
#ifdef NOVA64_SIMULATOR
    gravNova64FBRegister(&ctx->fb);
#endif

    return GRAV_SUCCESS;
}

/* =========================================================================
 * FRAME RENDER ET
 * ========================================================================= */

static void os_render_frame(GravOSContext* ctx, float angle) {
    float rot[2] = { cosf(angle), sinf(angle) };

    gravResetCommandBuffer(ctx->cmdBuf);
    gravBeginCommandBuffer(ctx->cmdBuf);

    gravCmdBeginRenderPass(ctx->cmdBuf, &(GravRenderPassBeginInfo){
        .renderPass  = ctx->renderPass,
        .framebuffer = ctx->framebuffer,
        .renderArea  = {0,0,SCREEN_W,SCREEN_H},
        .clearColor  = {0.02f, 0.02f, 0.05f, 1.0f},
        .clearDepth  = 1.0f
    });

    gravCmdBindPipeline(ctx->cmdBuf, ctx->pipeline);
    gravCmdSetUniforms(ctx->cmdBuf, rot, sizeof(rot));
    gravCmdBindVertexBuffer(ctx->cmdBuf, ctx->vertexBuf, 0);
    gravCmdDraw(ctx->cmdBuf, 3, 0);

    gravCmdEndRenderPass(ctx->cmdBuf);
    gravEndCommandBuffer(ctx->cmdBuf);
    gravSubmitCommandBuffer(ctx->device, ctx->cmdBuf);

    /* Render sonucunu fiziksel ekrana kopyala */
    gravFBPresent(ctx->device, &ctx->fb, ctx->colorImg, 0,0, 0,0, 0,0);

    /* NOVA-64: double buffering flip */
#ifdef NOVA64_SIMULATOR
    gravNova64FBFlip();
#endif
}

/* =========================================================================
 * TEMİZLİK
 * ========================================================================= */

static void os_shutdown(GravOSContext* ctx) {
    gravFreeCommandBuffer(ctx->device, ctx->cmdBuf);
    gravDestroyBuffer(ctx->device, ctx->vertexBuf);
    gravDestroyPipeline(ctx->device, ctx->pipeline);
    gravDestroyShaderModule(ctx->device, ctx->shader);
    gravDestroyFramebuffer(ctx->device, ctx->framebuffer);
    gravDestroyRenderPass(ctx->device, ctx->renderPass);
    gravDestroyImage(ctx->device, ctx->depthImg);
    gravDestroyImage(ctx->device, ctx->colorImg);
    gravDestroyDevice(ctx->device);
    gravDestroyInstance(ctx->instance);
}

/* =========================================================================
 * MAIN — OS kernel'i simüle et
 * ========================================================================= */

int main(void) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║   NOVA-64 OS — Gravityon GPU Stack   ║\n");
    printf("║   Doğrudan Framebuffer Erişimi       ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* Boot: fiziksel framebuffer adresini al */
    void* fbAddr = get_framebuffer_address();
    printf("[OS Boot] Framebuffer adresi: %p\n", fbAddr);
    printf("[OS Boot] Çözünürlük: %dx%d @ RGBA8\n\n", SCREEN_W, SCREEN_H);

    /* GPU'yu başlat */
    GravOSContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    GravResult r = os_init_gpu(&ctx, fbAddr);
    if (r != GRAV_SUCCESS) {
        printf("[HATA] GPU başlatılamadı: %s\n", gravResultString(r));
        return 1;
    }
    printf("[Gravityon] GPU hazır — %dx%d framebuffer bağlandı\n\n", SCREEN_W, SCREEN_H);

    /* 8 frame render et (gerçek OS'ta sonsuz döngü olurdu) */
    for (int i = 0; i < 8; i++) {
        float angle = (float)i / 8.0f * 6.2831853f;
        os_render_frame(&ctx, angle);

        uint64_t ns = gravGetLastSubmitTimeNs(ctx.device);
        printf("[Frame %d] angle=%.2f rad | render=%.2f ms | "
               "fb_present → 0x%p\n",
               i, angle, (double)ns/1e6, fbAddr);

        /* Host test: PPM olarak kaydet */
        char fname[64];
        snprintf(fname, sizeof(fname), "output_os_frame_%d.ppm", i);
        gravSaveImagePPM(ctx.device, ctx.colorImg, fname);
    }

    printf("\n[Gravityon] %d frame tamamlandı.\n", 8);
    printf("[OS Boot] GPU stack kapatılıyor...\n");

    os_shutdown(&ctx);
    free(fbAddr);   /* simülasyonda serbest bırak — gerçek OS'ta yapma */

    printf("[Gravityon] Tüm kaynaklar temizlendi.\n");
    return 0;
}
