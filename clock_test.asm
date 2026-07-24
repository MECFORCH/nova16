; clock_test.asm — Cycle sayacı (MTIME) ve sistem zamanlayıcı testi (NOVA-32)
;
; MTIME (CSR[35]) okunur (32-bit cycle sayacı).
; Timer'ın doğru cycle'da ateşlediği doğrulanır.
;
; Akış:
;   1. MTIME oku → ilk değer
;   2. MTIMECMP = 80 kur, MTVEC/MIE/MSTATUS kur
;   3. WFI
;   4. ISR: MTIME oku (>= 80 olmalı), kaydet
;   5. Sonuçları port'lara yaz
;
; Beklenen:
;   port[0] = 1          (ISR 1 kez çalıştı)
;   port[1] >= 80        (MTIME >= MTIMECMP'de ateşlendi)
;   port[2] = 0x80000001 (MCAUSE = CAUSE_INT_TIMER)
;   port[3] = 0          (MTIME_HI = 0, cycles 32-bit'te kalır)

start:
    ; MTVEC kur
    LI   R1, isr
    CSRW R1, 3

    ; İlk MTIME değerini oku
    CSRR R4, 35         ; CSR[35] = MTIME (32-bit)

    ; MTIMECMP = 80
    LI   R1, 80
    CSRW R1, 34

    ; MIE bit0 = timer
    LI   R1, 1
    CSRW R1, 32

    ; MSTATUS.MIE = 1
    LI   R1, 1
    CSRW R1, 4

    LI   R6, 0          ; ISR sayacı

    WFI

after_wfi:
    LI   R5, 1
    OUT  R5, 4          ; port[4] = 1

done:
    OUT  R6, 0          ; port[0] = ISR sayacı
    OUT  R4, 5          ; port[5] = setup'taki ilk MTIME
    HALT

isr:
    LI   R1, 1
    ADD  R6, R6, R1     ; sayaç++

    ; MTIME ISR anında oku → port[1]
    CSRR R2, 35
    OUT  R2, 1

    ; MCAUSE → port[2]
    CSRR R3, 2
    OUT  R3, 2

    ; MTIME_HI → port[3] (cycles 32-bit → hi daima 0)
    CSRR R3, 36
    OUT  R3, 3

    ; MIP temizle + MIE kapat
    LI   R1, 0
    CSRW R1, 33         ; MIP = 0
    CSRW R1, 32         ; MIE = 0

    ERET
