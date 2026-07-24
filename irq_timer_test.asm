; ═══════════════════════════════════════════════════════════════════
; NOVA-32 — Donanım (Zamanlayıcı) Kesme Testi
; (irq_timer_test.asm)
;
; Senaryo:
;   1. MTVEC = isr (kesme vektörü)
;   2. CSR_MTIMECMP = 20  (cycles >= 20 olunca timer kesmesi tetiklenir)
;   3. CSR_MIE bit0 = 1   (timer kesmesi etkin)
;   4. MSTATUS bit0 = 1   (global kesme etkin, MIE=1)
;   5. WFI ile bekle
;   6. ISR çalışır:
;       - R6++ (sayaç artır)
;       - MIP = 0 (MIP.TIMER temizle)
;       - MIE = 0 (global kesmeyi kapat)
;       - MEPC → port[3], MCAUSE → port[2]
;       - ERET ile after_wfi'ye dön
;   7. done: port[0]=R6, HALT
;
; Beklenen çıktı:
;   port[0] = 1          (ISR 1 kez çalıştı)
;   port[2] = 0x80000001 (CAUSE_INT_TIMER)
; ═══════════════════════════════════════════════════════════════════

start:
    LI   R1, isr
    CSRW R1, 3          ; MTVEC = isr adresi

    LI   R2, 20
    CSRW R2, 34         ; MTIMECMP = 20

    LI   R3, 1
    CSRW R3, 32         ; MIE = 0x1 (bit0: timer kesmesi etkin)

    LI   R6, 0          ; ISR sayacı = 0

    LI   R5, 1
    CSRW R5, 4          ; MSTATUS = 0x1 (MIE=1)

    WFI                 ; kesme gelene kadar bekle

after_wfi:
    LI   R4, 1
    OUT  R4, 4          ; port[4] = 1 → WFI'den döndük

done:
    OUT  R6, 0          ; port[0] = ISR sayacı (1 beklenir)
    HALT

; ── ISR: Machine modda çalışır ──────────────────────────────────
isr:
    LI   R1, 1
    ADD  R6, R6, R1     ; sayacı artır

    CSRR R2, 2
    OUT  R2, 2          ; port[2] = MCAUSE (0x80000001 beklenir)

    CSRR R3, 1
    OUT  R3, 3          ; port[3] = MEPC

    LI   R1, 0
    CSRW R1, 33         ; MIP = 0

    CSRW R1, 32         ; MIE = 0

    ERET                ; MEPC'ye (after_wfi) dön
