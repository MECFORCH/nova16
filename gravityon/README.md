# GRAVITYON GPU API

NOVA-64 ekosistemi için sıfırdan tasarlanmış, Vulkan'dan ilham alan yazılım GPU API'si.

## Mimari Özeti

```
Uygulama
    │
    ├── GravInstance      ← API girişi, uygulama kimliği
    │       └── GravDevice    ← Sanal GPU, tüm kaynakların sahibi
    │               ├── GravBuffer        ← Vertex / Index / Uniform / Storage
    │               ├── GravImage         ← Renk / Derinlik framebuffer'ları
    │               ├── GravShaderModule  ← Vertex + Fragment shader çifti (C fn ptr)
    │               ├── GravRenderPass    ← Attachment tanımları (loadOp, storeOp)
    │               ├── GravFramebuffer   ← RenderPass + Image bağlantısı
    │               ├── GravPipeline      ← Shader + rasterizasyon durumu
    │               └── GravCommandBuffer ← GPU komutlarının kaydı
    │
    └── gravSubmitCommandBuffer() → Software Rasterizer → Framebuffer
```

## Özellikler

| Özellik | Durum |
|---------|-------|
| Vertex / Index Buffer | ✅ |
| Triangle List & Strip | ✅ |
| Back-face Culling | ✅ |
| Z-Buffer (depth test/write) | ✅ |
| Perspektif-doğru interpolasyon | ✅ |
| Viewport Transform + Scissor | ✅ |
| Wireframe Modu | ✅ |
| PPM & BMP çıktı | ✅ |
| Command Buffer | ✅ |
| Uniform veri | ✅ |
| C99 uyumlu, bağımlılıksız | ✅ |
| Çok iş parçacığı | 🔜 |
| Texture sampler | 🔜 |
| Alpha blending | 🔜 |
| Compute pipeline | 🔜 |

## Hızlı Başlangıç

```bash
cd nova16/gravityon
make            # kütüphane + örnekler
make test       # triangle örneğini çalıştır
```

### Hello Triangle

```c
#include "gravityon.h"

// Vertex shader: pozisyonu clip-space'e taşı, rengi geçir
void my_vert(const void* vdata, const void* uni, float pos[4], float* ov) {
    float* v = (float*)vdata;
    pos[0] = v[0]; pos[1] = v[1]; pos[2] = 0; pos[3] = 1;
    ov[0] = v[2]; ov[1] = v[3]; ov[2] = v[4];   // rgb varying
}

// Fragment shader: interpole rengi yaz
void my_frag(const float* iv, const void* uni, float color[4]) {
    color[0]=iv[0]; color[1]=iv[1]; color[2]=iv[2]; color[3]=1;
}

int main(void) {
    GravInstance inst; gravCreateInstance(..., &inst);
    GravDevice   dev;  gravCreateDevice(inst, ..., &dev);

    // Image, RenderPass, Framebuffer, Shader, Pipeline oluştur...
    // Vertex buffer'a vertex yükle...

    GravCommandBuffer cmd;
    gravAllocateCommandBuffer(dev, NULL, &cmd);
    gravBeginCommandBuffer(cmd);
    gravCmdBeginRenderPass(cmd, &beginInfo);
    gravCmdBindPipeline(cmd, pipeline);
    gravCmdBindVertexBuffer(cmd, vbuf, 0);
    gravCmdDraw(cmd, 3, 0);
    gravCmdEndRenderPass(cmd);
    gravEndCommandBuffer(cmd);

    gravSubmitCommandBuffer(dev, cmd);
    gravSaveImagePPM(dev, colorImg, "out.ppm");

    // Kaynakları serbest bırak...
}
```

## Pipeline Akışı

```
Vertex Buffer (ham bayt)
       │
       ▼
  Vertex Shader (C fn ptr)
  ├── outPos[4]   → clip-space pozisyon
  └── outVaryings → fragment'e taşınacak veriler
       │
       ▼
  Primitive Assembly (Triangle List / Strip)
       │
       ▼
  Back-face Culling (CCW/CW, ekran-uzayı çapraz çarpım)
       │
       ▼
  Rasterizasyon (Barycentric, bounding-box tarama)
  ├── Scissor testi
  ├── Derinlik testi (z-buffer)
  └── Perspektif-doğru varying interpolasyonu
       │
       ▼
  Fragment Shader (C fn ptr)
  └── outColor[4] → RGBA
       │
       ▼
  Framebuffer Yazma (RGBA8 / D32)
       │
       ▼
  gravSaveImagePPM / gravSaveImageBMP
```

## Shader Modeli

Vulkan'daki SPIR-V'nin yerini C fonksiyon işaretçileri alır:

```c
// Vertex shader
typedef void (*GravVertFn)(
    const void* vertexData,   // ham vertex (stride pipeline'da tanımlı)
    const void* uniforms,     // uniform bloğu
    float       outPos[4],    // clip-space çıkış
    float*      outVaryings   // varyingCount boyutlu dizi
);

// Fragment shader
typedef void (*GravFragFn)(
    const float* varyings,   // perspektif-doğru interpolasyonlu varyinglar
    const void*  uniforms,   // uniform bloğu
    float        outColor[4] // RGBA çıkış
);
```

## Dosya Yapısı

```
gravityon/
├── gravityon.h          ← Public API (tek include yeterli)
├── gravityon.c          ← Software rasterizer implementasyonu
├── gravityon_math.h     ← Vec2/3/4, Mat4, perspektif, lookAt, rotate...
├── Makefile
├── README.md
├── SPEC.md              ← Tam API spesifikasyonu
└── examples/
    ├── triangle.c       ← Hello Triangle (800×600, gökkuşağı renkleri)
    └── cube.c           ← 3D dönen küp (32 frame animasyon, Phong ışık)
```

## gravitation_math.h — Mevcut Fonksiyonlar

| Fonksiyon | Açıklama |
|-----------|----------|
| `gv2(x,y)`, `gv3(x,y,z)`, `gv4(x,y,z,w)` | Vektör oluşturucular |
| `gv3_dot`, `gv3_cross`, `gv3_norm` | Vektör işlemleri |
| `gv3_reflect`, `gv3_lerp` | Grafik yardımcıları |
| `gm4_identity`, `gm4_mul` | Matris çarpımı |
| `gm4_translate`, `gm4_scale` | Dönüşüm matrisleri |
| `gm4_rotate_x/y/z`, `gm4_rotate` | Döndürme |
| `gm4_perspective` | Perspektif projeksiyon |
| `gm4_ortho` | Ortografik projeksiyon |
| `gm4_look_at` | Kamera matrisi |
| `gm4_mul_vec4` | Matrix × Vektör |

## Lisans

MIT Lisansı — NOVA Projesi
