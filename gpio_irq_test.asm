; gpio_irq_test.asm — GPIO kesme testi (NOVA-32)
;
; GPIO pin 3 → cycle 50'de 1'e çekiliyor (-g 50 3 1)
; GPIO_INT_EN: bit3 set (pin3 kesme üretsin)
; MIE bit1: harici/GPIO kesme etkin
; MSTATUS.MIE = 1
;
; Beklenen sonuçlar:
;   port[0] = 1              (ISR tam 1 kez çalıştı)
;   port[1] = 0x00000008     (GPIO_INT_ST: pin3 tetikledi)
;   port[2] = 0x80000002     (MCAUSE: CAUSE_INT_EXT)
;   port[4] = 1              (WFI'dan döndü)

start:
    ; MTVEC kur
    LI   R1, isr
    CSRW R1, 3          ; CSR[3] = MTVEC

    ; GPIO_INT_EN = 0x0008 (pin3 kesme)
    LI   R1, 0x0008
    OUT  R1, 0xF2       ; port[0xF2] = GPIO_INT_EN

    ; MIE: bit1 = harici/GPIO kesme
    LI   R1, 2          ; bit1
    CSRW R1, 32         ; CSR[32] = MIE

    ; MSTATUS.MIE = 1
    LI   R1, 1
    CSRW R1, 4          ; CSR[4] = MSTATUS

    ; ISR sayacı sıfırla
    LI   R6, 0

    WFI                 ; GPIO olayını bekle

after_wfi:
    LI   R4, 1
    OUT  R4, 4          ; port[4] = 1 (WFI döndü)

done:
    OUT  R6, 0          ; port[0] = ISR sayacı (1 beklenir)
    HALT

isr:
    ; ISR sayacını artır
    LI   R1, 1
    ADD  R6, R6, R1

    ; MCAUSE oku → port[2]
    CSRR R2, 2
    OUT  R2, 2

    ; GPIO_INT_ST oku → port[1] (hangi pin tetikledi)
    IN   R3, 0xF3
    OUT  R3, 1

    ; GPIO_INT_ST temizle: -1 = 0xFFFFFFFF (tüm 32 pin)
    LI   R5, -1
    OUT  R5, 0xF3

    ; MIE temizle (GPIO kesme tekrar tetiklenmesin)
    LI   R1, 0
    CSRW R1, 32         ; MIE = 0

    ERET
