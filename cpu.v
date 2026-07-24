// ============================================================
// NOVA-64 CPU — Verilog RTL (Register Transfer Level)
//
// Hedef   : Herhangi bir FPGA (Xilinx, Intel/Altera, Lattice)
// Sentez  : Xilinx Vivado / Intel Quartus / Yosys (açık kaynak)
//
// Mimari  : 64-bit veri yolu, 32 register, sabit 32-bit komut
//           (tek kelime = tek komut, sim.c/asm.c/dbg.c ile birebir uyumlu)
// Bellek  : Harici SRAM/Block-RAM (port üzerinden), 65536 kelime,
//           16-bit kelime adresi (adres yolu genişliği veri yolundan bağımsız)
// Pipeline: Yok (tek döngülü fetch-decode-execute)
// ============================================================

`timescale 1ns / 1ps

module nova32_cpu (
    // ── Saat ve Sıfırlama ───────────────────────────────
    input  wire        clk,      // sistem saati
    input  wire        rst_n,    // aktif-düşük asenkron sıfırlama

    // ── Bellek Arayüzü ──────────────────────────────────
    // Tek portlu, 64-bit word-addressed bellek (65536 kelime → 16-bit adres)
    // mem_addr/mem_we/mem_wdata KOMBİNASYONELDİR (senkron SRAM'e doğrudan
    // sürülür); mem_rdata bir sonraki saat kenarında geçerli olur.
    output reg  [15:0] mem_addr,  // bellek adresi (kelime)
    output reg         mem_we,    // write enable
    output reg  [63:0] mem_wdata, // yazılacak veri (64-bit)
    input  wire [63:0] mem_rdata, // okunan veri (64-bit)

    // ── G/Ç Arayüzü ─────────────────────────────────────
    output reg  [7:0]  io_addr,   // port numarası
    output reg         io_we,     // port yazma
    output reg  [63:0] io_wdata,  // yazılacak port verisi (64-bit)
    input  wire [63:0] io_rdata,  // okunan port verisi (64-bit)

    // ── Durum Çıkışları ──────────────────────────────────
    output reg         halted,    // HALT komutu çalıştı
    output reg  [63:0] cycles     // çalıştırılan komut sayısı (64-bit)
);

// ─── Opcode Sabitleri ────────────────────────────────────
localparam OP_NOP    = 6'h00;
localparam OP_ADD    = 6'h01;
localparam OP_SUB    = 6'h02;
localparam OP_AND    = 6'h03;
localparam OP_OR     = 6'h04;
localparam OP_XOR    = 6'h05;
localparam OP_SHL    = 6'h06;
localparam OP_SHR    = 6'h07;
localparam OP_LOAD   = 6'h08;
localparam OP_STORE  = 6'h09;
localparam OP_LI     = 6'h0A;
localparam OP_JMP    = 6'h0B;
localparam OP_JZ     = 6'h0C;
localparam OP_JNZ    = 6'h0D;
localparam OP_CALL   = 6'h0E;
localparam OP_RET    = 6'h0F;
localparam OP_OUT    = 6'h10;
localparam OP_IN     = 6'h11;
localparam OP_MUL    = 6'h1A;
localparam OP_DIV    = 6'h1B;
localparam OP_RFLAGS = 6'h1C;
localparam OP_HALT   = 6'h3F;

// ─── Durum Makinesi ─────────────────────────────────────
//
// NOVA-64'de her komut TEK 32-bit kelime olduğundan fetch tek adımda
// yapılır. mem_addr KOMBİNASYONEL olarak sürülür (registered değil),
// böylece senkron bellek bir sonraki saat kenarında doğru veriyi
// mem_rdata'ya yazar:
//
//   FETCH     → mem_addr = pc  (kombinasyonel)         → mem_rdata bir
//               sonraki kenarda mem[pc] olur
//   EXECUTE   → mem_rdata artık komut kelimesi; decode + çalıştır.
//               LOAD/STORE/CALL/RET için mem_addr yine kombinasyonel
//               olarak bu döngüde sürülür.
//   LOAD_WAIT / RET_WAIT / IN_WAIT → LOAD, RET ve IN için ek okuma
//               bekleme adımı (adres/istek EXECUTE'ta gönderilir,
//               veri bir döngü sonra mem_rdata/io_rdata'da hazır olur)
//
localparam S_FETCH     = 3'd0;
localparam S_EXECUTE   = 3'd1;
localparam S_LOAD_WAIT = 3'd2;
localparam S_RET_WAIT  = 3'd3;
localparam S_IN_WAIT   = 3'd4;

reg [2:0] state;

// ─── Register Dosyası ────────────────────────────────────
// regs[0] her zaman 0 (yazma yoksayılır)
reg [63:0] regs [0:31];

// ─── Dahili Sinyaller ────────────────────────────────────
reg [15:0] pc;          // program counter (kelime adresi)
reg [4:0]  wb_dest;     // LOAD/IN tamamlanınca yazılacak hedef register

// ─── Register Yazma Yardımcısı (R0 korumalı) ────────────
task write_reg;
    input [4:0]  rn;
    input [63:0] val;
    begin
        if (rn != 5'd0)
            regs[rn] <= val;
    end
endtask

// ─── Komut Alanları (EXECUTE'ta mem_rdata[31:0] = geçerli komut) ───
// Komut 32-bit; 64-bit bellek kelimesinin alt 32 bitinde saklanır.
wire [31:0] insn    = mem_rdata[31:0];
wire [5:0]  op      = insn[31:26];
wire [4:0]  fd      = insn[25:21];
wire [4:0]  fa      = insn[20:16];
wire [4:0]  fb      = insn[15:11];
wire [10:0] raw_imm = insn[10:0];
wire signed [63:0] imm_s = {{53{raw_imm[10]}}, raw_imm};  // 11→64 bit işaret uzatma

wire [63:0] rv_fd = (fd == 5'd0) ? 64'd0 : regs[fd];
wire [63:0] rv_fa = (fa == 5'd0) ? 64'd0 : regs[fa];
wire [63:0] rv_fb = (fb == 5'd0) ? 64'd0 : regs[fb];

wire [15:0] next_pc     = pc + 16'd1;            // fetch sonrası PC
wire [15:0] branch_dst  = next_pc + imm_s[15:0]; // göreli dal hedefi
wire [15:0] mem_eff     = rv_fa[15:0] + imm_s[15:0]; // LOAD/STORE efektif adres

// ─── Kombinasyonel Bellek/G-Ç Yolu Sürücüsü ─────────────
always @(*) begin
    mem_addr  = pc;
    mem_we    = 1'b0;
    mem_wdata = 64'd0;
    io_addr   = 8'd0;
    io_we     = 1'b0;
    io_wdata  = 64'd0;

    case (state)
        S_FETCH: mem_addr = pc;

        S_EXECUTE: begin
            case (op)
                OP_LOAD:  mem_addr = mem_eff;
                OP_STORE: begin
                    mem_addr  = mem_eff;
                    mem_we    = 1'b1;
                    mem_wdata = rv_fd;
                end
                OP_CALL: begin
                    mem_addr  = regs[7][15:0] - 16'd1; // yeni R7 (push)
                    mem_we    = 1'b1;
                    mem_wdata = {48'd0, next_pc};
                end
                OP_RET:  mem_addr = regs[7][15:0];     // mevcut R7 (pop)
                OP_OUT: begin
                    io_addr  = raw_imm[7:0];
                    io_we    = 1'b1;
                    io_wdata = rv_fd;
                end
                OP_IN:   io_addr = raw_imm[7:0];
                default: ;
            endcase
        end

        default: ; // LOAD_WAIT/RET_WAIT/IN_WAIT: adres önemsiz
    endcase
end

// ─── Ana Durum Makinesi ──────────────────────────────────
integer i;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        // ── Sıfırlama ──
        state   <= S_FETCH;
        pc      <= 16'd0;
        halted  <= 1'b0;
        cycles  <= 64'd0;
        wb_dest <= 5'd0;
        for (i = 0; i < 32; i = i + 1)
            regs[i] <= 64'd0;
    end
    else if (!halted) begin

        case (state)

            // ────────────────────────────────────────────
            // FETCH — mem_addr (kombinasyonel) = pc gönderildi;
            // bir sonraki döngüde mem_rdata geçerli olacak.
            // ────────────────────────────────────────────
            S_FETCH: state <= S_EXECUTE;

            // ────────────────────────────────────────────
            // EXECUTE — Komutu çalıştır (mem_rdata[31:0] = komut)
            // ────────────────────────────────────────────
            S_EXECUTE: begin
                pc     <= pc + 16'd1;   // varsayılan ilerleme (dallanma override eder)
                cycles <= cycles + 64'd1;
                state  <= S_FETCH;      // varsayılan; LOAD/RET/IN override eder

                case (op)
                    OP_NOP: ; // hiçbir şey yapma

                    OP_HALT: halted <= 1'b1;

                    OP_ADD: write_reg(fd, $signed(rv_fa) + $signed(rv_fb));
                    OP_SUB: write_reg(fd, $signed(rv_fa) - $signed(rv_fb));
                    OP_AND: write_reg(fd, rv_fa & rv_fb);
                    OP_OR:  write_reg(fd, rv_fa | rv_fb);
                    OP_XOR: write_reg(fd, rv_fa ^ rv_fb);

                    OP_SHL: write_reg(fd, rv_fa << raw_imm[5:0]);
                    OP_SHR: write_reg(fd, rv_fa >> raw_imm[5:0]);

                    OP_LI: write_reg(fd, imm_s);

                    OP_MUL: write_reg(fd, $signed(rv_fa) * $signed(rv_fb));

                    OP_DIV: begin
                        if (rv_fb == 64'd0)
                            write_reg(fd, 64'd0);
                        else
                            write_reg(fd, $signed(rv_fa) / $signed(rv_fb));
                    end

                    OP_RFLAGS: write_reg(fd, 64'd0); // basitleştirilmiş RTL: bayrak reg. yok

                    OP_LOAD: begin
                        // RD = mem[RA + IMM]; adres bu döngüde kombinasyonel gönderildi
                        wb_dest <= fd;
                        state   <= S_LOAD_WAIT;
                    end

                    OP_STORE: ; // mem[RA+IMM] = RD; yazma bu döngüde kombinasyonel yapıldı

                    OP_JMP:  pc <= branch_dst;
                    OP_JZ:   if (rv_fd == 64'd0) pc <= branch_dst;
                    OP_JNZ:  if (rv_fd != 64'd0) pc <= branch_dst;

                    OP_CALL: begin
                        // R7--; mem[yeni R7] = PC_next (bu döngüde kombinasyonel yazıldı); PC = hedef
                        regs[7] <= regs[7] - 64'd1;
                        pc      <= branch_dst;
                    end

                    OP_RET: begin
                        // PC = mem[R7] (bir sonraki döngüde hazır); R7++
                        regs[7] <= regs[7] + 64'd1;
                        state   <= S_RET_WAIT;
                    end

                    OP_OUT: ; // io_wdata bu döngüde kombinasyonel yazıldı

                    OP_IN: begin
                        // io_addr bu döngüde kombinasyonel gönderildi
                        wb_dest <= fd;
                        state   <= S_IN_WAIT;
                    end

                    default: halted <= 1'b1;  // geçersiz opcode
                endcase
            end // S_EXECUTE

            // ────────────────────────────────────────────
            // LOAD_WAIT — mem_rdata artık okunan değer (64-bit)
            // ────────────────────────────────────────────
            S_LOAD_WAIT: begin
                write_reg(wb_dest, mem_rdata);
                state <= S_FETCH;
            end

            // ────────────────────────────────────────────
            // RET_WAIT — mem_rdata artık dönüş adresi (alt 16 bit)
            // ────────────────────────────────────────────
            S_RET_WAIT: begin
                pc    <= mem_rdata[15:0];
                state <= S_FETCH;
            end

            // ────────────────────────────────────────────
            // IN_WAIT — io_rdata artık okunan port verisi (64-bit)
            // ────────────────────────────────────────────
            S_IN_WAIT: begin
                write_reg(wb_dest, io_rdata);
                state <= S_FETCH;
            end

        endcase
    end // !halted
end

endmodule


// ============================================================
// Test Bench — nova32_cpu_tb.v
// Icarus Verilog ile çalıştır:
//   iverilog -o sim_tb cpu.v -DTESTBENCH && vvp sim_tb
// ============================================================
`ifdef TESTBENCH

module nova32_cpu_tb;

    reg        clk;
    reg        rst_n;
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

    // ── CPU örneği ──────────────────────────────────────
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

    // ── Bellek modeli (256 kelime, 64-bit) ───────────────
    reg [63:0] sim_mem [0:255];
    integer    ii;

    always @(posedge clk) begin
        mem_rdata <= sim_mem[mem_addr[7:0]];
        if (mem_we)
            sim_mem[mem_addr[7:0]] <= mem_wdata;
    end

    // ── G/Ç modeli ───────────────────────────────────────
    reg [63:0] io_ports [0:255];
    always @(posedge clk) begin
        io_rdata <= io_ports[io_addr];
        if (io_we) begin
            io_ports[io_addr] <= io_wdata;
            $display("[OUT] port[%0d] = %0d", io_addr, io_wdata);
        end
    end

    // ── Saat üreteci (10ns periyot = 100 MHz) ───────────
    initial clk = 0;
    always #5 clk = ~clk;

    // ── Test: 1..10 toplama ──────────────────────────────
    //
    // Test programı (test.bin içeriği tek 32-bit kelime/komut):
    //   opcode[31:26] d[25:21] a[20:16] b[15:11] imm11[10:0]
    //
    //   0x00: LI  R1, 1
    //   0x01: LI  R2, 11
    //   0x02: LI  R3, 0
    //   0x03: LI  R4, 1
    //   0x04: ADD R3, R3, R1
    //   0x05: ADD R1, R1, R4
    //   0x06: SUB R5, R1, R2
    //   0x07: JNZ R5, -4   (pc+1 sonrası -4 → adres 0x04'e döner)
    //   0x08: OUT R3, 0
    //   0x09: HALT
    //
    localparam OP_ADD=6'h01, OP_SUB=6'h02, OP_LI=6'h0A, OP_JNZ=6'h0D,
               OP_OUT=6'h10, OP_HALT=6'h3F;

    function [31:0] enc;
        input [5:0]  op;
        input [4:0]  d, a, b;
        input [10:0] imm;
        begin
            enc = {op, d, a, b, imm};
        end
    endfunction

    initial begin
        // Bellek ve portları sıfırla
        for (ii = 0; ii < 256; ii = ii + 1) begin
            sim_mem[ii]  = 64'h0;
            io_ports[ii] = 64'h0;
        end

        // Test programını belleğe yükle (komutlar 64-bit kelimenin alt 32 bitinde)
        // Yeni format: [31:26]=op [25:21]=fd [20:16]=fa [15:11]=fb [10:0]=imm11
        sim_mem[0] = {32'd0, enc(OP_LI,  5'd1, 5'd0, 5'd0, 11'd1)};    // LI R1, 1
        sim_mem[1] = {32'd0, enc(OP_LI,  5'd2, 5'd0, 5'd0, 11'd11)};   // LI R2, 11
        sim_mem[2] = {32'd0, enc(OP_LI,  5'd3, 5'd0, 5'd0, 11'd0)};    // LI R3, 0
        sim_mem[3] = {32'd0, enc(OP_LI,  5'd4, 5'd0, 5'd0, 11'd1)};    // LI R4, 1
        sim_mem[4] = {32'd0, enc(OP_ADD, 5'd3, 5'd3, 5'd1, 11'd0)};    // ADD R3,R3,R1
        sim_mem[5] = {32'd0, enc(OP_ADD, 5'd1, 5'd1, 5'd4, 11'd0)};    // ADD R1,R1,R4
        sim_mem[6] = {32'd0, enc(OP_SUB, 5'd5, 5'd1, 5'd2, 11'd0)};    // SUB R5,R1,R2
        sim_mem[7] = {32'd0, enc(OP_JNZ, 5'd5, 5'd0, 5'd0, 11'h7FC)};  // JNZ R5, -4
        sim_mem[8] = {32'd0, enc(OP_OUT, 5'd3, 5'd0, 5'd0, 11'd0)};    // OUT R3, 0
        sim_mem[9] = {32'd0, enc(OP_HALT,5'd0, 5'd0, 5'd0, 11'd0)};    // HALT

        // Sıfırlama
        rst_n = 0;
        #20;
        rst_n = 1;

        // HALT bekle (max 500 döngü)
        for (ii = 0; ii < 500; ii = ii + 1) begin
            @(posedge clk);
            if (halted) begin
                #10;
                $display("-----------------------------------");
                $display("NOVA-64 Verilog Simülasyonu Tamam");
                $display("Cycles  : %0d", cycles);
                $display("Halted  : %b", halted);
                $display("port[0] : %0d (beklenen: 55)", io_ports[0]);
                if (io_ports[0] == 64'd55)
                    $display("TEST GECTI \u2713");
                else
                    $display("TEST BASARISIZ \u2717");
                $display("-----------------------------------");
                $finish;
            end
        end

        $display("ZAMAN ASIMI — CPU durmadi!");
        $finish;
    end

    // VCD dalga formu kaydı (GTKWave ile görüntülenebilir)
    initial begin
        $dumpfile("nova32_wave.vcd");
        $dumpvars(0, nova32_cpu_tb);
    end

endmodule

`endif
