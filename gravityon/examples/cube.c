/**
 * GRAVITYON — 3D Dönen Küp
 * ========================
 * Perspektif projeksiyon, z-buffer, Phong aydınlatma ile tam 3D örnek.
 * Küp birden fazla frame render edip animasyon karelerini kaydeder.
 * Çıktı: output_cube_NNNN.ppm (32 frame)
 *
 * Derleme:
 *   cd nova16/gravityon && make examples
 *   ./cube
 */

#include "../gravityon.h"
#include "../gravityon_math.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* =========================================================================
 * VERİ TİPLERİ
 * ========================================================================= */

typedef struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
} Vertex;

typedef struct Uniforms {
    float mvp[16];    /* model-view-projection matrisi */
    float model[16];  /* model matrisi (aydınlatma için) */
    float lightDir[3];
    float pad;
    float ambientColor[3];
    float pad2;
    float diffuseColor[3];
    float pad3;
} Uniforms;

/* =========================================================================
 * SHADERLAR
 * ========================================================================= */

/* Varying layout:
 *   [0..2]  = dünya-uzayı normal (nx, ny, nz)
 *   [3..4]  = UV koordinatları
 *   [5..7]  = dünya-uzayı pozisyon (px, py, pz)
 */

static void cube_vert(const void* vdata, const void* udata,
                       float outPos[4], float* ov) {
    const Vertex*   v  = (const Vertex*)vdata;
    const Uniforms* u  = (const Uniforms*)udata;
    const float*    m  = u->model;
    const float*    mvp = u->mvp;

    /* MVP ile clip-space pozisyon */
    float px = v->pos[0], py = v->pos[1], pz = v->pos[2];
    outPos[0] = mvp[0]*px + mvp[4]*py + mvp[8]*pz  + mvp[12];
    outPos[1] = mvp[1]*px + mvp[5]*py + mvp[9]*pz  + mvp[13];
    outPos[2] = mvp[2]*px + mvp[6]*py + mvp[10]*pz + mvp[14];
    outPos[3] = mvp[3]*px + mvp[7]*py + mvp[11]*pz + mvp[15];

    /* Dünya-uzayı normal (model matrisinin üst-sol 3x3'ü) */
    float nx = v->normal[0], ny = v->normal[1], nz = v->normal[2];
    ov[0] = m[0]*nx + m[4]*ny + m[8]*nz;
    ov[1] = m[1]*nx + m[5]*ny + m[9]*nz;
    ov[2] = m[2]*nx + m[6]*ny + m[10]*nz;

    /* UV */
    ov[3] = v->uv[0];
    ov[4] = v->uv[1];

    /* Dünya-uzayı pozisyon */
    ov[5] = m[0]*px + m[4]*py + m[8]*pz  + m[12];
    ov[6] = m[1]*px + m[5]*py + m[9]*pz  + m[13];
    ov[7] = m[2]*px + m[6]*py + m[10]*pz + m[14];
}

static void cube_frag(const float* iv, const void* udata, float outColor[4]) {
    const Uniforms* u = (const Uniforms*)udata;

    /* Normal'i yeniden normalize et */
    float nx = iv[0], ny = iv[1], nz = iv[2];
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 1e-6f) { nx/=len; ny/=len; nz/=len; }

    /* Işık yönü (normalize edilmiş) */
    float lx = u->lightDir[0], ly = u->lightDir[1], lz = u->lightDir[2];
    float ll = sqrtf(lx*lx + ly*ly + lz*lz);
    if (ll > 1e-6f) { lx/=ll; ly/=ll; lz/=ll; }

    /* Diffuse */
    float diff = nx*lx + ny*ly + nz*lz;
    if (diff < 0.0f) diff = 0.0f;

    /* Ambient + Diffuse */
    float r = u->ambientColor[0] + diff * u->diffuseColor[0];
    float g = u->ambientColor[1] + diff * u->diffuseColor[1];
    float b = u->ambientColor[2] + diff * u->diffuseColor[2];

    /* UV checkerboard deseni */
    float u_coord = iv[3], v_coord = iv[4];
    int checkX = (int)(u_coord * 4.0f) & 1;
    int checkY = (int)(v_coord * 4.0f) & 1;
    float checker = (checkX ^ checkY) ? 1.0f : 0.6f;

    outColor[0] = r * checker;
    outColor[1] = g * checker;
    outColor[2] = b * checker;
    outColor[3] = 1.0f;
}

/* =========================================================================
 * KÜPÜ TANIMLA — 6 yüz × 4 vertex = 24 vertex, 6×2 üçgen = 12 üçgen
 * ========================================================================= */

static const Vertex CUBE_VERTICES[] = {
    /* Ön (+Z) */
    {{-0.5f,-0.5f, 0.5f},{0,0,1},{0,0}}, {{ 0.5f,-0.5f, 0.5f},{0,0,1},{1,0}},
    {{ 0.5f, 0.5f, 0.5f},{0,0,1},{1,1}}, {{-0.5f, 0.5f, 0.5f},{0,0,1},{0,1}},
    /* Arka (-Z) */
    {{ 0.5f,-0.5f,-0.5f},{0,0,-1},{0,0}}, {{-0.5f,-0.5f,-0.5f},{0,0,-1},{1,0}},
    {{-0.5f, 0.5f,-0.5f},{0,0,-1},{1,1}}, {{ 0.5f, 0.5f,-0.5f},{0,0,-1},{0,1}},
    /* Sol (-X) */
    {{-0.5f,-0.5f,-0.5f},{-1,0,0},{0,0}}, {{-0.5f,-0.5f, 0.5f},{-1,0,0},{1,0}},
    {{-0.5f, 0.5f, 0.5f},{-1,0,0},{1,1}}, {{-0.5f, 0.5f,-0.5f},{-1,0,0},{0,1}},
    /* Sağ (+X) */
    {{ 0.5f,-0.5f, 0.5f},{1,0,0},{0,0}}, {{ 0.5f,-0.5f,-0.5f},{1,0,0},{1,0}},
    {{ 0.5f, 0.5f,-0.5f},{1,0,0},{1,1}}, {{ 0.5f, 0.5f, 0.5f},{1,0,0},{0,1}},
    /* Üst (+Y) */
    {{-0.5f, 0.5f, 0.5f},{0,1,0},{0,0}}, {{ 0.5f, 0.5f, 0.5f},{0,1,0},{1,0}},
    {{ 0.5f, 0.5f,-0.5f},{0,1,0},{1,1}}, {{-0.5f, 0.5f,-0.5f},{0,1,0},{0,1}},
    /* Alt (-Y) */
    {{-0.5f,-0.5f,-0.5f},{0,-1,0},{0,0}}, {{ 0.5f,-0.5f,-0.5f},{0,-1,0},{1,0}},
    {{ 0.5f,-0.5f, 0.5f},{0,-1,0},{1,1}}, {{-0.5f,-0.5f, 0.5f},{0,-1,0},{0,1}},
};

static const uint32_t CUBE_INDICES[] = {
     0, 1, 2,  0, 2, 3,   /* ön   */
     4, 5, 6,  4, 6, 7,   /* arka */
     8, 9,10,  8,10,11,   /* sol  */
    12,13,14, 12,14,15,   /* sağ  */
    16,17,18, 16,18,19,   /* üst  */
    20,21,22, 20,22,23,   /* alt  */
};

/* =========================================================================
 * MAIN
 * ========================================================================= */

int main(void) {
    const uint32_t WIDTH  = 800;
    const uint32_t HEIGHT = 600;
    const int      FRAMES = 32;

    printf("[Gravityon] 3D Dönen Küp — %d frame\n", FRAMES);

    /* Instance + Device */
    GravInstance instance;
    GravDevice   device;
    gravCreateInstance(&(GravInstanceCreateInfo){
        .appName = "Cube Demo", .apiVersion = GRAV_API_VERSION
    }, &instance);
    gravCreateDevice(instance, &(GravDeviceCreateInfo){0}, &device);

    /* Image'lar */
    GravImage colorImg, depthImg;
    gravCreateImage(device, &(GravImageCreateInfo){
        .extent={WIDTH,HEIGHT}, .format=GRAV_FORMAT_R8G8B8A8_UNORM,
        .usage=GRAV_IMAGE_USAGE_COLOR_ATTACHMENT
    }, &colorImg);
    gravCreateImage(device, &(GravImageCreateInfo){
        .extent={WIDTH,HEIGHT}, .format=GRAV_FORMAT_D32_SFLOAT,
        .usage=GRAV_IMAGE_USAGE_DEPTH_ATTACHMENT
    }, &depthImg);

    /* Render Pass */
    GravRenderPass rp;
    gravCreateRenderPass(device, &(GravRenderPassCreateInfo){
        .colorAttachment={GRAV_FORMAT_R8G8B8A8_UNORM, GRAV_LOAD_OP_CLEAR, GRAV_STORE_OP_STORE},
        .hasDepthAttachment=1,
        .depthAttachment={GRAV_FORMAT_D32_SFLOAT, GRAV_LOAD_OP_CLEAR, GRAV_STORE_OP_STORE}
    }, &rp);

    /* Framebuffer */
    GravFramebuffer fb;
    gravCreateFramebuffer(device, &(GravFramebufferCreateInfo){
        .renderPass=rp, .colorImage=colorImg, .depthImage=depthImg,
        .extent={WIDTH,HEIGHT}
    }, &fb);

    /* Shader */
    GravShaderModule shader;
    gravCreateShaderModule(device, &(GravShaderModuleCreateInfo){
        .vertFn=cube_vert, .fragFn=cube_frag
    }, &shader);

    /* Pipeline */
    GravPipeline pl;
    gravCreatePipeline(device, &(GravPipelineCreateInfo){
        .shaderModule  = shader,
        .renderPass    = rp,
        .vertexStride  = sizeof(Vertex),
        .attributeCount = 3,
        .attributes = {
            {offsetof(Vertex,pos),    GRAV_FORMAT_R32G32B32A32_F},
            {offsetof(Vertex,normal), GRAV_FORMAT_R32G32B32A32_F},
            {offsetof(Vertex,uv),     GRAV_FORMAT_R32G32B32A32_F},
        },
        .varyingCount     = 8,
        .topology         = GRAV_TOPOLOGY_TRIANGLE_LIST,
        .cullMode         = GRAV_CULL_BACK,
        .fillMode         = GRAV_FILL_SOLID,
        .depthTestEnable  = 1,
        .depthWriteEnable = 1,
        .depthCompare     = GRAV_COMPARE_LESS,
        .viewport = {0,0,(float)WIDTH,(float)HEIGHT,0,1},
        .scissor  = {0,0,WIDTH,HEIGHT}
    }, &pl);

    /* Vertex Buffer */
    GravBuffer vbuf;
    gravCreateBuffer(device, &(GravBufferCreateInfo){
        .size=sizeof(CUBE_VERTICES), .usage=GRAV_BUFFER_USAGE_VERTEX
    }, &vbuf);
    void* vm; gravMapBuffer(device, vbuf, &vm);
    memcpy(vm, CUBE_VERTICES, sizeof(CUBE_VERTICES));
    gravUnmapBuffer(device, vbuf);

    /* Index Buffer */
    GravBuffer ibuf;
    gravCreateBuffer(device, &(GravBufferCreateInfo){
        .size=sizeof(CUBE_INDICES), .usage=GRAV_BUFFER_USAGE_INDEX
    }, &ibuf);
    void* im; gravMapBuffer(device, ibuf, &im);
    memcpy(im, CUBE_INDICES, sizeof(CUBE_INDICES));
    gravUnmapBuffer(device, ibuf);

    /* Command Buffer */
    GravCommandBuffer cmd;
    gravAllocateCommandBuffer(device, NULL, &cmd);

    /* Animasyon döngüsü */
    for (int frame = 0; frame < FRAMES; frame++) {
        float angle = (float)frame / (float)FRAMES * GRAV_TWO_PI;

        /* Matrisler */
        GravMat4 model = gm4_mul(
            gm4_rotate_y(angle),
            gm4_rotate_x(angle * 0.4f)
        );
        GravMat4 view  = gm4_look_at(
            gv3(0,1.5f,3.5f),
            gv3(0,0,0),
            gv3(0,1,0)
        );
        GravMat4 proj  = gm4_perspective(45.0f, (float)WIDTH/(float)HEIGHT, 0.1f, 100.0f);
        GravMat4 mvp   = gm4_mul(proj, gm4_mul(view, model));

        Uniforms uni;
        memset(&uni, 0, sizeof(uni));
        gm4_to_array(mvp,   uni.mvp);
        gm4_to_array(model, uni.model);
        uni.lightDir[0]=1; uni.lightDir[1]=2; uni.lightDir[2]=2;
        uni.ambientColor[0]=uni.ambientColor[1]=uni.ambientColor[2]=0.15f;
        uni.diffuseColor[0]=0.8f; uni.diffuseColor[1]=0.6f; uni.diffuseColor[2]=0.4f;

        /* Komutları kaydet */
        gravResetCommandBuffer(cmd);
        gravBeginCommandBuffer(cmd);

        gravCmdBeginRenderPass(cmd, &(GravRenderPassBeginInfo){
            .renderPass  = rp,
            .framebuffer = fb,
            .renderArea  = {0,0,WIDTH,HEIGHT},
            .clearColor  = {0.08f, 0.08f, 0.12f, 1.0f},
            .clearDepth  = 1.0f
        });
        gravCmdBindPipeline(cmd, pl);
        gravCmdSetUniforms(cmd, &uni, sizeof(uni));
        gravCmdBindVertexBuffer(cmd, vbuf, 0);
        gravCmdBindIndexBuffer(cmd, ibuf, 0);
        gravCmdDrawIndexed(cmd, 36, 0, 0);
        gravCmdEndRenderPass(cmd);

        gravEndCommandBuffer(cmd);
        gravSubmitCommandBuffer(device, cmd);

        /* Kaydet */
        char fname[64];
        snprintf(fname, sizeof(fname), "output_cube_%04d.ppm", frame);
        gravSaveImagePPM(device, colorImg, fname);

        uint64_t ns = gravGetLastSubmitTimeNs(device);
        printf("[Gravityon] Frame %3d / %d — %.2f ms → %s\n",
               frame+1, FRAMES, (double)ns/1e6, fname);
    }

    /* Temizlik */
    gravFreeCommandBuffer(device, cmd);
    gravDestroyBuffer(device, ibuf);
    gravDestroyBuffer(device, vbuf);
    gravDestroyPipeline(device, pl);
    gravDestroyShaderModule(device, shader);
    gravDestroyFramebuffer(device, fb);
    gravDestroyRenderPass(device, rp);
    gravDestroyImage(device, depthImg);
    gravDestroyImage(device, colorImg);
    gravDestroyDevice(device);
    gravDestroyInstance(instance);

    printf("[Gravityon] Animasyon tamamlandı. %d PPM karesi kaydedildi.\n", FRAMES);
    return 0;
}
