/**
 * GBYT — Gravityon Bytecode ISA
 * ==============================
 * NOVA-64 GPU'sunun shader instruction set mimarisi.
 *
 * Her komut 32-bit sabit uzunlukta.
 *
 * Format A (3 kaynak + 1 hedef):
 *   [31:24] OPCODE  [23:19] DST  [18:14] SRC1  [13:9] SRC2  [8:4] SRC3  [3:0] FLAGS
 *
 * Format B (2 kaynak + 1 hedef):
 *   [31:24] OPCODE  [23:19] DST  [18:14] SRC1  [13:9] SRC2  [8:0] FLAGS
 *
 * Format C (anlık değer, immediate):
 *   [31:24] OPCODE  [23:19] DST  [18:0] IMM19 (işaretli, /1024.0 = float)
 *
 * Format D (kaynak yok):
 *   [31:24] OPCODE  [23:0] FLAGS/ADDR
 *
 * Register dosyası: 32 float kayıt (f0–f31) her iş parçacığına özel
 *   f0  = sabit 0.0
 *   f1  = sabit 1.0
 *   f28 = gl_FragCoord.x  (fragment shader'da)
 *   f29 = gl_FragCoord.y
 *   f30 = gl_FragDepth (okuma/yazma)
 *   f31 = serbest kullanım
 */

#ifndef GPU_BYTECODE_H
#define GPU_BYTECODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * OPCODE TABLOSU
 * ========================================================================= */

typedef enum GBYTOpcode {
    /* ── Temel ──────────────────────────────── */
    GBYT_NOP     = 0x00,  /* hiçbir şey yapma                            */
    GBYT_END     = 0x01,  /* shader sonu — iş parçacığını bitir          */
    GBYT_DISCARD = 0x02,  /* fragment'ı öldür (yaz, çizme)               */

    /* ── Veri Taşıma ─────────────────────────── */
    GBYT_MOV     = 0x03,  /* B: dst = src1                               */
    GBYT_MOVI    = 0x04,  /* C: dst = IMM19/1024.0  (float anlık)        */
    GBYT_MOVHI   = 0x05,  /* C: dst_üst16 = IMM19   (büyük sabitler için)*/

    /* ── Aritmetik ───────────────────────────── */
    GBYT_ADD     = 0x06,  /* B: dst = src1 + src2                        */
    GBYT_SUB     = 0x07,  /* B: dst = src1 - src2                        */
    GBYT_MUL     = 0x08,  /* B: dst = src1 * src2                        */
    GBYT_DIV     = 0x09,  /* B: dst = src1 / src2                        */
    GBYT_MAD     = 0x0A,  /* A: dst = src1 * src2 + src3                 */
    GBYT_NEG     = 0x0B,  /* B: dst = -src1                              */
    GBYT_ABS     = 0x0C,  /* B: dst = |src1|                             */
    GBYT_RCP     = 0x0D,  /* B: dst = 1 / src1                           */
    GBYT_SQRT    = 0x0E,  /* B: dst = sqrt(src1)                         */
    GBYT_RSQ     = 0x0F,  /* B: dst = 1 / sqrt(src1)                     */
    GBYT_MIN     = 0x10,  /* B: dst = min(src1, src2)                    */
    GBYT_MAX     = 0x11,  /* B: dst = max(src1, src2)                    */
    GBYT_CLAMP   = 0x12,  /* B: dst = clamp(src1, src2, src3)            */
    GBYT_LERP    = 0x13,  /* A: dst = src1 + (src2-src1)*src3            */
    GBYT_MOD     = 0x14,  /* B: dst = fmod(src1, src2)                   */
    GBYT_FLOOR   = 0x15,  /* B: dst = floor(src1)                        */
    GBYT_CEIL    = 0x16,  /* B: dst = ceil(src1)                         */
    GBYT_FRAC    = 0x17,  /* B: dst = src1 - floor(src1)                 */
    GBYT_SIGN    = 0x18,  /* B: dst = sign(src1)  (-1/0/+1)              */

    /* ── Trigonometri ────────────────────────── */
    GBYT_SIN     = 0x19,  /* B: dst = sin(src1)                          */
    GBYT_COS     = 0x1A,  /* B: dst = cos(src1)                          */
    GBYT_TAN     = 0x1B,  /* B: dst = tan(src1)                          */
    GBYT_ATAN2   = 0x1C,  /* B: dst = atan2(src1, src2)                  */
    GBYT_EXP2    = 0x1D,  /* B: dst = exp2(src1)                         */
    GBYT_LOG2    = 0x1E,  /* B: dst = log2(src1)                         */
    GBYT_POW     = 0x1F,  /* B: dst = pow(src1, src2)                    */

    /* ── Karşılaştırma ───────────────────────── */
    GBYT_SLT     = 0x20,  /* B: dst = src1 <  src2 ? 1.0 : 0.0          */
    GBYT_SLE     = 0x21,  /* B: dst = src1 <= src2 ? 1.0 : 0.0          */
    GBYT_SGT     = 0x22,  /* B: dst = src1 >  src2 ? 1.0 : 0.0          */
    GBYT_SGE     = 0x23,  /* B: dst = src1 >= src2 ? 1.0 : 0.0          */
    GBYT_SEQ     = 0x24,  /* B: dst = src1 == src2 ? 1.0 : 0.0          */
    GBYT_SNE     = 0x25,  /* B: dst = src1 != src2 ? 1.0 : 0.0          */
    GBYT_SEL     = 0x26,  /* A: dst = src1!=0 ? src2 : src3  (select)    */

    /* ── Kontrol Akışı ───────────────────────── */
    GBYT_JMP     = 0x27,  /* C: pc += IMM19  (göreceli atlama)           */
    GBYT_JNZ     = 0x28,  /* B: src1!=0 ise pc += src2_as_int            */
    GBYT_JZ      = 0x29,  /* B: src1==0 ise pc += src2_as_int            */
    GBYT_CALL    = 0x2A,  /* C: alt rutin çağrısı (IMM19=adres)          */
    GBYT_RET     = 0x2B,  /* D: dön                                      */

    /* ── Bellek: Uniform / Varying / Attribute ── */
    GBYT_LDU     = 0x2C,  /* C: dst = uniform[IMM19]  (4 float blok)     */
    GBYT_LDU1    = 0x2D,  /* C: dst = uniform[IMM19]  (tek float)        */
    GBYT_LDV     = 0x2E,  /* C: dst = varying[IMM19]  (fragment'te)      */
    GBYT_STV     = 0x2F,  /* B: varying[src2_int] = src1  (vertex'te)    */
    GBYT_LDATTR  = 0x30,  /* C: dst = vertex_attr[IMM19]                 */
    GBYT_LDATTR4 = 0x31,  /* C: dst..dst+3 = vertex_attr[IMM19..+3]      */

    /* ── Shader Çıkışları ────────────────────── */
    GBYT_SPOS    = 0x32,  /* A: gl_Position = (src1,src2,src3,src4)      */
                          /*    [23:19]=src1 [18:14]=src2 [13:9]=src3    */
                          /*    [8:4]=src4                               */
    GBYT_SCOL    = 0x33,  /* A: output_color = (src1,src2,src3,src4)     */
    GBYT_SDEPTH  = 0x34,  /* B: gl_FragDepth = src1                      */

    /* ── Doku Örnekleme ──────────────────────── */
    GBYT_TEX2D   = 0x35,  /* A: dst..+3 = tex2D(unit, u, v)              */
                          /*    [23:19]=dst [18:14]=unit [13:9]=u [8:4]=v */
    GBYT_TEX2DB  = 0x36,  /* A: bilinear filtre zorla                    */
    GBYT_TEXCUBE = 0x37,  /* A: dst..+3 = texCube(unit, dx, dy, dz)      */

    /* ── Dot / Cross Ürünleri ────────────────── */
    GBYT_DOT2    = 0x38,  /* B: dst = dot2(src1, src2)  [2 ardışık reg]  */
    GBYT_DOT3    = 0x39,  /* B: dst = dot3(src1, src2)  [3 ardışık reg]  */
    GBYT_DOT4    = 0x3A,  /* B: dst = dot4(src1, src2)  [4 ardışık reg]  */
    GBYT_CROSS3  = 0x3B,  /* B: dst..+2 = cross3(src1, src2)             */
    GBYT_NORM3   = 0x3C,  /* B: dst..+2 = normalize(src1..+2)            */
    GBYT_LEN3    = 0x3D,  /* B: dst = length(src1..+2)                   */

    /* ── Bit İşlemleri (int yorumu) ─────────── */
    GBYT_AND     = 0x3E,  /* B: dst = src1 & src2  (bit AND)             */
    GBYT_OR      = 0x3F,  /* B: dst = src1 | src2  (bit OR)              */
    GBYT_XOR     = 0x40,  /* B: dst = src1 ^ src2  (bit XOR)             */
    GBYT_SHL     = 0x41,  /* B: dst = src1 << src2                       */
    GBYT_SHR     = 0x42,  /* B: dst = src1 >> src2                       */
    GBYT_FTOI    = 0x43,  /* B: dst = (int)src1  (float→int dönüşüm)     */
    GBYT_ITOF    = 0x44,  /* B: dst = (float)src1                        */

    /* ── Atomik / Senkronizasyon ─────────────── */
    GBYT_BARRIER = 0x45,  /* D: iş grubu bariyer senkronizasyonu         */
    GBYT_MEMBAR  = 0x46,  /* D: bellek bariyer                           */

    GBYT_OPCOUNT             /* toplam opcode sayısı */
} GBYTOpcode;

/* =========================================================================
 * KOMUT KODLAMA / ÇÖZME YARDIMCILARI
 * ========================================================================= */

typedef uint32_t GBYTInstr;

/* Format A: 4 kayıt (dst, src1, src2, src3) */
static inline GBYTInstr gbyt_enc_a(GBYTOpcode op,
                                    uint8_t dst, uint8_t s1,
                                    uint8_t s2,  uint8_t s3) {
    return ((uint32_t)op  << 24) |
           ((uint32_t)(dst & 0x1F) << 19) |
           ((uint32_t)(s1  & 0x1F) << 14) |
           ((uint32_t)(s2  & 0x1F) <<  9) |
           ((uint32_t)(s3  & 0x1F) <<  4);
}

/* Format B: 3 kayıt (dst, src1, src2) */
static inline GBYTInstr gbyt_enc_b(GBYTOpcode op,
                                    uint8_t dst, uint8_t s1, uint8_t s2) {
    return ((uint32_t)op  << 24) |
           ((uint32_t)(dst & 0x1F) << 19) |
           ((uint32_t)(s1  & 0x1F) << 14) |
           ((uint32_t)(s2  & 0x1F) <<  9);
}

/* Format C: dst + 19-bit işaretli anlık değer */
static inline GBYTInstr gbyt_enc_c(GBYTOpcode op, uint8_t dst, int32_t imm) {
    uint32_t imm19 = (uint32_t)(imm & 0x7FFFF);
    return ((uint32_t)op << 24) |
           ((uint32_t)(dst & 0x1F) << 19) |
           imm19;
}

/* Format D: yalnızca opcode + bayraklar */
static inline GBYTInstr gbyt_enc_d(GBYTOpcode op, uint32_t flags) {
    return ((uint32_t)op << 24) | (flags & 0xFFFFFF);
}

/* Çözme */
static inline GBYTOpcode gbyt_opcode(GBYTInstr i) { return (GBYTOpcode)(i >> 24); }
static inline uint8_t    gbyt_dst   (GBYTInstr i) { return (i >> 19) & 0x1F; }
static inline uint8_t    gbyt_src1  (GBYTInstr i) { return (i >> 14) & 0x1F; }
static inline uint8_t    gbyt_src2  (GBYTInstr i) { return (i >>  9) & 0x1F; }
static inline uint8_t    gbyt_src3  (GBYTInstr i) { return (i >>  4) & 0x1F; }
static inline int32_t    gbyt_imm19 (GBYTInstr i) {
    uint32_t raw = i & 0x7FFFF;
    return (raw & 0x40000) ? (int32_t)(raw | 0xFFF80000) : (int32_t)raw;
}
static inline float gbyt_imm_float(GBYTInstr i) {
    return (float)gbyt_imm19(i) / 1024.0f;
}

/* =========================================================================
 * SHADER TÜRÜ
 * ========================================================================= */

typedef enum GBYTShaderType {
    GBYT_SHADER_VERTEX   = 0,
    GBYT_SHADER_FRAGMENT = 1,
    GBYT_SHADER_COMPUTE  = 2,
    GBYT_SHADER_NATIVE   = 0xFF, /* C fonksiyon işaretçisi — GPU sim özel modu */
} GBYTShaderType;

/* =========================================================================
 * SHADER MODÜLÜ (bellekteki temsil)
 * ========================================================================= */

#define GBYT_MAX_INSTRUCTIONS 4096
#define GBYT_MAX_REGS         32
#define GBYT_CONST_ZERO_REG   0    /* f0  = 0.0 (salt okunur) */
#define GBYT_CONST_ONE_REG    1    /* f1  = 1.0 (salt okunur) */
#define GBYT_FRAGCOORD_X      28   /* f28 = piksel X */
#define GBYT_FRAGCOORD_Y      29   /* f29 = piksel Y */
#define GBYT_FRAGDEPTH        30   /* f30 = derinlik */

typedef struct GBYTShader {
    GBYTShaderType type;
    uint32_t       instrCount;
    GBYTInstr      instrs[GBYT_MAX_INSTRUCTIONS];
} GBYTShader;

/* =========================================================================
 * MAKRO YARDIMCILARI (bytecode yazımını kolaylaştırır)
 * ========================================================================= */

/* Kolaylık: shader'a komut ekle */
#define GBYT_EMIT(sh, instr) \
    do { if ((sh)->instrCount < GBYT_MAX_INSTRUCTIONS) \
             (sh)->instrs[(sh)->instrCount++] = (instr); } while(0)

#define GBYT_NOP_(sh)                   GBYT_EMIT(sh, gbyt_enc_d(GBYT_NOP, 0))
#define GBYT_END_(sh)                   GBYT_EMIT(sh, gbyt_enc_d(GBYT_END, 0))
#define GBYT_DISCARD_(sh)               GBYT_EMIT(sh, gbyt_enc_d(GBYT_DISCARD, 0))

#define GBYT_MOV_(sh,d,s)               GBYT_EMIT(sh, gbyt_enc_b(GBYT_MOV,d,s,0))
#define GBYT_MOVI_(sh,d,imm)            GBYT_EMIT(sh, gbyt_enc_c(GBYT_MOVI,d,(int32_t)((imm)*1024.0f)))
#define GBYT_ADD_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_ADD,d,a,b))
#define GBYT_SUB_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_SUB,d,a,b))
#define GBYT_MUL_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_MUL,d,a,b))
#define GBYT_DIV_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_DIV,d,a,b))
#define GBYT_MAD_(sh,d,a,b,c)           GBYT_EMIT(sh, gbyt_enc_a(GBYT_MAD,d,a,b,c))
#define GBYT_NEG_(sh,d,s)               GBYT_EMIT(sh, gbyt_enc_b(GBYT_NEG,d,s,0))
#define GBYT_ABS_(sh,d,s)               GBYT_EMIT(sh, gbyt_enc_b(GBYT_ABS,d,s,0))
#define GBYT_SQRT_(sh,d,s)              GBYT_EMIT(sh, gbyt_enc_b(GBYT_SQRT,d,s,0))
#define GBYT_MIN_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_MIN,d,a,b))
#define GBYT_MAX_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_MAX,d,a,b))
#define GBYT_SIN_(sh,d,s)               GBYT_EMIT(sh, gbyt_enc_b(GBYT_SIN,d,s,0))
#define GBYT_COS_(sh,d,s)               GBYT_EMIT(sh, gbyt_enc_b(GBYT_COS,d,s,0))
#define GBYT_SLT_(sh,d,a,b)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_SLT,d,a,b))
#define GBYT_SEL_(sh,d,cond,a,b)        GBYT_EMIT(sh, gbyt_enc_a(GBYT_SEL,d,cond,a,b))
#define GBYT_LDU_(sh,d,idx)             GBYT_EMIT(sh, gbyt_enc_c(GBYT_LDU,d,idx))
#define GBYT_LDV_(sh,d,idx)             GBYT_EMIT(sh, gbyt_enc_c(GBYT_LDV,d,idx))
#define GBYT_LDATTR_(sh,d,idx)          GBYT_EMIT(sh, gbyt_enc_c(GBYT_LDATTR,d,idx))
#define GBYT_SPOS_(sh,x,y,z,w)          GBYT_EMIT(sh, gbyt_enc_a(GBYT_SPOS,x,y,z,w))
#define GBYT_SCOL_(sh,r,g,b,a)          GBYT_EMIT(sh, gbyt_enc_a(GBYT_SCOL,r,g,b,a))
#define GBYT_TEX2D_(sh,d,unit,u,v)      GBYT_EMIT(sh, gbyt_enc_a(GBYT_TEX2D,d,unit,u,v))
#define GBYT_DOT3_(sh,d,a,b)            GBYT_EMIT(sh, gbyt_enc_b(GBYT_DOT3,d,a,b))
#define GBYT_NORM3_(sh,d,s)             GBYT_EMIT(sh, gbyt_enc_b(GBYT_NORM3,d,s,0))
#define GBYT_JMP_(sh,off)               GBYT_EMIT(sh, gbyt_enc_c(GBYT_JMP,0,off))
#define GBYT_JNZ_(sh,cond,off)          GBYT_EMIT(sh, gbyt_enc_b(GBYT_JNZ,0,cond,(uint8_t)(off)))

/* =========================================================================
 * MNEMONİK TABLO (disassembler için)
 * ========================================================================= */

static const char* const GBYT_MNEMONIC[] = {
    "NOP","END","DISCARD","MOV","MOVI","MOVHI",
    "ADD","SUB","MUL","DIV","MAD","NEG","ABS","RCP","SQRT","RSQ",
    "MIN","MAX","CLAMP","LERP","MOD","FLOOR","CEIL","FRAC","SIGN",
    "SIN","COS","TAN","ATAN2","EXP2","LOG2","POW",
    "SLT","SLE","SGT","SGE","SEQ","SNE","SEL",
    "JMP","JNZ","JZ","CALL","RET",
    "LDU","LDU1","LDV","STV","LDATTR","LDATTR4",
    "SPOS","SCOL","SDEPTH",
    "TEX2D","TEX2DB","TEXCUBE",
    "DOT2","DOT3","DOT4","CROSS3","NORM3","LEN3",
    "AND","OR","XOR","SHL","SHR","FTOI","ITOF",
    "BARRIER","MEMBAR"
};

#ifdef __cplusplus
}
#endif

#endif /* GPU_BYTECODE_H */
