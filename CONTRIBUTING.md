# Katkıda Bulunma

NOVA-64 projesine katkı sağladığınız için teşekkürler! Bu doküman,
katkı sürecini hızlandırmak için temel kuralları özetler.

## Geliştirme Ortamı

Gerekli araçlar:

- `gcc` (veya uyumlu bir C derleyicisi)
- `make`
- `iverilog` (Verilog RTL testleri için, isteğe bağlı ama önerilir)

```bash
make            # C araç zincirini derle (sim, asm, dbg, hexdump)
make test       # regresyon testlerini çalıştır
make sim-verilog# cpu.v testbench'ini çalıştır
```

## Tutarlılık Kuralı: sim.c / asm.c / dbg.c / cpu.v

Bu dört dosya **aynı** komut kodlamasını, opcode tablosunu ve kelime
genişliğini (32-bit) paylaşır. Birini değiştirirken:

1. `SPEC.md`'deki opcode tablosunu güncelleyin.
2. Diğer üç dosyanın aynı opcode/kodlamayı kullandığından emin olun.
3. `make test` ve `make sim-verilog` ile hem simülatör hem RTL'i doğrulayın.

`cpu.v`, basitleştirilmiş bir RTL'dir: CSR/kesme/MPU gibi `sim.c`'nin
sahip olduğu gelişmiş özellikleri henüz içermez (bkz. `SPEC.md`). Bu iki
katman arasında **temel ISA** (ADD/SUB/.../HALT, LOAD/STORE, dallanma,
CALL/RET, IN/OUT) her zaman birebir uyumlu olmalıdır.

## Pull Request Süreci

1. Değişikliğinizi küçük ve odaklı tutun.
2. `make test` ve mümkünse `make sim-verilog` çalıştırıp geçtiğinden
   emin olun.
3. Davranış değiştiren her değişiklik için `tests/` altına bir regresyon
   testi ekleyin (bkz. `tests/README.md`).
4. Yaptığınız değişikliği ve nedenini açıklayan kısa bir PR açıklaması
   yazın.

## Hata Bildirimi

Bir hata bulduğunuzda lütfen şunları paylaşın:

- Kullandığınız komut (`./sim ... ` veya `./asm ...`)
- Beklenen ve gerçek çıktı
- Mümkünse en küçük tekrar üretebilen `.asm` dosyası

## Lisans

Katkılarınız [MIT Lisansı](../LICENSE) altında lisanslanacaktır.
