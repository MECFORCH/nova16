/*
 * NOVA-64 Assembler
 *
 * Derleme: gcc -o asm asm.c
 * Kullanım: ./asm kaynak.asm çıktı.bin
 *
 * Giriş : .asm metin dosyası (NOVA-64 assembly söz dizimi)
 * Çıkış : .bin dosyası (big-endian 32-bit kelimeler)
 *
 * İki geçişli assembler:
 *   Geçiş 1 — label adreslerini topla
 *   Geçiş 2 — komutları kodla, .bin yaz
 *
 * NOVA-64'de her komut tek bir 32-bit kelimedir; 64-bit kelimede saklanır (word_addr += 1).
 * Dal ofseti: hedef_kelime_adr - (mevcut_kelime_adr + 1)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ─── Sabitler ─────────────────────────────────────────── */

#define MAX_LABELS    1024
#define MAX_LABEL_LEN 64
#define MAX_LINE_LEN  256
#define MAX_INSNS     32768

/* ─── Opcode Tanımları ─────────────────────────────────── */

#define OP_NOP   0x00
#define OP_ADD   0x01
#define OP_SUB   0x02
#define OP_AND   0x03
#define OP_OR    0x04
#define OP_XOR   0x05
#define OP_SHL   0x06
#define OP_SHR   0x07
#define OP_LOAD  0x08
#define OP_STORE 0x09
#define OP_LI    0x0A
#define OP_JMP   0x0B
#define OP_JZ    0x0C
#define OP_JNZ   0x0D
#define OP_CALL  0x0E
#define OP_RET   0x0F
#define OP_OUT   0x10
#define OP_IN    0x11
#define OP_HALT  0x3F

/* ── Güvenlik Uzantısı (NOVA-64 SEC) ──────────────────── */
#define OP_ECALL 0x12
#define OP_ERET  0x13
#define OP_CSRW  0x14
#define OP_CSRR  0x15
#define OP_RAND  0x16
#define OP_FENCE 0x17
#define OP_AESE  0x18
#define OP_HASH  0x19

/* ── Aritmetik Uzantısı ────────────────────────────────── */
#define OP_MUL   0x1A
#define OP_DIV   0x1B
#define OP_ELOAD  0x1C
#define OP_ESTORE 0x1D
#define OP_RFLAGS 0x1E
#define OP_WFI   0x1F

/* ─── Label Tablosu ────────────────────────────────────── */

typedef struct {
    char     name[MAX_LABEL_LEN];
    uint32_t word_addr;
} Label;

static Label   labels[MAX_LABELS];
static int     label_count = 0;

static int label_add(const char *name, uint32_t addr)
{
    int i;
    if (label_count >= MAX_LABELS) {
        fprintf(stderr, "[HATA] Label tablosu doldu.\n");
        return 0;
    }
    for (i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0)
            return 0;
    }
    strncpy(labels[label_count].name, name, MAX_LABEL_LEN - 1);
    labels[label_count].name[MAX_LABEL_LEN - 1] = '\0';
    labels[label_count].word_addr = addr;
    label_count++;
    return 1;
}

static int32_t label_find(const char *name)
{
    int i;
    for (i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0)
            return (int32_t)labels[i].word_addr;
    }
    return -1;
}

/* ─── Hata Yönetimi ────────────────────────────────────── */

static int error_count = 0;

static void asm_error(int line_no, const char *msg)
{
    fprintf(stderr, "[HATA] Satır %d: %s\n", line_no, msg);
    error_count++;
}

/* ─── Metin Yardımcıları ───────────────────────────────── */

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static void trim_line(char *buf)
{
    char *p = buf;
    while (*p && *p != ';' && *p != '\n' && *p != '\r') p++;
    *p = '\0';
    while (p > buf && (*(p-1) == ' ' || *(p-1) == '\t')) {
        p--;
        *p = '\0';
    }
}

static int next_token(const char **pp, char *out, int max)
{
    const char *p = skip_ws(*pp);
    int i = 0;
    if (!*p) return 0;
    while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';') {
        if (i < max - 1) out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    p = skip_ws(p);
    if (*p == ',') p++;
    *pp = p;
    return i > 0;
}

static void to_upper(char *s)
{
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
        s++;
    }
}

/* ─── Sayı Ayrıştırıcı ─────────────────────────────────── */

static int parse_number(const char *s, int32_t *out)
{
    int      sign = 1;
    int32_t  val  = 0;
    const char *p = s;

    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!*p) return 0;
        while (*p) {
            int d;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else return 0;
            val = val * 16 + d;
            p++;
        }
    } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        p += 2;
        if (!*p) return 0;
        while (*p) {
            if (*p != '0' && *p != '1') return 0;
            val = val * 2 + (*p - '0');
            p++;
        }
    } else {
        if (!(*p >= '0' && *p <= '9')) return 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (*p) return 0;
    }

    *out = sign * val;
    return 1;
}

/* ─── Register Ayrıştırıcı ─────────────────────────────── */

static int parse_reg(const char *s)
{
    char buf[8];
    int  i, n;
    for (i = 0; s[i] && i < 7; i++) {
        buf[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    }
    buf[i] = '\0';
    if (buf[0] != 'R') return -1;
    /* R0-R9 */
    if (buf[1] >= '0' && buf[1] <= '9' && buf[2] == '\0') {
        n = buf[1] - '0';
        return (n <= 31) ? n : -1;
    }
    /* R10-R31 */
    if (buf[1] >= '1' && buf[1] <= '3' &&
        buf[2] >= '0' && buf[2] <= '9' && buf[3] == '\0') {
        n = (buf[1] - '0') * 10 + (buf[2] - '0');
        return (n <= 31) ? n : -1;
    }
    return -1;
}

/* ─── Komut Kodlayıcı ──────────────────────────────────── */

/*
 * 32-bit NOVA-64 komut kelimesini oluşturur.
 *
 * Bit düzeni:
 *   [31:26] opcode (6 bit)
 *   [25:21] fd     (5 bit → R0-R31)
 *   [20:16] fa     (5 bit)
 *   [15:11] fb     (5 bit)
 *   [10:0]  imm    (11 bit, işaretli: -1024..1023)
 */
static uint32_t encode(uint32_t opcode, uint32_t fd, uint32_t fa,
                       uint32_t fb, int32_t imm)
{
    uint32_t imm11 = (uint32_t)imm & 0x7FFu;
    return (opcode << 26) | (fd << 21) | (fa << 16) | (fb << 11) | imm11;
}

/* ─── IMM Doğrulama ────────────────────────────────────── */

static int check_imm11(int32_t v, int line_no)
{
    if (v < -1024 || v > 1023) {
        asm_error(line_no, "Immediate değer 11-bit aralığını aşıyor "
                           "(-1024..1023)");
        return 0;
    }
    return 1;
}

/* ─── İki Geçişli Assembler ────────────────────────────── */

static uint32_t insn_buf[MAX_INSNS];
static int      insn_count = 0;

/*
 * Bir metin satırını işle.
 *
 * NOVA-64: her komut TEK kelime → word_addr += 1
 * Dal ofseti: hedef - (word_addr + 1)   [NOVA-16'da +2'ydi]
 */
static int process_line(const char *raw, int line_no,
                        int pass, uint32_t *word_addr)
{
    char line[MAX_LINE_LEN];
    char tok[MAX_LABEL_LEN];
    const char *p;
    int  rlen;

    rlen = 0;
    while (raw[rlen] && rlen < MAX_LINE_LEN - 1) {
        line[rlen] = raw[rlen];
        rlen++;
    }
    line[rlen] = '\0';
    trim_line(line);

    p = skip_ws(line);
    if (!*p) return 1;

    /* ── Label tespiti ── */
    {
        const char *colon = p;
        int is_label = 0;
        while ((*colon >= 'a' && *colon <= 'z') ||
               (*colon >= 'A' && *colon <= 'Z') ||
               (*colon >= '0' && *colon <= '9') ||
               *colon == '_')
            colon++;
        if (*colon == ':') is_label = 1;
        if (is_label) {
            char lname[MAX_LABEL_LEN];
            int  llen = (int)(colon - p);
            if (llen >= MAX_LABEL_LEN) {
                asm_error(line_no, "Label ismi çok uzun");
                return 0;
            }
            strncpy(lname, p, (unsigned)llen);
            lname[llen] = '\0';
            if (pass == 1) {
                if (label_find(lname) >= 0) {
                    asm_error(line_no, "Label zaten tanımlı");
                    return 0;
                }
                label_add(lname, *word_addr);
            }
            p = skip_ws(colon + 1);
            if (!*p) return 1;
        }
    }

    /* ── Mnemonik oku ── */
    if (!next_token(&p, tok, (int)sizeof(tok))) return 1;
    to_upper(tok);

    /* ── Komut Kodlama ── */

#define REQ_REG(var)                                            \
    do {                                                        \
        char _t[MAX_LABEL_LEN];                                 \
        if (!next_token(&p, _t, (int)sizeof(_t))) {             \
            asm_error(line_no, "Register bekleniyor");          \
            return 0;                                           \
        }                                                       \
        (var) = parse_reg(_t);                                  \
        if ((var) < 0) {                                        \
            asm_error(line_no, "Geçersiz register");            \
            return 0;                                           \
        }                                                       \
    } while(0)

    /*
     * NOVA-64 dal ofseti: hedef - (word_addr + 1)
     * (NOVA-16'da +2'ydi çünkü her komut 2 kelime kaplıyordu)
     */
#define REQ_IMM_OR_LABEL(imm_var, is_branch)                        \
    do {                                                             \
        char    _t[MAX_LABEL_LEN];                                   \
        int32_t _v;                                                  \
        if (!next_token(&p, _t, (int)sizeof(_t))) {                  \
            asm_error(line_no, "Immediate veya label bekleniyor");   \
            return 0;                                                \
        }                                                            \
        if (parse_number(_t, &_v)) {                                 \
            (imm_var) = _v;                                          \
        } else {                                                     \
            int32_t _addr = label_find(_t);                          \
            if (_addr < 0) {                                         \
                if (pass == 2) {                                     \
                    asm_error(line_no, "Tanımsız label");            \
                    return 0;                                        \
                }                                                    \
                _addr = 0;                                           \
            }                                                        \
            if (is_branch)                                           \
                (imm_var) = _addr - (int32_t)(*word_addr + 1);      \
            else                                                     \
                (imm_var) = _addr;                                   \
        }                                                            \
    } while(0)

    {
        int     rd = 0, rs1 = 0, rs2 = 0;
        int32_t imm = 0;
        uint32_t insn = 0;

        if (strcmp(tok, "NOP") == 0) {
            insn = encode(OP_NOP, 0, 0, 0, 0);

        } else if (strcmp(tok, "HALT") == 0) {
            insn = encode(OP_HALT, 0, 0, 0, 0);

        } else if (strcmp(tok, "ADD") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_ADD, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "SUB") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_SUB, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "AND") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_AND, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "OR") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_OR, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "XOR") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_XOR, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "MUL") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_MUL, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "DIV") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_DIV, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else if (strcmp(tok, "SHL") == 0) {
            REQ_REG(rd); REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_SHL, (uint32_t)rd, (uint32_t)rs1, 0, imm);

        } else if (strcmp(tok, "SHR") == 0) {
            REQ_REG(rd); REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_SHR, (uint32_t)rd, (uint32_t)rs1, 0, imm);

        } else if (strcmp(tok, "LOAD") == 0) {
            REQ_REG(rd); REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_LOAD, (uint32_t)rd, (uint32_t)rs1, 0, imm);

        } else if (strcmp(tok, "STORE") == 0) {
            REQ_REG(rs1); REQ_REG(rs2);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_STORE, (uint32_t)rs1, (uint32_t)rs2, 0, imm);

        } else if (strcmp(tok, "ELOAD") == 0) {
            REQ_REG(rd); REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_ELOAD, (uint32_t)rd, (uint32_t)rs1, 0, imm);

        } else if (strcmp(tok, "ESTORE") == 0) {
            REQ_REG(rs1); REQ_REG(rs2);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_ESTORE, (uint32_t)rs1, (uint32_t)rs2, 0, imm);

        } else if (strcmp(tok, "LI") == 0) {
            REQ_REG(rd);
            REQ_IMM_OR_LABEL(imm, 0);
            /*
             * LI 17-bit işaretli immediate yükler (-65536..+65535).
             * 0x8000–0xFFFF için bit16'yı işaret biti yaparak negatif kodla
             * (sim'de sign_extend11 ile 32-bit'e uzar).
             */
            if (imm >= 0 && imm <= 2047) {
                if (imm > 1023) {
                    int32_t signed_imm = imm - 2048;
                    imm = signed_imm;
                }
            } else if (!check_imm11(imm, line_no)) {
                return 0;
            }
            insn = encode(OP_LI, (uint32_t)rd, 0, 0, imm);

        } else if (strcmp(tok, "JMP") == 0) {
            REQ_IMM_OR_LABEL(imm, 1);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_JMP, 0, 0, 0, imm);

        } else if (strcmp(tok, "JZ") == 0) {
            REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 1);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_JZ, (uint32_t)rs1, 0, 0, imm);

        } else if (strcmp(tok, "JNZ") == 0) {
            REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 1);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_JNZ, (uint32_t)rs1, 0, 0, imm);

        } else if (strcmp(tok, "CALL") == 0) {
            REQ_IMM_OR_LABEL(imm, 1);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_CALL, 0, 0, 0, imm);

        } else if (strcmp(tok, "RET") == 0) {
            insn = encode(OP_RET, 0, 0, 0, 0);

        } else if (strcmp(tok, "OUT") == 0) {
            REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_OUT, (uint32_t)rs1, 0, 0, imm);

        } else if (strcmp(tok, "IN") == 0) {
            REQ_REG(rd);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_IN, (uint32_t)rd, 0, 0, imm);

        /* ══ GÜVENLİK UZANTISI KOMUTLARI (NOVA-64 SEC) ══════ */

        } else if (strcmp(tok, "ECALL") == 0) {
            insn = encode(OP_ECALL, 0, 0, 0, 0);

        } else if (strcmp(tok, "ERET") == 0) {
            insn = encode(OP_ERET, 0, 0, 0, 0);

        } else if (strcmp(tok, "CSRW") == 0) {
            REQ_REG(rs1);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_CSRW, (uint32_t)rs1, 0, 0, imm);

        } else if (strcmp(tok, "CSRR") == 0) {
            REQ_REG(rd);
            REQ_IMM_OR_LABEL(imm, 0);
            if (!check_imm11(imm, line_no)) return 0;
            insn = encode(OP_CSRR, (uint32_t)rd, 0, 0, imm);

        } else if (strcmp(tok, "RAND") == 0) {
            REQ_REG(rd);
            insn = encode(OP_RAND, (uint32_t)rd, 0, 0, 0);

        } else if (strcmp(tok, "RFLAGS") == 0) {
            REQ_REG(rd);
            insn = encode(OP_RFLAGS, (uint32_t)rd, 0, 0, 0);

        } else if (strcmp(tok, "WFI") == 0) {
            insn = encode(OP_WFI, 0, 0, 0, 0);

        } else if (strcmp(tok, "FENCE") == 0) {
            insn = encode(OP_FENCE, 0, 0, 0, 0);

        } else if (strcmp(tok, "AESE") == 0) {
            REQ_REG(rd); REQ_REG(rs1);
            insn = encode(OP_AESE, (uint32_t)rd, (uint32_t)rs1, 0, 0);

        } else if (strcmp(tok, "HASH") == 0) {
            REQ_REG(rd); REQ_REG(rs1); REQ_REG(rs2);
            insn = encode(OP_HASH, (uint32_t)rd, (uint32_t)rs1,
                          (uint32_t)rs2, 0);

        } else {
            asm_error(line_no, "Bilinmeyen mnemonik");
            return 0;
        }

        if (pass == 2) {
            if (insn_count >= MAX_INSNS) {
                asm_error(line_no, "Program çok büyük");
                return 0;
            }
            insn_buf[insn_count++] = insn;
        }
        *word_addr += 1;   /* NOVA-64: her komut 1 kelime */
    }

#undef REQ_REG
#undef REQ_IMM_OR_LABEL

    return 1;
}

/* ─── Dosya Geçişi ─────────────────────────────────────── */

static int run_pass(FILE *f, int pass)
{
    char     line[MAX_LINE_LEN];
    int      line_no  = 0;
    uint32_t word_addr = 0;

    rewind(f);
    while (fgets(line, (int)sizeof(line), f)) {
        line_no++;
        if (!process_line(line, line_no, pass, &word_addr))
            if (error_count > 10) {
                fprintf(stderr, "[HATA] Çok fazla hata, derleme durdu.\n");
                return 0;
            }
    }
    return error_count == 0;
}

/* ─── ELF Yardımcıları ─────────────────────────────────── */

static void elf_u16(FILE *f, uint16_t v)
{
    fputc((v >> 8) & 0xFF, f);
    fputc( v       & 0xFF, f);
}

static void elf_u32(FILE *f, uint32_t v)
{
    fputc((v >> 24) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >>  8) & 0xFF, f);
    fputc( v        & 0xFF, f);
}

/* ─── ELF32 Big-Endian Yazıcı ──────────────────────────── */

/*
 * ELF32 big-endian çıktı (EM_NONE = özel mimari).
 * GDB ile kullanım:
 *   (gdb) set architecture big
 *   (gdb) file program.elf
 *   (gdb) target remote :2345
 *
 * Bölümler: .text (komutlar) + .symtab (label'lar) + .strtab + .shstrtab
 * Tüm label'lar global STT_NOTYPE sembol olarak dışa aktarılır.
 *
 * Dosya düzeni:
 *   0     : ELF başlığı      (52 byte)
 *   52    : Program başlığı  (32 byte) — PT_LOAD, vaddr=0
 *   84    : .text verisi     (insn_count * 4 byte)
 *   84+T  : .symtab          ((1 + label_count) * 16 byte)
 *   84+T+Y: .strtab          (1 + toplam label uzunlukları)
 *   ...   : .shstrtab        (33 byte sabit)
 *   son   : 5 bölüm başlığı  (5 * 40 byte)
 */
static int write_elf(const char *path)
{
    FILE    *f;
    int      i;
    unsigned k;
    uint32_t text_sz, sym_sz, strtab_sz;
    uint32_t off_text, off_symtab, off_strtab, off_shstrtab, off_shdrs;

    /* .shstrtab sabit içerik: "\0.text\0.symtab\0.strtab\0.shstrtab\0"
     * Toplam 33 byte:
     *   [0]  ""         → boş bölüm adı
     *   [1]  ".text"    → bölüm 1
     *   [7]  ".symtab"  → bölüm 2
     *   [15] ".strtab"  → bölüm 3
     *   [23] ".shstrtab"→ bölüm 4
     */
    static const char shstrtab_data[] =
        "\0.text\0.symtab\0.strtab\0.shstrtab\0";
    uint32_t shstrtab_sz = (uint32_t)(sizeof(shstrtab_data) - 1u); /* 33 */

    /* Boyut hesapları */
    text_sz   = (uint32_t)insn_count * 4u;
    sym_sz    = (uint32_t)(1 + label_count) * 16u; /* 1 null + N label */
    strtab_sz = 1u;  /* baştaki null */
    for (i = 0; i < label_count; i++)
        strtab_sz += (uint32_t)strlen(labels[i].name) + 1u;

    /* Dosya ofseti hesapları */
    off_text     = 84u;                            /* ELF(52) + PHDR(32) */
    off_symtab   = off_text     + text_sz;
    off_strtab   = off_symtab   + sym_sz;
    off_shstrtab = off_strtab   + strtab_sz;
    off_shdrs    = off_shstrtab + shstrtab_sz;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[HATA] ELF çıktı dosyası açılamadı: %s\n", path);
        return 0;
    }

    /* ── ELF Başlığı (52 byte) ── */
    fputc(0x7F, f); fputc('E', f); fputc('L', f); fputc('F', f); /* magic */
    fputc(1, f);    /* ELFCLASS32   */
    fputc(2, f);    /* ELFDATA2MSB  */
    fputc(1, f);    /* EV_CURRENT   */
    fputc(0, f);    /* ELFOSABI_NONE */
    for (i = 0; i < 8; i++) fputc(0, f);  /* e_ident padding */
    elf_u16(f, 2);            /* ET_EXEC      */
    elf_u16(f, 0);            /* EM_NONE      */
    elf_u32(f, 1);            /* e_version    */
    elf_u32(f, 0);            /* e_entry      */
    elf_u32(f, 52);           /* e_phoff      */
    elf_u32(f, off_shdrs);    /* e_shoff      */
    elf_u32(f, 0);            /* e_flags      */
    elf_u16(f, 52);           /* e_ehsize     */
    elf_u16(f, 32);           /* e_phentsize  */
    elf_u16(f, 1);            /* e_phnum      */
    elf_u16(f, 40);           /* e_shentsize  */
    elf_u16(f, 5);            /* e_shnum      */
    elf_u16(f, 4);            /* e_shstrndx   */

    /* ── Program Başlığı (32 byte) ── */
    elf_u32(f, 1);            /* PT_LOAD      */
    elf_u32(f, off_text);     /* p_offset     */
    elf_u32(f, 0);            /* p_vaddr      */
    elf_u32(f, 0);            /* p_paddr      */
    elf_u32(f, text_sz);      /* p_filesz     */
    elf_u32(f, text_sz);      /* p_memsz      */
    elf_u32(f, 5);            /* PF_R | PF_X  */
    elf_u32(f, 4);            /* p_align      */

    /* ── .text bölümü ── */
    for (i = 0; i < insn_count; i++) {
        uint32_t insn = insn_buf[i];
        fputc((insn >> 24) & 0xFF, f);
        fputc((insn >> 16) & 0xFF, f);
        fputc((insn >>  8) & 0xFF, f);
        fputc( insn        & 0xFF, f);
    }

    /* ── .symtab bölümü ── */
    /* Null sembol (16 byte sıfır) */
    for (i = 0; i < 16; i++) fputc(0, f);
    /* Her label için bir Elf32_Sym girişi */
    {
        uint32_t strtab_off = 1u; /* baştaki null sonrası */
        for (i = 0; i < label_count; i++) {
            elf_u32(f, strtab_off);               /* st_name  */
            elf_u32(f, labels[i].word_addr * 8u); /* st_value (byte adr, 64-bit bellek) */
            elf_u32(f, 0);                         /* st_size  */
            fputc(0x10, f);                        /* st_info: STB_GLOBAL|STT_NOTYPE */
            fputc(0, f);                           /* st_other */
            elf_u16(f, 1);                         /* st_shndx (.text = 1) */
            strtab_off += (uint32_t)strlen(labels[i].name) + 1u;
        }
    }

    /* ── .strtab bölümü ── */
    fputc(0, f); /* baştaki null */
    for (i = 0; i < label_count; i++) {
        const char *n = labels[i].name;
        while (*n) fputc(*n++, f);
        fputc(0, f);
    }

    /* ── .shstrtab bölümü ── */
    for (k = 0; k < (unsigned)(sizeof(shstrtab_data) - 1); k++)
        fputc(shstrtab_data[k], f);

    /* ── Bölüm Başlıkları (5 × 40 byte) ── */

    /* [0] NULL */
    for (i = 0; i < 40; i++) fputc(0, f);

    /* [1] .text — SHT_PROGBITS(1), SHF_ALLOC|SHF_EXECINSTR(6) */
    elf_u32(f, 1);        elf_u32(f, 1);        elf_u32(f, 6);
    elf_u32(f, 0);        elf_u32(f, off_text);  elf_u32(f, text_sz);
    elf_u32(f, 0);        elf_u32(f, 0);         elf_u32(f, 4);
    elf_u32(f, 0);

    /* [2] .symtab — SHT_SYMTAB(2), link=3(.strtab), info=1(ilk global) */
    elf_u32(f, 7);        elf_u32(f, 2);         elf_u32(f, 0);
    elf_u32(f, 0);        elf_u32(f, off_symtab); elf_u32(f, sym_sz);
    elf_u32(f, 3);        elf_u32(f, 1);          elf_u32(f, 4);
    elf_u32(f, 16);

    /* [3] .strtab — SHT_STRTAB(3) */
    elf_u32(f, 15);       elf_u32(f, 3);         elf_u32(f, 0);
    elf_u32(f, 0);        elf_u32(f, off_strtab); elf_u32(f, strtab_sz);
    elf_u32(f, 0);        elf_u32(f, 0);          elf_u32(f, 1);
    elf_u32(f, 0);

    /* [4] .shstrtab — SHT_STRTAB(3) */
    elf_u32(f, 23);          elf_u32(f, 3);            elf_u32(f, 0);
    elf_u32(f, 0);           elf_u32(f, off_shstrtab);  elf_u32(f, shstrtab_sz);
    elf_u32(f, 0);           elf_u32(f, 0);             elf_u32(f, 1);
    elf_u32(f, 0);

    fclose(f);
    return 1;
}

/* ─── .bin Yazıcı ──────────────────────────────────────── */

/*
 * Her komut big-endian 4 byte olarak yazılır.
 * sim.c'nin load_bin() fonksiyonu aynı formatı okur.
 */
static int write_bin(const char *path)
{
    FILE    *f;
    int      i;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[HATA] Çıktı dosyası açılamadı: %s\n", path);
        return 0;
    }

    for (i = 0; i < insn_count; i++) {
        uint32_t insn = insn_buf[i];
        fputc((insn >> 24) & 0xFF, f);
        fputc((insn >> 16) & 0xFF, f);
        fputc((insn >>  8) & 0xFF, f);
        fputc( insn        & 0xFF, f);
    }

    fclose(f);
    return 1;
}

/* ─── main ─────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    FILE *f;
    int   i;

    if (argc < 3) {
        fprintf(stderr,
            "Kullanım: %s <kaynak.asm> <çıktı.bin|çıktı.elf>\n"
            "  Çıktı dosyası \".elf\" ile bitiyorsa ELF32 big-endian\n"
            "  (sembol tablosu ile) üretilir; aksi halde ham .bin.\n",
            argv[0]);
        return 1;
    }

    f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "[HATA] Kaynak dosya açılamadı: %s\n", argv[1]);
        return 1;
    }

    printf("NOVA-64 Assembler\n");
    printf("Kaynak: %s\n\n", argv[1]);

    /* ── Geçiş 1: Label adreslerini topla ── */
    printf("==> Geçiş 1: label'lar taranıyor...\n");
    if (!run_pass(f, 1)) goto fail;

    printf("    %d label bulundu:\n", label_count);
    for (i = 0; i < label_count; i++) {
        printf("      %-20s → kelime 0x%04X (byte 0x%05X)\n",
               labels[i].name,
               (unsigned)labels[i].word_addr,
               (unsigned)labels[i].word_addr * 8);
    }

    /* ── Geçiş 2: Komutları kodla ── */
    printf("\n==> Geçiş 2: komutlar kodlanıyor...\n");
    insn_count = 0;
    if (!run_pass(f, 2)) goto fail;

    printf("    %d komut kodlandı (%d byte).\n",
           insn_count, insn_count * 4);

    /* ── Çıktı yaz (.elf uzantısı → ELF32, aksi halde ham .bin) ── */
    {
        size_t outlen  = strlen(argv[2]);
        int    is_elf  = (outlen >= 4 &&
                           strcmp(argv[2] + outlen - 4, ".elf") == 0);
        if (is_elf) {
            if (!write_elf(argv[2])) goto fail;
            printf("\n==> ELF çıktı yazıldı: %s (%d sembol)\n",
                   argv[2], label_count);
        } else {
            if (!write_bin(argv[2])) goto fail;
            printf("\n==> Çıktı yazıldı: %s\n", argv[2]);
        }
    }

    fclose(f);
    return 0;

fail:
    fprintf(stderr, "\nDerleme %d hatayla başarısız.\n", error_count);
    fclose(f);
    return 1;
}
