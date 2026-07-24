/*
 * NOVA-64 CPU Simülatörü
 *
 * Derleme: gcc -O2 -o sim sim.c
 * Kullanım: ./sim program.bin [seçenekler]
 *
 * Mimari: 64-bit veri yolu, 32 register, 32-bit sabit komut uzunluğu
 * Bellek : 65536 adet 64-bit kelime (word-addressed, 512 KB)
 *
 * Pipeline: 2 aşamalı — FETCH önceden yapılır, dal alındığında +1 çevrim ceza.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "gdb_stub.h"

/* ─── Sabitler ─────────────────────────────────────────── */

#define MEM_SIZE   65536   /* 32-bit kelime sayısı             */
#define REG_COUNT  32      /* R0–R31                           */
#define IO_PORTS   256     /* G/Ç port sayısı                  */

/* ── Kalıcı Bellek (EEPROM/Flash simülasyonu) ─────────────
 * mem[]'den ayrı, cpu_reset() ile sıfırlanmaz. Diskteki bir
 * dosyaya (varsayılan: nova64_eeprom.bin) yansıtılır.
 */
#define EEPROM_SIZE   4096
#define EEPROM_DEFAULT_PATH "nova64_eeprom.bin"

/* ── UART (Seri Port) — gerçek karakter G/Ç ────────────────
 * Port 0xFE = TX: OUT ile yazılan alt byte doğrudan stdout'a
 * Port 0xFF = RX: IN ile okunduğunda stdin'den bir karakter
 *             alınır. Girdi biterse (EOF) 0xFFFFFFFF döner.
 */
#define IO_PORT_UART_TX 0xFE
#define IO_PORT_UART_RX 0xFF

/* ── GPIO (Genel Amaçlı G/Ç) — 32 pin ───────────────────────
 * Port 0xF0: GPIO_DIR     — Yön: bit=1 çıkış, 0=giriş
 * Port 0xF1: GPIO_VAL     — Veri: çıkış yazılır, mixed okunur
 * Port 0xF2: GPIO_INT_EN  — Hangi giriş pinleri IRQ üretsin
 * Port 0xF3: GPIO_INT_ST  — Hangi pinler tetikledi (yazarak temizlenir)
 */
#define IO_PORT_GPIO_DIR    0xF0
#define IO_PORT_GPIO_VAL    0xF1
#define IO_PORT_GPIO_INT_EN 0xF2
#define IO_PORT_GPIO_INT_ST 0xF3

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

/* ── Güvenlik Uzantısı Opcode'ları (NOVA-64 SEC) ──────── */
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

/* ── Kalıcı Bellek Uzantısı ────────────────────────────── */
#define OP_ELOAD  0x1C
#define OP_ESTORE 0x1D

/* ── Bayrak (FLAGS) Uzantısı ───────────────────────────── */
#define OP_RFLAGS 0x1E

/* ── Donanım Kesme (Interrupt) Uzantısı ────────────────── */
#define OP_WFI    0x1F

/* FLAGS register bitleri — ADD/SUB/MUL sonrası güncellenir */
#define FLAG_Z (1u << 0)   /* Zero     : sonuç == 0                       */
#define FLAG_C (1u << 1)   /* Carry    : unsigned taşma/underflow (borç)  */
#define FLAG_V (1u << 2)   /* oVerflow : signed taşma/underflow           */
#define FLAG_N (1u << 3)   /* Negative : sonucun bit63'i (işaret biti)    */

/* ── CSR İndeks Tanımları ──────────────────────────────── */
#define CSR_PRIV    0
#define CSR_MEPC    1
#define CSR_MCAUSE  2
#define CSR_MTVEC   3
#define CSR_MSTATUS 4
#define CSR_SEPC    5
#define CSR_STVEC   6
#define CSR_SATP    7
/* CSR 8-15: MPU_BASE[0-7], 16-23: MPU_MASK[0-7], 24-31: MPU_PERM[0-7] */
#define CSR_MPU_BASE(n) (8  + (n))
#define CSR_MPU_MASK(n) (16 + (n))
#define CSR_MPU_PERM(n) (24 + (n))
/* ── Donanım Kesme (Interrupt) CSR'ları ────────────────── */
#define CSR_MIE       32
#define CSR_MIP       33
#define CSR_MTIMECMP  34
/* ── Sistem Zamanlayıcı / Cycle Sayacı ─────────────────── */
#define CSR_MTIME     35   /* Cycle sayacı [63:0]  (salt okunur, 64-bit)   */
#define CSR_MTIME_HI  36   /* cycles[63:32] — 64-bit cycle sayacının üst yarısı */
/* ── Delegasyon / Supervisor Kesme CSR'ları ─────────────── */
#define CSR_MIDELEG   37
#define CSR_MEDELEG   38
#define CSR_SIE       39
#define CSR_SCAUSE    40
#define CSR_COUNT     41

/* MIE/MIP bit maskeleri */
#define MIP_TIMER  (1u << 0)
#define MIP_EXT    (1u << 1)

/* MSTATUS bitleri */
#define MSTATUS_MIE  (1u << 0)
#define MSTATUS_MPIE (1u << 1)
#define MSTATUS_SIE  (1u << 5)
#define MSTATUS_SPIE (1u << 6)
#define MSTATUS_SPP  (1u << 7)

/* ── PRIV Seviyeleri ───────────────────────────────────── */
#define PRIV_MACHINE    0
#define PRIV_SUPERVISOR 1
#define PRIV_USER       2

/* ── MCAUSE Kodları ────────────────────────────────────── */
#define CAUSE_ILL_INSN   1
#define CAUSE_MPU_READ   2
#define CAUSE_MPU_WRITE  3
#define CAUSE_MPU_EXEC   4
#define CAUSE_ECALL_U    5
#define CAUSE_ECALL_S    6
#define CAUSE_CSR_PRIV   7
#define CAUSE_IO_PRIV    8
#define CAUSE_ERET_PRIV  9
#define CAUSE_DIV_ZERO   10

/* Kesme nedenleri: bit63 set → asenkron kesme */
#define CAUSE_INT_FLAG   0x8000000000000000ull
#define CAUSE_INT_TIMER  (CAUSE_INT_FLAG | 1ull)
#define CAUSE_INT_EXT    (CAUSE_INT_FLAG | 2ull)

/* ─── CPU Durumu ───────────────────────────────────────── */

static uint64_t reg[REG_COUNT];      /* 32 adet 64-bit register      */
static uint64_t mem[MEM_SIZE];       /* 64K kelime bellek (512 KB)   */
static uint32_t pc;                  /* program counter (word idx)   */
static uint8_t  halted;
static uint64_t cycles;
static uint64_t io_ports[IO_PORTS];
static uint64_t flags;               /* Z/C/V/N bayrakları           */

/* ── Donanım Kesme Durumu ───────────────────────────────── */
static uint8_t  waiting_for_interrupt = 0;
static int64_t  irq_ext_at_cycle      = -1;
static uint32_t health_interrupts     = 0;

/* ── GPIO Durumu (32 pin) ────────────────────────────────── */
static uint64_t gpio_input       = 0;

#define MAX_GPIO_EVENTS 16
typedef struct {
    int64_t  cycle;
    uint8_t  pin;    /* 0-31 */
    uint8_t  val;
} GpioEvent;
static GpioEvent gpio_events[MAX_GPIO_EVENTS];
static int       gpio_event_count = 0;

/* ── Kalıcı Bellek (EEPROM/Flash) ──────────────────────── */
static uint64_t eeprom[EEPROM_SIZE];
static char     eeprom_path[512] = EEPROM_DEFAULT_PATH;
static uint8_t  eeprom_dirty = 0;

/* ── Güvenlik Durumu (NOVA-64 SEC) ────────────────────── */
static uint8_t  priv;
static uint64_t csr[CSR_COUNT];
static uint64_t trng_lfsr;

/* ── Simülatör Kontrol Bayrakları ──────────────────────── */
static uint8_t  quiet      = 0;
static uint8_t  bench      = 0;

/* ── 2-Aşamalı Pipeline Durumu ─────────────────────────── */
static uint32_t pipe_insn  = 0;
static uint8_t  pipe_valid = 0;
static uint8_t  pipe_flush = 0;

/* ── RESILIENCE: Kendi Kendini Onarma Sistemi ──────────── */
static uint8_t  resilient      = 0;
static uint8_t  health_report  = 0;
static uint32_t wdt_limit      = 100000u;
static uint32_t wdt_stall_count = 0;
static uint64_t wdt_last_sig    = 0xFFFFFFFFFFFFFFFFull;

static uint32_t health_total_traps       = 0;
static uint32_t health_illegal_recovered = 0;
static uint32_t health_mpu_recovered     = 0;
static uint32_t health_div_recovered     = 0;
static uint32_t health_watchdog_resets   = 0;

/* ─── AES S-Box (FIPS-197) ─────────────────────────────── */

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* ─── Yardımcı: 11-bit İşaret Uzatma ──────────────────── */

static int64_t sign_extend11(uint32_t v)
{
    if (v & 0x400u)
        return (int64_t)((uint64_t)v | 0xFFFFFFFFFFFFF800ull);
    return (int64_t)v;
}

/* ─── CPU Fonksiyonları ────────────────────────────────── */

/* TRNG: bir adım ilerlet, 64-bit döndür */
static uint64_t trng_next(void)
{
    trng_lfsr = (trng_lfsr << 1) ^
                (((trng_lfsr >> 63) ^ (trng_lfsr >> 21) ^
                  (trng_lfsr >>  1) ^  trng_lfsr) & 1ull);
    return trng_lfsr;
}

/*
 * MPU kontrolü: addr adresine priv seviyesinde erişim izinli mi?
 * mode: 'r'=okuma, 'w'=yazma, 'x'=çalıştırma
 */
static int mpu_check(uint64_t addr, char mode)
{
    int n;
    for (n = 0; n < 8; n++) {
        uint64_t base = csr[CSR_MPU_BASE(n)];
        uint64_t mask = csr[CSR_MPU_MASK(n)];
        uint64_t perm = csr[CSR_MPU_PERM(n)];
        uint64_t min_priv;

        if (mask == 0) continue;

        if ((addr & mask) == (base & mask)) {
            min_priv = (perm >> 3) & 0x3ull;

            if (priv > min_priv) {
                csr[CSR_MCAUSE] = CAUSE_CSR_PRIV;
                return 0;
            }
            if (mode == 'r' && !(perm & 1)) {
                csr[CSR_MCAUSE] = CAUSE_MPU_READ;
                return 0;
            }
            if (mode == 'w' && !(perm & 2)) {
                csr[CSR_MCAUSE] = CAUSE_MPU_WRITE;
                return 0;
            }
            if (mode == 'x' && !(perm & 4)) {
                csr[CSR_MCAUSE] = CAUSE_MPU_EXEC;
                return 0;
            }
            return 1;
        }
    }
    if (priv != PRIV_MACHINE) {
        csr[CSR_MCAUSE] = CAUSE_CSR_PRIV;
        return 0;
    }
    return 1;
}

static int is_recoverable_cause(uint64_t cause)
{
    return cause == CAUSE_ILL_INSN  ||
           cause == CAUSE_MPU_READ  ||
           cause == CAUSE_MPU_WRITE ||
           cause == CAUSE_MPU_EXEC  ||
           cause == CAUSE_DIV_ZERO;
}

/*
 * Trap üret: cause 32-bit.
 * bit31=1 → asenkron kesme, bit31=0 → senkron istisna.
 */
static void trap(uint64_t cause)
{
    health_total_traps++;

    /* ── S-moda istisna delegasyonu (MEDELEG) ──────────────
     * Sadece senkron istisnalar (cause < 16) delegasyon adayı.
     */
    if (priv != PRIV_MACHINE &&
        cause < 16u &&
        (csr[CSR_MEDELEG] & (1u << cause)) &&
        csr[CSR_STVEC] != 0) {
        uint32_t st = csr[CSR_MSTATUS];
        csr[CSR_SEPC]   = pc;
        csr[CSR_SCAUSE] = cause;
        st = (st & ~MSTATUS_SPP) |
             ((priv == PRIV_SUPERVISOR) ? MSTATUS_SPP : 0u);
        if (st & MSTATUS_SIE) st |=  MSTATUS_SPIE;
        else                   st &= ~MSTATUS_SPIE;
        st &= ~MSTATUS_SIE;
        csr[CSR_MSTATUS] = st;
        priv          = PRIV_SUPERVISOR;
        csr[CSR_PRIV] = PRIV_SUPERVISOR;
        pipe_flush    = 1;
        pc            = csr[CSR_STVEC];
        if (!quiet)
            fprintf(stderr, "[TRAP-S] cause=%u → PC=0x%08X (STVEC)\n",
                    (unsigned)cause, (unsigned)pc);
        return;
    }

    /* ── Machine moda trap ──────────────────────────────── */
    csr[CSR_MEPC]    = pc;
    csr[CSR_MCAUSE]  = cause;
    /* MSTATUS.MPP = mevcut priv, bit[4:3] */
    csr[CSR_MSTATUS] = (csr[CSR_MSTATUS] & ~(uint64_t)0x18ull) |
                       ((uint64_t)(priv & 3u) << 3);
    if (csr[CSR_MSTATUS] & MSTATUS_MIE)
        csr[CSR_MSTATUS] |= MSTATUS_MPIE;
    else
        csr[CSR_MSTATUS] &= ~MSTATUS_MPIE;
    csr[CSR_MSTATUS] &= ~MSTATUS_MIE;
    priv             = PRIV_MACHINE;
    csr[CSR_PRIV]    = PRIV_MACHINE;
    pipe_flush       = 1;

    if (csr[CSR_MTVEC] == 0) {
        if (resilient && is_recoverable_cause(cause)) {
            if      (cause == CAUSE_ILL_INSN) health_illegal_recovered++;
            else if (cause == CAUSE_DIV_ZERO) health_div_recovered++;
            else                               health_mpu_recovered++;
            if (!quiet)
                fprintf(stderr,
                        "[SELF-HEAL] cause=%u kurtarıldı → PC=0x%08X'ten devam\n",
                        (unsigned)cause, (unsigned)pc);
            return;
        }
        fprintf(stderr, "[TRAP] MTVEC=0, HALT (cause=%u)\n", (unsigned)cause);
        halted = 1;
        return;
    }
    pc = csr[CSR_MTVEC];
    if (!quiet)
        fprintf(stderr, "[TRAP] cause=%u → PC=0x%08X (MTVEC)\n",
                (unsigned)cause, (unsigned)pc);
}

/*
 * Timer / harici / GPIO olaylarını kontrol edip MIP bitlerini günceller.
 */
static void poll_interrupt_sources(void)
{
    int i;

    if (csr[CSR_MTIMECMP] != 0 && cycles >= csr[CSR_MTIMECMP])
        csr[CSR_MIP] |= MIP_TIMER;

    if (irq_ext_at_cycle >= 0 && cycles >= (uint64_t)irq_ext_at_cycle) {
        csr[CSR_MIP] |= MIP_EXT;
        irq_ext_at_cycle = -1;
    }

    for (i = 0; i < gpio_event_count; i++) {
        if (gpio_events[i].cycle >= 0 &&
            cycles >= (uint64_t)gpio_events[i].cycle) {
            uint8_t  pin    = gpio_events[i].pin;
            uint8_t  val    = gpio_events[i].val;
            uint64_t old_in = gpio_input;
            uint64_t mask   = (1ull << (pin & 63u));
            gpio_input      = (gpio_input & ~mask) | (val ? mask : 0ull);
            {
                uint64_t changed   = old_in ^ gpio_input;
                uint64_t gpio_dir  = io_ports[IO_PORT_GPIO_DIR];
                uint64_t gpio_ien  = io_ports[IO_PORT_GPIO_INT_EN];
                uint64_t triggered = changed & ~gpio_dir & gpio_ien;
                if (triggered) {
                    io_ports[IO_PORT_GPIO_INT_ST] |= triggered;
                    csr[CSR_MIP] |= MIP_EXT;
                }
            }
            if (!quiet)
                fprintf(stderr, "[GPIO] cycle=%u pin=%u → %u\n",
                        (unsigned)cycles, (unsigned)pin, (unsigned)val);
            gpio_events[i].cycle = -1;
        }
    }
}

static int interrupt_pending(void)
{
    uint64_t pend_m = csr[CSR_MIP] & csr[CSR_MIE];
    if ((csr[CSR_MSTATUS] & MSTATUS_MIE) && pend_m) return 1;
    if (priv != PRIV_MACHINE) {
        uint64_t pend_s = csr[CSR_MIP] & csr[CSR_MIDELEG] & csr[CSR_SIE];
        if ((csr[CSR_MSTATUS] & MSTATUS_SIE) && pend_s) return 1;
    }
    return 0;
}

static void take_interrupt_s(uint64_t cause)
{
    uint64_t st = csr[CSR_MSTATUS];
    health_interrupts++;
    waiting_for_interrupt = 0;
    csr[CSR_SEPC]   = pc;
    csr[CSR_SCAUSE] = cause;
    st = (st & ~MSTATUS_SPP) |
         ((priv == PRIV_SUPERVISOR) ? MSTATUS_SPP : 0u);
    if (st & MSTATUS_SIE) st |=  MSTATUS_SPIE;
    else                   st &= ~MSTATUS_SPIE;
    st &= ~MSTATUS_SIE;
    csr[CSR_MSTATUS] = st;
    priv          = PRIV_SUPERVISOR;
    csr[CSR_PRIV] = PRIV_SUPERVISOR;
    pipe_flush    = 1;
    pipe_valid    = 0;
    pc            = csr[CSR_STVEC];
    if (!quiet)
        fprintf(stderr, "[IRQ-S] cause=0x%08X → PC=0x%08X (STVEC)\n",
                (unsigned)cause, (unsigned)pc);
}

static void take_interrupt(void)
{
    /* ── S-moda delegasyon? ─────────────────────────── */
    if (priv != PRIV_MACHINE) {
        uint64_t pend_s = csr[CSR_MIP] & csr[CSR_MIDELEG] & csr[CSR_SIE];
        if ((csr[CSR_MSTATUS] & MSTATUS_SIE) && pend_s) {
            uint64_t cause = (pend_s & MIP_TIMER) ? CAUSE_INT_TIMER : CAUSE_INT_EXT;
            if (csr[CSR_STVEC] == 0) {
                csr[CSR_MIP] &= ~pend_s;
                fprintf(stderr, "[IRQ-S] STVEC=0 — cause=0x%08X düşürüldü\n",
                        (unsigned)cause);
                return;
            }
            take_interrupt_s(cause);
            return;
        }
    }

    /* ── Machine moda kesme ─────────────────────────── */
    {
        uint64_t pend  = csr[CSR_MIP] & csr[CSR_MIE];
        uint64_t cause = (pend & MIP_TIMER) ? CAUSE_INT_TIMER : CAUSE_INT_EXT;

        if (csr[CSR_MTVEC] == 0) {
            csr[CSR_MIP] &= ~(pend & (MIP_TIMER | MIP_EXT));
            fprintf(stderr,
                    "[IRQ] MTVEC=0 — cause=0x%08X düşürüldü\n", (unsigned)cause);
            return;
        }

        health_interrupts++;
        waiting_for_interrupt = 0;
        csr[CSR_MEPC]    = pc;
        csr[CSR_MCAUSE]  = cause;
        csr[CSR_MSTATUS] = (csr[CSR_MSTATUS] & ~(uint64_t)0x18ull) |
                           ((uint64_t)(priv & 3u) << 3);
        if (csr[CSR_MSTATUS] & MSTATUS_MIE)
            csr[CSR_MSTATUS] |= MSTATUS_MPIE;
        else
            csr[CSR_MSTATUS] &= ~MSTATUS_MPIE;
        csr[CSR_MSTATUS] &= ~MSTATUS_MIE;
        priv          = PRIV_MACHINE;
        csr[CSR_PRIV] = PRIV_MACHINE;
        pipe_flush    = 1;
        pipe_valid    = 0;
        pc            = csr[CSR_MTVEC];
        if (!quiet)
            fprintf(stderr, "[IRQ] cause=0x%08X → PC=0x%08X (MTVEC)\n",
                    (unsigned)cause, (unsigned)pc);
    }
}

/*
 * CSR Erişim Tablosu:
 *  0-4   (PRIV/MEPC/MCAUSE/MTVEC/MSTATUS) : Machine-only
 *  5-7   (SEPC/STVEC/SATP)                 : S veya M
 *  8-38  (MPU, MIE/MIP/MTIMECMP/…)         : Machine-only
 *  39    (SIE)                              : S veya M
 *  40    (SCAUSE)                           : S veya M (salt okunur)
 */
static uint64_t csr_read(uint32_t idx)
{
    if (idx >= CSR_COUNT) return 0;

    if ((idx < 5u || (idx >= 8u && idx < (uint32_t)CSR_SIE)) &&
        priv != PRIV_MACHINE) {
        trap(CAUSE_CSR_PRIV);
        return 0;
    }
    if ((idx >= 5u && idx < 8u) || idx == (uint32_t)CSR_SIE ||
        idx == (uint32_t)CSR_SCAUSE) {
        if (priv == PRIV_USER) { trap(CAUSE_CSR_PRIV); return 0; }
    }

    if (idx == (uint32_t)CSR_PRIV)    return (uint64_t)priv;
    if (idx == (uint32_t)CSR_MTIME)   return cycles;
    if (idx == (uint32_t)CSR_MTIME_HI)return (cycles >> 32);  /* cycles 64-bit üst 32 bit */
    return csr[idx];
}

static void csr_write(uint32_t idx, uint64_t val)
{
    if (idx >= CSR_COUNT) return;

    if ((idx < 5u || (idx >= 8u && idx < (uint32_t)CSR_SIE)) &&
        priv != PRIV_MACHINE) {
        trap(CAUSE_CSR_PRIV);
        return;
    }
    if ((idx >= 5u && idx < 8u) || idx == (uint32_t)CSR_SIE) {
        if (priv == PRIV_USER) { trap(CAUSE_CSR_PRIV); return; }
    }

    /* Salt okunur CSR'lar */
    if (idx == (uint32_t)CSR_PRIV     ||
        idx == (uint32_t)CSR_MCAUSE   ||
        idx == (uint32_t)CSR_MTIME    ||
        idx == (uint32_t)CSR_MTIME_HI ||
        idx == (uint32_t)CSR_SCAUSE)
        return;

    csr[idx] = val;
}

/* Tüm CPU durumunu sıfırla */
void cpu_reset(void)
{
    int i;
    for (i = 0; i < REG_COUNT; i++)
        reg[i] = 0;
    memset(mem, 0, sizeof(mem));
    memset(io_ports, 0, sizeof(io_ports));
    memset(csr, 0, sizeof(csr));
    pc          = 0;
    halted      = 0;
    cycles      = 0;
    flags       = 0;
    priv        = PRIV_MACHINE;
    trng_lfsr   = 0xDEADBEEFDEADBEEFull;
    csr[CSR_PRIV] = PRIV_MACHINE;
    waiting_for_interrupt = 0;
    gpio_input       = 0;
    gpio_event_count = 0;
    for (i = 0; i < MAX_GPIO_EVENTS; i++)
        gpio_events[i].cycle = -1;
    pipe_insn   = 0;
    pipe_valid  = 0;
    pipe_flush  = 0;
    wdt_stall_count = 0;
    wdt_last_sig    = 0xFFFFFFFFFFFFFFFFull;
}

/*
 * Bellekten 32-bit komut oku, PC'yi 1 ilerlet.
 * Her komut tek bir 32-bit kelimedir (64-bit kelime içi alt 32 bit).
 */
static uint32_t fetch(void)
{
    uint32_t insn = (uint32_t)mem[pc];
    pc++;
    return insn;
}

/* Bir komut çalıştır */
void execute(uint32_t insn)
{
    uint32_t opcode  = (insn >> 26) & 0x3Fu;
    uint32_t fd      = (insn >> 21) & 0x1Fu;
    uint32_t fa      = (insn >> 16) & 0x1Fu;
    uint32_t fb      = (insn >> 11) & 0x1Fu;
    uint32_t imm_raw =  insn        & 0x7FFu;
    int64_t  imm     = sign_extend11(imm_raw);

    uint64_t addr;
    uint64_t result;

    (void)fb;

    switch (opcode) {

        /* ── Sistem ──────────────────────── */
        case OP_NOP:
            break;

        case OP_HALT:
            halted     = 1;
            pipe_flush = 1;
            break;

        /* ── Aritmetik / Mantık ──────────── */
        case OP_ADD: {
            uint64_t a = reg[fa], b = reg[fb];
            result = a + b;
            flags  = 0;
            if (result == 0)                                          flags |= FLAG_Z;
            if (result < a)                                           flags |= FLAG_C;  /* unsigned taşma */
            if (!((a ^ b) >> 63) && ((a ^ result) >> 63))            flags |= FLAG_V;  /* signed taşma */
            if (result >> 63)                                         flags |= FLAG_N;
            if (fd != 0) reg[fd] = result;
            break;
        }

        case OP_SUB: {
            uint64_t a = reg[fa], b = reg[fb];
            result = a - b;
            flags  = 0;
            if (result == 0)                                          flags |= FLAG_Z;
            if (a < b)                                                flags |= FLAG_C;  /* unsigned borç */
            if (((a ^ b) >> 63) && ((a ^ result) >> 63))             flags |= FLAG_V;  /* signed taşma */
            if (result >> 63)                                         flags |= FLAG_N;
            if (fd != 0) reg[fd] = result;
            break;
        }

        case OP_AND:
            result = reg[fa] & reg[fb];
            if (fd != 0) reg[fd] = result;
            break;

        case OP_OR:
            result = reg[fa] | reg[fb];
            if (fd != 0) reg[fd] = result;
            break;

        case OP_XOR:
            result = reg[fa] ^ reg[fb];
            if (fd != 0) reg[fd] = result;
            break;

        case OP_SHL:
            result = reg[fa] << (imm & 0x3Full);
            if (fd != 0) reg[fd] = result;
            break;

        case OP_SHR:
            result = reg[fa] >> (imm & 0x3Full);
            if (fd != 0) reg[fd] = result;
            break;

        case OP_MUL: {
            /* 64×64→64 bit çarpma (üst 64 bit kaybolur — FLAG_C/V bunu bildirir) */
            __uint128_t wide = (__uint128_t)reg[fa] * (__uint128_t)reg[fb];
            result = (uint64_t)wide;
            flags  = 0;
            if (result == 0)              flags |= FLAG_Z;
            if (wide > ((__uint128_t)0xFFFFFFFFFFFFFFFFull)) flags |= (FLAG_C | FLAG_V);
            if (result >> 63)             flags |= FLAG_N;
            if (fd != 0) reg[fd] = result;
            break;
        }

        case OP_DIV:
            if (reg[fb] == 0) {
                trap(CAUSE_DIV_ZERO);
                break;
            }
            result = (int64_t)reg[fa] / (int64_t)reg[fb];
            if (fd != 0) reg[fd] = result;
            break;

        /* ── Bellek ──────────────────────── */
        case OP_LOAD:
            addr = (uint32_t)((int32_t)reg[fa] + imm) & 0xFFFFu;
            if (fd != 0) reg[fd] = mem[addr];
            break;

        case OP_STORE:
            addr = (uint32_t)((int32_t)reg[fa] + imm) & 0xFFFFu;
            mem[addr] = reg[fd];
            break;

        /* ── Kalıcı Bellek (EEPROM/Flash) ─── */
        case OP_ELOAD:
            addr = (uint32_t)((int32_t)reg[fa] + imm) % EEPROM_SIZE;
            if (fd != 0) reg[fd] = eeprom[addr];
            break;

        case OP_ESTORE:
            addr = (uint32_t)((int32_t)reg[fa] + imm) % EEPROM_SIZE;
            eeprom[addr]  = reg[fd];
            eeprom_dirty  = 1;
            break;

        /* ── Bayrak (FLAGS) ────────────────── */
        case OP_RFLAGS:
            if (fd != 0) reg[fd] = flags & 0x000Fu;
            break;

        /* ── Immediate ───────────────────── */
        case OP_LI:
            /* IMM 17-bit işaret uzatmalı; -65536..+65535 yükler (64-bit) */
            if (fd != 0) reg[fd] = (uint64_t)imm;
            break;

        /* ── Dal / Atlama ────────────────── */
        case OP_JMP:
            pc        += (uint32_t)imm;
            pipe_flush = 1;
            break;

        case OP_JZ:
            if (reg[fd] == 0) {
                pc        += (uint32_t)imm;
                pipe_flush = 1;
            }
            break;

        case OP_JNZ:
            if (reg[fd] != 0) {
                pc        += (uint32_t)imm;
                pipe_flush = 1;
            }
            break;

        /* ── Alt Program ─────────────────── */
        case OP_CALL:
            reg[7]--;
            mem[reg[7] & 0xFFFFu] = pc;
            pc        += (uint32_t)imm;
            pipe_flush = 1;
            break;

        case OP_RET:
            pc         = mem[reg[7] & 0xFFFFu];
            reg[7]++;
            pipe_flush = 1;
            break;

        /* ── Giriş / Çıkış ───────────────── */
        case OP_OUT:
            if (priv == PRIV_USER) { trap(CAUSE_IO_PRIV); break; }
            {
                uint8_t port = (uint8_t)(imm & 0xFF);
                io_ports[port] = reg[fd];
                if (port == IO_PORT_UART_TX) {
                    putchar((int)(reg[fd] & 0xFF));
                    fflush(stdout);
                } else if (port == IO_PORT_GPIO_INT_ST) {
                    io_ports[IO_PORT_GPIO_INT_ST] &= ~reg[fd];
                    if (io_ports[IO_PORT_GPIO_INT_ST] == 0)
                        csr[CSR_MIP] &= ~MIP_EXT;
                } else if (!quiet) {
                    printf("[OUT] port[%d] = %llu (0x%016llX)\n",
                           (int)port, (unsigned long long)reg[fd], (unsigned long long)reg[fd]);
                }
            }
            break;

        case OP_IN:
            if (priv == PRIV_USER) { trap(CAUSE_IO_PRIV); break; }
            {
                uint8_t port = (uint8_t)(imm & 0xFF);
                if (port == IO_PORT_UART_RX) {
                    int ch = getchar();
                    if (fd != 0) reg[fd] = (ch == EOF) ? 0xFFFFFFFFFFFFFFFFull : (uint64_t)(ch & 0xFF);
                } else if (port == IO_PORT_GPIO_VAL) {
                    uint64_t dir = io_ports[IO_PORT_GPIO_DIR];
                    uint64_t out = io_ports[IO_PORT_GPIO_VAL];
                    if (fd != 0) reg[fd] = (out & dir) | (gpio_input & ~dir);
                } else {
                    if (fd != 0) reg[fd] = io_ports[port];
                }
            }
            break;

        /* ══ GÜVENLİK UZANTISI OPCODE'LARI ══════════════════ */

        case OP_ECALL:
            if      (priv == PRIV_USER)       trap(CAUSE_ECALL_U);
            else if (priv == PRIV_SUPERVISOR)  trap(CAUSE_ECALL_S);
            else {
                fprintf(stderr, "[ECALL] Machine moddan ECALL\n");
            }
            break;

        case OP_ERET:
            if (priv == PRIV_USER) { trap(CAUSE_ERET_PRIV); break; }
            if (priv == PRIV_SUPERVISOR) {
                /* SRET */
                priv = (csr[CSR_MSTATUS] & MSTATUS_SPP)
                       ? PRIV_SUPERVISOR : PRIV_USER;
                csr[CSR_PRIV] = priv;
                pc = csr[CSR_SEPC];
                if (csr[CSR_MSTATUS] & MSTATUS_SPIE)
                    csr[CSR_MSTATUS] |=  MSTATUS_SIE;
                else
                    csr[CSR_MSTATUS] &= ~MSTATUS_SIE;
                csr[CSR_MSTATUS] |=  MSTATUS_SPIE;
                csr[CSR_MSTATUS] &= ~MSTATUS_SPP;
                pipe_flush = 1;
                if (!quiet)
                    printf("[SRET] priv=%u, PC=0x%08X\n",
                           (unsigned)priv, (unsigned)pc);
            } else {
                /* MRET */
                priv          = (uint8_t)((csr[CSR_MSTATUS] >> 3) & 0x3u);
                csr[CSR_PRIV] = priv;
                pc             = csr[CSR_MEPC];
                pipe_flush     = 1;
                if (csr[CSR_MSTATUS] & MSTATUS_MPIE)
                    csr[CSR_MSTATUS] |= MSTATUS_MIE;
                else
                    csr[CSR_MSTATUS] &= ~MSTATUS_MIE;
                if (!quiet)
                    printf("[MRET] priv=%u, PC=0x%08X\n",
                           (unsigned)priv, (unsigned)pc);
            }
            break;

        case OP_CSRW:
            csr_write((uint32_t)(imm & 0x3Fu), reg[fd]);
            if (!quiet)
                printf("[CSRW] CSR[%u] = 0x%016llX\n",
                       (unsigned)(imm & 0x3Fu), (unsigned long long)reg[fd]);
            break;

        case OP_CSRR:
            result = csr_read((uint32_t)(imm & 0x3Fu));
            if (fd != 0) reg[fd] = result;
            if (!quiet)
                printf("[CSRR] CSR[%u] = 0x%016llX → R%u\n",
                       (unsigned)(imm & 0x3Fu), (unsigned long long)result, (unsigned)fd);
            break;

        case OP_RAND:
            if (priv != PRIV_MACHINE) { trap(CAUSE_CSR_PRIV); break; }
            result = trng_next();
            if (fd != 0) reg[fd] = result;
            if (!quiet)
                printf("[RAND] 0x%016llX → R%u\n", (unsigned long long)result, (unsigned)fd);
            break;

        case OP_FENCE:
            if (!quiet)
                printf("[FENCE]\n");
            break;

        case OP_WFI:
            if (priv == PRIV_USER) { trap(CAUSE_CSR_PRIV); break; }
            waiting_for_interrupt = 1;
            if (!quiet)
                printf("[WFI] kesme bekleniyor (priv=%s)...\n",
                       priv == PRIV_MACHINE ? "M" : "S");
            break;

        case OP_AESE:
            if (priv != PRIV_MACHINE) { trap(CAUSE_CSR_PRIV); break; }
            {
                /* 64-bit SubBytes: her byte bağımsız S-Box'tan geçer (8 byte) */
                uint8_t b0 = aes_sbox[ reg[fa]        & 0xFFu];
                uint8_t b1 = aes_sbox[(reg[fa] >>  8) & 0xFFu];
                uint8_t b2 = aes_sbox[(reg[fa] >> 16) & 0xFFu];
                uint8_t b3 = aes_sbox[(reg[fa] >> 24) & 0xFFu];
                uint8_t b4 = aes_sbox[(reg[fa] >> 32) & 0xFFu];
                uint8_t b5 = aes_sbox[(reg[fa] >> 40) & 0xFFu];
                uint8_t b6 = aes_sbox[(reg[fa] >> 48) & 0xFFu];
                uint8_t b7 = aes_sbox[(reg[fa] >> 56) & 0xFFu];
                result = ((uint64_t)b7 << 56) | ((uint64_t)b6 << 48) |
                         ((uint64_t)b5 << 40) | ((uint64_t)b4 << 32) |
                         ((uint64_t)b3 << 24) | ((uint64_t)b2 << 16) |
                         ((uint64_t)b1 <<  8) |  (uint64_t)b0;
                if (fd != 0) reg[fd] = result;
                if (!quiet)
                    printf("[AESE] S-Box(0x%016llX) = 0x%016llX → R%u\n",
                           (unsigned long long)reg[fa], (unsigned long long)result, (unsigned)fd);
            }
            break;

        case OP_HASH:
            if (priv != PRIV_MACHINE) { trap(CAUSE_CSR_PRIV); break; }
            {
                /* 64-bit SHA-512 benzeri bit karıştırma */
                uint64_t a   = reg[fa];
                uint64_t b   = reg[fb];
                uint64_t r28 = (a >> 28) | (a << 36);
                uint64_t r34 = (a >> 34) | (a << 30);
                uint64_t r39 = (a >> 39) | (a << 25);
                uint64_t sig = r28 ^ r34 ^ r39;
                uint64_t ch  = (a & b) ^ (~a & (b >> 1));
                uint64_t maj = (a & b) ^ (a & (b >> 2)) ^ (b & (b >> 2));
                result = sig + ch + maj;
                if (fd != 0) reg[fd] = result;
                if (!quiet)
                    printf("[HASH] step(0x%016llX,0x%016llX) = 0x%016llX → R%u\n",
                           (unsigned long long)a, (unsigned long long)b,
                           (unsigned long long)result, (unsigned)fd);
            }
            break;

        /* ── Bilinmeyen Opcode ────────────── */
        default:
            fprintf(stderr,
                    "[HATA] Bilinmeyen opcode: 0x%02X  "
                    "(adres: kelime 0x%08X)\n",
                    (unsigned)opcode, (unsigned)(pc - 1));
            trap(CAUSE_ILL_INSN);
            break;
    }

    /* R0 her zaman sıfır kalır */
    reg[0] = 0;
}

/*
 * ─── 2-Aşamalı Pipeline ile Tek Adım ──────────────────────
 *
 * NOVA-64'de her komut tek bir 32-bit kelimedir (64-bit kelimede saklanır); pc += 1 per fetch.
 */
void cpu_step(void)
{
    uint32_t insn;

    poll_interrupt_sources();

    if (waiting_for_interrupt) {
        cycles++;
        if (interrupt_pending()) {
            take_interrupt();
        }
        return;
    }

    if (halted) return;

    if (interrupt_pending()) {
        take_interrupt();
        return;
    }

    pipe_flush = 0;

    /* AŞAMA 1: FETCH */
    if (pipe_valid) {
        insn = pipe_insn;
        pc++;
        pipe_valid = 0;
    } else {
        insn = fetch();
    }

    /* AŞAMA 2: ÖNCEDEN AL */
    if (!halted && pc < MEM_SIZE) {
        pipe_insn  = mem[pc];
        pipe_valid = 1;
    }

    /* AŞAMA 3: EXECUTE */
    execute(insn);
    cycles++;

    /* AŞAMA 4: FLUSH */
    if (pipe_flush) {
        pipe_valid = 0;
        cycles++;
    }

    /* WATCHDOG */
    if (resilient && wdt_limit > 0 && !halted) {
        uint64_t sig = pc;
        int r;
        for (r = 0; r < REG_COUNT; r++)
            sig = (sig * 33ull) ^ reg[r];

        if (sig == wdt_last_sig) {
            wdt_stall_count++;
            if (wdt_stall_count >= wdt_limit) {
                health_watchdog_resets++;
                if (!quiet)
                    fprintf(stderr,
                            "[WATCHDOG] %u adım boyunca durum değişmedi — "
                            "yumuşak reset (PC=0x0000)\n",
                            (unsigned)wdt_limit);
                pc              = 0;
                pipe_valid      = 0;
                wdt_stall_count = 0;
                wdt_last_sig    = 0xFFFFFFFFu;
            }
        } else {
            wdt_stall_count = 0;
            wdt_last_sig    = sig;
        }
    }
}

void cpu_run(int max_cycles)
{
    while (!halted) {
        if (max_cycles >= 0 && (int)cycles >= max_cycles)
            break;
        cpu_step();
    }
}

/* ─── GDB Stub Accessor'ları ────────────────────────────── */

uint64_t nova_reg_get(int idx)             { return (idx >= 0 && idx < REG_COUNT) ? reg[idx] : 0; }
void     nova_reg_set(int idx, uint64_t v) { if (idx > 0 && idx < REG_COUNT) reg[idx] = v; } /* R0 sabit 0 */
uint32_t nova_pc_get(void)                 { return pc; }
void     nova_pc_set(uint32_t w)           { pc = w < MEM_SIZE ? w : pc; pipe_valid = 0; pipe_flush = 1; }
uint64_t nova_flags_get(void)              { return flags; }
void     nova_flags_set(uint64_t f)        { flags = f; }
uint8_t  nova_halted_get(void)             { return halted; }
void     nova_halted_set(uint8_t h)        { halted = h; }

/*
 * Bellek byte erişimi (GDB byte-addressed → NOVA-64 word-addressed).
 * NOVA-64 big-endian: word içi byte 0 = MSB (bit 63-56).
 *   byte_addr / 8  → word index
 *   byte_addr % 8  → byte offset (0=MSB, 7=LSB)
 */
uint8_t nova_mem_read_byte(uint32_t byte_addr)
{
    uint32_t waddr  = byte_addr >> 3;
    uint32_t offset = byte_addr & 7u;
    if (waddr >= MEM_SIZE) return 0xFF;
    return (uint8_t)(mem[waddr] >> (56u - offset * 8u));
}

void nova_mem_write_byte(uint32_t byte_addr, uint8_t val)
{
    uint32_t waddr  = byte_addr >> 3;
    uint32_t offset = byte_addr & 7u;
    uint64_t shift  = 56u - offset * 8u;
    if (waddr >= MEM_SIZE) return;
    mem[waddr] = (mem[waddr] & ~(0xFFull << shift)) | ((uint64_t)val << shift);
}

uint32_t nova_mem_size_bytes(void) { return (uint32_t)MEM_SIZE * 8u; }

void nova_step(void) { cpu_step(); }

/* CPU durumunu stdout'a yaz */
void cpu_dump(void)
{
    static const char *priv_name[] = {"Machine", "Supervisor", "User", "???"};
    static const char *csr_name[]  = {
        "PRIV   ","MEPC   ","MCAUSE ","MTVEC  ",
        "MSTATUS","SEPC   ","STVEC  ","SATP   "
    };
    int i;

    printf("╔════════════════════════════════════════════╗\n");
    printf("║       NOVA-64 SEC CPU Durumu               ║\n");
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ PC     : 0x%08X (kelime adr.)          ║\n", (unsigned)pc);
    printf("║ Cycles : %-20llu          ║\n", (unsigned long long)cycles);
    printf("║ Halted : %-3s                             ║\n", halted ? "EVET" : "HAYIR");
    printf("║ PRIV   : %-34s║\n", priv_name[priv < 3 ? priv : 3]);
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ Registerlar:                               ║\n");
    for (i = 0; i < REG_COUNT; i++) {
        const char *note = "       ";
        if (i == 0) note = " [zero]";
        if (i == 7) note = " [SP]  ";
        printf("║   R%d = %20llu  0x%016llX%s║\n",
               i, (unsigned long long)reg[i], (unsigned long long)reg[i], note);
    }
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ FLAGS  : Z=%d C=%d V=%d N=%d (0x%02X)            ║\n",
           (flags & FLAG_Z) ? 1 : 0, (flags & FLAG_C) ? 1 : 0,
           (flags & FLAG_V) ? 1 : 0, (flags & FLAG_N) ? 1 : 0,
           (unsigned)(flags & 0x000Fu));
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ CSR Registerlar (temel 8):                 ║\n");
    for (i = 0; i < 8; i++) {
        uint64_t val = (i == CSR_PRIV) ? (uint64_t)priv : csr[i];
        printf("║   CSR[%d] %s = 0x%016llX  ║\n",
               i, csr_name[i], (unsigned long long)val);
    }
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ Kesme (Interrupt) Durumu:                  ║\n");
    printf("║  Machine:                                  ║\n");
    printf("║   MIE      = 0x%016llX  MSTATUS.MIE=%d║\n",
           (unsigned long long)csr[CSR_MIE], (csr[CSR_MSTATUS] & MSTATUS_MIE) ? 1 : 0);
    printf("║   MIP      = 0x%016llX          ║\n", (unsigned long long)csr[CSR_MIP]);
    printf("║   MTIMECMP = 0x%016llX          ║\n", (unsigned long long)csr[CSR_MTIMECMP]);
    printf("║   MTIME    = 0x%016llX (= cyc) ║\n", (unsigned long long)cycles);
    printf("║   MIDELEG  = 0x%016llX          ║\n", (unsigned long long)csr[CSR_MIDELEG]);
    printf("║   MEDELEG  = 0x%016llX          ║\n", (unsigned long long)csr[CSR_MEDELEG]);
    printf("║  Supervisor:                               ║\n");
    printf("║   SIE      = 0x%016llX  MSTATUS.SIE=%d║\n",
           (unsigned long long)csr[CSR_SIE], (csr[CSR_MSTATUS] & MSTATUS_SIE) ? 1 : 0);
    printf("║   SIP      = 0x%016llX  (MIP&MIDELEG)║\n",
           (unsigned long long)(csr[CSR_MIP] & csr[CSR_MIDELEG]));
    printf("║   SCAUSE   = 0x%016llX          ║\n", (unsigned long long)csr[CSR_SCAUSE]);
    printf("║  WFI bekleme: %-27s║\n",
           waiting_for_interrupt ? "EVET" : "HAYIR");
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ GPIO Durumu (32 pin):                      ║\n");
    printf("║   GPIO_DIR = 0x%016llX (1=çıkış)║\n",
           (unsigned long long)io_ports[IO_PORT_GPIO_DIR]);
    printf("║   GPIO_VAL = 0x%016llX (çıkış) ║\n",
           (unsigned long long)io_ports[IO_PORT_GPIO_VAL]);
    printf("║   gpio_in  = 0x%016llX (giriş) ║\n", (unsigned long long)gpio_input);
    printf("║   INT_EN   = 0x%016llX          ║\n",
           (unsigned long long)io_ports[IO_PORT_GPIO_INT_EN]);
    printf("║   INT_ST   = 0x%016llX          ║\n",
           (unsigned long long)io_ports[IO_PORT_GPIO_INT_ST]);
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ G/C Portlari (sadece yazilanlar):          ║\n");
    {
        int found = 0;
        for (i = 0; i < IO_PORTS; i++) {
            if (io_ports[i] != 0) {
                printf("║   port[%3d] = %20llu 0x%016llX║\n",
                       i, (unsigned long long)io_ports[i], (unsigned long long)io_ports[i]);
                found = 1;
            }
        }
        if (!found)
            printf("║   (yok)                                    ║\n");
    }
    printf("╚════════════════════════════════════════════╝\n");
}

/* ─── .bin Dosyası Yükleme ─────────────────────────────── */

/*
 * .bin dosyası big-endian 32-bit kelimelerden oluşur.
 * Her dört byte bir mem[] girişi olarak yüklenir.
 * Assembler da aynı formatı üretir (her komut = 4 byte).
 */
static int load_bin(const char *path)
{
    FILE   *f;
    int     b1, b2, b3, b4, i;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[HATA] Dosya açılamadı: %s\n", path);
        return 0;
    }

    i = 0;
    while (i < MEM_SIZE) {
        b1 = fgetc(f);
        b2 = fgetc(f);
        b3 = fgetc(f);
        b4 = fgetc(f);
        if (b1 == EOF || b2 == EOF || b3 == EOF || b4 == EOF) break;
        /* Komutlar 32-bit; 64-bit kelime içi alt 32 bite yükle */
        mem[i++] = ((uint64_t)b1 << 24) | ((uint64_t)b2 << 16) |
                   ((uint64_t)b3 <<  8) |  (uint64_t)b4;
    }

    fclose(f);
    if (!quiet)
        printf("[BİLGİ] %d kelime (%d byte program, %d byte bellek) belleğe yüklendi.\n", i, i * 4, i * 8);
    return 1;
}

/* ─── EEPROM/Flash Kalıcılığı ──────────────────────────── */

static void eeprom_load(const char *path)
{
    FILE *f;
    int   b1, b2, b3, b4, i;

    memset(eeprom, 0, sizeof(eeprom));
    f = fopen(path, "rb");
    if (!f) return;

    i = 0;
    while (i < EEPROM_SIZE) {
        b1 = fgetc(f); b2 = fgetc(f);
        b3 = fgetc(f); b4 = fgetc(f);
        if (b1 == EOF || b2 == EOF || b3 == EOF || b4 == EOF) break;
        eeprom[i++] = ((uint64_t)b1 << 24) | ((uint64_t)b2 << 16) |
                      ((uint64_t)b3 <<  8) |  (uint64_t)b4;
    }
    fclose(f);
    if (!quiet)
        printf("[EEPROM] %d kelime '%s' dosyasından yüklendi (kalıcı).\n",
               i, path);
}

static void eeprom_save(const char *path)
{
    FILE    *f;
    int      i;
    uint64_t v;

    if (!eeprom_dirty) return;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[HATA] EEPROM dosyası yazılamadı: %s\n", path);
        return;
    }
    for (i = 0; i < EEPROM_SIZE; i++) {
        v = eeprom[i];
        fputc((v >> 24) & 0xFF, f);
        fputc((v >> 16) & 0xFF, f);
        fputc((v >>  8) & 0xFF, f);
        fputc( v        & 0xFF, f);
    }
    fclose(f);
    if (!quiet)
        printf("[EEPROM] %d kelime '%s' dosyasına kalıcı olarak yazıldı.\n",
               EEPROM_SIZE, path);
}

/* ─── main ─────────────────────────────────────────────── */

static int parse_uint_arg(const char *s, unsigned int *out)
{
    *out = 0;
    if (!(*s >= '0' && *s <= '9')) return 0;
    while (*s >= '0' && *s <= '9') *out = *out * 10 + (unsigned)(*s++ - '0');
    return *s == '\0';
}

int main(int argc, char *argv[])
{
    int          max_cycles = -1;
    int          gdb_port   = -1;   /* >= 0 ise GDB RSP modu aktif */
    int          i;
    clock_t      t_start, t_end;
    double       elapsed;

    if (argc < 2) {
        fprintf(stderr,
            "Kullanım: %s <program.bin> [seçenekler]\n"
            "\n"
            "Seçenekler:\n"
            "  -c <N>        En fazla N komut çalıştır (varsayılan: sınırsız)\n"
            "  -p <port> <v> Başlamadan önce port'u v değerine ayarla\n"
            "  -q            Sessiz mod\n"
            "  -b            Benchmark modu\n"
            "  -r            RESILIENCE: kendi kendini onarma modu\n"
            "  -w <N>        Watchdog eşiği (varsayılan 100000, 0=kapalı)\n"
            "  -h            Sağlık/dayanıklılık raporu\n"
            "  -e <dosya>    EEPROM/flash kalıcı bellek dosyası\n"
            "  -i <N>        N. cycle'da harici kesme tetikle\n"
            "  -G <hex>      Başlangıç GPIO giriş pini değeri (32-bit)\n"
            "  -g <cy> <p> <v>  cy. cycle'da p. pini v değerine ayarla\n"
            "  --gdb <PORT>  GDB RSP stub'ı <PORT> üzerinde başlat\n"
            "\n"
            "UART: OUT R, 0xFE → TX; IN R, 0xFF → RX (EOF=0xFFFFFFFFFFFFFFFF)\n"
            "GPIO portları: 0xF0=DIR 0xF1=VAL 0xF2=INT_EN 0xF3=INT_ST\n"
            "\n"
            "Donanım Kesmeleri:\n"
            "  CSR[32]=MIE CSR[33]=MIP CSR[34]=MTIMECMP\n"
            "  CSR[35]=MTIME (salt okunur, 32-bit cycle sayacı)\n"
            "  CSR[37]=MIDELEG CSR[38]=MEDELEG CSR[39]=SIE CSR[40]=SCAUSE\n"
            "\n"
            "Örnek:\n"
            "  %s program.bin -c 1000\n"
            "  %s gpio_test.bin -G 0xFF -g 100 3 1 -h\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    /* İlk geçiş: erken bayraklar */
    for (i = 2; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'q' && argv[i][2] == '\0')
            quiet = 1;
        if (argv[i][0] == '-' && argv[i][1] == 'b' && argv[i][2] == '\0')
            bench = 1;
        if (argv[i][0] == '-' && argv[i][1] == 'r' && argv[i][2] == '\0')
            resilient = 1;
        if (argv[i][0] == '-' && argv[i][1] == 'h' && argv[i][2] == '\0')
            health_report = 1;
        if (strcmp(argv[i], "--gdb") == 0 && i + 1 < argc) {
            unsigned int p; i++;
            if (parse_uint_arg(argv[i], &p) && p > 0 && p < 65536)
                gdb_port = (int)p;
            else
                fprintf(stderr, "[UYARI] --gdb: geçersiz port: %s\n", argv[i]);
        }
        if (argv[i][0] == '-' && argv[i][1] == 'w' && argv[i][2] == '\0') {
            unsigned int w;
            resilient = 1;
            if (i + 1 < argc && parse_uint_arg(argv[i+1], &w))
                wdt_limit = (uint32_t)w;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'e' && argv[i][2] == '\0') {
            if (i + 1 < argc) {
                strncpy(eeprom_path, argv[i+1], sizeof(eeprom_path) - 1);
                eeprom_path[sizeof(eeprom_path) - 1] = '\0';
            }
        }
        if (argv[i][0] == '-' && argv[i][1] == 'i' && argv[i][2] == '\0') {
            unsigned int c;
            if (i + 1 < argc && parse_uint_arg(argv[i+1], &c))
                irq_ext_at_cycle = (int64_t)c;
        }
    }

    cpu_reset();
    eeprom_load(eeprom_path);

    if (!load_bin(argv[1]))
        return 1;

    /* İkinci geçiş: kalan argümanlar (GPIO dahil — cpu_reset() sonrası) */
    for (i = 2; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'c' && argv[i][2] == '\0') {
            if (i + 1 >= argc) {
                fprintf(stderr, "[HATA] -c seçeneği değer gerektiriyor\n");
                return 1;
            }
            i++;
            {
                const char *s = argv[i];
                int sign = 1;
                max_cycles = 0;
                if (*s == '-') { sign = -1; s++; }
                while (*s >= '0' && *s <= '9')
                    max_cycles = max_cycles * 10 + (*s++ - '0');
                max_cycles *= sign;
            }
        } else if (argv[i][0] == '-' && argv[i][1] == 'p' && argv[i][2] == '\0') {
            unsigned int port_no, port_val;
            if (i + 2 >= argc) {
                fprintf(stderr, "[HATA] -p iki değer gerektiriyor\n");
                return 1;
            }
            if (!parse_uint_arg(argv[i+1], &port_no) || port_no >= IO_PORTS) {
                fprintf(stderr, "[HATA] Geçersiz port: %s\n", argv[i+1]);
                return 1;
            }
            if (!parse_uint_arg(argv[i+2], &port_val)) {
                fprintf(stderr, "[HATA] Geçersiz değer: %s\n", argv[i+2]);
                return 1;
            }
            io_ports[port_no] = port_val;
            if (!quiet)
                printf("[BİLGİ] port[%u] = %u önceden ayarlandı\n",
                       port_no, port_val);
            i += 2;
        } else if (argv[i][0] == '-' && argv[i][1] == 'w' && argv[i][2] == '\0') {
            i++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'e' && argv[i][2] == '\0') {
            i++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'i' && argv[i][2] == '\0') {
            i++;
        } else if (argv[i][0] == '-' && argv[i][1] == 'G' && argv[i][2] == '\0') {
            if (i + 1 < argc) {
                unsigned int gv = 0;
                const char *s = argv[i+1];
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    s += 2;
                    while (*s) {
                        gv <<= 4;
                        if (*s >= '0' && *s <= '9') gv |= (unsigned)(*s - '0');
                        else if (*s >= 'a' && *s <= 'f') gv |= (unsigned)(*s - 'a' + 10);
                        else if (*s >= 'A' && *s <= 'F') gv |= (unsigned)(*s - 'A' + 10);
                        s++;
                    }
                } else {
                    parse_uint_arg(s, &gv);
                }
                gpio_input = gv;
                i++;
            }
        } else if (argv[i][0] == '-' && argv[i][1] == 'g' && argv[i][2] == '\0') {
            if (i + 3 < argc && gpio_event_count < MAX_GPIO_EVENTS) {
                unsigned int cy, pin, val;
                if (parse_uint_arg(argv[i+1], &cy) &&
                    parse_uint_arg(argv[i+2], &pin) &&
                    parse_uint_arg(argv[i+3], &val) &&
                    pin < 32) {
                    gpio_events[gpio_event_count].cycle = (int64_t)cy;
                    gpio_events[gpio_event_count].pin   = (uint8_t)pin;
                    gpio_events[gpio_event_count].val   = (uint8_t)(val ? 1 : 0);
                    gpio_event_count++;
                } else {
                    fprintf(stderr, "[HATA] -g: beklenen -g <cycle> <pin 0-31> <0|1>\n");
                }
            }
            i += 3;
        } else if (argv[i][0] == '-' &&
                   ((argv[i][1] == 'q' || argv[i][1] == 'b' ||
                     argv[i][1] == 'r' || argv[i][1] == 'h') && argv[i][2] == '\0')) {
            /* zaten ilk geçişte işlendi */
        } else if (strcmp(argv[i], "--gdb") == 0) {
            i++; /* PORT argümanını atla, ilk geçişte zaten işlendi */
        } else if (argv[i][0] != '-') {
            const char *s = argv[i];
            int sign = 1;
            max_cycles = 0;
            if (*s == '-') { sign = -1; s++; }
            while (*s >= '0' && *s <= '9')
                max_cycles = max_cycles * 10 + (*s++ - '0');
            max_cycles *= sign;
        } else {
            fprintf(stderr, "[UYARI] Bilinmeyen seçenek: %s\n", argv[i]);
        }
    }

    if (!quiet)
        printf("[BİLGİ] NOVA-64 simülasyonu başlıyor "
               "(max_cycles=%d, pipeline=ON)...\n\n", max_cycles);

    if (gdb_port >= 0) {
        /* GDB RSP modu: stub bağlantıyı alır ve çalıştırmayı yönetir */
        if (gdb_stub_init(gdb_port) < 0) return 1;
        gdb_stub_run();
        t_start = t_end = clock();
    } else {
        t_start = clock();
        cpu_run(max_cycles);
        t_end   = clock();
    }

    if (!quiet) printf("\n");

    cpu_dump();

    if (bench) {
        elapsed = (double)(t_end - t_start) / (double)CLOCKS_PER_SEC;
        printf("\n");
        printf("╔════════════════════════════════════════════╗\n");
        printf("║         NOVA-64 Benchmark Sonucu           ║\n");
        printf("╠════════════════════════════════════════════╣\n");
        printf("║ Toplam cycles  : %-24llu║\n", (unsigned long long)cycles);
        if (elapsed > 0.0) {
            double mips = (double)(unsigned long long)cycles / elapsed / 1e6;
            printf("║ Gerçek süre    : %-21.6f s ║\n", elapsed);
            printf("║ Hız            : %-18.3f MIPS  ║\n", mips);
        } else {
            printf("║ Gerçek süre    : <ölçülemez (çok hızlı)>  ║\n");
        }
        printf("╚════════════════════════════════════════════╝\n");
    }

    if (health_report) {
        printf("\n");
        printf("╔════════════════════════════════════════════╗\n");
        printf("║     NOVA-64 Sağlık / Dayanıklılık          ║\n");
        printf("╠════════════════════════════════════════════╣\n");
        printf("║ Resilience modu    : %-20s║\n", resilient ? "AÇIK" : "KAPALI");
        printf("║ Watchdog eşiği     : %-20u║\n", (unsigned)(resilient ? wdt_limit : 0));
        printf("║ Toplam trap        : %-20u║\n", (unsigned)health_total_traps);
        printf("║ Illegal opcode kurt.: %-19u║\n", (unsigned)health_illegal_recovered);
        printf("║ MPU ihlali kurt.    : %-19u║\n", (unsigned)health_mpu_recovered);
        printf("║ Sıfıra bölme kurt.  : %-19u║\n", (unsigned)health_div_recovered);
        printf("║ Watchdog reset      : %-19u║\n", (unsigned)health_watchdog_resets);
        printf("║ Alınan kesme (IRQ)  : %-19u║\n", (unsigned)health_interrupts);
        printf("╚════════════════════════════════════════════╝\n");
    }

    eeprom_save(eeprom_path);

    return halted ? 0 : 1;
}
