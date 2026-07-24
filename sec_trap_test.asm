; ═══════════════════════════════════════════════════════════════════
; NOVA-16 SEC — Trap / ERET Mekanizması Test Programı
; (sec_trap_test.asm)
;
; Senaryo:
;   1. MTVEC kur
;   2. R6 = 0 (trap sayacı)
;   3. User moda inemeyiz (CSRW PRIV salt okunur, manuel değil),
;      bunun yerine Supervisor moddan ECALL yaparak trap üretiriz.
;   4. Trap handler çalışır: R6++ yazar, ERET ile döner
;   5. ERET sonrası port[0] = R6 (kaç trap oldu)
; ═══════════════════════════════════════════════════════════════════

start:
    ; MTVEC = trap_handler
    LI   R1, trap_handler
    CSRW R1, 3

    ; trap sayacını sıfırla
    LI   R6, 0

    ; 1. ECALL — Machine'dan (sadece log)
    ECALL

    ; MSTATUS.MPP'yi Supervisor'a çek (0x08 = priv=1 << 3)
    LI   R2, 0x0008
    CSRW R2, 4          ; MSTATUS = 0x0008 (MPP=Supervisor)

    ; MEPC'yi buradan biraz ileri kur, ERET oraya dönecek
    LI   R3, after_eret
    CSRW R3, 1          ; MEPC = after_eret

    ; ERET — Supervisor moduna geç ve MEPC'ye atla
    ERET

after_eret:
    ; Supervisor modundayız — şimdi ECALL (S→trap)
    ECALL

done:
    ; Sonuçları yaz
    OUT  R6, 0          ; port[0] = trap sayacı
    CSRR R7, 2          ; R7 = son MCAUSE
    OUT  R7, 1          ; port[1] = son MCAUSE
    HALT

; ── Trap Handler (Machine modda çalışır) ────────────────
trap_handler:
    ; sayacı artır
    LI   R1, 1
    ADD  R6, R6, R1

    ; MCAUSE oku ve porta yaz
    CSRR R2, 2
    OUT  R2, 2          ; port[2] = mevcut MCAUSE

    ; MEPC'ye bak
    CSRR R3, 1
    OUT  R3, 3          ; port[3] = mevcut MEPC

    ; Geri dön
    ERET
