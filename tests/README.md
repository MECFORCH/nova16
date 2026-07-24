# NOVA-64 Regression Tests

Bu klasördeki her `*.asm` dosyası, eşleşen bir `*.expected` dosyasıyla
birlikte otomatik test çalıştırıcısı (`run_tests.sh`) tarafından
derlenir (`asm`), çalıştırılır (`sim -q`) ve çıktısı kontrol edilir.

`*.expected` dosyası, `sim` çıktısında **alt dize (substring) olarak**
bulunması gereken satırları içerir (regex değil, düz metin eşleşmesi).
Boş satırlar yoksayılır.

## Çalıştırma

```bash
make test
# veya doğrudan:
./tests/run_tests.sh
```

## Yeni test ekleme

1. `tests/isim.asm` dosyasını yazın.
2. `./asm tests/isim.asm /tmp/x.bin && ./sim /tmp/x.bin -q` ile beklenen
   çıktıyı görün.
3. Çıktıdan doğrulamak istediğiniz satırları `tests/isim.expected`
   dosyasına kopyalayın.

## Kapsam dışı testler

Repo kökündeki `clock_test.asm`, `gpio_irq_test.asm`, `irq_timer_test.asm`,
`sec_test.asm`, `sec_trap_test.asm`, `supervisor_irq_test.asm` gibi
programlar kesme/GPIO/CSR senaryolarını `sim`'e özel `-g` bayraklarıyla
manuel olarak tetikleyerek test eder; bu otomatik koşucunun kapsamı
dışındadır (bkz. her dosyanın başındaki yorum bloğu için çalıştırma
talimatları).
