# ============================================================================
# GRAVITYON GPU API — Makefile
# ============================================================================

CC      = gcc
CFLAGS  = -std=gnu99 -O2 -Wall -Wextra -I. -lm
DBGFLAGS = -std=gnu99 -O0 -g -Wall -Wextra -I. -lm

SRC     = gravityon.c gravityon_fb.c
OBJ     = gravityon.o gravityon_fb.o

# Paylaşılan kütüphane
LIBNAME = libgravityon.a

# Örnekler
EXAMPLES = triangle cube nova_os

.PHONY: all lib examples clean test

all: lib examples

# ─── Statik kütüphane ─────────────────────────────────────────────────────
lib: $(LIBNAME)

$(LIBNAME): $(OBJ)
	ar rcs $@ $^
	@echo "[Gravityon] $(LIBNAME) oluşturuldu."

gravityon.o: gravityon.c gravityon.h
	$(CC) $(CFLAGS) -c gravityon.c -o gravityon.o

gravityon_fb.o: gravityon_fb.c gravityon_fb.h gravityon.h
	$(CC) $(CFLAGS) -c gravityon_fb.c -o gravityon_fb.o

# ─── Örnekler ─────────────────────────────────────────────────────────────
examples: $(EXAMPLES)

triangle: examples/triangle.c $(LIBNAME)
	$(CC) $(CFLAGS) $< -L. -lgravityon -o $@
	@echo "[Gravityon] 'triangle' derlendi."

cube: examples/cube.c $(LIBNAME)
	$(CC) $(CFLAGS) $< -L. -lgravityon -o $@
	@echo "[Gravityon] 'cube' derlendi."

nova_os: examples/nova_os.c $(LIBNAME)
	$(CC) $(CFLAGS) $< -L. -lgravityon -o $@
	@echo "[Gravityon] 'nova_os' derlendi."

# ─── Debug derlemesi ──────────────────────────────────────────────────────
debug: gravityon.c gravityon.h
	$(CC) $(DBGFLAGS) -c gravityon.c -o gravityon_dbg.o
	ar rcs libgravityon_dbg.a gravityon_dbg.o
	@echo "[Gravityon] Debug kütüphanesi hazır."

# ─── Hızlı test: triangle çalıştır ───────────────────────────────────────
test: triangle
	./triangle
	@echo "[Gravityon] Test tamamlandı. output_triangle.ppm / .bmp incelenebilir."

# ─── Temizlik ─────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJ) gravityon_dbg.o $(LIBNAME) libgravityon_dbg.a
	rm -f $(EXAMPLES) $(addsuffix .exe,$(EXAMPLES))
	rm -f output_*.ppm output_*.bmp
	@echo "[Gravityon] Temizlendi."

# ─── Yardım ───────────────────────────────────────────────────────────────
help:
	@echo "Hedefler:"
	@echo "  all       — kütüphane + örnekler (varsayılan)"
	@echo "  lib       — sadece libgravityon.a"
	@echo "  examples  — triangle ve cube örnekleri"
	@echo "  test      — triangle'ı derle ve çalıştır"
	@echo "  debug     — debug bayraklarıyla derle"
	@echo "  clean     — tüm üretilmiş dosyaları sil"
