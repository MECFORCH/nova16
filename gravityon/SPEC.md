# GRAVITYON GPU API — Tam Spesifikasyon

**Versiyon:** 1.0.0  
**Proje:** NOVA-64  
**Lisans:** MIT

---

## 1. Genel Bakış

Gravityon, NOVA-64 mimarisine özgü, yazılım tabanlı bir GPU API'sidir.  
Vulkan'ın nesne modeli ve komut-kayıt akışından ilham alır; ancak SPIR-V yerine  
C fonksiyon işaretçilerini shader modeli olarak kullanır. Tüm rasterizasyon,  
bir yazılım backend'i tarafından gerçekleştirilir.

### 1.1 Tasarım İlkeleri

- **Açık kaynak yönetimi** — Tüm nesneler açıkça oluşturulur ve yok edilir.
- **Komut tamponu ayrımı** — GPU komutları önce kaydedilir, sonra `Submit` ile yürütülür.
- **Tek iş parçacığı güvenliği** — Bu versiyonda senkronizasyon uygulamaya bırakılmıştır.
- **Sıfır bağımlılık** — Yalnızca libc (`stdlib`, `stdio`, `math`, `string`, `time`).

---

## 2. Nesne Modeli

```
GravInstance
  └─ GravDevice
       ├─ GravBuffer          (vertex, index, uniform, storage)
       ├─ GravImage           (color, depth)
       ├─ GravShaderModule    (vertFn + fragFn)
       ├─ GravRenderPass      (attachment tanımları)
       ├─ GravFramebuffer     (renderpass + image'lar)
       ├─ GravPipeline        (shader + rast durumu + vertex layout)
       └─ GravCommandBuffer   (komut listesi)
```

---

## 3. Yaşam Döngüsü

### 3.1 Başlatma

```
gravCreateInstance → gravCreateDevice
```

### 3.2 Kaynak Oluşturma

```
gravCreateBuffer      / gravDestroyBuffer
gravCreateImage       / gravDestroyImage
gravCreateShaderModule/ gravDestroyShaderModule
gravCreateRenderPass  / gravDestroyRenderPass
gravCreateFramebuffer / gravDestroyFramebuffer
gravCreatePipeline    / gravDestroyPipeline
```

### 3.3 Render Döngüsü

```
gravAllocateCommandBuffer
gravBeginCommandBuffer
  gravCmdBeginRenderPass     ← clear ops burada gerçekleşir
  gravCmdBindPipeline
  gravCmdSetUniforms         ← isteğe bağlı, cmd inline uniform
  gravCmdBindVertexBuffer
  gravCmdBindIndexBuffer     ← isteğe bağlı
  gravCmdDraw / gravCmdDrawIndexed
  gravCmdEndRenderPass
gravEndCommandBuffer
gravSubmitCommandBuffer      ← rasterizasyon burada çalışır
gravSaveImagePPM / BMP       ← çıktı al
```

### 3.4 Temizlik

```
gravFreeCommandBuffer
gravDestroy* (oluşturma tersi sırayla)
gravDestroyDevice
gravDestroyInstance
```

---

## 4. Sonuç Kodları

| Kod | Değer | Açıklama |
|-----|-------|----------|
| `GRAV_SUCCESS` | 0 | Başarılı |
| `GRAV_NOT_READY` | 1 | Hazır değil |
| `GRAV_TIMEOUT` | 2 | Zaman aşımı |
| `GRAV_ERROR_OUT_OF_MEMORY` | -1 | Bellek yetersiz |
| `GRAV_ERROR_INVALID_HANDLE` | -2 | NULL veya geçersiz handle |
| `GRAV_ERROR_INVALID_ARGUMENT` | -3 | Geçersiz parametre |
| `GRAV_ERROR_OUT_OF_RANGE` | -4 | Sınır dışı |
| `GRAV_ERROR_COMMAND_BUFFER_FULL` | -5 | Komut tamponu dolu |
| `GRAV_ERROR_NOT_RECORDING` | -6 | Begin çağrılmadı |
| `GRAV_ERROR_ALREADY_RECORDING` | -7 | Zaten kaydediliyor |
| `GRAV_ERROR_RENDER_PASS_NOT_BEGUN` | -8 | RenderPass başlatılmadı |
| `GRAV_ERROR_NO_PIPELINE_BOUND` | -9 | Pipeline bağlanmadı |
| `GRAV_ERROR_NO_VERTEX_BUFFER_BOUND` | -10 | Vertex buffer yok |
| `GRAV_ERROR_IO` | -11 | Dosya G/Ç hatası |

---

## 5. Format Tablosu

| Format | Bayt/Piksel | Kullanım |
|--------|-------------|---------|
| `GRAV_FORMAT_R8G8B8A8_UNORM` | 4 | Renk attachment, PPM/BMP çıktı |
| `GRAV_FORMAT_R32G32B32A32_F` | 16 | HDR renk, vertex attribute tipi |
| `GRAV_FORMAT_D32_SFLOAT` | 4 | Derinlik attachment |

---

## 6. Shader Modeli

### 6.1 Vertex Shader

```c
typedef void (*GravVertFn)(
    const void* vertexData,  // GravPipelineCreateInfo::vertexStride baytı
    const void* uniforms,    // NULL veya uniform bloğu
    float       outPos[4],   // clip-space [x, y, z, w]
    float*      outVaryings  // [0 .. varyingCount-1]
);
```

**Kurallar:**
- `outPos[3]` (w) sıfır olmamalıdır; perspektif bölme `1/w` ile yapılır.
- `outVaryings` dizisi en az `varyingCount` eleman içermelidir.
- Vertex shader her vertex için çağrılır.

### 6.2 Fragment Shader

```c
typedef void (*GravFragFn)(
    const float* varyings,   // perspektif-doğru interpole, [0..varyingCount-1]
    const void*  uniforms,   // NULL veya uniform bloğu
    float        outColor[4] // [r, g, b, a] → 0.0–1.0 aralığında
);
```

**Kurallar:**
- `varyings` okunabilir; yazılmamalıdır.
- `outColor` değerleri `[0, 1]` aralığının dışına çıkabilir; yazma sırasında kırpılır.
- Fragment shader, her kapsanan piksel için çağrılır (derinlik testini geçerse).

### 6.3 Varying İnterpolasyonu

Perspektif-doğru interpolasyon uygulanır:

```
varying_frag = (w0/W0·v0 + w1/W1·v1 + w2/W2·v2) / (w0/W0 + w1/W1 + w2/W2)
```

Burada `w0, w1, w2` piksel için barycentric koordinatlar,  
`W0, W1, W2` ise clip-space `w` değerleridir.

---

## 7. Rasterizasyon Pipeline Detayları

### 7.1 Vertex İşleme

1. `vertexData = vertexBase + index * vertexStride`
2. `vertFn(vertexData, uniforms, clipPos, varyings)` çağrısı
3. Perspektif bölme: `NDC = clipPos.xyz / clipPos.w`
4. Viewport dönüşümü → ekran koordinatları

### 7.2 Back-face Culling

Ekran-uzayı 2D çapraz çarpım işareti:

```
edge = (v1−v0) × (v2−v0)   (Z bileşeni)
```

- `GRAV_CULL_BACK`  → `edge ≥ 0` ise üçgeni atla (CCW front-face)
- `GRAV_CULL_FRONT` → `edge ≤ 0` ise atla
- `GRAV_CULL_NONE`  → atlamaz

### 7.3 Barycentric Rasterizasyon

Bounding-box tarama yöntemi. Her piksel `(px, py)` için:

```
w0 = edge(v1,v2,p) / triangleArea
w1 = edge(v2,v0,p) / triangleArea
w2 = 1 - w0 - w1
```

`w0, w1, w2 ≥ 0` ise piksel üçgenin içindedir.

### 7.4 Scissor

Scissor dikdörtgeninin dışına düşen pikseller çizilmez.  
Bounding-box scissor ile kesiştirilir; bu sayede dışarısı test edilmez.

### 7.5 Derinlik Testi

| Mod | Geçer Koşul |
|-----|-------------|
| `GRAV_COMPARE_LESS` | `z_frag < z_buf` |
| `GRAV_COMPARE_LEQUAL` | `z_frag ≤ z_buf` |
| `GRAV_COMPARE_GREATER` | `z_frag > z_buf` |
| `GRAV_COMPARE_ALWAYS` | Her zaman geçer |

Derinlik image `GRAV_FORMAT_D32_SFLOAT` formatında, `z ∈ [0, 1]` aralığında saklanır.

---

## 8. Uniform Veri

İki yol mevcuttur:

**Pipeline sabit:**
```c
GravPipelineCreateInfo::uniforms = &myData;
GravPipelineCreateInfo::uniformSize = sizeof(myData);
```

**Komut zamanında güncelleme (en fazla 256 bayt kopyalanır):**
```c
gravCmdSetUniforms(cmd, &myData, sizeof(myData));
```

Komut zaman uniform, pipeline sabit olanı geçersiz kılar.  
Uniform verisi, `Submit` sırasında shader'lara `const void*` olarak iletilir.

---

## 9. Vertex Layout

Pipeline oluşturulurken tanımlanır:

```c
GravPipelineCreateInfo::vertexStride   = sizeof(MyVertex);
GravPipelineCreateInfo::attributeCount = 2;
GravPipelineCreateInfo::attributes[0]  = { offsetof(MyVertex, pos),   ... };
GravPipelineCreateInfo::attributes[1]  = { offsetof(MyVertex, color), ... };
```

Vertex shader `vertexData` ham işaretçiyi alır; `attributes` tablosu  
ileride otomatik attribute besleme için ayrılmıştır.

---

## 10. Topoloji

| Topoloji | Üçgen Başına Vertex |
|----------|---------------------|
| `GRAV_TOPOLOGY_TRIANGLE_LIST` | Her 3 → 1 üçgen |
| `GRAV_TOPOLOGY_TRIANGLE_STRIP` | Her yeni vertex yeni bir üçgen ekler; çift indexli üçgenler ters çevrilir |
| `GRAV_TOPOLOGY_LINE_LIST` | 🔜 |
| `GRAV_TOPOLOGY_POINT_LIST` | 🔜 |

---

## 11. Wireframe Modu

`GRAV_FILL_WIREFRAME` seçildiğinde üçgenlerin 3 kenarı  
Bresenham çizgi algoritmasıyla beyaz olarak çizilir.  
Fragment shader çağrılmaz.

---

## 12. Çıktı Formatları

### 12.1 PPM (Portable Pixmap)

Binary P6 formatı. Her piksel için 3 bayt (RGB), alpha atılır.  
Herhangi bir görüntü görüntüleyici ile açılabilir.

```
gravSaveImagePPM(device, colorImage, "render.ppm");
```

### 12.2 BMP (Windows Bitmap)

24-bit BGR, dolgusuz satır hizalamalı. `-Y` yükseklik ile üstten-aşağı.

```
gravSaveImageBMP(device, colorImage, "render.bmp");
```

---

## 13. Performans

`gravGetLastSubmitTimeNs()` son `Submit` işleminin duvar-saati süresini  
nanosaniye cinsinden döndürür:

```c
gravSubmitCommandBuffer(device, cmd);
uint64_t ns = gravGetLastSubmitTimeNs(device);
printf("%.2f ms\n", (double)ns / 1e6);
```

---

## 14. Sınırlamalar (v1.0)

| Kısıt | Değer |
|-------|-------|
| Maksimum varying sayısı | 16 (`GRAV_MAX_VARYINGS`) |
| Vertex attribute sayısı | 16 (`GRAV_MAX_VERTEX_ATTRIBUTES`) |
| Komut tamponu kapasitesi | 4096 (`GRAV_MAX_COMMANDS`) |
| Inline uniform boyutu | 256 bayt |
| Maksimum image boyutu | 16384 × 16384 |
| İş parçacığı | Tek iş parçacığı |

---

## 15. Gelecek Planları

- [ ] Texture sampler (`GravSampler`, `GravImageView`)
- [ ] Alpha blending (kaynak, hedef, mix modları)
- [ ] Stencil buffer
- [ ] Compute pipeline (`GravComputePipeline`)
- [ ] Çok iş parçacıklı rasterizasyon (tile-based)
- [ ] NOVA-64 assembly shader derleyicisi (`grav-asm`)
- [ ] Gerçek donanım hedefi (FPGA GPU uzantısı)

---

## 16. NOVA Ekosistemi ile Entegrasyon

Gravityon, NOVA-64 simülatörü (`sim.c`) ile birlikte şu şekilde kullanılabilir:

1. NOVA-64 assembly programı hesaplama yapar, sonuçları belleğe yazar.
2. Gravityon bu veriyi vertex/uniform buffer olarak yükler.
3. Render edilen görüntü host sistemine PPM/BMP olarak aktarılır.

Bu akış, NOVA-64'ün GPU hızlandırmalı görselleştirme pipeline'ı olarak  
Gravityon'u kullanmasını sağlar.
