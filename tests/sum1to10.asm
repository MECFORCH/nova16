; ═══════════════════════════════════════════════════════
; NOVA-16 Test Programı: 1'den 10'a kadar toplama
;
; Algoritma:
;   R1 = counter  (1..10)
;   R2 = limit+1  (11 — döngü bitişi)
;   R3 = toplam   (başlangıç 0)
;   R4 = artış    (1)
;   R5 = geçici   (karşılaştırma için)
;
; Beklenen sonuç: R3 = 55, port[0] = 55
; ═══════════════════════════════════════════════════════

    LI   R1, 1       ; counter = 1
    LI   R2, 11      ; limit+1 = 11 (counter 11 olunca dur)
    LI   R3, 0       ; toplam = 0
    LI   R4, 1       ; artış sabiti = 1

dongu:
    ADD  R3, R3, R1  ; toplam += counter
    ADD  R1, R1, R4  ; counter++
    SUB  R5, R1, R2  ; R5 = counter - 11
    JNZ  R5, dongu   ; counter != 11 ise geri dön

    OUT  R3, 0       ; port[0] = toplam (55 olmalı)
    HALT
