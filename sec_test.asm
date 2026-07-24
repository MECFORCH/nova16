; ═══════════════════════════════════════════════════════════════════
; NOVA-16 SEC — Güvenlik Uzantısı Test Programı (sec_test.asm)
;
; Sıra:
;   1. Machine modda MTVEC kur (trap handler)
;   2. Machine modda RAND ile rastgele sayı üret
;   3. Machine modda AESE ile AES S-Box uygula
;   4. Machine modda HASH ile SHA-256 adımı hesapla
;   5. CSRW/CSRR dön: MTVEC'i yaz ve geri oku
;   6. ECALL ile kasıtlı trap üret, trap handler çalışsın
;   7. ERET ile geri dön
;   8. Sonuçları port[0]'a yaz, HALT
; ═══════════════════════════════════════════════════════════════════

; ── Program başlangıcı (kelime adresi 0) ────────────────────────

start:
    ; MTVEC = trap_handler adresi (CSR 3)
    LI   R1, trap_handler
    CSRW R1, 3              ; CSR[3] = MTVEC = trap_handler

    ; MTVEC'i geri oku ve doğrula
    CSRR R2, 3              ; R2 = CSR[3] (MTVEC)
    OUT  R2, 5              ; port[5] = MTVEC adresi (doğrulama)

    ; TRNG'den rastgele sayı üret
    RAND R3                 ; R3 = TRNG()
    OUT  R3, 1              ; port[1] = rastgele değer

    ; AES S-Box: R3'ü dönüştür
    AESE R4, R3             ; R4 = SubBytes(R3)
    OUT  R4, 2              ; port[2] = AES S-Box çıktısı

    ; SHA-256 adımı: R3 ve R4 ile
    HASH R5, R3, R4         ; R5 = SHA_step(R3, R4)
    OUT  R5, 3              ; port[3] = HASH çıktısı

    ; FENCE — bellek engeli
    FENCE

    ; ECALL ile trap üret (Machine moddan — sadece log)
    LI   R6, 0x1234
    ECALL                   ; Tetikler: [ECALL] Machine moddan ECALL

    ; Döngü testi: port[0]'a sonuç yaz
    LI   R1, 0xABCD
    OUT  R1, 0              ; port[0] = 0xABCD (başarı işareti)

    HALT

; ── Trap Handler ────────────────────────────────────────────────
trap_handler:
    ; MCAUSE oku
    CSRR R1, 2              ; R1 = MCAUSE
    OUT  R1, 4              ; port[4] = trap sebebi

    ; MEPC oku
    CSRR R2, 1              ; R2 = MEPC (trap olan PC)
    OUT  R2, 6              ; port[6] = trap PC

    ; ERET ile geri dön
    ERET
