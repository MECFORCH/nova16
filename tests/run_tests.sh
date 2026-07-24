#!/usr/bin/env bash
# ============================================================
# NOVA-64 Test Runner
#
# Assembles each regression test under tests/*.asm with ./asm,
# runs it under ./sim, and checks the captured output against
# the matching tests/*.expected file (substring match, one
# expected line per required substring).
#
# Usage:
#   ./tests/run_tests.sh          (run from the nova16/ directory)
#   make test                     (equivalent, via Makefile)
# ============================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR" || exit 1

ASM_BIN="./asm"
SIM_BIN="./sim"
PASS=0
FAIL=0

if [ ! -x "$ASM_BIN" ] || [ ! -x "$SIM_BIN" ]; then
    echo "[HATA] ./asm ve ./sim derlenmemiş. Önce 'make' çalıştırın." >&2
    exit 1
fi

for asm_file in "$SCRIPT_DIR"/*.asm; do
    [ -e "$asm_file" ] || continue
    name="$(basename "$asm_file" .asm)"
    expected_file="$SCRIPT_DIR/${name}.expected"
    bin_file="$(mktemp /tmp/nova32_test_XXXXXX.bin)"

    if [ ! -f "$expected_file" ]; then
        echo "[ATLA] $name — .expected dosyası yok"
        continue
    fi

    if ! "$ASM_BIN" "$asm_file" "$bin_file" > /tmp/nova32_test_asm.log 2>&1; then
        echo "[FAIL] $name — assembler hatası:"
        cat /tmp/nova32_test_asm.log
        FAIL=$((FAIL + 1))
        rm -f "$bin_file"
        continue
    fi

    actual_output="$("$SIM_BIN" "$bin_file" -q 2>&1)"
    rm -f "$bin_file"

    ok=1
    while IFS= read -r expected_line; do
        [ -z "$expected_line" ] && continue
        if ! grep -qF "$expected_line" <<< "$actual_output"; then
            ok=0
            echo "[FAIL] $name — beklenen satır bulunamadı: $expected_line"
        fi
    done < "$expected_file"

    if [ "$ok" -eq 1 ]; then
        echo "[OK]   $name"
        PASS=$((PASS + 1))
    else
        echo "----- $name çıktısı -----"
        echo "$actual_output"
        echo "-------------------------"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "Sonuç: $PASS geçti, $FAIL başarısız."

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0
