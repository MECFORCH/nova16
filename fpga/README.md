# NOVA-16 CPU — Basys3 FPGA Projesi

## Kart: Digilent Basys3 (Xilinx Artix-7 XC7A35T-1CPG236C)

---

## Donanım Haritası

| Fiziksel | Sinyal | İşlev |
|---|---|---|
| W5 (CLK) | `clk_100mhz` | 100 MHz sistem saati |
| U18 (BTNC) | `btnC` | CPU reset (bas → bırak) |
| LED[15:0] | `port[0]` | Hesaplama sonucu (bit bit) |
| LED[15] | `halted` | HALT sonrası yanıp söner |
| SW[15:0] | `port[1]` girişi | Runtime veri girişi |
| 7-Seg | `port[0]` | Sonucu hex gösterir |

## Yüklü Program: İnteraktif Toplama (interactive_sum.asm)

SW switch'leri ile runtime'da N değeri seçilir, sonuç anlık görünür:

```
SW[7:0] = N  →  CPU Σ(1+2+...+N) hesaplar  →  7-Seg + LED
```

| SW[7:0] | Beklenen Sonuç | 7-Seg |
|---|---|---|
| `00001010` (10) | 55 | `0037` |
| `00000101` (5) | 15 | `000F` |
| `01100100` (100) | 5050 | `13BA` |
| `00000000` (0) | 0 | `0000` |

BTNC'ye basıp bıraktıktan sonra:
- **SW[7:0]** ile N değerini ayarla
- **7-Segment:** Σ(1..N) sonucunu anlık gösterir
- **LED[15:0]:** Sonucu bit bit gösterir
- Switch değiştiğinde sonuç **otomatik güncellenir** (HALT yok)

Simülatörde test:
```bash
cd nova16
./sim interactive_sum.bin -p 1 10 -c 500     # N=10  → port[0]=55
./sim interactive_sum.bin -p 1 100 -c 50000  # N=100 → port[0]=5050
```

---

## Derleme — Vivado ile

### Gereksinimler
- Xilinx Vivado 2020.x veya üzeri (ücretsiz WebPACK sürümü yeterli)
- Digilent Basys3 kartı
- Micro-USB kablosu

### Adımlar

**1. Vivado'yu aç:**
```
vivado
```

**2. Tcl Console'da build scriptini çalıştır:**
```tcl
cd /yol/nova16/fpga
source vivado_build.tcl
```

**Ya da komut satırından:**
```bash
vivado -mode batch -source vivado_build.tcl
```

**3. Bitstream karta yükle:**
```tcl
open_hw_manager
connect_hw_server
open_hw_target
set_property PROGRAM.FILE {nova16_fpga/nova16_fpga.runs/impl_1/nova16_top.bit} [get_hw_devices xc7a35t_0]
program_hw_devices [get_hw_devices xc7a35t_0]
```

---

## Kendi Programını Yüklemek

1. `asm.c` ile kendi `.asm` dosyandan `.bin` üret:
   ```bash
   cd nova16
   ./asm benim_programim.asm benim.bin
   ```

2. `nova16_bram.v` içindeki `initial begin ... end` bloğunu yeni programınla güncelle.

3. Vivado'da "Generate Bitstream"i yeniden çalıştır.

---

## Dosya Yapısı

```
nova16/fpga/
├── nova16_top.v      ← Üst modül (CPU + BRAM + LED + 7-seg bağlantısı)
├── nova16_bram.v     ← 64K × 16-bit Block RAM (test programı yüklü)
├── seg7_ctrl.v       ← 7-segment display kontrolcüsü (4 hex basamak)
├── basys3.xdc        ← Pin kısıtlamaları (tüm I/O atamaları)
├── vivado_build.tcl  ← Otomatik build scripti
└── program.mem       ← Test programı (referans hex dosyası)

nova16/
├── cpu_synth.v       ← CPU RTL (senteze hazır Verilog-2005)
├── SPEC.md           ← ISA spesifikasyonu
├── sim.c / sim       ← C simülatörü
├── asm.c / asm       ← Assembler
└── dbg.c / dbg       ← İnteraktif debugger
```

---

## Kaynak Kullanımı (Tahmini)

Yosys sentezinden elde edilen değerler:

| Kaynak | Kullanılan | Basys3 Kapasitesi |
|---|---|---|
| LUT | ~1800 | 20800 |
| FF (Flip-Flop) | ~271 | 41600 |
| BRAM (18Kb) | 2 | 50 |
| IO | 36 | 106 |

→ Basys3 kapasitesinin **%9**'u kullanılıyor.  
→ Kalan kapasite ile ikinci çekirdek eklenebilir!

---

## Saat Hızı (Tahmini)

Yosys/ABC optimizasyonu sonrası ~150 MHz kritik yol tahmini.  
Tasarım 100 MHz'de rahatlıkla çalışır.

Her komut ~3 saat döngüsü (FETCH1 → FETCH2 → EXECUTE):
- 100 MHz → ~33 MIPS efektif hız
