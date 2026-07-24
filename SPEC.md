# NOVA-64 ISA Spesifikasyonu

**Mimari Adı:** NOVA-64  
**Veri Yolu:** 64-bit  
**Komut Uzunluğu:** Sabit 32-bit (her komut tek kelime = tam 4 byte)  
**Register Sayısı:** 32 adet (R0–R31)  
**Bellek:** 65536 adet 64-bit kelime (word-addressed, 16-bit adres yolu, 512 KB)  
**Byte Düzeni:** Big-endian

---

## Register Tablosu

| Register  | Kullanım                        | Not                             |
|-----------|---------------------------------|---------------------------------|
| R0        | Hardwired Zero                  | Her zaman 0, yazılamaz          |
| R1–R6     | Genel amaçlı                    |                                 |
| R7        | Dönüş değeri / genel            |                                 |
| R8–R28    | Genel amaçlı                    |                                 |
| R29       | Frame Pointer (FP) — önerilir   |                                 |
| R30       | Link Register (LR) — önerilir   | CALL dönüş adresi için          |
| R31       | Stack Pointer (SP) — önerilir   | CALL/RET tarafından kullanılır  |

---

## Komut Formatı (32-bit Sabit)

```
 31      26 25      21 20      16 15      11 10           0
 ┌────────┬──────────┬──────────┬──────────┬─────────────┐
 │ OPCODE │    FD    │    FA    │    FB    │     IMM      │
 │  6 bit │  5 bit   │  5 bit   │  5 bit   │   11 bit    │
 └────────┴──────────┴──────────┴──────────┴─────────────┘
```

| Alan   | Bit Aralığı | Açıklama                                             |
|--------|-------------|------------------------------------------------------|
| OPCODE | [31:26]     | İşlem kodu (6 bit, 64 farklı komut)                 |
| FD     | [25:21]     | Hedef / ilk kaynak register (5 bit → R0–R31)        |
| FA     | [20:16]     | Birinci kaynak register (5 bit)                     |
| FB     | [15:11]     | İkinci kaynak register (5 bit)                      |
| IMM    | [10:0]      | 11-bit işaretli immediate / ofset (sign-extended)   |

**IMM işaret uzatma:** Bit 10 işaret bitidir. Bit 10 = 1 ise değer negatiftir.  
**Aralık:** −1024 … +1023 (11-bit two's complement, 64-bit register genişliğine işaret uzatılır)

**Not — STORE için alan yorumu:**
```
STORE RS1, RS2, IMM   ; mem[RS2 + IMM] = RS1
  FD  alanı → RS1 (veri kaynağı)
  FA  alanı → RS2 (adres tabanı)
  FB  alanı → kullanılmaz (0)
```

---

## Opcode Tablosu

| Opcode (hex) | Opcode (bin) | Mnemonik | Format                        |
|:------------:|:------------:|----------|-------------------------------|
| 0x00         | 000000       | NOP      | —                             |
| 0x01         | 000001       | ADD      | RD, RS1, RS2                  |
| 0x02         | 000010       | SUB      | RD, RS1, RS2                  |
| 0x03         | 000011       | AND      | RD, RS1, RS2                  |
| 0x04         | 000100       | OR       | RD, RS1, RS2                  |
| 0x05         | 000101       | XOR      | RD, RS1, RS2                  |
| 0x06         | 000110       | SHL      | RD, RS1, IMM                  |
| 0x07         | 000111       | SHR      | RD, RS1, IMM                  |
| 0x08         | 001000       | LOAD     | RD, RS1, IMM                  |
| 0x09         | 001001       | STORE    | RS1, RS2, IMM                 |
| 0x0A         | 001010       | LI       | RD, IMM                       |
| 0x0B         | 001011       | JMP      | IMM                           |
| 0x0C         | 001100       | JZ       | RS1, IMM                      |
| 0x0D         | 001101       | JNZ      | RS1, IMM                      |
| 0x0E         | 001110       | CALL     | IMM                           |
| 0x0F         | 001111       | RET      | —                             |
| 0x10         | 010000       | OUT      | RS1, IMM                      |
| 0x11         | 010001       | IN       | RD, IMM                       |
| 0x1A         | 011010       | MUL      | RD, RS1, RS2                  |
| 0x1B         | 011011       | DIV      | RD, RS1, RS2                  |
| 0x1C         | 011100       | ELOAD    | RD, RS1, IMM                  |
| 0x1D         | 011101       | ESTORE   | RS1, RS2, IMM                 |
| 0x1E         | 011110       | RFLAGS   | RD                            |
| 0x3F         | 111111       | HALT     | —                             |

> Not: 0x12–0x19 arası opcode'lar NOVA-16 SEC güvenlik uzantısına ayrılmıştır (bkz. `SECURITY.md`).
> Not: 0x1F ve sonrası gelecekteki uzantılar için ayrılmıştır.

---

## Komut Açıklamaları

### Aritmetik / Mantık

#### ADD — Toplama
```
Kodlama: [000001][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 + RS2  (32-bit, sonuç kesilir — bkz. FLAGS)
```

#### SUB — Çıkarma
```
Kodlama: [000010][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 - RS2  (32-bit, sonuç kesilir — bkz. FLAGS)
```

#### AND — Bit düzeyinde VE
```
Kodlama: [000011][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 & RS2
```

#### OR — Bit düzeyinde VEYA
```
Kodlama: [000100][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 | RS2
```

#### XOR — Bit düzeyinde DIŞLAYAN VEYA
```
Kodlama: [000101][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 ^ RS2
```

#### SHL — Sola Kaydır
```
Kodlama: [000110][RD][RS1][000][IMM(11-bit)]
İşlem  : RD = RS1 << (IMM & 0x1F)
```

#### SHR — Sağa Kaydır (mantıksal, sıfır doldurur)
```
Kodlama: [000111][RD][RS1][000][IMM(11-bit)]
İşlem  : RD = RS1 >> (IMM & 0x1F)  (unsigned)
```

#### MUL — Çarpma
```
Kodlama: [011010][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 * RS2  (32-bit, sonuç kesilir — üst 32 bit atılır, bkz. FLAGS)
```

#### DIV — Bölme (unsigned)
```
Kodlama: [011011][RD][RS1][RS2][00000000000000000]
İşlem  : RD = RS1 / RS2  (unsigned tam sayı bölme)
Not    : RS2 == 0 ise CAUSE_DIV_ZERO (10) trap üretir. RESILIENCE modu
         (-r) açıkken bu trap otomatik atlanır ve çalışma devam eder
         (bkz. RESILIENCE bölümü). DIV FLAGS'i değiştirmez.
```

---

### FLAGS (Bayrak) Registerı

`ADD`, `SUB` ve `MUL` komutlarından sonra 4 bitlik bir FLAGS registerı
otomatik güncellenir (diğer komutlar FLAGS'e dokunmaz). CSR alanından
ayrıdır; okumak için `RFLAGS` komutu kullanılır.

| Bit | Ad | Anlamı                                                                 |
|:---:|:--:|--------------------------------------------------------------------------|
| 0   | Z  | Zero — sonuç 0 ise 1                                                     |
| 1   | C  | Carry — unsigned taşma (ADD/MUL) veya unsigned underflow/borç (SUB)      |
| 2   | V  | Overflow — signed taşma/underflow (sonuç -2147483648..2147483647 aralığı dışında, ya da MUL 32-bit'e sığmadıysa) |
| 3   | N  | Negative — sonucun bit31'i (işaretli yorumda negatif)                    |

Örnek: `0xFFFFFFFF + 1` → sonuç `0x00000000`, Z=1 C=1 V=0 N=0 (unsigned taşma var, signed taşma yok).  
Örnek: `5 - 10` → sonuç `0xFFFFFFFB` (-5), C=1 (borç) N=1.  
Örnek: `2000000000 + 2000000000` → sonuç `0xEE6B2800`, unsigned sığar (C=0), signed taşar (V=1), N=1.

#### RFLAGS — FLAGS'i Oku
```
Kodlama: [011110][RD][000][000][00000000000000000]
İşlem  : RD = FLAGS & 0x000F   (sadece Z/C/V/N bitleri, üst bitler 0)
```

---

### Bellek

#### LOAD — Bellekten Yükle
```
Kodlama: [001000][RD][RS1][000][IMM(11-bit)]
İşlem  : RD = mem[RS1 + IMM]    (word adresi)
```

#### STORE — Belleğe Yaz
```
Kodlama: [001001][RS1][RS2][000][IMM(11-bit)]
İşlem  : mem[RS2 + IMM] = RS1   (word adresi)
Not    : FD alanına RS1, FA alanına RS2 kodlanır
```

---

### Kalıcı Bellek (EEPROM/Flash)

NOVA-16, `mem[]`'den tamamen ayrı, **4096 kelimelik** bir EEPROM/flash
alanına sahiptir. Bu alan `cpu_reset()`'ten etkilenmez ve simülatör
süreci kapanıp yeniden başlatılsa bile diskteki bir dosya aracılığıyla
korunur — yani gerçek bir EEPROM/flash çipinin davranışını taklit eder.

- Varsayılan dosya: `nova16_eeprom.bin` (çalışma dizininde)
- `-e <dosya>` bayrağı ile özel bir yol belirtilebilir
- Program başlamadan önce dosyadan otomatik yüklenir (dosya yoksa
  sıfırlanmış EEPROM ile başlanır — hata değildir)
- Simülasyon sonunda, en az bir ESTORE yapıldıysa diske otomatik yazılır

#### ELOAD — EEPROM'dan Yükle
```
Kodlama: [011100][RD][RS1][000][IMM(11-bit)]
İşlem  : RD = eeprom[(RS1 + IMM) % 4096]
```

#### ESTORE — EEPROM'a Yaz
```
Kodlama: [011101][RS1][RS2][000][IMM(11-bit)]
İşlem  : eeprom[(RS2 + IMM) % 4096] = RS1
Not    : FD alanına RS1 (veri), FA alanına RS2 (adres) kodlanır — STORE ile aynı şekil.
         Ayrıcalık kontrolü yoktur (MPU/SEC kapsamı dışında); herhangi bir
         modda kullanılabilir.
```

---

### Immediate

#### LI — Anlık Değer Yükle
```
Kodlama: [001010][RD][000][000][IMM(11-bit)]
İşlem  : RD = sign_extend11(IMM)   (11-bit işaretli, 32-bit register genişliğine uzatılır)
Not    : −65536 … +65535 aralığındaki herhangi bir değer yüklenebilir.
         Değer negatif ise IMM bit16=1 yapılır.
```

---

### Dal ve Atlama

Tüm dal komutlarında **IMM**, `fetch` sonrası PC'ye göre göreli kelime ofesetidir.  
Yani: yeni PC = (PC'nin fetch sonrası değeri) + IMM

Assembler, label kullanımında IMM'yi otomatik hesaplar:  
`IMM = hedef_kelime_adresi − (komut_kelime_adresi + 2)`

#### JMP — Koşulsuz Atlama
```
Kodlama: [001011][000][000][000][IMM(11-bit)]
İşlem  : PC = PC + IMM   (göreli, word birimi)
```

#### JZ — Sıfırsa Atla
```
Kodlama: [001100][RS1][000][000][IMM(11-bit)]
İşlem  : if (RS1 == 0) PC = PC + IMM
Not    : RS1, FD alanına kodlanır
```

#### JNZ — Sıfır Değilse Atla
```
Kodlama: [001101][RS1][000][000][IMM(11-bit)]
İşlem  : if (RS1 != 0) PC = PC + IMM
Not    : RS1, FD alanına kodlanır
```

---

### Alt Program

R7 register'ı Stack Pointer olarak kullanılır (aşağı büyüyen yığın).  
CALL/RET, R7'yi doğrudan günceller.

#### CALL — Alt Program Çağrısı
```
Kodlama: [001110][000][000][000][IMM(11-bit)]
İşlem  : R7 = R7 - 1
          mem[R7] = PC   (dönüş adresi = sonraki komutun kelime adresi)
          PC = PC + IMM
```

#### RET — Alt Programdan Dön
```
Kodlama: [001111][000][000][000][00000000000000000]
İşlem  : PC = mem[R7]
          R7 = R7 + 1
```

---

### Giriş / Çıkış

256 adet 32-bit G/Ç portu bulunur (port[0] … port[255]).

#### OUT — Porta Yaz
```
Kodlama: [010000][RS1][000][000][IMM(11-bit)]
İşlem  : port[IMM & 0xFF] = RS1
Not    : RS1, FD alanına kodlanır
```

#### IN — Porttan Oku
```
Kodlama: [010001][RD][000][000][IMM(11-bit)]
İşlem  : RD = port[IMM & 0xFF]
```

#### UART (Seri Port) — port 0xFE / 0xFF

İki port özel olarak gerçek karakter G/Ç için ayrılmıştır:

| Port  | Yön | Anlamı                                                        |
|:-----:|-----|----------------------------------------------------------------|
| 0xFE  | TX  | `OUT R, 0xFE` → R'nin alt 8 biti stdout'a ham ASCII olarak yazılır |
| 0xFF  | RX  | `IN R, 0xFF`  → stdin'den bir karakter okunur; dosya sonu (EOF) ise R = 0xFFFFFFFF |

Bu iki port normal `io_ports[]` dizisine de yazılır/okunur (durum takip
amaçlı), fakat `OUT`/`IN` çalıştığında ek olarak gerçek stdout/stdin
etkileşimi tetiklenir. Diğer 254 port yalnızca durum tutan sanal
G/Ç portlarıdır — gerçek dünya etkisi yoktur.

---

### Sistem

#### NOP — İşlem Yok
```
Kodlama: [000000][000][000][000][00000000000000000]
İşlem  : —
```

#### HALT — Dur
```
Kodlama: [111111][000][000][000][00000000000000000]
İşlem  : CPU durur
```

---

## Ayrıcalık Modları

NOVA-16 üç ayrıcalık seviyesi destekler:

| Mod        | PRIV Değeri | Açıklama                                                    |
|------------|:-----------:|-------------------------------------------------------------|
| Machine    | 2           | Tam erişim. Başlangıç modu. Donanım trap'leri buraya gelir. |
| Supervisor | 1           | İşletim sistemi çekirdeği için. CSR[5–7] ve SIE erişimi.   |
| User       | 0           | Uygulama kodu. Ayrıcalıklı CSR/G/Ç erişimi trap üretir.   |

Mevcut mod `CSR[0]` (CSR_PRIV) içinde okunabilir.

### Mod Geçişleri

| Komut | Kaynak → Hedef                    | Açıklama                                       |
|-------|-----------------------------------|------------------------------------------------|
| ERET  | Machine → (MPP'ye bak)            | MRET: MEPC'e döner, priv=MPP, MIE←MPIE        |
| ERET  | Supervisor → (SPP'ye bak)         | SRET: SEPC'e döner, priv=SPP, SIE←SPIE        |
| TRAP  | Herhangi → Machine (veya S-moda delegasyon) | Donanım/yazılım istisnası           |

---

## CSR (Kontrol ve Durum Registerları)

CSR erişimi `CSRW` (yaz) ve `CSRR` (oku) komutlarıyla yapılır. IMM alanının alt 6 biti CSR indeksini seçer (0–63 arası adreslenebilir).

### Erişim Hakları

| Erişim Seviyesi | Anlamı                                              |
|-----------------|-----------------------------------------------------|
| Machine-only    | Sadece Machine modundan okunup yazılabilir          |
| S-veya-üstü     | Machine ve Supervisor modundan erişilebilir         |
| Salt okunur     | Yazma girişimi sessizce yoksayılır                  |

### CSR Tablosu

| İndeks | Sembol     | Erişim       | Açıklama                                                       |
|:------:|------------|--------------|----------------------------------------------------------------|
| 0      | CSR_PRIV   | Salt okunur  | Mevcut ayrıcalık modu (0=U, 1=S, 2=M)                        |
| 1      | MEPC       | Machine-only | Machine Exception PC — trap/kesme anındaki PC                  |
| 2      | MCAUSE     | Salt okunur  | Machine Cause — son trap/kesme nedeni (bkz. Cause Tablosu)     |
| 3      | MTVEC      | Machine-only | Machine Trap Vector — M-modu trap/ISR adresi                   |
| 4      | MSTATUS    | Machine-only | Machine Status (bkz. MSTATUS Bitleri)                          |
| 5      | SEPC       | S-veya-üstü  | Supervisor Exception PC                                        |
| 6      | STVEC      | S-veya-üstü  | Supervisor Trap Vector — S-modu ISR adresi                     |
| 7      | SATP       | S-veya-üstü  | Supervisor Address Translation (gelecek kullanım)              |
| 8      | MPU_BASE   | Machine-only | MPU bölge tabanı (güvenlik uzantısı, SECURITY.md bkz.)        |
| 9      | MPU_LIMIT  | Machine-only | MPU bölge limiti                                               |
| 10     | MPU_FLAGS  | Machine-only | MPU izinleri                                                   |
| …      | …          | …            | 11–31: SEC/MPU uzantısı (SECURITY.md)                          |
| 32     | MIE        | Machine-only | Machine Interrupt Enable — bit maskesi (bit0=timer, bit1=ext) |
| 33     | MIP        | Machine-only | Machine Interrupt Pending — bit maskesi (salt okunur etkin)   |
| 34     | MTIMECMP   | Machine-only | Timer karşılaştırma eşiği (MTIME >= MTIMECMP → timer IRQ)    |
| 35     | MTIME      | Salt okunur  | Cycle sayacı alt 16 bit (tüm simülasyon cycle'larını yansıtır)|
| 36     | MTIME_HI   | Salt okunur  | Cycle sayacı üst 16 bit                                        |
| 37     | MIDELEG    | Machine-only | Machine Interrupt Delegation — bit N=1 → IRQ N S-moda devret |
| 38     | MEDELEG    | Machine-only | Machine Exception Delegation — bit N=1 → İstisna N S-moda devret|
| 39     | SIE        | S-veya-üstü  | Supervisor Interrupt Enable (bit0=timer, bit1=ext)            |
| 40     | SCAUSE     | Salt okunur  | Supervisor Cause — S-modu trap nedeni                          |

### MSTATUS Bitleri

| Bit   | Ad   | Açıklama                                                         |
|:-----:|------|------------------------------------------------------------------|
| 0     | MIE  | Machine Global Interrupt Enable                                  |
| 1     | MPIE | Machine Previous Interrupt Enable (MIE'nin kaydı)               |
| 4:3   | MPP  | Machine Previous Privilege (MRET'te geri yüklenecek mod)        |
| 5     | SIE  | Supervisor Global Interrupt Enable                               |
| 6     | SPIE | Supervisor Previous Interrupt Enable (SIE'nin kaydı)            |
| 7     | SPP  | Supervisor Previous Privilege (SRET'te geri yüklenecek mod)     |

### Cause Tablosu (MCAUSE / SCAUSE)

Bit 15 (`0x8000`) set ise kesme (asenkron), sıfır ise istisna (senkron).

| Değer  | Anlamı                                        |
|:------:|-----------------------------------------------|
| 0x8001 | Asenkron: Timer kesmesi (MTIME >= MTIMECMP)   |
| 0x8002 | Asenkron: Harici/GPIO kesmesi (MIP_EXT)       |
| 0      | Senkron: İllegal komut (CAUSE_ILL_INSN)       |
| 5      | Senkron: ECALL — User moddan                  |
| 6      | Senkron: ECALL — Supervisor moddan            |
| 7      | Senkron: ECALL — Machine moddan               |
| 8      | Senkron: CSR ayrıcalık ihlali                 |
| 9      | Senkron: G/Ç ayrıcalık ihlali                |
| 10     | Senkron: Sıfıra bölme                         |
| 11     | Senkron: ERET ayrıcalık ihlali                |
| 3–4,12–15 | MPU erişim ihlalleri (SECURITY.md bkz.)   |

---

## Donanım Kesmeleri (Interrupt)

### Kesme Kaynakları

| Kaynak        | MIP Biti | MIE Biti | Açıklama                                                  |
|---------------|:--------:|:--------:|-----------------------------------------------------------|
| Timer         | bit 0    | bit 0    | `MTIME >= MTIMECMP` koşulu her cycle'da kontrol edilir   |
| Harici / GPIO | bit 1    | bit 1    | GPIO pini değişimi veya `-i` bayrağıyla manuel tetikleme |

### Machine Modu Kesme Akışı

1. `MIE[n]` set **ve** `MSTATUS.MIE` set **ve** `MIP[n]` set
2. `MEPC ← PC`, `MCAUSE ← 0x8001/0x8002`
3. `MSTATUS.MPIE ← MSTATUS.MIE`, `MSTATUS.MIE ← 0`
4. `MSTATUS.MPP ← mevcut priv`, `priv ← Machine`
5. `PC ← MTVEC`

### Supervisor Modu Kesme Delegasyonu (MIDELEG)

`MIDELEG[n]` set ise n. kesme S-moda devredilir:

1. `SIE[n]` set **ve** `MSTATUS.SIE` set **ve** `MIP[n]` set **ve** `MIDELEG[n]` set
2. `SEPC ← PC`, `SCAUSE ← 0x8001/0x8002`
3. `MSTATUS.SPIE ← MSTATUS.SIE`, `MSTATUS.SIE ← 0`
4. `MSTATUS.SPP ← mevcut priv`, `priv ← Supervisor`
5. `PC ← STVEC`

### Supervisor Modu İstisna Delegasyonu (MEDELEG)

`MEDELEG[n]` set ise n. senkron istisna S-moda devredilir (interrupt bit'i yoktur, yani cause < 16):

Akış yukarıdaki S-modu kesme akışıyla aynı (SEPC/SCAUSE/STVEC kullanılır).

### İstisna İşleyiciden Dönüş (ERET)

- **Machine modunda ERET (MRET):** `PC ← MEPC`, `priv ← MPP`, `MIE ← MPIE`, `MPIE ← 1`, `MPP ← 0`
- **Supervisor modunda ERET (SRET):** `PC ← SEPC`, `priv ← SPP`, `SIE ← SPIE`, `SPIE ← 1`, `SPP ← 0`

---

## GPIO Donanımı

### Portlar

| Port   | Ad          | Yön | Açıklama                                                        |
|:------:|-------------|-----|-----------------------------------------------------------------|
| 0xF0   | GPIO_DIR    | R/W | Yön: bit N=1 → pin N çıkış, 0 → giriş                         |
| 0xF1   | GPIO_VAL    | R/W | Yazma: çıkış pini değeri. Okuma: `(GPIO_VAL & DIR) \| (gpio_in & ~DIR)` |
| 0xF2   | GPIO_INT_EN | R/W | Kesme etkinleştirme: bit N=1 → pin N değişince IRQ üret        |
| 0xF3   | GPIO_INT_ST | R/W | Kesme durumu: set donanım tarafından, OUT ile yazarak temizlenir|

### GPIO Kesme Akışı

1. Dışarıdan pin değişimi (`-g <cycle> <pin> <val>` veya `-G <hex>`)
2. Pin değişimi `GPIO_INT_EN` ile örtüşüyorsa `GPIO_INT_ST[pin]` set edilir
3. `MIP.EXT` (bit 1) set edilir
4. `MIE.EXT` ve `MSTATUS.MIE` set ise Machine ISR (MTVEC) tetiklenir
5. `MIDELEG[1]` set ise S-modu ISR (STVEC) tetiklenir
6. ISR sonunda: `OUT R, 0xF3` ile `GPIO_INT_ST` temizlenir; bu `MIP.EXT`'i de sıfırlar

### Simülatör CLI Bayrakları

```bash
./sim program.bin -G 0x00FF          # Pin 0-7 başlangıçta HIGH
./sim program.bin -g 100 3 1 -g 200 5 0   # 100. cycle'da pin3=1, 200. cycle'da pin5=0
```

---

## Sistem Zamanlayıcısı (MTIME / MTIMECMP)

`MTIME` (CSR[35]) ve `MTIME_HI` (CSR[36]) salt okunur olup simülatörün toplam cycle sayacını yansıtır.

```
MTIME    = cycles & 0xFFFF    (CSR[35])
MTIME_HI = cycles >> 16       (CSR[36])
```

Timer kesme koşulu: `cycles >= (MTIMECMP)` (her cycle'da poll edilir).

`MTIMECMP = 0` ise zamanlayıcı devre dışıdır.

---

## WFI — Kesme Bekle

```
Kodlama: [100010][000][000][000][00000000000000000]
İşlem  : Pending kesme oluşana kadar cycle tüketmeden bekle
Not    : Machine ve Supervisor modda geçerli.
         User modda CAUSE_ERET_PRIV trap üretir.
```

---

## Bellek Haritası (Önerilen)

| Adres Aralığı | Kullanım                         |
|---------------|----------------------------------|
| 0x0000–0x7FFF | Program ve veri alanı            |
| 0x8000–0xFFFE | Yığın alanı (R7'den aşağı büyür, kelime adresi hâlâ 16-bit)|
| 0xFFFF        | Stack başlangıcı (R7 başlangıç, kelime adresi)  |

---

## Kodlama Örneği

```
LI R1, 42
```
- Opcode = 0x0A = 001010
- FD = R1 = 001
- FA = 000, FB = 000
- IMM = 42 = 0x0002A → bit16=0, [10:0] = 0000000000101010

```
Bit düzeni: 00101000010000000000000000101010
Hex       : 0x28400002A  → 0x2840002A
```

```
ADD R3, R1, R2
```
- Opcode = 0x01 = 000001
- FD = R3 = 011
- FA = R1 = 001
- FB = R2 = 010
- IMM = 0

```
Bit düzeni: 00000101100010100000000000000000  
Hex       : 0x05A80000... → 0x05A80000
```

---

## Assembler Söz Dizimi

```asm
; bu bir yorum satırı

baslangic:           ; etiket tanımı (label)
    LI   R1, 42      ; anlık değer yükle
    LI   R2, 8
    ADD  R3, R1, R2  ; R3 = R1 + R2
    OUT  R3, 0       ; port[0] = R3
    HALT

dongu:
    JNZ  R1, dongu   ; label ile göreli atlama
```

- Register: `R0`–`R7` (büyük/küçük harf duyarsız: `r0` de geçerli)
- Sayılar: ondalık (`42`), hex (`0x2A`), ikili (`0b101010`)
- Label: `isim:` biçiminde, sadece harfler/rakamlar/alt çizgi
- Yorum: `;` sonrası satır sonu kadar

---

## Kısıtlar ve Kurallar

1. R0'a yazma girişimleri sessizce yoksayılır.
2. Tüm aritmetik 32-bit modüler aritmetikle yapılır (taşma işareti yok).
3. Bellek erişimleri word (32-bit kelime) adresleridir (byte değil); adres yolu 16-bit'tir (65536 kelime).
4. Her komut tam 1 kelime kaplar (PC her komuttan sonra 1 artar).
5. Tanımsız opcode çalışma zamanı hatası üretir ve CPU durur (varsayılan modda).

---

## Simülatör: Performans ve Dayanıklılık (RESILIENCE)

`sim.c`, gerçek donanımın üstüne üç simülatör-seviyesi özellik ekler.
Bunlar ISA'nın bir parçası değildir — sadece `sim` programının davranışıdır.

### 2-Aşamalı Pipeline (her zaman açık)

Her `cpu_step()` çağrısı EXECUTE aşamasıyla aynı anda bir sonraki komutu
spekülatif olarak önden okur (prefetch). Bir dal/jump/call/ret/trap PC'yi
beklenmedik şekilde değiştirirse prefetch atılır ve **+1 cycle** boru hattı
cezası uygulanır — gerçekçi cycle sayımı sağlar, program semantiğini değiştirmez.

### Sessiz Mod (`-q`) ve Benchmark (`-b`)

- `-q`: `[OUT]`, `[CSRW]`, `[RAND]` gibi tüm bilgi amaçlı çıktılar bastırılır.
- `-b`: Gerçek çalışma süresi ve MIPS (milyon komut/saniye) raporlanır.

### RESILIENCE — Kendi Kendini Onarma (`-r`, `-w <N>`, `-h`)

Varsayılan olarak KAPALIDIR (mevcut davranışı bozmaz). `-r` ile açılır:

1. **Trap-tabanlı otomatik kurtarma** — Yazılım trap handler'ı (MTVEC) hiç
   kurulmamışsa ve neden `ILL_INSN` / `MPU_READ` / `MPU_WRITE` / `MPU_EXEC`
   ise, CPU durmak yerine hatalı komutu atlayıp bir sonraki komuttan devam
   eder. **Ayrıcalık/güvenlik ihlalleri (ECALL, CSR_PRIV, IO_PRIV,
   ERET_PRIV) hiçbir zaman otomatik bastırılmaz** — self-heal bir güvenlik
   açığına dönüşemez.
2. **Watchdog Timer** (`-w <N>`, varsayılan eşik 100000, `-w` kullanımı
   `-r`'yi otomatik açar) — CPU'nun tam durumu (PC + tüm registerlar) N
   ardışık adım boyunca hiç değişmezse (gerçek kilitlenme — örn. `JMP`
   kendi adresine), sadece PC ve pipeline sıfırlanır (registerlar/bellek
   korunur). Normal döngüler her adımda register/bellek değiştirdiği için
   asla yanlışlıkla tetiklenmez.
3. **Sağlık Raporu** (`-h`) — Simülasyon sonunda toplam trap sayısı,
   illegal-opcode/MPU kurtarma sayısı ve watchdog reset sayısını basar.

```bash
./sim program.bin -r -h          # self-heal + watchdog + rapor
./sim program.bin -r -w 5000 -h  # özel watchdog eşiği
```

### Kalıcı Depolama ve Seri Port (`-e <dosya>`)

- `-e <dosya>`: EEPROM/flash için özel dosya yolu belirtir (varsayılan:
  `nova16_eeprom.bin`). `ELOAD`/`ESTORE` komutlarıyla yazılan veriler bu
  dosyada saklanır ve simülatör yeniden başlatıldığında geri yüklenir.
- UART: Ayrı bir bayrak gerekmez — `OUT R, 0xFE` ve `IN R, 0xFF` doğrudan
  stdout/stdin ile etkileşir (bkz. Giriş/Çıkış bölümü).

```bash
./sim program.bin -e save.eeprom   # özel EEPROM dosyası
echo "merhaba" | ./sim program.bin # UART üzerinden stdin'den okuma
```
