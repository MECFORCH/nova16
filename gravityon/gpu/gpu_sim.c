/**
 * GRAVITYON GPU Simülatörü — Uygulama
 * =====================================
 * GBYT bytecode VM + software rasterizer + ring buffer işleyici.
 * FPGA'daki gerçek GPU çekirdeğiyle birebir aynı davranış.
 */

#define _POSIX_C_SOURCE 199309L
#include "gpu_sim.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ─── yardımcılar ─────────────────────────────────────────────────── */
#define CLAMP01(v)   ((v)<0.f?0.f:(v)>1.f?1.f:(v))
#define CLAMPF(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
#define MIN2(a,b)    ((a)<(b)?(a):(b))
#define MAX2(a,b)    ((a)>(b)?(a):(b))

/* =========================================================================
 * GBYT SHADER VM
 * ========================================================================= */

typedef struct GBYTThread {
    float regs[GBYT_MAX_REGS];
    float varyings_out[GPU_SIM_MAX_VARYINGS];
    float clip_pos[4];      /* vertex shader çıkışı */
    float frag_color[4];    /* fragment shader çıkışı */
    float frag_depth;
    int   discarded;
    int   pc;
    /* erişim bağlamı */
    const float*  attr_base;   /* vertex attribute bloğu */
    uint32_t      attr_count;
    const float*  varying_in;  /* fragment varying girişi */
    uint32_t      varying_count;
    const float*  uniforms;    /* uniform float dizisi */
    uint32_t      uniform_count;
    GPUTexSlot*   tex_slots;
    uint32_t      fragX, fragY;
} GBYTThread;

/* Bilinear doku örneklemesi */
static void tex_sample(GPUTexSlot* slot, float u, float v, float out[4]) {
    out[0]=out[1]=out[2]=0.f; out[3]=1.f;
    if (!slot || !slot->valid || !slot->data) return;
    u = u - floorf(u); v = v - floorf(v); /* tekrar */
    float fx = u * (float)(slot->width  - 1);
    float fy = v * (float)(slot->height - 1);
    int x0=(int)fx, y0=(int)fy;
    int x1=MIN2(x0+1,(int)slot->width-1);
    int y1=MIN2(y0+1,(int)slot->height-1);
    float wx=fx-(float)x0, wy=fy-(float)y0;
    const uint8_t* p00 = slot->data + (y0*slot->width+x0)*4;
    const uint8_t* p10 = slot->data + (y0*slot->width+x1)*4;
    const uint8_t* p01 = slot->data + (y1*slot->width+x0)*4;
    const uint8_t* p11 = slot->data + (y1*slot->width+x1)*4;
    for (int c=0;c<4;c++) {
        float t = (1-wx)*(1-wy)*p00[c] + wx*(1-wy)*p10[c]
                + (1-wx)*wy   *p01[c] + wx*wy   *p11[c];
        out[c] = t / 255.f;
    }
}

/* GBYT VM çalıştır */
static void gbyt_run(GBYTThread* t, const GBYTShader* sh) {
    t->regs[GBYT_CONST_ZERO_REG] = 0.f;
    t->regs[GBYT_CONST_ONE_REG]  = 1.f;
    t->regs[GBYT_FRAGCOORD_X] = (float)t->fragX;
    t->regs[GBYT_FRAGCOORD_Y] = (float)t->fragY;
    t->pc = 0;
    t->discarded = 0;

    while (t->pc < (int)sh->instrCount) {
        GBYTInstr ins = sh->instrs[t->pc++];
        GBYTOpcode op = gbyt_opcode(ins);
        uint8_t d  = gbyt_dst(ins);
        uint8_t s1 = gbyt_src1(ins);
        uint8_t s2 = gbyt_src2(ins);
        uint8_t s3 = gbyt_src3(ins);
        float*  r  = t->regs;

#define R1 r[s1]
#define R2 r[s2]
#define R3 r[s3]
#define RD r[d]
        switch (op) {
        case GBYT_NOP:   break;
        case GBYT_END:   return;
        case GBYT_DISCARD: t->discarded=1; return;
        case GBYT_MOV:   RD=R1; break;
        case GBYT_MOVI:  RD=gbyt_imm_float(ins); break;
        case GBYT_ADD:   RD=R1+R2; break;
        case GBYT_SUB:   RD=R1-R2; break;
        case GBYT_MUL:   RD=R1*R2; break;
        case GBYT_DIV:   RD=(fabsf(R2)>1e-30f)?R1/R2:0.f; break;
        case GBYT_MAD:   RD=R1*R2+R3; break;
        case GBYT_NEG:   RD=-R1; break;
        case GBYT_ABS:   RD=fabsf(R1); break;
        case GBYT_RCP:   RD=(fabsf(R1)>1e-30f)?1.f/R1:0.f; break;
        case GBYT_SQRT:  RD=(R1>=0.f)?sqrtf(R1):0.f; break;
        case GBYT_RSQ:   RD=(R1>1e-30f)?1.f/sqrtf(R1):0.f; break;
        case GBYT_MIN:   RD=MIN2(R1,R2); break;
        case GBYT_MAX:   RD=MAX2(R1,R2); break;
        case GBYT_CLAMP: RD=CLAMPF(R1,R2,R3); break;
        case GBYT_LERP:  RD=R1+(R2-R1)*R3; break;
        case GBYT_MOD:   RD=(fabsf(R2)>1e-30f)?fmodf(R1,R2):0.f; break;
        case GBYT_FLOOR: RD=floorf(R1); break;
        case GBYT_CEIL:  RD=ceilf(R1); break;
        case GBYT_FRAC:  RD=R1-floorf(R1); break;
        case GBYT_SIGN:  RD=(R1>0.f)?1.f:(R1<0.f)?-1.f:0.f; break;
        case GBYT_SIN:   RD=sinf(R1); break;
        case GBYT_COS:   RD=cosf(R1); break;
        case GBYT_TAN:   RD=tanf(R1); break;
        case GBYT_ATAN2: RD=atan2f(R1,R2); break;
        case GBYT_EXP2:  RD=exp2f(R1); break;
        case GBYT_LOG2:  RD=(R1>0.f)?log2f(R1):-1e10f; break;
        case GBYT_POW:   RD=(R1>=0.f)?powf(R1,R2):0.f; break;
        case GBYT_SLT:   RD=(R1<R2)?1.f:0.f; break;
        case GBYT_SLE:   RD=(R1<=R2)?1.f:0.f; break;
        case GBYT_SGT:   RD=(R1>R2)?1.f:0.f; break;
        case GBYT_SGE:   RD=(R1>=R2)?1.f:0.f; break;
        case GBYT_SEQ:   RD=(R1==R2)?1.f:0.f; break;
        case GBYT_SNE:   RD=(R1!=R2)?1.f:0.f; break;
        case GBYT_SEL:   RD=(R1!=0.f)?R2:R3; break;
        case GBYT_JMP:   t->pc += gbyt_imm19(ins); break;
        case GBYT_JNZ:   if(R1!=0.f) t->pc+=(int8_t)s2; break;
        case GBYT_JZ:    if(R1==0.f) t->pc+=(int8_t)s2; break;
        case GBYT_RET:   return;

        /* Bellek erişimleri */
        case GBYT_LDATTR: {
            int32_t idx = gbyt_imm19(ins);
            RD = (t->attr_base && idx < (int32_t)t->attr_count)
               ? t->attr_base[idx] : 0.f;
            break;
        }
        case GBYT_LDATTR4: {
            int32_t idx = gbyt_imm19(ins);
            for (int i=0;i<4;i++) {
                int ri = d+i;
                if (ri < GBYT_MAX_REGS)
                    r[ri] = (t->attr_base && idx+i < (int32_t)t->attr_count)
                           ? t->attr_base[idx+i] : 0.f;
            }
            break;
        }
        case GBYT_LDV: {
            int32_t idx = gbyt_imm19(ins);
            RD = (t->varying_in && idx < (int32_t)t->varying_count)
               ? t->varying_in[idx] : 0.f;
            break;
        }
        case GBYT_STV: {
            int32_t idx = (int32_t)(uint32_t)s2;
            if (idx < GPU_SIM_MAX_VARYINGS)
                t->varyings_out[idx] = R1;
            break;
        }
        case GBYT_LDU1: {
            int32_t idx = gbyt_imm19(ins);
            RD = (t->uniforms && idx < (int32_t)t->uniform_count)
               ? t->uniforms[idx] : 0.f;
            break;
        }
        case GBYT_LDU: {
            int32_t base = gbyt_imm19(ins);
            for (int i=0;i<4;i++) {
                int ri=d+i;
                if (ri<GBYT_MAX_REGS)
                    r[ri] = (t->uniforms && base+i<(int32_t)t->uniform_count)
                           ? t->uniforms[base+i] : 0.f;
            }
            break;
        }

        /* Shader çıkışları */
        case GBYT_SPOS:
            t->clip_pos[0]=r[d]; t->clip_pos[1]=r[s1];
            t->clip_pos[2]=r[s2]; t->clip_pos[3]=r[s3];
            break;
        case GBYT_SCOL:
            t->frag_color[0]=r[d]; t->frag_color[1]=r[s1];
            t->frag_color[2]=r[s2]; t->frag_color[3]=r[s3];
            break;
        case GBYT_SDEPTH:
            t->frag_depth = R1; break;

        /* Doku */
        case GBYT_TEX2D:
        case GBYT_TEX2DB: {
            uint8_t unit=(uint8_t)s1;
            float ou[4];
            if (t->tex_slots && unit < GPU_SIM_MAX_TEX_SLOTS)
                tex_sample(&t->tex_slots[unit], r[s2], r[s3], ou);
            else { ou[0]=ou[1]=ou[2]=0.f; ou[3]=1.f; }
            for (int i=0;i<4;i++) if(d+i<GBYT_MAX_REGS) r[d+i]=ou[i];
            break;
        }

        /* Vektör */
        case GBYT_DOT3: {
            float dot=r[d]*r[s1]+r[d+1]*r[s1+1]+r[d+2]*r[s1+2];
            r[d]=dot; break;
        }
        case GBYT_NORM3: {
            float len=sqrtf(r[s1]*r[s1]+r[s1+1]*r[s1+1]+r[s1+2]*r[s1+2]);
            float inv=(len>1e-8f)?1.f/len:0.f;
            r[d]=r[s1]*inv; r[d+1]=r[s1+1]*inv; r[d+2]=r[s1+2]*inv;
            break;
        }
        case GBYT_LEN3: {
            RD=sqrtf(r[s1]*r[s1]+r[s1+1]*r[s1+1]+r[s1+2]*r[s1+2]); break;
        }
        case GBYT_FTOI: RD=(float)(int)R1; break;
        case GBYT_ITOF: RD=R1; break;
        case GBYT_BARRIER: break;
        case GBYT_MEMBAR: break;
        default: break;
        }
#undef R1
#undef R2
#undef R3
#undef RD
    }
}

/* =========================================================================
 * RASTERIZER
 * ========================================================================= */

static inline void write_px(GPUSim* g, int x, int y, float r,float gr,float b,float a) {
    if (x<0||y<0||(uint32_t)x>=g->fbWidth||(uint32_t)y>=g->fbHeight) return;
    uint8_t* fb = g->vram + g->fbOffset;
    uint8_t* px = fb + y*g->fbPitch + x*4;
    px[0]=(uint8_t)(CLAMP01(r)*255); px[1]=(uint8_t)(CLAMP01(gr)*255);
    px[2]=(uint8_t)(CLAMP01(b)*255); px[3]=(uint8_t)(CLAMP01(a)*255);
    g->pixelsFilled++;
}

static inline float read_depth(GPUSim* g, int x, int y) {
    if (!g->dbOffset) return 1.f;
    float* db = (float*)(g->vram + g->dbOffset);
    return db[y*g->fbWidth+x];
}

static inline void write_depth(GPUSim* g, int x, int y, float d) {
    if (!g->dbOffset) return;
    float* db = (float*)(g->vram + g->dbOffset);
    db[y*g->fbWidth+x] = d;
}

static inline int depth_pass(uint32_t op, float z, float zb) {
    switch(op) {
        case 0: return z<zb;
        case 1: return z<=zb;
        case 2: return z>zb;
        case 3: return 1;
        default: return z<zb;
    }
}

static void rasterize(GPUSim* g,
                       float clip[3][4],
                       float var[3][GPU_SIM_MAX_VARYINGS],
                       uint32_t nvar) {
    GBYTShader* fsh = &g->shaders[g->activeFragSlot];
    if (!g->shaderValid[g->activeFragSlot]) return;

    /* Perspektif bölme */
    float ndc[3][3], invW[3];
    for (int i=0;i<3;i++) {
        float w = fabsf(clip[i][3])>1e-7f ? clip[i][3] : 1e-7f;
        invW[i]=1.f/w;
        ndc[i][0]=clip[i][0]*invW[i];
        ndc[i][1]=clip[i][1]*invW[i];
        ndc[i][2]=clip[i][2]*invW[i];
    }

    /* Viewport */
    float sx[3],sy[3],sz[3];
    for (int i=0;i<3;i++) {
        sx[i]=(ndc[i][0]*0.5f+0.5f)*g->vpW + g->vpX;
        sy[i]=(1.f-(ndc[i][1]*0.5f+0.5f))*g->vpH + g->vpY;
        sz[i]=ndc[i][2]*(g->vpMaxDepth-g->vpMinDepth)*0.5f
             +(g->vpMaxDepth+g->vpMinDepth)*0.5f;
    }

    /* Backface cull */
    float cross=(sx[1]-sx[0])*(sy[2]-sy[0])-(sy[1]-sy[0])*(sx[2]-sx[0]);
    if (g->cullMode==1 && cross>=0) return;
    if (g->cullMode==2 && cross<=0) return;

    /* BBox */
    int mnx=(int)floorf(MIN2(sx[0],MIN2(sx[1],sx[2])));
    int mny=(int)floorf(MIN2(sy[0],MIN2(sy[1],sy[2])));
    int mxx=(int) ceilf(MAX2(sx[0],MAX2(sx[1],sx[2])));
    int mxy=(int) ceilf(MAX2(sy[0],MAX2(sy[1],sy[2])));
    mnx=MAX2(mnx,(int)g->scX); mny=MAX2(mny,(int)g->scY);
    mxx=MIN2(mxx,(int)(g->scX+g->scW-1)); mxy=MIN2(mxy,(int)(g->scY+g->scH-1));
    mnx=MAX2(mnx,0); mny=MAX2(mny,0);
    mxx=MIN2(mxx,(int)g->fbWidth-1); mxy=MIN2(mxy,(int)g->fbHeight-1);

    float area=(sx[1]-sx[0])*(sy[2]-sy[0])-(sy[1]-sy[0])*(sx[2]-sx[0]);
    if (fabsf(area)<1e-6f) return;
    float invArea=1.f/area;

    float fv[GPU_SIM_MAX_VARYINGS];
    GBYTThread th; memset(&th,0,sizeof(th));
    th.tex_slots    = g->texSlots;
    th.varying_in   = fv;
    th.varying_count= nvar;
    th.uniforms     = g->uniforms[0];
    th.uniform_count= g->uniformCount[0];

    for (int py=mny;py<=mxy;py++) {
        for (int px2=mnx;px2<=mxx;px2++) {
            float fpx=(float)px2+0.5f, fpy=(float)py+0.5f;
            float w0=((fpx-sx[1])*(sy[2]-sy[1])-(fpy-sy[1])*(sx[2]-sx[1]))*invArea;
            float w1=((fpx-sx[2])*(sy[0]-sy[2])-(fpy-sy[2])*(sx[0]-sx[2]))*invArea;
            float w2=1.f-w0-w1;
            if (w0<0||w1<0||w2<0) continue;

            float z=w0*sz[0]+w1*sz[1]+w2*sz[2];
            if (z<0.f||z>1.f) continue;
            if (g->depthTestEnable) {
                if (!depth_pass(g->depthCompareOp, z, read_depth(g,px2,py))) continue;
                if (g->depthWriteEnable) write_depth(g,px2,py,z);
            }

            /* Perspektif doğru interpolasyon */
            float wi=w0*invW[0]+w1*invW[1]+w2*invW[2];
            float iwi=(fabsf(wi)>1e-8f)?1.f/wi:0.f;
            for (uint32_t v=0;v<nvar&&v<GPU_SIM_MAX_VARYINGS;v++)
                fv[v]=(w0*invW[0]*var[0][v]+w1*invW[1]*var[1][v]
                      +w2*invW[2]*var[2][v])*iwi;

            th.fragX=px2; th.fragY=py;
            th.frag_depth=z;
            th.frag_color[0]=th.frag_color[1]=th.frag_color[2]=0.f;
            th.frag_color[3]=1.f;
            gbyt_run(&th, fsh);
            g->shaderInvocations++;

            if (!th.discarded)
                write_px(g,px2,py,
                         th.frag_color[0],th.frag_color[1],
                         th.frag_color[2],th.frag_color[3]);
        }
    }
    g->trianglesDrawn++;
}

/* =========================================================================
 * DRAW ÇAĞRISI
 * ========================================================================= */

static void exec_draw_verts(GPUSim* g,
                             const uint8_t* vbase, uint32_t vcount,
                             const uint32_t* ibuf, uint32_t icount,
                             uint32_t stride, uint32_t topology) {
    GBYTShader* vsh = &g->shaders[g->activeVertSlot];
    if (!g->shaderValid[g->activeVertSlot]) return;
    uint32_t nvar = g->activeVaryingCount;
    uint32_t total = ibuf ? icount : vcount;
    if (total < 3) return;

    float clip[3][4];
    float var[3][GPU_SIM_MAX_VARYINGS];
    uint32_t triIdx=0;

    uint32_t i=0;
    while (i+2 < total) {
        uint32_t idx[3];
        if (topology==1) { /* strip */
            idx[0]=ibuf?ibuf[i]:i;
            idx[1]=ibuf?ibuf[i+1]:i+1;
            idx[2]=ibuf?ibuf[i+2]:i+2;
            if (triIdx&1) { uint32_t t=idx[1]; idx[1]=idx[2]; idx[2]=t; }
            i++;
        } else {
            idx[0]=ibuf?ibuf[i]:i;
            idx[1]=ibuf?ibuf[i+1]:i+1;
            idx[2]=ibuf?ibuf[i+2]:i+2;
            i+=3;
        }
        triIdx++;

        for (int v=0;v<3;v++) {
            const float* attr=(const float*)(vbase + idx[v]*stride);
            uint32_t attr_count = stride/sizeof(float);

            GBYTThread th; memset(&th,0,sizeof(th));
            th.attr_base    = attr;
            th.attr_count   = attr_count;
            th.uniforms     = g->uniforms[0];
            th.uniform_count= g->uniformCount[0];
            th.tex_slots    = g->texSlots;
            gbyt_run(&th, vsh);
            g->shaderInvocations++;

            memcpy(clip[v], th.clip_pos, 16);
            memcpy(var[v],  th.varyings_out, nvar*sizeof(float));
        }
        rasterize(g, clip, var, nvar);
    }
}

/* =========================================================================
 * KOMUT YÜRÜTMESİ
 * ========================================================================= */

#define MAX_SHADER_PAYLOAD (sizeof(GBYTShader) + 32)
#define MAX_BUF_PAYLOAD    (16*1024*1024)

int gpusim_exec_cmd(GPUSim* gpu, GPUCmdType type, const void* payload, uint32_t size) {
    (void)size;
    switch (type) {

    case GPU_CMD_NOP: break;

    case GPU_CMD_CLEAR: {
        const GPUCmdClear* c = (const GPUCmdClear*)payload;
        if (c->flags & 1) { /* color */
            uint8_t* fb = gpu->vram + gpu->fbOffset;
            uint8_t r=(uint8_t)(CLAMP01(c->r)*255), g2=(uint8_t)(CLAMP01(c->g)*255),
                    b=(uint8_t)(CLAMP01(c->b)*255), a=(uint8_t)(CLAMP01(c->a)*255);
            for (uint32_t y=0;y<gpu->fbHeight;y++)
                for (uint32_t x=0;x<gpu->fbWidth;x++) {
                    uint8_t* p=fb+y*gpu->fbPitch+x*4;
                    p[0]=r;p[1]=g2;p[2]=b;p[3]=a;
                }
        }
        if ((c->flags&2) && gpu->dbOffset) {
            float* db=(float*)(gpu->vram+gpu->dbOffset);
            uint32_t n=gpu->fbWidth*gpu->fbHeight;
            for (uint32_t i=0;i<n;i++) db[i]=c->depth;
        }
        break;
    }

    case GPU_CMD_UPLOAD_SHADER: {
        const GPUCmdUploadShader* u = (const GPUCmdUploadShader*)payload;
        uint32_t slot = u->shaderSlot % GPU_SIM_MAX_SHADERS;
        GBYTShader* sh = &gpu->shaders[slot];
        sh->type       = (GBYTShaderType)u->shaderType;
        sh->instrCount = u->instrCount < GBYT_MAX_INSTRUCTIONS
                       ? u->instrCount : GBYT_MAX_INSTRUCTIONS;
        const GBYTInstr* src = (const GBYTInstr*)(u+1);
        memcpy(sh->instrs, src, sh->instrCount * sizeof(GBYTInstr));
        gpu->shaderValid[slot] = 1;
        break;
    }

    case GPU_CMD_BIND_SHADER: {
        const GPUCmdBindShader* b = (const GPUCmdBindShader*)payload;
        gpu->activeVertSlot   = b->vertSlot % GPU_SIM_MAX_SHADERS;
        gpu->activeFragSlot   = b->fragSlot % GPU_SIM_MAX_SHADERS;
        break;
    }

    case GPU_CMD_UPLOAD_VB:
    case GPU_CMD_UPLOAD_IB: {
        const GPUCmdUploadBuffer* u = (const GPUCmdUploadBuffer*)payload;
        if (u->vramOffset + u->sizeBytes <= gpu->vramSize)
            memcpy(gpu->vram + u->vramOffset, (const uint8_t*)(u+1), u->sizeBytes);
        break;
    }

    case GPU_CMD_UPLOAD_UB: {
        const GPUCmdUploadUB* u = (const GPUCmdUploadUB*)payload;
        uint32_t slot = u->slot % GPU_SIM_MAX_UB_SLOTS;
        uint32_t n    = u->sizeBytes / sizeof(float);
        if (n > GPU_SIM_MAX_UNIFORMS) n = GPU_SIM_MAX_UNIFORMS;
        memcpy(gpu->uniforms[slot], (const uint8_t*)(u+1), n*sizeof(float));
        gpu->uniformCount[slot] = n;
        break;
    }

    case GPU_CMD_UPLOAD_TEX: {
        const GPUCmdUploadTex* u = (const GPUCmdUploadTex*)payload;
        uint32_t slot = u->slot % GPU_SIM_MAX_TEX_SLOTS;
        if (u->vramOffset + u->sizeBytes <= gpu->vramSize) {
            memcpy(gpu->vram + u->vramOffset, (const uint8_t*)(u+1), u->sizeBytes);
            GPUTexSlot* ts = &gpu->texSlots[slot];
            ts->data   = gpu->vram + u->vramOffset;
            ts->width  = u->width;
            ts->height = u->height;
            ts->format = u->format;
            ts->valid  = 1;
        }
        break;
    }

    case GPU_CMD_BIND_TEX: {
        const GPUCmdBindTex* b = (const GPUCmdBindTex*)payload;
        uint32_t slot = b->slot % GPU_SIM_MAX_TEX_SLOTS;
        GPUTexSlot* ts = &gpu->texSlots[slot];
        if (b->vramOffset + b->width*b->height*4 <= gpu->vramSize) {
            ts->data   = gpu->vram + b->vramOffset;
            ts->width  = b->width;
            ts->height = b->height;
            ts->format = b->format;
            ts->valid  = 1;
        }
        break;
    }

    case GPU_CMD_SET_VIEWPORT: {
        const GPUCmdViewport* v=(const GPUCmdViewport*)payload;
        gpu->vpX=v->x; gpu->vpY=v->y;
        gpu->vpW=v->width; gpu->vpH=v->height;
        gpu->vpMinDepth=v->minDepth; gpu->vpMaxDepth=v->maxDepth;
        break;
    }

    case GPU_CMD_SET_SCISSOR: {
        const GPUCmdScissor* s=(const GPUCmdScissor*)payload;
        gpu->scX=s->x; gpu->scY=s->y;
        gpu->scW=s->width; gpu->scH=s->height;
        break;
    }

    case GPU_CMD_SET_RASTER: {
        const GPUCmdRaster* r=(const GPUCmdRaster*)payload;
        gpu->cullMode=r->cullMode; gpu->fillMode=r->fillMode; break;
    }

    case GPU_CMD_SET_DEPTH: {
        const GPUCmdDepth* d=(const GPUCmdDepth*)payload;
        gpu->depthTestEnable  = d->testEnable;
        gpu->depthWriteEnable = d->writeEnable;
        gpu->depthCompareOp   = d->compareOp;
        break;
    }

    case GPU_CMD_DRAW: {
        const GPUCmdDraw* d=(const GPUCmdDraw*)payload;
        const uint8_t* vb = gpu->vram + d->vbOffset;
        exec_draw_verts(gpu, vb+d->firstVertex*d->vertexStride,
                        d->vertexCount, NULL, 0,
                        d->vertexStride, d->topology);
        break;
    }

    case GPU_CMD_DRAW_INDEXED: {
        const GPUCmdDrawIndexed* d=(const GPUCmdDrawIndexed*)payload;
        const uint8_t*  vb  = gpu->vram + d->vbOffset;
        const uint32_t* ib  = (const uint32_t*)(gpu->vram + d->ibOffset);
        exec_draw_verts(gpu, vb, 0, ib+d->firstIndex, d->indexCount,
                        d->vertexStride, d->topology);
        break;
    }

    case GPU_CMD_FILL_RECT: {
        const GPUCmdFillRect* f=(const GPUCmdFillRect*)payload;
        for (int32_t y=f->y; y<f->y+(int32_t)f->height; y++)
            for (int32_t x=f->x; x<f->x+(int32_t)f->width; x++)
                write_px(gpu,x,y,f->r,f->g,f->b,f->a);
        break;
    }

    case GPU_CMD_PRESENT:
        gpu->framesRendered++;
        if (gpu->irqMask & GPU_IRQ_FRAME_DONE) {
            gpu->irqStatus |= GPU_IRQ_FRAME_DONE;
            if (gpu->irqCallback)
                gpu->irqCallback(GPU_IRQ_FRAME_DONE, gpu->irqUserdata);
        }
        break;

    case GPU_CMD_FENCE: {
        const GPUCmdFence* f=(const GPUCmdFence*)payload;
        gpu->portRegs[GPU_PORT_DEBUG] = f->fenceId;
        break;
    }

    default: break;
    }
    return 0;
}

/* =========================================================================
 * RING BUFFER İŞLEYİCİ
 * ========================================================================= */

int gpusim_process(GPUSim* gpu) {
    if (!gpu->initialized) return -1;
    int count = 0;
    static uint8_t payloadBuf[64*1024];

    while (!gpu_ring_empty(&gpu->ring)) {
        GPUCmdHeader hdr;
        if (gpu_ring_read_header(&gpu->ring, &hdr) < 0) break;
        if (hdr.payloadBytes > sizeof(payloadBuf)) {
            gpu_ring_skip(&gpu->ring, hdr.payloadBytes);
            continue;
        }
        if (hdr.payloadBytes > 0)
            gpu_ring_read_payload(&gpu->ring, payloadBuf, hdr.payloadBytes);
        gpusim_exec_cmd(gpu, (GPUCmdType)hdr.type,
                        hdr.payloadBytes>0 ? payloadBuf : NULL,
                        hdr.payloadBytes);
        count++;
    }
    return count;
}

/* =========================================================================
 * I/O PORT SİMÜLASYONU
 * ========================================================================= */

void gpusim_port_write(GPUSim* gpu, uint8_t port, uint64_t value) {
    gpu->portRegs[port] = value;
    switch (port) {
    case GPU_PORT_CTRL:
        if (value & GPU_CTRL_RESET) {
            gpu->activeVertSlot=gpu->activeFragSlot=0;
            gpu->framesRendered=gpu->trianglesDrawn=0;
            gpu->pixelsFilled=gpu->shaderInvocations=0;
        }
        break;
    case GPU_PORT_RING_BASE:
        gpu->ring.base = (uint8_t*)(uintptr_t)value;
        break;
    case GPU_PORT_RING_SIZE:
        gpu->ring.size = (uint32_t)value;
        break;
    case GPU_PORT_RING_HEAD:
        gpu->ring.head = (uint32_t)value;
        break;
    case GPU_PORT_DOORBELL:
        gpusim_process(gpu);
        break;
    case GPU_PORT_FB_ADDR:   gpu->fbOffset = value; break;
    case GPU_PORT_FB_WIDTH:  gpu->fbWidth  = (uint32_t)value;
                             gpu->vpW=(float)gpu->fbWidth;
                             gpu->scW=gpu->fbWidth; break;
    case GPU_PORT_FB_HEIGHT: gpu->fbHeight = (uint32_t)value;
                             gpu->vpH=(float)gpu->fbHeight;
                             gpu->scH=gpu->fbHeight; break;
    case GPU_PORT_FB_PITCH:  gpu->fbPitch  = (uint32_t)value; break;
    case GPU_PORT_FB_FORMAT: gpu->fbFormat = (uint32_t)value; break;
    case GPU_PORT_FB_FLIP:   gpu->framesRendered++; break;
    case GPU_PORT_IRQ_MASK:  gpu->irqMask = (uint32_t)value; break;
    case GPU_PORT_IRQ_CLEAR: gpu->irqStatus &= ~(uint32_t)value; break;
    default: break;
    }
}

uint64_t gpusim_port_read(GPUSim* gpu, uint8_t port) {
    switch (port) {
    case GPU_PORT_ID:      return 0x47505500ULL;
    case GPU_PORT_VERSION: return GRAV_MAKE_VERSION(1,0,0);
    case GPU_PORT_STATUS: {
        uint64_t s = GPU_STATUS_IDLE | GPU_STATUS_FB_READY;
        if (gpu_ring_empty(&gpu->ring)) s |= GPU_STATUS_RING_EMPTY;
        return s;
    }
    case GPU_PORT_RING_TAIL:  return gpu->ring.tail;
    case GPU_PORT_VRAM_SIZE:  return gpu->vramSize;
    case GPU_PORT_VRAM_FREE:  return gpu->vramSize - gpu->fbOffset
                                   - (uint64_t)gpu->fbWidth*gpu->fbHeight*8;
    case GPU_PORT_IRQ_STATUS: return gpu->irqStatus;
    default: return gpu->portRegs[port];
    }
}

/* =========================================================================
 * INIT / DESTROY
 * ========================================================================= */

int gpusim_init(GPUSim* gpu) {
    memset(gpu, 0, sizeof(*gpu));
    gpu->vram = (uint8_t*)calloc(1, GPU_SIM_VRAM_SIZE);
    if (!gpu->vram) return -1;
    gpu->vramSize = GPU_SIM_VRAM_SIZE;

    /* Framebuffer ve derinlik buffer yerleşimi */
    gpu->fbOffset = 0;
    gpu->dbOffset = 0; /* framebuffer boyutu bilinince ayarlanır */

    /* Ring buffer: VRAM'in sonunda */
    gpu_ring_init(&gpu->ring, gpu->ringMem, GPU_SIM_RING_SIZE);

    /* Varsayılan durum */
    gpu->cullMode         = 1;   /* back cull */
    gpu->fillMode         = 0;   /* solid */
    gpu->depthTestEnable  = 1;
    gpu->depthWriteEnable = 1;
    gpu->depthCompareOp   = 0;   /* less */
    gpu->vpMinDepth       = 0.f;
    gpu->vpMaxDepth       = 1.f;

    /* I/O port kayıtları */
    gpu->portRegs[GPU_PORT_ID]      = 0x47505500ULL;
    gpu->portRegs[GPU_PORT_VERSION] = GRAV_MAKE_VERSION(1,0,0);
    gpu->portRegs[GPU_PORT_STATUS]  = GPU_STATUS_IDLE|GPU_STATUS_RING_EMPTY;
    gpu->portRegs[GPU_PORT_VRAM_SIZE] = GPU_SIM_VRAM_SIZE;

    gpu->initialized = 1;
    printf("[GPUSim] Başlatıldı — VRAM: %llu MB\n",
           (unsigned long long)(GPU_SIM_VRAM_SIZE >> 20));
    return 0;
}

void gpusim_destroy(GPUSim* gpu) {
    if (!gpu->initialized) return;
    free(gpu->vram);
    gpu->vram = NULL;
    gpu->initialized = 0;
}

void gpusim_set_irq_callback(GPUSim* gpu,
                              void (*cb)(uint32_t flags, void* ud),
                              void* userdata) {
    gpu->irqCallback = cb;
    gpu->irqUserdata = userdata;
}

GravResult gpusim_readback_color(GPUSim* gpu, GravImage image) {
    if (!gpu || !image) return GRAV_ERROR_INVALID_HANDLE;
    void*  px; size_t sz;
    GravResult r = gravGetImageData(NULL, image, &px, &sz);
    if (r != GRAV_SUCCESS) return r;
    uint32_t w,h;
    gravGetImageExtent(NULL, image, &w, &h);
    uint32_t cpyW = w<gpu->fbWidth  ? w : gpu->fbWidth;
    uint32_t cpyH = h<gpu->fbHeight ? h : gpu->fbHeight;
    uint8_t* src = gpu->vram + gpu->fbOffset;
    uint8_t* dst = (uint8_t*)px;
    for (uint32_t row=0;row<cpyH;row++)
        memcpy(dst+row*w*4, src+row*gpu->fbPitch, cpyW*4);
    return GRAV_SUCCESS;
}

void gpusim_reset_stats(GPUSim* gpu) {
    gpu->framesRendered=gpu->trianglesDrawn=0;
    gpu->pixelsFilled=gpu->shaderInvocations=0;
    gpu->totalNs=0;
}

void gpusim_print_stats(GPUSim* gpu) {
    printf("┌────────────────────────────────────┐\n");
    printf("│   Gravityon GPU Simülatör İstatistik │\n");
    printf("├────────────────────────────────────┤\n");
    printf("│ Frameler       : %10llu         │\n", (unsigned long long)gpu->framesRendered);
    printf("│ Üçgenler       : %10llu         │\n", (unsigned long long)gpu->trianglesDrawn);
    printf("│ Piksel yazma   : %10llu         │\n", (unsigned long long)gpu->pixelsFilled);
    printf("│ Shader çağrısı : %10llu         │\n", (unsigned long long)gpu->shaderInvocations);
    printf("└────────────────────────────────────┘\n");
}
