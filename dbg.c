/*
 * NOVA-64 İnteraktif Debugger
 *
 * Derleme: gcc -o dbg dbg.c
 * Kullanım: ./dbg program.bin
 *
 * Not: Bu araç simülatör (sim.c) ve assembler (asm.c) ile aynı NOVA-64
 * kelime formatını kullanır (32-bit komut, 64-bit veri yolu, word-addressed).
 *
 * Özellikler:
 *   - Adım adım yürütme (step)
 *   - Breakpoint ekleme / silme / listeleme
 *   - Disassembler (çalışan talimatın okunabilir gösterimi)
 *   - Register ve bellek dump
 *   - Register'a doğrudan değer yazma
 *   - Yürütme geçmişi (son N komut)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ─── Sabitler ─────────────────────────────────────────── */

#define MEM_SIZE      65536
#define REG_COUNT     32
#define IO_PORTS      256
#define MAX_BP        16       /* maksimum breakpoint sayısı   */
#define HISTORY_LEN   16       /* saklanacak komut geçmişi     */

/* ─── Opcode ───────────────────────────────────────────── */

#define OP_NOP    0x00
#define OP_ADD    0x01
#define OP_SUB    0x02
#define OP_AND    0x03
#define OP_OR     0x04
#define OP_XOR    0x05
#define OP_SHL    0x06
#define OP_SHR    0x07
#define OP_LOAD   0x08
#define OP_STORE  0x09
#define OP_LI     0x0A
#define OP_JMP    0x0B
#define OP_JZ     0x0C
#define OP_JNZ    0x0D
#define OP_CALL   0x0E
#define OP_RET    0x0F
#define OP_OUT    0x10
#define OP_IN     0x11
#define OP_MUL    0x1A
#define OP_DIV    0x1B
#define OP_ELOAD  0x1C
#define OP_ESTORE 0x1D
#define OP_RFLAGS 0x1E
#define OP_HALT   0x3F

/* FLAGS register bitleri — ADD/SUB/MUL sonrası güncellenir */
#define FLAG_Z (1u << 0)
#define FLAG_C (1u << 1)
#define FLAG_V (1u << 2)
#define FLAG_N (1u << 3)

/* ─── CPU Durumu ───────────────────────────────────────── */
/* NOVA-64: 64-bit veri yolu, 32-bit sabit komut,
 * word-addressed bellek (pc += 1 per fetch). */

static uint64_t reg[REG_COUNT];
static uint64_t mem[MEM_SIZE];  /* 64-bit kelimeler; komutlar alt 32 bitte */
static uint32_t pc;
static uint8_t  halted;
static uint64_t cycles;
static uint64_t io_ports[IO_PORTS];
static uint64_t flags;

/* ─── Breakpoint Tablosu ───────────────────────────────── */

typedef struct {
    uint32_t word_addr;
    int      active;
} Breakpoint;

static Breakpoint breakpoints[MAX_BP];
static int        bp_count = 0;

/* ─── Yürütme Geçmişi ──────────────────────────────────── */

typedef struct {
    uint32_t pc;
    uint32_t insn;
    uint64_t regs[REG_COUNT];
} HistEntry;

static HistEntry history[HISTORY_LEN];
static int       hist_head = 0;   /* dairesel tampon başı */
static int       hist_size = 0;

/* ─── Yardımcılar ──────────────────────────────────────── */

static int64_t sign_extend11(uint32_t v)
{
    if (v & 0x400u)
        return (int64_t)((uint64_t)v | 0xFFFFFFFFFFFFF800ull);
    return (int64_t)v;
}

/* Basit (stdlib bağımsız) string karşılaştırma */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* Baştaki boşlukları atla */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Basit ondalık/hex ayrıştırıcı (imzasız) */
static int parse_uint(const char *s, uint32_t *out)
{
    const char *p = s;
    *out = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!*p) return 0;
        while (*p) {
            int d;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else return 0;
            *out = *out * 16 + (uint32_t)d;
            p++;
        }
        return 1;
    }
    if (!(*p >= '0' && *p <= '9')) return 0;
    while (*p >= '0' && *p <= '9') { *out = *out * 10 + (uint32_t)(*p++ - '0'); }
    return *p == '\0';
}

/* ─── Disassembler ─────────────────────────────────────── */

static const char *reg_name(int r)
{
    static const char *names[] = {
        "R0","R1","R2","R3","R4","R5","R6","R7",
        "R8","R9","R10","R11","R12","R13","R14","R15",
        "R16","R17","R18","R19","R20","R21","R22","R23",
        "R24","R25","R26","R27","R28","R29","R30","R31"
    };
    if (r < 0 || r > 31) return "??";
    return names[r];
}

/*
 * 32-bit komut + kelime adresi → okunabilir metin.
 * out tamponu en az 80 byte olmalı.
 */
static void disasm(uint32_t insn, uint32_t waddr, char *out)
{
    uint32_t opcode  = (insn >> 26) & 0x3F;
    int      fd      = (int)((insn >> 21) & 0x1F);
    int      fa      = (int)((insn >> 16) & 0x1F);
    int      fb      = (int)((insn >> 11) & 0x1F);
    uint32_t imm_raw =  insn & 0x7FFu;
    int64_t  imm     = sign_extend11(imm_raw);

    /* Göreli dal adresini mutlak kelime adresine çevir:
       IMM, fetch sonrası PC'ye göre (waddr+1) görelidir (1 kelime/komut). */
    uint32_t branch_target = (uint32_t)((int32_t)(waddr + 1) + (int32_t)imm);

#define W(fmt, ...) sprintf(out, fmt, __VA_ARGS__)

    switch (opcode) {
        case OP_NOP:    W("%s", "NOP"); break;
        case OP_HALT:   W("%s", "HALT"); break;
        case OP_RET:    W("%s", "RET"); break;
        case OP_RFLAGS: W("RFLAGS %s", reg_name(fd)); break;

        case OP_ADD:
            W("ADD  %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_SUB:
            W("SUB  %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_AND:
            W("AND  %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_OR:
            W("OR   %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_XOR:
            W("XOR  %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_MUL:
            W("MUL  %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_DIV:
            W("DIV  %s, %s, %s", reg_name(fd), reg_name(fa), reg_name(fb));
            break;
        case OP_SHL:
            W("SHL  %s, %s, %lld", reg_name(fd), reg_name(fa), (long long)imm);
            break;
        case OP_SHR:
            W("SHR  %s, %s, %lld", reg_name(fd), reg_name(fa), (long long)imm);
            break;

        case OP_LOAD:
            W("LOAD %s, %s, %lld", reg_name(fd), reg_name(fa), (long long)imm);
            break;
        case OP_STORE:
            W("STORE %s, %s, %lld", reg_name(fd), reg_name(fa), (long long)imm);
            break;
        case OP_ELOAD:
            W("ELOAD %s, %s, %lld", reg_name(fd), reg_name(fa), (long long)imm);
            break;
        case OP_ESTORE:
            W("ESTORE %s, %s, %lld", reg_name(fd), reg_name(fa), (long long)imm);
            break;

        case OP_LI:
            W("LI   %s, %lld  (0x%016llX)", reg_name(fd),
              (long long)imm, (unsigned long long)(uint64_t)imm);
            break;

        case OP_JMP:
            W("JMP  %lld  -> kelime 0x%08X", (long long)imm, (unsigned)branch_target);
            break;
        case OP_JZ:
            W("JZ   %s, %lld  -> kelime 0x%08X",
              reg_name(fd), (long long)imm, (unsigned)branch_target);
            break;
        case OP_JNZ:
            W("JNZ  %s, %lld  -> kelime 0x%08X",
              reg_name(fd), (long long)imm, (unsigned)branch_target);
            break;

        case OP_CALL:
            W("CALL %lld  -> kelime 0x%08X", (long long)imm, (unsigned)branch_target);
            break;

        case OP_OUT:
            W("OUT  %s, port[%d]", reg_name(fd), (int)(imm & 0xFF));
            break;
        case OP_IN:
            W("IN   %s, port[%d]", reg_name(fd), (int)(imm & 0xFF));
            break;

        default:
            W("??? (opcode=0x%02X raw=0x%08X)", (unsigned)opcode, (unsigned)insn);
            break;
    }
#undef W
}

/* ─── CPU Fonksiyonları ────────────────────────────────── */

static void cpu_reset(void)
{
    int i;
    for (i = 0; i < REG_COUNT; i++) reg[i] = 0;
    memset(mem, 0, sizeof(mem));
    memset(io_ports, 0, sizeof(io_ports));
    pc = 0; halted = 0; cycles = 0; flags = 0;
}

/*
 * Bellekten 32-bit komut oku, PC'yi 1 ilerlet.
 * Her kelime 64-bit; komut alt 32 bitte saklanır.
 */
static uint32_t cpu_fetch(void)
{
    uint32_t insn = (uint32_t)(mem[pc & (MEM_SIZE - 1)]);
    pc++;
    return insn;
}

static void cpu_execute(uint32_t insn)
{
    uint32_t opcode  = (insn >> 26) & 0x3F;
    uint32_t fd      = (insn >> 21) & 0x1Fu;
    uint32_t fa      = (insn >> 16) & 0x1Fu;
    uint32_t fb      = (insn >> 11) & 0x1Fu;
    int64_t  imm     = sign_extend11(insn & 0x7FFu);
    uint64_t addr;
    uint64_t result;

    switch (opcode) {
        case OP_NOP:  break;
        case OP_HALT: halted = 1; break;

        case OP_ADD: {
            uint64_t a = reg[fa], b = reg[fb];
            result = a + b;
            flags  = 0;
            if (result == 0)                               flags |= FLAG_Z;
            if (result < a)                                flags |= FLAG_C;
            if (!((a ^ b) >> 63) && ((a ^ result) >> 63)) flags |= FLAG_V;
            if (result >> 63)                              flags |= FLAG_N;
            if (fd) reg[fd] = result;
            break;
        }
        case OP_SUB: {
            uint64_t a = reg[fa], b = reg[fb];
            result = a - b;
            flags  = 0;
            if (result == 0)                               flags |= FLAG_Z;
            if (a < b)                                     flags |= FLAG_C;
            if (((a ^ b) >> 63) && ((a ^ result) >> 63))  flags |= FLAG_V;
            if (result >> 63)                              flags |= FLAG_N;
            if (fd) reg[fd] = result;
            break;
        }
        case OP_AND:  if (fd) reg[fd] = reg[fa] & reg[fb]; break;
        case OP_OR:   if (fd) reg[fd] = reg[fa] | reg[fb]; break;
        case OP_XOR:  if (fd) reg[fd] = reg[fa] ^ reg[fb]; break;

        /* 6-bit kaydırma maskesi (0-63) */
        case OP_SHL:  if (fd) reg[fd] = reg[fa] << (imm & 0x3F); break;
        case OP_SHR:  if (fd) reg[fd] = reg[fa] >> (imm & 0x3F); break;

        case OP_MUL: {
            __uint128_t wide = (__uint128_t)reg[fa] * (__uint128_t)reg[fb];
            result = (uint64_t)wide;
            flags  = 0;
            if (result == 0)                                        flags |= FLAG_Z;
            if (wide > ((__uint128_t)0xFFFFFFFFFFFFFFFFull))        flags |= (FLAG_C | FLAG_V);
            if (result >> 63)                                        flags |= FLAG_N;
            if (fd) reg[fd] = result;
            break;
        }
        case OP_DIV:
            if (reg[fb] == 0) {
                fprintf(stderr, "  [HATA] Sıfıra bölme (DIV)\n");
                halted = 1;
                break;
            }
            result = (uint64_t)((int64_t)reg[fa] / (int64_t)reg[fb]);
            if (fd) reg[fd] = result;
            break;

        case OP_LOAD:
            addr = (uint64_t)((int64_t)reg[fa] + imm) & (MEM_SIZE - 1);
            if (fd) reg[fd] = mem[addr];
            break;
        case OP_STORE:
            addr = (uint64_t)((int64_t)reg[fa] + imm) & (MEM_SIZE - 1);
            mem[addr] = reg[fd];
            break;

        case OP_ELOAD:
        case OP_ESTORE:
            /* EEPROM, dbg.c'de ayrı olarak modellenmez — sim.c kullanın. */
            if (!halted)
                printf("  [BİLGİ] ELOAD/ESTORE dbg'de desteklenmiyor, sim kullanın.\n");
            break;

        case OP_RFLAGS:
            if (fd) reg[fd] = flags & 0xFu;
            break;

        case OP_LI:
            if (fd) reg[fd] = (uint64_t)imm;
            break;

        case OP_JMP:
            pc = (uint32_t)((int32_t)pc + (int32_t)imm);
            break;
        case OP_JZ:
            if (reg[fd] == 0) pc = (uint32_t)((int32_t)pc + (int32_t)imm);
            break;
        case OP_JNZ:
            if (reg[fd] != 0) pc = (uint32_t)((int32_t)pc + (int32_t)imm);
            break;

        case OP_CALL:
            reg[7]--;
            mem[reg[7] & (MEM_SIZE - 1)] = (uint64_t)(pc);
            pc = (uint32_t)((int32_t)pc + (int32_t)imm);
            break;
        case OP_RET:
            pc = (uint32_t)(mem[reg[7] & (MEM_SIZE - 1)]);
            reg[7]++;
            break;

        case OP_OUT:
            io_ports[imm & 0xFF] = reg[fd];
            printf("  \033[33m[OUT] port[%d] = %llu (0x%016llX)\033[0m\n",
                   (int)(imm & 0xFF),
                   (unsigned long long)reg[fd],
                   (unsigned long long)reg[fd]);
            break;
        case OP_IN:
            if (fd) reg[fd] = io_ports[imm & 0xFF];
            break;

        default:
            fprintf(stderr, "  [HATA] Bilinmeyen opcode: 0x%02X\n", (unsigned)opcode);
            halted = 1;
            break;
    }
    reg[0] = 0;  /* R0 hardwired zero */
}

/*
 * Tek adım: komut öncesi geçmişe kaydet, fetch → execute.
 * Döner: 1 = normal, 0 = halted
 */
static int cpu_step(void)
{
    uint32_t insn;
    int      i;
    HistEntry *h;

    if (halted) return 0;

    /* Geçmişe kaydet */
    h = &history[hist_head % HISTORY_LEN];
    h->pc   = pc;
    h->insn = (uint32_t)(mem[pc & (MEM_SIZE - 1)]);
    for (i = 0; i < REG_COUNT; i++) h->regs[i] = reg[i];
    hist_head++;
    if (hist_size < HISTORY_LEN) hist_size++;

    insn = cpu_fetch();
    cpu_execute(insn);
    cycles++;
    return !halted;
}

/* ─── Breakpoint Kontrol ───────────────────────────────── */

static int bp_hit(void)
{
    int i;
    for (i = 0; i < bp_count; i++) {
        if (breakpoints[i].active && breakpoints[i].word_addr == pc)
            return i;
    }
    return -1;
}

/* ─── Görüntüleme ──────────────────────────────────────── */

/* Renk kodları */
#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_RED    "\033[31m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_CYAN   "\033[36m"
#define C_DIM    "\033[2m"

static void print_banner(void)
{
    printf(C_BOLD C_CYAN
           "┌──────────────────────────────────────────┐\n"
           "│          NOVA-64 İnteraktif Debugger      │\n"
           "│  Yardım için 'h' yazın                    │\n"
           "└──────────────────────────────────────────┘\n"
           C_RESET);
}

static void print_help(void)
{
    printf(C_BOLD "Komutlar:\n" C_RESET);
    printf("  " C_YELLOW "s" C_RESET "  / step           — Tek komut adımı\n");
    printf("  " C_YELLOW "n" C_RESET "  <count>          — N adım at (ör. n 10)\n");
    printf("  " C_YELLOW "c" C_RESET "  / run            — Breakpoint veya HALT'a kadar koş\n");
    printf("  " C_YELLOW "b" C_RESET "  <addr>           — Breakpoint ekle (hex/dec kelime adresi)\n");
    printf("  " C_YELLOW "bl" C_RESET "                  — Breakpoint'leri listele\n");
    printf("  " C_YELLOW "bc" C_RESET " <id>             — Breakpoint sil (listedeki numara)\n");
    printf("  " C_YELLOW "d" C_RESET "  / dump           — Register dump\n");
    printf("  " C_YELLOW "m" C_RESET "  <addr> [count]   — Bellek görüntüle (varsayılan: 8 kelime)\n");
    printf("  " C_YELLOW "r" C_RESET "  <reg> <val>      — Register'a değer yaz (ör. r R1 42)\n");
    printf("  " C_YELLOW "p" C_RESET "  <port>           — G/Ç port değerini oku\n");
    printf("  " C_YELLOW "hist" C_RESET "                — Son komut geçmişini göster\n");
    printf("  " C_YELLOW "di" C_RESET " [addr] [count]   — Disassemble (varsayılan: PC'den 8 komut)\n");
    printf("  " C_YELLOW "rst" C_RESET "                 — CPU'yu sıfırla (belleği koru)\n");
    printf("  " C_YELLOW "q" C_RESET "  / quit           — Çıkış\n");
}

/* Mevcut komutun üstündeki bilgi satırını yazdır */
static void print_status(void)
{
    char     dis[128];
    uint32_t insn;
    int      bpid;

    printf(C_DIM "────────────────────────────────────────────\n" C_RESET);

    if (halted) {
        printf(C_RED C_BOLD "  [HALT] CPU durdu. Cycles: %llu\n" C_RESET,
               (unsigned long long)cycles);
        return;
    }

    insn  = (uint32_t)(mem[pc & (MEM_SIZE - 1)]);
    bpid  = bp_hit();
    disasm(insn, pc, dis);

    if (bpid >= 0)
        printf(C_RED "  ●BP%-2d " C_RESET, bpid);
    else
        printf("  " C_GREEN "►" C_RESET "     ");

    printf(C_BOLD "0x%08X" C_RESET "  %08X  " C_CYAN "%s\n" C_RESET,
           (unsigned)pc, (unsigned)insn, dis);
}

static void print_regs(void)
{
    int i;
    printf(C_BOLD "  Registers:\n" C_RESET);
    for (i = 0; i < REG_COUNT; i++) {
        const char *note = "";
        if (i == 0)  note = C_DIM " [zero]" C_RESET;
        if (i == 29) note = C_DIM " [FP]  " C_RESET;
        if (i == 30) note = C_DIM " [LR]  " C_RESET;
        if (i == 31) note = C_DIM " [SP]  " C_RESET;
        printf("    R%-2d= " C_YELLOW "%20llu" C_RESET "  0x%016llX%s\n",
               i,
               (unsigned long long)reg[i],
               (unsigned long long)reg[i],
               note);
    }
    printf("  FLAGS: Z=%d C=%d V=%d N=%d\n",
           (flags & FLAG_Z) ? 1 : 0, (flags & FLAG_C) ? 1 : 0,
           (flags & FLAG_V) ? 1 : 0, (flags & FLAG_N) ? 1 : 0);
    printf("  PC = 0x%08X  Cycles = %llu  Halted = %s\n",
           (unsigned)pc,
           (unsigned long long)cycles,
           halted ? C_RED "EVET" C_RESET : C_GREEN "HAYIR" C_RESET);
}

static void print_mem(uint32_t addr, int count)
{
    int i;
    printf(C_BOLD "  Bellek (kelime adresi 0x%08X'ten %d kelime):\n" C_RESET,
           (unsigned)addr, count);
    for (i = 0; i < count; i++) {
        uint32_t a = (addr + (uint32_t)i) & (MEM_SIZE - 1);
        printf("    [0x%08X] = " C_YELLOW "0x%016llX" C_RESET "  (%llu)\n",
               (unsigned)a,
               (unsigned long long)mem[a],
               (unsigned long long)mem[a]);
    }
}

static void print_bps(void)
{
    int i, found = 0;
    printf(C_BOLD "  Breakpoint'ler:\n" C_RESET);
    for (i = 0; i < bp_count; i++) {
        if (breakpoints[i].active) {
            printf("    [%d] kelime 0x%08X\n", i, (unsigned)breakpoints[i].word_addr);
            found = 1;
        }
    }
    if (!found) printf("    (yok)\n");
}

static void print_history(void)
{
    int i, n, idx;
    char dis[128];
    printf(C_BOLD "  Yürütme Geçmişi (en yeni → en eski):\n" C_RESET);
    if (hist_size == 0) { printf("    (boş)\n"); return; }
    n = hist_size < HISTORY_LEN ? hist_size : HISTORY_LEN;
    for (i = 0; i < n; i++) {
        idx = (hist_head - 1 - i + HISTORY_LEN * 2) % HISTORY_LEN;
        disasm(history[idx].insn, history[idx].pc, dis);
        printf("    #%02d  0x%08X  %08X  %s\n",
               i,
               (unsigned)history[idx].pc,
               (unsigned)history[idx].insn,
               dis);
    }
}

static void print_disasm(uint32_t addr, int count)
{
    int i;
    char dis[128];
    printf(C_BOLD "  Disassembly:\n" C_RESET);
    for (i = 0; i < count; i++) {
        uint32_t a    = (addr + (uint32_t)i) & (MEM_SIZE - 1);
        uint32_t insn = (uint32_t)mem[a];
        const char *arrow;
        disasm(insn, a, dis);
        arrow = (a == pc && !halted) ? C_GREEN "►" C_RESET : " ";
        printf("    %s 0x%08X  %s\n", arrow, (unsigned)a, dis);
    }
}

/* ─── .bin Yükleme ─────────────────────────────────────── */

/*
 * .bin dosyası big-endian 32-bit kelimelerden oluşur (asm.c/sim.c formatı).
 * Her komut = 4 byte; 64-bit bellek kelimesinin alt 32 bitine yüklenir.
 */
static int load_bin(const char *path)
{
    FILE *f;
    int   b1, b2, b3, b4, i;

    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[HATA] Açılamadı: %s\n", path); return 0; }

    i = 0;
    while (i < MEM_SIZE) {
        b1 = fgetc(f); b2 = fgetc(f); b3 = fgetc(f); b4 = fgetc(f);
        if (b1 == EOF || b2 == EOF || b3 == EOF || b4 == EOF) break;
        /* Komut alt 32 bitte; üst 32 bit sıfır */
        mem[i++] = ((uint64_t)b1 << 24) | ((uint64_t)b2 << 16) |
                   ((uint64_t)b3 <<  8) |  (uint64_t)b4;
    }
    fclose(f);
    printf("[BİLGİ] %d komut (%d byte program, %d byte bellek) yüklendi.\n\n",
           i, i * 4, i * 8);
    return 1;
}

/* ─── Komut Satırı Okuyucu ─────────────────────────────── */

static int read_line(char *buf, int max)
{
    int i = 0;
    int c;
    while (i < max - 1) {
        c = fgetc(stdin);
        if (c == EOF) return 0;
        if (c == '\n') break;
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return 1;
}

/* Token ayrıştır (boşlukla ayrılmış) */
static int get_token(const char **pp, char *out, int max)
{
    const char *p = skip_ws(*pp);
    int i = 0;
    if (!*p) return 0;
    while (*p && *p != ' ' && *p != '\t') {
        if (i < max - 1) out[i++] = *p;
        p++;
    }
    out[i] = '\0';
    *pp = p;
    return i > 0;
}

/* ─── main ─────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    char    line[256];
    char    cmd[64], arg1[64], arg2[64];
    const char *p;
    uint32_t uval;
    int     i;

    if (argc < 2) {
        fprintf(stderr, "Kullanım: %s <program.bin>\n", argv[0]);
        return 1;
    }

    cpu_reset();
    if (!load_bin(argv[1])) return 1;

    print_banner();
    print_status();

    /* ── Ana Komut Döngüsü ── */
    for (;;) {
        printf(C_BOLD C_CYAN "nova64> " C_RESET);
        fflush(stdout);

        if (!read_line(line, (int)sizeof(line))) break;

        p = skip_ws(line);
        if (!*p) {
            /* Boş satır → son komutu tekrar (varsayılan: adım) */
            if (halted) { printf("  CPU zaten durmuş.\n"); continue; }
            cpu_step();
            print_status();
            continue;
        }

        cmd[0] = arg1[0] = arg2[0] = '\0';
        get_token(&p, cmd, (int)sizeof(cmd));
        get_token(&p, arg1, (int)sizeof(arg1));
        get_token(&p, arg2, (int)sizeof(arg2));

        /* ── Komutlar ── */

        if (str_eq(cmd, "q") || str_eq(cmd, "quit")) {
            printf("Çıkış.\n");
            break;

        } else if (str_eq(cmd, "h") || str_eq(cmd, "help") || str_eq(cmd, "?")) {
            print_help();

        } else if (str_eq(cmd, "s") || str_eq(cmd, "step")) {
            if (halted) { printf("  CPU zaten durmuş.\n"); continue; }
            cpu_step();
            print_status();

        } else if (str_eq(cmd, "n")) {
            /* N adım */
            uint32_t count = 1;
            if (arg1[0]) parse_uint(arg1, &count);
            for (i = 0; i < (int)count && !halted; i++) {
                if (bp_hit() >= 0 && i > 0) break;
                cpu_step();
            }
            print_status();

        } else if (str_eq(cmd, "c") || str_eq(cmd, "run")) {
            /* Breakpoint veya HALT'a kadar koş */
            if (halted) { printf("  CPU zaten durmuş.\n"); continue; }
            /* İlk adımı at (mevcut BP'den çıkmak için) */
            cpu_step();
            while (!halted) {
                int bpid = bp_hit();
                if (bpid >= 0) {
                    printf(C_RED "  [Breakpoint %d] kelime 0x%08X\n" C_RESET,
                           bpid, (unsigned)pc);
                    break;
                }
                cpu_step();
            }
            print_status();

        } else if (str_eq(cmd, "d") || str_eq(cmd, "dump")) {
            print_regs();

        } else if (str_eq(cmd, "b")) {
            /* Breakpoint ekle */
            if (!arg1[0]) { printf("  Kullanım: b <kelime_adresi>\n"); continue; }
            if (!parse_uint(arg1, &uval)) { printf("  Geçersiz adres.\n"); continue; }
            if (bp_count >= MAX_BP) { printf("  Breakpoint tablosu dolu.\n"); continue; }
            breakpoints[bp_count].word_addr = uval;
            breakpoints[bp_count].active    = 1;
            printf("  Breakpoint [%d] eklendi → kelime 0x%08X\n",
                   bp_count, (unsigned)uval);
            bp_count++;

        } else if (str_eq(cmd, "bl")) {
            print_bps();

        } else if (str_eq(cmd, "bc")) {
            /* Breakpoint sil */
            if (!arg1[0]) { printf("  Kullanım: bc <id>\n"); continue; }
            if (!parse_uint(arg1, &uval) || (int)uval >= bp_count) {
                printf("  Geçersiz breakpoint ID.\n"); continue;
            }
            breakpoints[uval].active = 0;
            printf("  Breakpoint [%u] silindi.\n", (unsigned)uval);

        } else if (str_eq(cmd, "m")) {
            /* Bellek dump */
            uint32_t addr  = pc;
            uint32_t count = 8;
            if (arg1[0]) parse_uint(arg1, &addr);
            if (arg2[0]) parse_uint(arg2, &count);
            if (count > 64) count = 64;
            print_mem(addr, (int)count);

        } else if (str_eq(cmd, "r")) {
            /* Register yaz: r R1 42 */
            int      rn;
            uint32_t val;
            if (!arg1[0] || !arg2[0]) {
                printf("  Kullanım: r <register> <değer>  (ör. r R3 0xFF)\n");
                continue;
            }
            rn = -1;
            {
                char rb[8];
                int  ri;
                for (ri = 0; arg1[ri] && ri < 7; ri++)
                    rb[ri] = (arg1[ri] >= 'a' && arg1[ri] <= 'z') ? arg1[ri]-32 : arg1[ri];
                rb[ri] = '\0';
                if (rb[0] == 'R' && rb[1] >= '0' && rb[1] <= '9' && rb[2] == '\0') {
                    rn = rb[1] - '0';  /* R0-R9 */
                } else if (rb[0] == 'R' && rb[1] >= '1' && rb[1] <= '3' &&
                           rb[2] >= '0' && rb[2] <= '9' && rb[3] == '\0') {
                    rn = (rb[1]-'0')*10 + (rb[2]-'0');  /* R10-R31 */
                    if (rn > 31) rn = -1;
                }
            }
            if (rn < 0) { printf("  Geçersiz register.\n"); continue; }
            if (rn == 0) { printf("  R0 hardwired zero, yazılamaz.\n"); continue; }
            if (!parse_uint(arg2, &val)) { printf("  Geçersiz değer.\n"); continue; }
            reg[rn] = (uint64_t)val;
            printf("  R%d = %llu (0x%016llX)\n",
                   rn,
                   (unsigned long long)reg[rn],
                   (unsigned long long)reg[rn]);

        } else if (str_eq(cmd, "p")) {
            /* G/Ç port oku */
            uint32_t port;
            if (!arg1[0]) { printf("  Kullanım: p <port>\n"); continue; }
            if (!parse_uint(arg1, &port) || port >= IO_PORTS) {
                printf("  Geçersiz port.\n"); continue;
            }
            printf("  port[%u] = %llu (0x%016llX)\n",
                   (unsigned)port,
                   (unsigned long long)io_ports[port],
                   (unsigned long long)io_ports[port]);

        } else if (str_eq(cmd, "hist")) {
            print_history();

        } else if (str_eq(cmd, "di")) {
            /* Disassembly */
            uint32_t addr  = pc;
            uint32_t count = 8;
            if (arg1[0]) parse_uint(arg1, &addr);
            if (arg2[0]) parse_uint(arg2, &count);
            if (count > 32) count = 32;
            print_disasm(addr, (int)count);

        } else if (str_eq(cmd, "rst")) {
            /* CPU sıfırla ama belleği koru */
            static uint64_t mem_backup[MEM_SIZE];
            memcpy(mem_backup, mem, sizeof(mem));
            cpu_reset();
            memcpy(mem, mem_backup, sizeof(mem));
            hist_size = 0; hist_head = 0;
            printf("  CPU sıfırlandı (bellek korundu).\n");
            print_status();

        } else {
            printf("  Bilinmeyen komut: '%s'  — yardım için 'h'\n", cmd);
        }
    }

    return 0;
}
