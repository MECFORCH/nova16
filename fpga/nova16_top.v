// ============================================================
// NOVA-64 FPGA Üst Modülü — Xilinx Basys3 (Artix-7 XC7A35T)
//
// Donanım haritası:
//   Saat     : 100 MHz (W5)
//   Reset    : BTNC (U18) — aktif-yüksek, CPU için aktif-düşük çevrilir
//   LED[15:0]: port[0] çıkışının alt 16 biti — hesaplama sonucunu gösterir
//   SW[15:0] : port[1] girişi — runtime veri girişi (64-bit portun alt yarısı)
//   7-Seg    : port[0] değerinin alt 16 biti hex olarak gösterilir
//   LED[15]  : CPU halted göstergesi (yanıp söner)
//
// Test programı:
//   1+2+...+10 = 55 (0x0000000000000037) → 7-seg "0037" gösterir
//   LED[5:0] = 0b110111 = 0x37 yanar
// ============================================================

module nova16_top (
    // ── Saat / Reset ────────────────────────────────────
    input  wire        clk_100mhz,   // W5  — 100 MHz sistem saati
    input  wire        btnC,         // U18 — merkez buton (reset)

    // ── LED'ler ─────────────────────────────────────────
    output wire [15:0] led,          // U16..L1

    // ── Slide Switchler ──────────────────────────────────
    input  wire [15:0] sw,           // V17..R2

    // ── 7-Segment Display ────────────────────────────────
    output wire [6:0]  seg,          // CA..CG
    output wire        dp,           // ondalık nokta
    output wire [3:0]  an,           // AN0..AN3 (aktif-düşük)

    // ── UART (USB-UART köprüsü, Basys3: RX=B18, TX=A18) ──
    output wire        uart_tx,      // FPGA → PC
    input  wire        uart_rx       // PC → FPGA
);

    // ─── Sıfırlama ────────────────────────────────────────
    // btnC aktif-yüksek (basılı=1), CPU rst_n aktif-düşük
    // Synchronizer: glitch önleme
    reg [1:0] rst_sync;
    always @(posedge clk_100mhz) begin
        rst_sync <= {rst_sync[0], btnC};
    end
    wire rst_n = ~rst_sync[1];

    // ─── CPU ↔ Bellek Sinyalleri ──────────────────────────
    wire [15:0] mem_addr;
    wire        mem_we;
    wire [63:0] mem_wdata;
    wire [63:0] mem_rdata;

    // ─── CPU ↔ G/Ç Sinyalleri ────────────────────────────
    wire [7:0]  io_addr;
    wire        io_we;
    wire [63:0] io_wdata;
    reg  [63:0] io_rdata;

    // ─── CPU Durum ────────────────────────────────────────
    wire        halted;
    wire [63:0] cycles;

    // ─── NOVA-64 CPU Örneği ───────────────────────────────
    nova32_cpu cpu (
        .clk      (clk_100mhz),
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

    // ─── Block RAM ────────────────────────────────────────
    nova32_bram bram (
        .clk  (clk_100mhz),
        .addr (mem_addr),
        .we   (mem_we),
        .wdata(mem_wdata),
        .rdata(mem_rdata)
    );

    // ─── UART Çevre Birimi ─────────────────────────────────
    // IO haritası: 0xFE=TX yaz, 0xFF=RX oku, 0xFD=durum oku
    // (sim.c'deki UART port numaralarıyla birebir uyumlu)
    wire [63:0] uart_rdata;
    wire        uart_sel;

    nova_uart #(
        .CLK_HZ (100_000_000),
        .BAUD   (115200)
    ) uart (
        .clk       (clk_100mhz),
        .rst_n     (rst_n),
        .io_addr   (io_addr),
        .io_we     (io_we),
        .io_wdata  (io_wdata),
        .uart_rdata(uart_rdata),
        .uart_sel  (uart_sel),
        .uart_tx   (uart_tx),
        .uart_rx   (uart_rx)
    );

    // ─── G/Ç Port Registerları ────────────────────────────
    reg [63:0] port0_out;   // OUT komutunun port 0 çıkışı → LED + 7-seg
    // port 1 giriş: doğrudan SW'den okunur (64-bit portun alt 16 biti)

    always @(posedge clk_100mhz or negedge rst_n) begin
        if (!rst_n) begin
            port0_out <= 64'd0;
            io_rdata  <= 64'd0;
        end else begin
            if (io_we && io_addr == 8'd0)
                port0_out <= io_wdata;

            // Port okuma: io_rdata bir döngü sonra geçerli
            // UART portları (0xFD/0xFE/0xFF) uart_sel ile önceliklendirilir.
            if (uart_sel) begin
                io_rdata <= uart_rdata;
            end else begin
                case (io_addr)
                    8'd0:    io_rdata <= port0_out;
                    8'd1:    io_rdata <= {48'd0, sw}; // SW switch girişi
                    default: io_rdata <= 64'd0;
                endcase
            end
        end
    end

    // ─── LED Sürücüsü ────────────────────────────────────
    // LED[14:0]: port[0] çıkışının alt 15 biti
    // LED[15]  : halted göstergesi (yanıp söner)
    reg [24:0] blink_ctr;
    always @(posedge clk_100mhz or negedge rst_n) begin
        if (!rst_n) blink_ctr <= 25'd0;
        else        blink_ctr <= blink_ctr + 25'd1;
    end
    // ~1.5Hz yanıp sönme: bit 24 = 100MHz/2^25 ≈ 3Hz, /2 = 1.5Hz
    wire blink = blink_ctr[24];

    assign led[14:0] = port0_out[14:0];
    assign led[15]   = halted ? blink : 1'b0;

    // ─── 7-Segment Display ────────────────────────────────
    // port[0] değerinin alt 16 bitini 4 hex basamak olarak göster
    seg7_ctrl seg7 (
        .clk  (clk_100mhz),
        .rst_n(rst_n),
        .value(port0_out[15:0]),
        .seg  (seg),
        .an   (an),
        .dp   (dp)
    );

endmodule
