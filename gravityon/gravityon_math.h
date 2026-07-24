/**
 * GRAVITYON MATH — Vektör & Matris Kütüphanesi
 * =============================================
 * Inline, bağımlılıksız, C99 uyumlu matematik yardımcıları.
 * 3D grafik için gerekli tüm işlemleri kapsar.
 */

#ifndef GRAVITYON_MATH_H
#define GRAVITYON_MATH_H

#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * SABITLER
 * ========================================================================= */

#define GRAV_PI     3.14159265358979323846f
#define GRAV_TWO_PI 6.28318530717958647692f
#define GRAV_DEG2RAD(d) ((d) * GRAV_PI / 180.0f)
#define GRAV_RAD2DEG(r) ((r) * 180.0f / GRAV_PI)

/* =========================================================================
 * VEC2
 * ========================================================================= */

typedef struct GravVec2 { float x, y; } GravVec2;

static inline GravVec2 gv2(float x, float y) { return (GravVec2){x, y}; }
static inline GravVec2 gv2_add(GravVec2 a, GravVec2 b) { return (GravVec2){a.x+b.x, a.y+b.y}; }
static inline GravVec2 gv2_sub(GravVec2 a, GravVec2 b) { return (GravVec2){a.x-b.x, a.y-b.y}; }
static inline GravVec2 gv2_scale(GravVec2 v, float s)  { return (GravVec2){v.x*s, v.y*s}; }
static inline float    gv2_dot(GravVec2 a, GravVec2 b) { return a.x*b.x + a.y*b.y; }
static inline float    gv2_len(GravVec2 v)             { return sqrtf(gv2_dot(v,v)); }
static inline GravVec2 gv2_norm(GravVec2 v) {
    float l = gv2_len(v);
    return l > 1e-8f ? (GravVec2){v.x/l, v.y/l} : (GravVec2){0,0};
}

/* =========================================================================
 * VEC3
 * ========================================================================= */

typedef struct GravVec3 { float x, y, z; } GravVec3;

static inline GravVec3 gv3(float x, float y, float z) { return (GravVec3){x,y,z}; }
static inline GravVec3 gv3_add(GravVec3 a, GravVec3 b) { return (GravVec3){a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline GravVec3 gv3_sub(GravVec3 a, GravVec3 b) { return (GravVec3){a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline GravVec3 gv3_scale(GravVec3 v, float s)  { return (GravVec3){v.x*s, v.y*s, v.z*s}; }
static inline GravVec3 gv3_neg(GravVec3 v)             { return (GravVec3){-v.x,-v.y,-v.z}; }
static inline float    gv3_dot(GravVec3 a, GravVec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float    gv3_len(GravVec3 v)             { return sqrtf(gv3_dot(v,v)); }
static inline GravVec3 gv3_norm(GravVec3 v) {
    float l = gv3_len(v);
    return l > 1e-8f ? (GravVec3){v.x/l, v.y/l, v.z/l} : (GravVec3){0,0,0};
}
static inline GravVec3 gv3_cross(GravVec3 a, GravVec3 b) {
    return (GravVec3){
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}
static inline GravVec3 gv3_lerp(GravVec3 a, GravVec3 b, float t) {
    return (GravVec3){a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t};
}
static inline GravVec3 gv3_reflect(GravVec3 v, GravVec3 n) {
    return gv3_sub(v, gv3_scale(n, 2.0f * gv3_dot(v, n)));
}

/* =========================================================================
 * VEC4
 * ========================================================================= */

typedef struct GravVec4 { float x, y, z, w; } GravVec4;

static inline GravVec4 gv4(float x, float y, float z, float w) { return (GravVec4){x,y,z,w}; }
static inline GravVec4 gv4_from3(GravVec3 v, float w) { return (GravVec4){v.x,v.y,v.z,w}; }
static inline GravVec3 gv4_xyz(GravVec4 v) { return (GravVec3){v.x,v.y,v.z}; }
static inline GravVec4 gv4_add(GravVec4 a, GravVec4 b) { return (GravVec4){a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w}; }
static inline GravVec4 gv4_scale(GravVec4 v, float s)  { return (GravVec4){v.x*s,v.y*s,v.z*s,v.w*s}; }
static inline float    gv4_dot(GravVec4 a, GravVec4 b) { return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w; }

/* Perspektif bölme: clip → NDC */
static inline GravVec3 gv4_perspective_divide(GravVec4 v) {
    float inv = (v.w != 0.0f) ? 1.0f / v.w : 0.0f;
    return (GravVec3){v.x*inv, v.y*inv, v.z*inv};
}

/* =========================================================================
 * MAT4 (sütun-büyük, OpenGL uyumlu düzen)
 * ========================================================================= */

typedef struct GravMat4 { float m[16]; } GravMat4;

/* m[col*4 + row] */
#define GMAT(M, col, row) (M).m[(col)*4+(row)]

static inline GravMat4 gm4_identity(void) {
    GravMat4 r; memset(&r, 0, sizeof(r));
    GMAT(r,0,0)=GMAT(r,1,1)=GMAT(r,2,2)=GMAT(r,3,3)=1.0f;
    return r;
}

static inline GravMat4 gm4_mul(GravMat4 a, GravMat4 b) {
    GravMat4 r; memset(&r, 0, sizeof(r));
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                GMAT(r,c,row) += GMAT(a,k,row) * GMAT(b,c,k);
    return r;
}

static inline GravVec4 gm4_mul_vec4(GravMat4 m, GravVec4 v) {
    return (GravVec4){
        GMAT(m,0,0)*v.x + GMAT(m,1,0)*v.y + GMAT(m,2,0)*v.z + GMAT(m,3,0)*v.w,
        GMAT(m,0,1)*v.x + GMAT(m,1,1)*v.y + GMAT(m,2,1)*v.z + GMAT(m,3,1)*v.w,
        GMAT(m,0,2)*v.x + GMAT(m,1,2)*v.y + GMAT(m,2,2)*v.z + GMAT(m,3,2)*v.w,
        GMAT(m,0,3)*v.x + GMAT(m,1,3)*v.y + GMAT(m,2,3)*v.z + GMAT(m,3,3)*v.w,
    };
}

/* Öteleme matrisi */
static inline GravMat4 gm4_translate(float tx, float ty, float tz) {
    GravMat4 r = gm4_identity();
    GMAT(r,3,0)=tx; GMAT(r,3,1)=ty; GMAT(r,3,2)=tz;
    return r;
}

/* Ölçek matrisi */
static inline GravMat4 gm4_scale(float sx, float sy, float sz) {
    GravMat4 r = gm4_identity();
    GMAT(r,0,0)=sx; GMAT(r,1,1)=sy; GMAT(r,2,2)=sz;
    return r;
}

/* X ekseni etrafında döndürme */
static inline GravMat4 gm4_rotate_x(float rad) {
    GravMat4 r = gm4_identity();
    float c = cosf(rad), s = sinf(rad);
    GMAT(r,1,1)= c; GMAT(r,2,1)=-s;
    GMAT(r,1,2)= s; GMAT(r,2,2)= c;
    return r;
}

/* Y ekseni etrafında döndürme */
static inline GravMat4 gm4_rotate_y(float rad) {
    GravMat4 r = gm4_identity();
    float c = cosf(rad), s = sinf(rad);
    GMAT(r,0,0)= c; GMAT(r,2,0)= s;
    GMAT(r,0,2)=-s; GMAT(r,2,2)= c;
    return r;
}

/* Z ekseni etrafında döndürme */
static inline GravMat4 gm4_rotate_z(float rad) {
    GravMat4 r = gm4_identity();
    float c = cosf(rad), s = sinf(rad);
    GMAT(r,0,0)= c; GMAT(r,1,0)=-s;
    GMAT(r,0,1)= s; GMAT(r,1,1)= c;
    return r;
}

/* Keyfi eksen etrafında döndürme (normalize edilmiş eksen) */
static inline GravMat4 gm4_rotate(GravVec3 axis, float rad) {
    float c = cosf(rad), s = sinf(rad), t = 1.0f - c;
    float x = axis.x, y = axis.y, z = axis.z;
    GravMat4 r = gm4_identity();
    GMAT(r,0,0)=t*x*x+c;   GMAT(r,1,0)=t*x*y-s*z; GMAT(r,2,0)=t*x*z+s*y;
    GMAT(r,0,1)=t*x*y+s*z; GMAT(r,1,1)=t*y*y+c;   GMAT(r,2,1)=t*y*z-s*x;
    GMAT(r,0,2)=t*x*z-s*y; GMAT(r,1,2)=t*y*z+s*x; GMAT(r,2,2)=t*z*z+c;
    return r;
}

/** Perspektif projeksiyon (fovY derece, aspect, zNear, zFar) */
static inline GravMat4 gm4_perspective(float fovYDeg, float aspect, float zNear, float zFar) {
    float f = 1.0f / tanf(GRAV_DEG2RAD(fovYDeg) * 0.5f);
    float nf = 1.0f / (zNear - zFar);
    GravMat4 r; memset(&r, 0, sizeof(r));
    GMAT(r,0,0) = f / aspect;
    GMAT(r,1,1) = f;
    GMAT(r,2,2) = (zFar + zNear) * nf;
    GMAT(r,3,2) = 2.0f * zFar * zNear * nf;
    GMAT(r,2,3) = -1.0f;
    return r;
}

/** Ortografik projeksiyon */
static inline GravMat4 gm4_ortho(float l, float r, float b, float t, float zn, float zf) {
    GravMat4 m; memset(&m, 0, sizeof(m));
    GMAT(m,0,0) =  2.0f/(r-l);
    GMAT(m,1,1) =  2.0f/(t-b);
    GMAT(m,2,2) = -2.0f/(zf-zn);
    GMAT(m,3,0) = -(r+l)/(r-l);
    GMAT(m,3,1) = -(t+b)/(t-b);
    GMAT(m,3,2) = -(zf+zn)/(zf-zn);
    GMAT(m,3,3) =  1.0f;
    return m;
}

/** LookAt view matrisi */
static inline GravMat4 gm4_look_at(GravVec3 eye, GravVec3 center, GravVec3 up) {
    GravVec3 f = gv3_norm(gv3_sub(center, eye));
    GravVec3 s = gv3_norm(gv3_cross(f, up));
    GravVec3 u = gv3_cross(s, f);
    GravMat4 r; memset(&r, 0, sizeof(r));
    GMAT(r,0,0)= s.x; GMAT(r,1,0)= s.y; GMAT(r,2,0)= s.z;
    GMAT(r,0,1)= u.x; GMAT(r,1,1)= u.y; GMAT(r,2,1)= u.z;
    GMAT(r,0,2)=-f.x; GMAT(r,1,2)=-f.y; GMAT(r,2,2)=-f.z;
    GMAT(r,3,0)=-gv3_dot(s,eye);
    GMAT(r,3,1)=-gv3_dot(u,eye);
    GMAT(r,3,2)= gv3_dot(f,eye);
    GMAT(r,3,3)= 1.0f;
    return r;
}

/* Mat4'ü float dizisine yaz (shader uniform için) */
static inline void gm4_to_array(GravMat4 m, float out[16]) {
    memcpy(out, m.m, 16 * sizeof(float));
}

/* =========================================================================
 * YARDIMCI: Clamp, Lerp, vs.
 * ========================================================================= */

static inline float grav_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float grav_lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float grav_maxf (float a, float b) { return a > b ? a : b; }
static inline float grav_minf (float a, float b) { return a < b ? a : b; }

#ifdef __cplusplus
}
#endif

#endif /* GRAVITYON_MATH_H */
