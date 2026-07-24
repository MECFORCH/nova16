# NOVA-64

NOVA-64, öğrenim ve deney amaçlı, sıfırdan tasarlanmış 64-bit bir CPU
mimarisidir. Proje şunları içerir:

- **`sim.c`** — C ile yazılmış NOVA-64 simülatörü (kesmeler, CSR,
  MPU/güvenlik uzantısı, GPIO, UART dahil)
- **`asm.c`** — İki geçişli assembler (`.asm` → `.bin`)
- **`dbg.c`** — Basit adım adım hata ayıklayıcı (step debugger)
- **`cpu.v`** — Gerçek donanım hedefli Verilog RTL (`nova32_cpu`), FPGA'ya
  sentezlenebilir (bkz. `fpga/`)
- **`SPEC.md`** — Tam ISA spesifikasyonu (opcode tablosu, kodlama, FLAGS,
  bellek haritası)
- **`SECURITY.md`** — Güvenlik uzantısı (MPU, ayrıcalık modları, trap'ler)

## Mimari Özeti

- 64-bit veri yolu, 8 genel amaçlı register (R0 sabit 0, R7 önerilen SP)
- Sabit uzunluklu 32-bit komut (`OPCODE[6] | FD[3] | FA[3] | FB[3] | IMM[17]`)
- 65536 kelimelik bellek (word-addressed, 16-bit adres yolu, 512 KB)
- 256 adet 64-bit G/Ç portu

Ayrıntılar için `SPEC.md` dosyasına bakın.

## Hızlı Başlangıç

```bash
make            # sim, asm, dbg, hexdump derle
make test       # regresyon testlerini çalıştır
make sim-verilog# cpu.v RTL testbench'ini çalıştır (iverilog gerekir)

./asm test.asm program.bin
./sim program.bin
```

Adım adım hata ayıklama için:

```bash
./dbg program.bin
```

## Test Etme

`tests/` klasöründeki regresyon testleri hem assembler hem simülatörü
uçtan uca doğrular:

```bash
make test
```

Yeni test eklemek için `tests/README.md` dosyasına bakın.

## FPGA

`fpga/` klasörü, `cpu.v`'yi bir Xilinx Basys3 kartına (Artix-7) bağlayan
üst modülü (`nova16_top.v`), block RAM'i (`nova16_bram.v`) ve 7-segment
sürücüsünü içerir.

## Katkıda Bulunma

Bkz. [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Lisans

Bu proje [MIT Lisansı](../LICENSE) ile lisanslanmıştır.
