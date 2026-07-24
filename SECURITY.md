# NOVA-16 SEC — Güvenlik Mimarisi Spesifikasyonu

## Genel Bakış

NOVA-16 SEC, NOVA-16 16-bit mimarisine tam donanım güvenlik desteği ekler.
RISC-V Privileged Specification'dan ilham alınmış, ancak 16-bit adres uzayına
ve yerleşik kriptografik hızlandırıcılara uyacak şekilde yeniden tasarlanmıştır.

---

## 1. Ayrıcalık Seviyeleri

| Seviye | Kod | Ad         | Tanım                                               |
|--------|-----|------------|-----------------------------------------------------|
| 0      | 00  | Machine    | Tam donanım erişimi. Önyükleme modu. Trap hedefi.  |
| 1      | 01  | Supervisor | İşletim sistemi çekirdeği / hypervisor.            |
| 2      | 10  | User       | Uygulama kodu. En kısıtlı.                         |

- Sistem **Machine modunda** başlar.
- Ayrıcalık yalnızca `ERET` ile düşürülebilir (MSTATUS.MPP ayarlandıktan sonra).
- Ayrıcalık yalnızca trap ile yükselebilir (her zaman Machine'e gider).

---

## 2. Komut Seti Uzantısı (SEC ISA)

Temel NOVA-16 komutlarına ek olarak 8 yeni opcode:

| Mnemonik | Opcode | Encoding                                      | Açıklama                              | Min Priv |
|----------|--------|-----------------------------------------------|---------------------------------------|----------|
| `ECALL`  | 0x12   | `[31:26]=0x12 [diğer=0]`                      | Ortam çağrısı → Machine trap          | Herhangi |
| `ERET`   | 0x13   | `[31:26]=0x13 [diğer=0]`                      | Exception dönüşü (MEPC'ye atla)       | M veya S |
| `CSRW`   | 0x14   | `[31:26]=0x14 [25:21]=RS1 [15:11]=CSR_IDX`   | CSR[imm[4:0]] = RS1                   | Değişken |
| `CSRR`   | 0x15   | `[31:26]=0x15 [25:21]=RD  [15:11]=CSR_IDX`    | RD = CSR[imm[4:0]]                    | Değişken |
| `RAND`   | 0x16   | `[31:26]=0x16 [25:21]=RD  [diğer=0]`          | RD = TRNG()                           | Machine  |
| `FENCE`  | 0x17   | `[31:26]=0x17 [diğer=0]`                      | Bellek/IO engeli                      | Herhangi |
| `AESE`   | 0x18   | `[31:26]=0x18 [25:21]=RD  [20:16]=RS1`        | RD = AES_SubBytes(RS1)                | Machine  |
| `HASH`   | 0x19   | `[31:26]=0x19 [25:21]=RD  [20:16]=RS1 [15:11]=RS2` | RD = SHA_step(RS1, RS2)          | Machine  |

### Bit Düzeni Notu
Tüm NOVA-16 komutları 32-bittir: `[31:26]=OPCODE [25:21]=FD [20:16]=FA [15:11]=FB [10:0]=IMM`
- `CSRW/CSRR`: CSR indeksi IMM[4:0] (5 bit = 32 CSR adresi)
- `AESE`: FA=RS1 (giriş), FD=RD (çıkış)
- `HASH`: FD=RD, FA=RS1, FB=RS2

---

## 3. CSR Dosyası

32 adet 16-bit Kontrol/Durum Registerı (CSR):

| İndeks | İsim      | Erişim | Tanım                                               |
|--------|-----------|--------|-----------------------------------------------------|
| 0      | PRIV      | RO     | Mevcut ayrıcalık seviyesi (0=M, 1=S, 2=U)          |
| 1      | MEPC      | M-RW   | Machine Exception PC (trap olan kelime adresi)      |
| 2      | MCAUSE    | RO     | Son trap sebebi kodu                                |
| 3      | MTVEC     | M-RW   | Machine Trap Vector (trap handler kelime adresi)    |
| 4      | MSTATUS   | M-RW   | bit[4:3]=MPP (önceki priv, ERET için)               |
| 5      | SEPC      | S-RW   | Supervisor Exception PC                             |
| 6      | STVEC     | S-RW   | Supervisor Trap Vector                              |
| 7      | SATP      | S-RW   | Supervisor Adres/Koruma                             |
| 8-15   | MPU_BASE  | M-RW   | MPU bölge 0-7 taban adresleri                      |
| 16-23  | MPU_MASK  | M-RW   | MPU bölge 0-7 maskeleri (0=bölge devre dışı)       |
| 24-31  | MPU_PERM  | M-RW   | MPU bölge 0-7 erişim izinleri                      |

### CSR Erişim Kuralları
- İndeks 0-4: **Yalnızca Machine** okuyup yazabilir
- İndeks 5-7: **Machine ve Supervisor** okuyup yazabilir
- İndeks 8-31 (MPU): **Yalnızca Machine** yapılandırabilir
- Yanlış seviyeden erişim → `CAUSE_CSR_PRIV` (7) trap üretir

---

## 4. MPU (Bellek Koruma Birimi)

8 adet programlanabilir bölge. Her bölge 3 CSR üçlüsüyle tanımlanır:

```
MPU_BASE[n] = CSR[ 8+n]  : bölge taban adresi (word-addressed)
MPU_MASK[n] = CSR[16+n]  : adres maskesi (0 = bölge devre dışı)
MPU_PERM[n] = CSR[24+n]  : erişim izinleri (bkz. aşağıdaki bit tablosu)
```

### MPU_PERM Bit Alanları

| Bitler | Alan     | Tanım                                                    |
|--------|----------|----------------------------------------------------------|
| [0]    | READ     | Okuma izni (1=izinli)                                    |
| [1]    | WRITE    | Yazma izni (1=izinli)                                    |
| [2]    | EXEC     | Çalıştırma izni (1=izinli)                               |
| [4:3]  | MIN_PRIV | Bu bölgeye erişmek için gereken minimum ayrıcalık kodu   |

- Bir adres birden fazla bölgeyle eşleşirse: n=0 önceliklidir (ilk eşleşen)
- Hiçbir bölgeyle eşleşmeyen adres: sadece Machine erişebilir

### MPU Yapılandırma Örneği
```asm
; Bölge 0: 0x0000-0x00FF → Machine-only çalıştırma (trap vektörü alanı)
LI   R1, 0x0000
CSRW R1, 8         ; MPU_BASE[0] = 0x0000
LI   R1, 0xFF00
CSRW R1, 16        ; MPU_MASK[0] = 0xFF00
LI   R1, 0b00100   ; EXEC=1, WRITE=0, READ=0, MIN_PRIV=Machine(00)
CSRW R1, 24        ; MPU_PERM[0]
```

---

## 5. Exception / Trap Mekanizması

### Trap Akışı (Donanım)

```
1. Trap koşulu tespit edilir (EXECUTE fazında, kombinasyonel)
2. MEPC    ← PC  (fetch+decode sonrası, zaten +2 ilerlenmiş)
3. MCAUSE  ← hata kodu
4. MSTATUS[4:3] ← mevcut PRIV  (MPP = dönüş için sakla)
5. PRIV    ← MACHINE (0)
6. PC      ← MTVEC
   (MTVEC = 0 ise → HALT)
```

### ERET Akışı

```
1. Ayrıcalık kontrolü: User moddan ERET → CAUSE_ERET_PRIV trap üretir
2. PRIV ← MSTATUS[4:3]  (MPP — kayıtlı önceki seviye)
3. PC   ← MEPC
```

### MCAUSE Kodları

| Kod | Sabit          | Tetikleyen Durum                                        |
|-----|----------------|---------------------------------------------------------|
| 1   | ILL_INSN       | Bilinmeyen opcode                                       |
| 2   | MPU_READ       | MPU okuma erişimi reddedildi                            |
| 3   | MPU_WRITE      | MPU yazma erişimi reddedildi                            |
| 4   | MPU_EXEC       | MPU çalıştırma erişimi reddedildi                       |
| 5   | ECALL_U        | User moddan ECALL                                       |
| 6   | ECALL_S        | Supervisor moddan ECALL                                 |
| 7   | CSR_PRIV       | Yanlış ayrıcalıktan CSR erişimi veya güvenlik komutu   |
| 8   | IO_PRIV        | User moddan OUT veya IN komutu                          |
| 9   | ERET_PRIV      | User moddan ERET komutu                                 |
| 10  | DIV_ZERO       | DIV komutu — sıfıra bölme (RESILIENCE ile kurtarılabilir) |

---

## 6. Kriptografik Hızlandırıcılar

### 6.1 TRNG (Donanım Rastgele Sayı Üreteci)

64-bit Fibonacci LFSR, tap noktaları: bit 63, 21, 1, 0

```
Geri besleme : fb = lfsr[31] ⊕ lfsr[21] ⊕ lfsr[1] ⊕ lfsr[0]
Her saat     : lfsr ← {lfsr[30:0], fb}
RAND çıkışı  : out[15:0] = lfsr[31:16] ⊕ lfsr[15:0]
Başlangıç    : seed = 0xDEADBEEF
```

Kullanım: `RAND RD` — yalnızca Machine modunda

### 6.2 AES SubBytes Hızlandırıcı (AESE)

Tam FIPS-197 S-Box (256 × 8-bit ROM), 16-bit kelimenin her baytına ayrı ayrı uygulanır:

```
AESE RD, RS1:
    RD[7:0]  = S-Box( RS1[7:0]  )
    RD[15:8] = S-Box( RS1[15:8] )
```

Tek saat döngüsünde 16-bit SubBytes. Yalnızca Machine modunda.

### 6.3 SHA-256 Sıkıştırma Adımı (HASH)

16-bit uyarlamalı SHA-256 Σ0 + Maj bileşimi:

```
HASH RD, RS1, RS2:
    S0  = ROTR2(RS1) ⊕ ROTR7(RS1) ⊕ ROTR13(RS1)
    Maj = (RS1[15:8] & RS1[7:0]) ⊕ (RS1[15:8] & RS2[15:8]) ⊕ (RS1[7:0] & RS2[7:0])
    RD  = S0 + {Maj, Maj}
```

Yalnızca Machine modunda.

### 6.4 FENCE

Bellek/IO sıralama garantisi. Simülatörde no-op. RTL'de tüm bekleyen bellek
işlemleri tamamlanana kadar pipeline ilerlemez.

---

## 7. Donanım Gerçekleme Notları

### RTL Dosyaları

| Dosya                    | İçerik                                                      |
|--------------------------|-------------------------------------------------------------|
| `cpu_synth.v`            | NOVA-16 SEC tam CPU RTL (tüm güvenlik uzantıları dahil)    |
| `fpga/nova16_sec.v`      | FPGA için yardımcı güvenlik modülleri                      |
| `fpga/nova16_top.v`      | Basys3 top-level (CPU + BRAM + 7-seg + GPIO)               |
| `fpga/nova16_bram.v`     | Dual-port BRAM (program yükleme arayüzü; ROM yoktur)       |
| `fpga/basys3.xdc`        | Xilinx Basys3 pin kısıtlamaları                            |
| `fpga/vivado_build.tcl`  | Vivado proje oluşturma ve sentez scripti                   |

### Donanım Güvenlik Gözlem Portları (cpu_synth.v)

```verilog
output wire [1:0]  dbg_priv,    // mevcut ayrıcalık seviyesi
output wire [15:0] dbg_mcause,  // son trap sebebi kodu
output wire [15:0] dbg_mepc,    // son trap PC'si
```

### Sentez Uyumluluğu

- **Verilog-2005** (IEEE 1364-2005) — `generate`, `always_ff`, `logic` yok
- **AES S-Box**: 256-durum `case` fonksiyonu → sentezde BRAM'a maplenir (~1 BRAM36)
- **MPU döngüsü**: 8-adımlı `for` döngüsü → tam unroll (~400 LUT)
- **CSR dosyası**: 32 × 16-bit flip-flop (~512 FF)
- **TRNG LFSR**: 64-bit shift register + 4-input XOR (~68 FF + 4 LUT)
- **Branch/ERET**: tek döngüde PC güncelleme (kombinasyonel hesap)

---

## 8. Yazılım Kullanım Rehberi

### Minimal Machine-Mode Trap Handler

```asm
_start:
    LI   R1, trap_handler
    CSRW R1, 3          ; CSR[3] = MTVEC = trap_handler

    ; ... uygulama kodu ...

trap_handler:
    CSRR R1, 2          ; R1 = MCAUSE
    ; R1'e göre sebep yönlendirmesi yapılabilir
    ERET                ; MSTATUS.MPP'deki seviyeye dön
```

### User Mode'a Geçiş

```asm
    LI   R1, 0x0010     ; MSTATUS.MPP = User (2 << 3 = 0x10)
    CSRW R1, 4
    LI   R2, user_entry ; MEPC = kullanıcı giriş noktası
    CSRW R2, 1
    ERET                ; User moduna geç, user_entry'den çalışmaya başla
```

### Kriptografik İş Akışı (Machine modunda)

```asm
    RAND R1             ; R1 = TRNG (rastgele 16-bit)
    AESE R2, R1         ; R2 = SubBytes(R1)   — AES S-Box
    HASH R3, R1, R2     ; R3 = SHA_step(R1, R2)
    FENCE               ; bellek engelini uygula
```

---

## 9. Test Programları

| Dosya                | Test Kapsamı                                          |
|----------------------|-------------------------------------------------------|
| `sec_test.asm`       | CSRW/CSRR, RAND, AESE, HASH, FENCE, ECALL (M)        |
| `sec_trap_test.asm`  | ERET, ayrıcalık geçişi, Supervisor ECALL trap, CAUSE  |

Derleme ve çalıştırma:
```sh
./asm sec_test.asm sec_test.bin && ./sim sec_test.bin
./asm sec_trap_test.asm sec_trap_test.bin && ./sim sec_trap_test.bin
```

---

## 10. Güvenlik Özellikleri Özeti

| Özellik                   | Durum | Detay                                               |
|---------------------------|-------|-----------------------------------------------------|
| 3 ayrıcalık seviyesi      | ✓     | M/S/U, reset → Machine                             |
| 8 bölgeli MPU             | ✓     | BASE/MASK/PERM, Machine-only yapılandırma           |
| CSR dosyası (32 giriş)    | ✓     | Ayrıcalık kontrollü okuma/yazma                     |
| ECALL / ERET              | ✓     | Tam trap + dönüş mekanizması, tüm seviyeler         |
| TRNG                      | ✓     | 64-bit Fibonacci LFSR, Machine-only                 |
| AES SubBytes (AESE)       | ✓     | Tam FIPS-197 S-Box, 16-bit, Machine-only            |
| SHA-256 adımı (HASH)      | ✓     | 16-bit uyarlamalı Σ0+Maj, Machine-only              |
| Bellek engeli (FENCE)     | ✓     | Yazılım/donanım sıralama sinyali                    |
| I/O ayrıcalık koruması    | ✓     | User moddan OUT/IN → CAUSE_IO_PRIV trap             |
| Donanım trap mekanizması  | ✓     | MEPC/MCAUSE/MSTATUS/MTVEC tam zincir               |
| Güvenlik gözlem portları  | ✓     | dbg_priv, dbg_mcause, dbg_mepc (sentez çıkışı)     |
| EEPROM/UART ayrıcalık     | ✓     | ELOAD/ESTORE ayrıcalık kontrolü gerektirmez (veri deposu); UART portları (0xFE/0xFF) normal OUT/IN yolundan geçtiği için User moddan erişim yine CAUSE_IO_PRIV üretir |
| FLAGS registerı           | ✓     | Ayrıcalık kontrolü gerektirmez; CSR alanından bağımsız, RFLAGS ile herhangi bir modda okunur |

### 10.1 EEPROM ve UART Not

`ELOAD`/`ESTORE` (0x1C/0x1D) `mem[]`'den ayrı, MPU kapsamı dışında bir
depolama alanına erişir; yeni bir trap nedeni veya ayrıcalık deliği
oluşturmaz. UART (port 0xFE/0xFF), mevcut `OUT`/`IN` komutlarının bir
özel durumu olarak uygulanmıştır, bu yüzden mevcut `CAUSE_IO_PRIV`
korumasını aynen miras alır — User modda UART'a erişim de trap üretir.

### 10.2 FLAGS Notu

FLAGS (Z/C/V/N) registerı salt gözlem amaçlıdır — hiçbir komutun
davranışını (dallanma, trap vb.) etkilemez, sadece `RFLAGS` ile okunabilir.
Bu yüzden ayrıcalık modeline dahil değildir ve yeni bir MCAUSE kodu veya
trap yolu gerektirmez.
