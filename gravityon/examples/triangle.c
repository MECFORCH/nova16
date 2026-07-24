/**
 * GRAVITYON — Hello Triangle
 * ==========================
 * Gravityon GPU API'siyle ilk üçgeni render eden minimal örnek.
 * Çıktı: output_triangle.ppm
 *
 * Derleme:
 *   cd nova16/gravityon
 *   make examples
 *   ./triangle
 */

#include "../gravityon.h"
#include "../gravityon_math.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * VERTEX YAPISI
 * ========================================================================= */

typedef struct Vertex {
    float pos[3];    /* x, y, z */
    float color[3];  /* r, g, b */
} Vertex;

/* =========================================================================
 * SHADER'LAR
 * ========================================================================= */

/* Varying layout: [0..2] = renk (rgb) */

static void vert_shader(const void* vdata, const void* uniforms,
                         float outPos[4], float* outVaryings) {
    const Vertex* v = (const Vertex*)vdata;
    (void)uniforms;

    /* Doğrudan clip space'e yerleştir (2D üçgen, w=1) */
    outPos[0] = v->pos[0];
    outPos[1] = v->pos[1];
    outPos[2] = v->pos[2];
    outPos[3] = 1.0f;

    /* Rengi varying olarak geçir */
    outVaryings[0] = v->color[0];
    outVaryings[1] = v->color[1];
    outVaryings[2] = v->color[2];
}

static void frag_shader(const float* varyings, const void* uniforms,
                          float outColor[4]) {
    (void)uniforms;
    outColor[0] = varyings[0];  /* R */
    outColor[1] = varyings[1];  /* G */
    outColor[2] = varyings[2];  /* B */
    outColor[3] = 1.0f;         /* A */
}

/* =========================================================================
 * MAIN
 * ========================================================================= */

int main(void) {
    GravResult r;
    const uint32_t WIDTH  = 800;
    const uint32_t HEIGHT = 600;

    printf("[Gravityon] Hello Triangle\n");

    /* --- Instance ve Device --- */
    GravInstance instance;
    r = gravCreateInstance(&(GravInstanceCreateInfo){
        .appName    = "Hello Triangle",
        .appVersion = GRAV_MAKE_VERSION(1,0,0),
        .apiVersion = GRAV_API_VERSION
    }, &instance);
    if (r != GRAV_SUCCESS) { fprintf(stderr, "Instance hatası: %s\n", gravResultString(r)); return 1; }

    GravDevice device;
    r = gravCreateDevice(instance, &(GravDeviceCreateInfo){ .threadCount = 1 }, &device);
    if (r != GRAV_SUCCESS) { fprintf(stderr, "Device hatası: %s\n", gravResultString(r)); return 1; }

    /* --- Color ve Depth Image --- */
    GravImage colorImg, depthImg;
    gravCreateImage(device, &(GravImageCreateInfo){
        .extent = {WIDTH, HEIGHT},
        .format = GRAV_FORMAT_R8G8B8A8_UNORM,
        .usage  = GRAV_IMAGE_USAGE_COLOR_ATTACHMENT
    }, &colorImg);

    gravCreateImage(device, &(GravImageCreateInfo){
        .extent = {WIDTH, HEIGHT},
        .format = GRAV_FORMAT_D32_SFLOAT,
        .usage  = GRAV_IMAGE_USAGE_DEPTH_ATTACHMENT
    }, &depthImg);

    /* --- Render Pass --- */
    GravRenderPass renderPass;
    gravCreateRenderPass(device, &(GravRenderPassCreateInfo){
        .colorAttachment = {
            .format  = GRAV_FORMAT_R8G8B8A8_UNORM,
            .loadOp  = GRAV_LOAD_OP_CLEAR,
            .storeOp = GRAV_STORE_OP_STORE
        },
        .hasDepthAttachment = 1,
        .depthAttachment = {
            .format  = GRAV_FORMAT_D32_SFLOAT,
            .loadOp  = GRAV_LOAD_OP_CLEAR,
            .storeOp = GRAV_STORE_OP_STORE
        }
    }, &renderPass);

    /* --- Framebuffer --- */
    GravFramebuffer framebuffer;
    gravCreateFramebuffer(device, &(GravFramebufferCreateInfo){
        .renderPass = renderPass,
        .colorImage = colorImg,
        .depthImage = depthImg,
        .extent     = {WIDTH, HEIGHT}
    }, &framebuffer);

    /* --- Shader Module --- */
    GravShaderModule shaderModule;
    gravCreateShaderModule(device, &(GravShaderModuleCreateInfo){
        .vertFn = vert_shader,
        .fragFn = frag_shader
    }, &shaderModule);

    /* --- Pipeline --- */
    GravPipeline pipeline;
    gravCreatePipeline(device, &(GravPipelineCreateInfo){
        .shaderModule  = shaderModule,
        .renderPass    = renderPass,
        .vertexStride  = sizeof(Vertex),
        .attributeCount = 2,
        .attributes = {
            { .offset = offsetof(Vertex, pos),   .format = GRAV_FORMAT_R32G32B32A32_F },
            { .offset = offsetof(Vertex, color), .format = GRAV_FORMAT_R32G32B32A32_F },
        },
        .varyingCount      = 3,
        .topology          = GRAV_TOPOLOGY_TRIANGLE_LIST,
        .cullMode          = GRAV_CULL_NONE,
        .fillMode          = GRAV_FILL_SOLID,
        .depthTestEnable   = 1,
        .depthWriteEnable  = 1,
        .depthCompare      = GRAV_COMPARE_LESS,
        .viewport = {
            .x = 0, .y = 0,
            .width = (float)WIDTH, .height = (float)HEIGHT,
            .minDepth = 0.0f, .maxDepth = 1.0f
        },
        .scissor = { .x=0, .y=0, .width=WIDTH, .height=HEIGHT }
    }, &pipeline);

    /* --- Vertex Buffer (gökkuşağı üçgeni) --- */
    Vertex vertices[] = {
        /* NDC koordinatları: x∈[-1,1], y∈[-1,1] */
        { { 0.0f,  0.7f, 0.0f }, { 1.0f, 0.0f, 0.0f } },  /* üst  — kırmızı */
        { {-0.7f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },  /* sol  — yeşil   */
        { { 0.7f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } },  /* sağ  — mavi    */
    };

    GravBuffer vertexBuffer;
    gravCreateBuffer(device, &(GravBufferCreateInfo){
        .size  = sizeof(vertices),
        .usage = GRAV_BUFFER_USAGE_VERTEX
    }, &vertexBuffer);

    void* mapped;
    gravMapBuffer(device, vertexBuffer, &mapped);
    memcpy(mapped, vertices, sizeof(vertices));
    gravUnmapBuffer(device, vertexBuffer);

    /* --- Command Buffer --- */
    GravCommandBuffer cmdBuf;
    gravAllocateCommandBuffer(device, NULL, &cmdBuf);

    gravBeginCommandBuffer(cmdBuf);

    gravCmdBeginRenderPass(cmdBuf, &(GravRenderPassBeginInfo){
        .renderPass  = renderPass,
        .framebuffer = framebuffer,
        .renderArea  = { .x=0, .y=0, .width=WIDTH, .height=HEIGHT },
        .clearColor  = { .r=0.05f, .g=0.05f, .b=0.1f, .a=1.0f },
        .clearDepth  = 1.0f
    });

    gravCmdBindPipeline(cmdBuf, pipeline);
    gravCmdBindVertexBuffer(cmdBuf, vertexBuffer, 0);
    gravCmdDraw(cmdBuf, 3, 0);

    gravCmdEndRenderPass(cmdBuf);
    gravEndCommandBuffer(cmdBuf);

    /* --- Submit --- */
    r = gravSubmitCommandBuffer(device, cmdBuf);
    if (r != GRAV_SUCCESS) {
        fprintf(stderr, "Submit hatası: %s\n", gravResultString(r));
        return 1;
    }

    uint64_t ns = gravGetLastSubmitTimeNs(device);
    printf("[Gravityon] Render tamamlandı — %.2f ms\n", (double)ns / 1e6);

    /* --- Kaydet --- */
    r = gravSaveImagePPM(device, colorImg, "output_triangle.ppm");
    if (r == GRAV_SUCCESS)
        printf("[Gravityon] output_triangle.ppm kaydedildi (%ux%u)\n", WIDTH, HEIGHT);

    r = gravSaveImageBMP(device, colorImg, "output_triangle.bmp");
    if (r == GRAV_SUCCESS)
        printf("[Gravityon] output_triangle.bmp kaydedildi\n");

    /* --- Temizlik --- */
    gravFreeCommandBuffer(device, cmdBuf);
    gravDestroyBuffer(device, vertexBuffer);
    gravDestroyPipeline(device, pipeline);
    gravDestroyShaderModule(device, shaderModule);
    gravDestroyFramebuffer(device, framebuffer);
    gravDestroyRenderPass(device, renderPass);
    gravDestroyImage(device, depthImg);
    gravDestroyImage(device, colorImg);
    gravDestroyDevice(device);
    gravDestroyInstance(instance);

    printf("[Gravityon] Tüm kaynaklar serbest bırakıldı.\n");
    return 0;
}
