; supervisor_irq_test.asm — Supervisor modu ISR testi (NOVA-32)
;
; M-modu setup:
;   MTVEC = m_trap (güvenlik ağı)
;   STVEC = s_isr
;   MIDELEG[0] = 1: timer → S-moda devret
;   MTIMECMP   = 30
;   MIE[0]     = 1 (delegasyon için M-modu timer enable gerekli)
;   MSTATUS: MIE=1, SIE=1, MPP=Supervisor
;
; M-moddan S-moda geç (MRET):
;   priv → Supervisor, MIE ← MPIE=0 (kapalı), SIE=1
;
; S-modda:
;   SIE[0]=1 set → supervisor timer etkin
;   WFI → timer → take_interrupt_s() → STVEC=s_isr
;   SRET → after_wfi → done
;
; Beklenen:
;   port[0] = 1          (S-modu ISR 1 kez çalıştı)
;   port[2] = 0x80000001 (SCAUSE = CAUSE_INT_TIMER)
;   port[3] = 1          (SEPC nonzero doğrulaması)
;   port[4] = 1          (WFI döndü)
;   port[5] = 0          (M-modu güvenlik ağı ÇALIŞMAMALI)

start:
    ; M-modu güvenlik ağı trap handler
    LI   R1, m_trap
    CSRW R1, 3          ; MTVEC = m_trap

    ; Supervisor ISR vektörü
    LI   R1, s_isr
    CSRW R1, 6          ; STVEC = s_isr

    ; MTIMECMP = 30
    LI   R1, 30
    CSRW R1, 34

    ; MIDELEG[0] = 1: timer kesmesini S-moda devret
    LI   R1, 1
    CSRW R1, 37         ; MIDELEG = 1

    ; SIE[0] = 1: supervisor timer etkin
    LI   R1, 1
    CSRW R1, 39         ; SIE = 1

    ; MIE[0] = 1: delegasyon için M-modu enable gerekli
    LI   R1, 1
    CSRW R1, 32         ; MIE = 1

    ; ISR sayacı
    LI   R6, 0

    ; MSTATUS hazırla:
    ;   bit[4:3] = MPP = 01 (Supervisor) → 0x0008
    ;   bit0 = MIE = 1  → 0x0001
    ;   bit5 = SIE = 1  → 0x0020
    ;   = 0x0029
    LI   R2, 0x0029
    CSRW R2, 4          ; MSTATUS

    ; MEPC = supervisor_entry
    LI   R2, supervisor_entry
    CSRW R2, 1          ; MEPC

    ERET                ; MRET: M → Supervisor moda geç

; ── Supervisor modu başlangıcı ──────────────────────────
supervisor_entry:
    WFI                 ; timer kesmesini bekle

after_wfi:
    LI   R4, 1
    OUT  R4, 4          ; port[4] = 1 (WFI döndü)

done:
    OUT  R6, 0          ; port[0] = ISR sayacı (1 beklenir)
    HALT

; ── Supervisor ISR ─────────────────────────────────────
s_isr:
    LI   R1, 1
    ADD  R6, R6, R1     ; sayaç++

    ; SCAUSE (CSR[40]) oku → port[2]
    CSRR R2, 40
    OUT  R2, 2          ; 0x80000001 beklenir

    ; SEPC (CSR[5]) nonzero olduğunu doğrula
    CSRR R3, 5
    LI   R4, 1
    OUT  R4, 3          ; port[3] = 1

    ; SIE temizle
    LI   R1, 0
    CSRW R1, 39         ; SIE = 0

    ERET                ; SRET → SEPC'e dön

; ── M-modu güvenlik ağı (BU ÇALIŞMAMALI) ──────────────
m_trap:
    LI   R7, 0xDEAD
    OUT  R7, 5
    HALT
