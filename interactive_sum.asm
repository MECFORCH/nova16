; ═══════════════════════════════════════════════════════════════
; interactive_sum.asm — SW Switch ile Etkileşimli 1..N Toplama
;
; Kullanım (FPGA Basys3):
;   SW[7:0] = N değerini belirler (0..255)
;   port[0] = Σ(1+2+...+N) → LED + 7-Segment anlık güncellenir
;
; Kullanım (simülatör):
;   ./sim interactive_sum.bin -p 1 10   → N=10, beklenen: 55
;   ./sim interactive_sum.bin -p 1 5    → N=5,  beklenen: 15
;   ./sim interactive_sum.bin -p 1 100  → N=100, beklenen: 5050
;
; Register Haritası:
;   R1 = counter (1..N)
;   R2 = N (switchlerden okunur, her döngüde taze)
;   R3 = toplam (sonuç)
;   R4 = 1 (artış sabiti, döngü boyunca kalıcı)
;   R5 = 0x00FF (bit maskesi, döngü boyunca kalıcı)
;   R6 = N+1 (döngü bitiş koşulu)
;   R7 = geçici karşılaştırma (CALL/RET kullanılmadığından SP olarak değil)
; ═══════════════════════════════════════════════════════════════

init:
    LI   R4, 1           ; artış sabiti = 1 (bir kez yükle)
    LI   R5, 255         ; alt 8-bit maskesi = 0x00FF

; ─── Ana döngü: switch oku → hesapla → yaz → tekrar ──────────
main_loop:
    IN   R2, 1           ; R2 = port[1] = SW[15:0]
    AND  R2, R2, R5      ; R2 = N = SW[7:0] (üst 8 biti sıfırla)
    JZ   R2, send_zero   ; N = 0 ise: doğrudan 0 yaz

    ; ── N+1 hesapla (döngü bitişi için) ────────────────────
    ADD  R6, R2, R4      ; R6 = N+1

    ; ── Toplama döngüsünü başlat ────────────────────────────
    LI   R1, 1           ; counter = 1
    LI   R3, 0           ; toplam = 0

sum_loop:
    ADD  R3, R3, R1      ; toplam += counter
    ADD  R1, R1, R4      ; counter++
    SUB  R7, R1, R6      ; R7 = counter - (N+1)
    JNZ  R7, sum_loop    ; counter != N+1 ise devam

    ; ── Sonucu yaz ──────────────────────────────────────────
    OUT  R3, 0           ; port[0] = toplam → LED + 7-seg
    JMP  main_loop       ; yeniden switch oku (sonsuz döngü)

; ─── N=0 özel durumu ──────────────────────────────────────────
send_zero:
    LI   R3, 0
    OUT  R3, 0           ; port[0] = 0
    JMP  main_loop
