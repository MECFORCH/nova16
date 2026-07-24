; MUL/DIV regresyon testi
; 6*7=42, 100/9=11 (kalan 1), sonra div/0

LI R1, 6
LI R2, 7
MUL R3, R1, R2      ; R3 = 42

LI R4, 100
LI R5, 9
DIV R6, R4, R5      ; R6 = 11

STORE R3, R0, 0     ; port kullanmadan mem'e yaz (kontrol için)
OUT R3, 0
OUT R6, 1

LI R7, 0
DIV R0, R4, R7      ; div/0 -> trap/halt

HALT
