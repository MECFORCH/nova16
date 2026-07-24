---
name: NOVA-64 migration decisions
description: Key decisions and pitfalls when migrating NOVA-32 to 64-bit data path
---

## The rule
Instruction format stays 32-bit (fixed-width RISC style — ARM64/RISC-V pattern); only data types widen to 64-bit.

**Why:** Assembler (.asm→.bin), existing .bin test files, and hardware pipeline all encode 32-bit instructions. Widening instructions would break the entire toolchain.

**How to apply:** When loading .bin: store each 32-bit instruction in the lower 32 bits of a 64-bit memory word (`mem[i] = (uint64_t)b1<<24 | ...`). fetch() casts back: `(uint32_t)mem[pc]`.

## Critical pitfall: interrupt cause path must be fully widened
CAUSE_INT_FLAG lives at bit 63 (`0x8000000000000000ull`). If `trap()`, `is_recoverable_cause()`, `take_interrupt_s()`, or local `cause` variables remain `uint32_t`, the high bit is silently truncated and async interrupts lose their flag semantics.
**Fix:** All cause-related signatures and locals must be `uint64_t`.

## GDB stub: address conversion changes from ÷4 to ÷8
NOVA-64 words are 8 bytes. GDB byte addresses must be divided by 8 (not 4) for word index conversion. Register serialization changes from 8 hex chars per register (32-bit) to 16 hex chars (64-bit). Both `gdb_stub.c` and `gdb_stub.h` must be updated together.

## Byte addressing in memory accessor functions
`nova_mem_read_byte` / `nova_mem_write_byte`: shift from `>>2`/`&3`/`24u-offset*8` (32-bit big-endian) to `>>3`/`&7`/`56u-offset*8` (64-bit big-endian).

## Formal verification: props file needs 64-bit wire widths
`wire [31:0]` → `wire [63:0]` for mem_rdata, io_rdata, mem_wdata, io_wdata, cycles in the formal wrapper. Assert literals: `64'd0` not `32'd0`.

## Arithmetic changes
- ADD/SUB: overflow detection uses bit-63 sign formulas (not 32-bit masks)
- MUL: use `__uint128_t` for overflow detection on 64-bit operands
- SHL/SHR: shift mask `& 0x3F` (6 bits for 0..63) not `& 0x1F`
- TRNG LFSR: tap at bit 63 (not bit 31)
- FLAG_N: `result >> 63` not `result & 0x80000000u`

## Yosys formal: flatten + async2sync required
SAT backend fails on `$adff` cells without `flatten; async2sync;` in the script. This was already needed pre-migration and remains unchanged post-migration.

## Cell count after migration
Synthesis: ~57 926 cells (up from ~18 162 for 32-bit) due to 64-bit ALU/datapath width. Expected and acceptable.
