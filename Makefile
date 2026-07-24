# ============================================================
# NOVA-32 Toolchain — Makefile
#
# Hedefler:
#   make            — sim, asm, dbg, hexdump derle
#   make test       — regresyon testlerini çalıştır (tests/run_tests.sh)
#   make sim-verilog— cpu.v testbench'ini iverilog ile derle ve çalıştır
#   make synth-check— Yosys ile sentez smoke-test (yosys gerekli)
#   make formal     — Yosys SAT ile formal doğrulama (yosys gerekli)
#   make clean      — üretilen ikili dosyaları sil
# ============================================================

CC      ?= gcc
CFLAGS  ?= -O2 -Wall

TOOLS   := sim asm dbg hexdump

.PHONY: all test sim-verilog synth-check formal clean

all: $(TOOLS)

sim: sim.c gdb_stub.c gdb_stub.h
	$(CC) $(CFLAGS) -o $@ sim.c gdb_stub.c

asm: asm.c
	$(CC) $(CFLAGS) -o $@ $<

dbg: dbg.c
	$(CC) $(CFLAGS) -o $@ $<

hexdump: hexdump.c
	$(CC) $(CFLAGS) -o $@ $<

test: sim asm
	./tests/run_tests.sh

sim-verilog: cpu.v
	iverilog -o /tmp/nova32_cpu_tb cpu.v -DTESTBENCH
	vvp /tmp/nova32_cpu_tb

# ── Sentez smoke-test (Yosys) ─────────────────────────────
# Yosys generic sentezi çalıştırır; RTL derleme hatası varsa başarısız olur.
# Kurulum: sudo apt install yosys
synth-check: cpu.v
	@echo "==> Yosys sentez kontrolü başlıyor..."
	yosys -Q -p \
	  "read_verilog cpu.v; \
	   synth -top nova32_cpu -flatten; \
	   stat" 2>&1 | tee /tmp/nova32_synth.log
	@grep -q "Number of cells" /tmp/nova32_synth.log && \
	  echo "==> Sentez GECTI" || \
	  (echo "==> Sentez BASARISIZ"; exit 1)

# ── Formal doğrulama (Yosys SAT, sınırlı model kontrolü) ─
# P1: R0 her zaman 0
# P2: halted yapışkandır
# P3: cycles monoton artar
# P4: state geçerli aralıkta (0-4)
# Kurulum: sudo apt install yosys
formal: cpu.v formal/nova32_props.v
	@echo "==> Yosys formal doğrulama başlıyor (depth=10)..."
	yosys -Q -p \
	  "read_verilog -formal cpu.v formal/nova32_props.v; \
	   prep -top nova32_cpu_formal; \
	   flatten; \
	   async2sync; \
	   sat -prove-asserts -seq 10 -show-inputs -show-outputs" 2>&1 \
	  | tee /tmp/nova32_formal.log
	@grep -q "SAT proof finished" /tmp/nova32_formal.log && \
	  echo "==> Formal doğrulama GECTI" || \
	  (echo "==> Formal doğrulama BASARISIZ"; exit 1)

clean:
	rm -f $(TOOLS) *.o /tmp/nova32_test_*.bin nova32_wave.vcd \
	      /tmp/nova32_synth.log /tmp/nova32_formal.log \
	      /tmp/nova32_cpu_tb
