// ============================================================
// nova32_props.v — NOVA-64 Formal Doğrulama Özellikleri
//
// Yosys SAT ile çalıştır:
//   yosys -Q -p "
//     read_verilog -formal ../cpu.v formal/nova32_props.v;
//     prep -top nova32_cpu_formal;
//     flatten; async2sync;
//     sat -prove-asserts -seq 20 -show-inputs -show-outputs" 2>&1
//
// ya da: make formal
//
// Doğrulanan özellikler (depth=10; 32 register × 64-bit = geniş durum uzayı):
//   P1 — R0 her zaman sıfır (reset sonrası)
//   P2 — halted yapışkandır (bir kez duran CPU devam etmez)
//   P3 — cycles yalnızca artar (ya da reset ile sıfırlanır)
//   P4 — state geçerli değerlerde kalır (0-4 arası)
// ============================================================

`timescale 1ns / 1ps

module nova32_cpu_formal (
    input wire clk,
    input wire rst_n
);

    // ── Stub bellek/IO sinyalleri ────────────────────────
    wire [15:0] mem_addr;
    wire        mem_we;
    wire [63:0] mem_wdata;
    reg  [63:0] mem_rdata;
    wire [7:0]  io_addr;
    wire        io_we;
    wire [63:0] io_wdata;
    reg  [63:0] io_rdata;
    wire        halted;
    wire [63:0] cycles;

    // Formal: bellek ve IO'yu sabit tut (herhangi bir değer alabilir,
    // ancak senkron bellek modeline uygun — tek saat gecikmeli okuma)
    always @(*) begin
        mem_rdata = $anyseq;
        io_rdata  = $anyseq;
    end

    // ── CPU örneği ───────────────────────────────────────
    nova32_cpu uut (
        .clk      (clk),
        .rst_n    (rst_n),
        .mem_addr (mem_addr),
        .mem_we   (mem_we),
        .mem_wdata(mem_wdata),
        .mem_rdata(mem_rdata),
        .io_addr  (io_addr),
        .io_we    (io_we),
        .io_wdata (io_wdata),
        .io_rdata (io_rdata),
        .halted   (halted),
        .cycles   (cycles)
    );

    // ── Başlangıç varsayımı ──────────────────────────────
    // Formal analizde rst_n'nin deassert edildiğinden başla
    initial assume (!rst_n);

    // ── P1: R0 her zaman sıfır ───────────────────────────
    // cpu.v'de regs[0] her zaman 0'a yazılır (write_reg R0'ı reddeder).
    always @(posedge clk) begin
        if (rst_n)
            assert (uut.regs[0] == 64'd0);
    end

    // ── P2: halted yapışkandır ───────────────────────────
    // Bir kez halted=1 olursa bir sonraki döngüde de halted=1 olmalı.
    always @(posedge clk) begin
        if (rst_n && $past(halted) && $past(rst_n))
            assert (halted == 1'b1);
    end

    // ── P3: cycles monoton artar ─────────────────────────
    // Sıfırlama yoksa cycles bir önceki değerden küçük olamaz.
    always @(posedge clk) begin
        if (rst_n && $past(rst_n))
            assert (cycles >= $past(cycles));
    end

    // ── P4: state geçerli aralıkta ───────────────────────
    // state [2:0] sinyali yalnızca 0-4 değerlerini alabilir (5 durum tanımlı).
    always @(posedge clk) begin
        if (rst_n)
            assert (uut.state <= 3'd4);
    end

endmodule
