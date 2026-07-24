// ============================================================
// nova_uart.v — NOVA-64 UART Modülü (8N1)
//
// TX: OUT Rx, 0xFE   → bir byte iletmek için port 0xFE'ye yaz
// RX: IN  Rx, 0xFF   → bir byte almak için port 0xFF'i oku
// ST: IN  Rx, 0xFD   → durum portu: bit0=tx_hazır, bit1=rx_geçerli
//
// Bu modül CPU'nun IO bus'ına bağlanır.
// Baud divisor: clk_hz / baud_rate  (örn. 100 MHz / 115200 ≈ 868)
//
// Kullanım: nova16_top.v içinde örneklenir; tx/rx pinleri FPGA'ya çıkar.
// ============================================================

`timescale 1ns / 1ps

// ────────────────────────────────────────────────────────────
// UART TX — 8N1, baud_div döngü başına bölücü
// ────────────────────────────────────────────────────────────
module uart_tx (
    input  wire        clk,
    input  wire        rst_n,
    // IO arayüzü
    input  wire [7:0]  tx_data,   // yazılacak byte
    input  wire        tx_valid,  // 1 döngü yüksek → yeni veri
    output reg         tx_ready,  // 1 = yeni veri kabul edilebilir
    // Seri çıkış
    output reg         tx,
    // Baud rate bölücü (clk ticks per bit)
    input  wire [15:0] baud_div
);
    localparam IDLE  = 2'd0,
               START = 2'd1,
               DATA  = 2'd2,
               STOP  = 2'd3;

    reg [1:0]  state;
    reg [15:0] clk_cnt;    // baud bölücü sayacı
    reg [7:0]  shift;      // gönderilecek veri kaydırma kaydı
    reg [2:0]  bit_idx;    // 0-7 (8 veri biti)

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state   <= IDLE;
            tx      <= 1'b1;   // hat boşta = HIGH
            tx_ready<= 1'b1;
            clk_cnt <= 16'd0;
            shift   <= 8'd0;
            bit_idx <= 3'd0;
        end else begin
            case (state)
                IDLE: begin
                    tx       <= 1'b1;
                    tx_ready <= 1'b1;
                    if (tx_valid) begin
                        shift    <= tx_data;
                        clk_cnt  <= baud_div - 16'd1;
                        tx       <= 1'b0;   // start bit
                        tx_ready <= 1'b0;
                        state    <= START;
                    end
                end
                START: begin
                    if (clk_cnt == 16'd0) begin
                        clk_cnt <= baud_div - 16'd1;
                        tx      <= shift[0];
                        shift   <= {1'b0, shift[7:1]};
                        bit_idx <= 3'd0;
                        state   <= DATA;
                    end else begin
                        clk_cnt <= clk_cnt - 16'd1;
                    end
                end
                DATA: begin
                    if (clk_cnt == 16'd0) begin
                        clk_cnt <= baud_div - 16'd1;
                        if (bit_idx == 3'd7) begin
                            tx    <= 1'b1;   // stop bit
                            state <= STOP;
                        end else begin
                            bit_idx <= bit_idx + 3'd1;
                            tx      <= shift[0];
                            shift   <= {1'b0, shift[7:1]};
                        end
                    end else begin
                        clk_cnt <= clk_cnt - 16'd1;
                    end
                end
                STOP: begin
                    if (clk_cnt == 16'd0) begin
                        tx_ready <= 1'b1;
                        state    <= IDLE;
                    end else begin
                        clk_cnt <= clk_cnt - 16'd1;
                    end
                end
            endcase
        end
    end
endmodule

// ────────────────────────────────────────────────────────────
// UART RX — 8N1, 16x oversampling (start biti ortasını yakalar)
// ────────────────────────────────────────────────────────────
module uart_rx (
    input  wire        clk,
    input  wire        rst_n,
    // Seri giriş
    input  wire        rx,
    // Çıkış
    output reg [7:0]   rx_data,
    output reg         rx_valid,   // 1 döngü yüksek → geçerli byte hazır
    // Baud rate bölücü (clk per bit)
    input  wire [15:0] baud_div
);
    localparam IDLE  = 2'd0,
               START = 2'd1,
               DATA  = 2'd2,
               STOP  = 2'd3;

    // 2-FF senkronizasyonu (metastabilite koruması)
    reg rx_s0, rx_s1;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin rx_s0 <= 1'b1; rx_s1 <= 1'b1; end
        else        begin rx_s0 <= rx;    rx_s1 <= rx_s0; end
    end
    wire rx_sync = rx_s1;

    reg [1:0]  state;
    reg [15:0] clk_cnt;
    reg [7:0]  shift;
    reg [2:0]  bit_idx;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state   <= IDLE;
            rx_data <= 8'd0;
            rx_valid<= 1'b0;
            clk_cnt <= 16'd0;
            shift   <= 8'd0;
            bit_idx <= 3'd0;
        end else begin
            rx_valid <= 1'b0;

            case (state)
                IDLE: begin
                    if (!rx_sync) begin
                        // Start biti tespit edildi; bit ortasına atla (baud_div/2)
                        clk_cnt <= (baud_div >> 1) - 16'd1;
                        state   <= START;
                    end
                end
                START: begin
                    if (clk_cnt == 16'd0) begin
                        if (!rx_sync) begin
                            // Gerçek start biti onaylandı
                            clk_cnt <= baud_div - 16'd1;
                            bit_idx <= 3'd0;
                            state   <= DATA;
                        end else begin
                            state <= IDLE;   // gürültü, iptal
                        end
                    end else begin
                        clk_cnt <= clk_cnt - 16'd1;
                    end
                end
                DATA: begin
                    if (clk_cnt == 16'd0) begin
                        clk_cnt <= baud_div - 16'd1;
                        shift   <= {rx_sync, shift[7:1]};  // LSB önce
                        if (bit_idx == 3'd7) begin
                            state <= STOP;
                        end else begin
                            bit_idx <= bit_idx + 3'd1;
                        end
                    end else begin
                        clk_cnt <= clk_cnt - 16'd1;
                    end
                end
                STOP: begin
                    if (clk_cnt == 16'd0) begin
                        if (rx_sync) begin   // geçerli stop biti
                            rx_data  <= shift;
                            rx_valid <= 1'b1;
                        end
                        state <= IDLE;
                    end else begin
                        clk_cnt <= clk_cnt - 16'd1;
                    end
                end
            endcase
        end
    end
endmodule

// ────────────────────────────────────────────────────────────
// nova_uart — CPU IO bus'a bağlanan üst modül
//
// IO port haritası (sim.c ile tutarlı):
//   0xFE yazma  → TX byte gönder (tx_ready=0 ise veri kaybolabilir; uygulamaya bağlı)
//   0xFF okuma  → RX byte al (rx_valid temizlenir)
//   0xFD okuma  → durum: {62'd0, rx_valid, tx_ready}
// ────────────────────────────────────────────────────────────
module nova_uart #(
    parameter CLK_HZ   = 100_000_000,   // sistem saati frekansı
    parameter BAUD     = 115200          // baud rate
) (
    input  wire        clk,
    input  wire        rst_n,
    // CPU IO bus (nova32_cpu io_* sinyalleri)
    input  wire [7:0]  io_addr,
    input  wire        io_we,
    input  wire [63:0] io_wdata,
    output reg  [63:0] uart_rdata,      // io_rdata'ya MUX'lanır
    output reg         uart_sel,        // bu modül io_addr'ı sahipleniyor mu
    // FPGA pinleri
    output wire        uart_tx,
    input  wire        uart_rx
);
    localparam [15:0] BAUD_DIV = CLK_HZ / BAUD;

    // TX sinyalleri
    wire       tx_ready;
    reg  [7:0] tx_data;
    reg        tx_valid;

    // RX sinyalleri
    wire [7:0] rx_data;
    wire       rx_valid_pulse;
    reg  [7:0] rx_buf;
    reg        rx_buf_valid;

    uart_tx #() u_tx (
        .clk      (clk),
        .rst_n    (rst_n),
        .tx_data  (tx_data),
        .tx_valid (tx_valid),
        .tx_ready (tx_ready),
        .tx       (uart_tx),
        .baud_div (BAUD_DIV)
    );

    uart_rx #() u_rx (
        .clk      (clk),
        .rst_n    (rst_n),
        .rx       (uart_rx),
        .rx_data  (rx_data),
        .rx_valid (rx_valid_pulse),
        .baud_div (BAUD_DIV)
    );

    // RX tamponu: son alınan byte'ı sakla
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_buf       <= 8'd0;
            rx_buf_valid <= 1'b0;
        end else begin
            if (rx_valid_pulse) begin
                rx_buf       <= rx_data;
                rx_buf_valid <= 1'b1;
            end
            // CPU 0xFF'i okuyunca rx_buf_valid temizlenir
            if (io_addr == 8'hFF && !io_we)
                rx_buf_valid <= 1'b0;
        end
    end

    // TX yazma
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_data  <= 8'd0;
            tx_valid <= 1'b0;
        end else begin
            tx_valid <= 1'b0;   // tek döngü darbe
            if (io_we && io_addr == 8'hFE) begin
                tx_data  <= io_wdata[7:0];
                tx_valid <= 1'b1;
            end
        end
    end

    // Okuma MUX + seçim
    always @(*) begin
        uart_sel   = 1'b0;
        uart_rdata = 64'd0;
        case (io_addr)
            8'hFE: begin uart_sel = 1'b1; uart_rdata = 64'd0; end
            8'hFF: begin uart_sel = 1'b1; uart_rdata = {56'd0, rx_buf}; end
            8'hFD: begin uart_sel = 1'b1; uart_rdata = {62'd0, rx_buf_valid, tx_ready}; end
            default: ;
        endcase
    end

endmodule
