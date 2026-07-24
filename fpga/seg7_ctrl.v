// ============================================================
// 7-Segment Display Kontrolcüsü — Basys3 (Xilinx Artix-7)
//
// Basys3'te 4 adet common-anode 7-segment digit var.
// Multiplexing: ~1kHz tarama hızı (100MHz / 100000)
//
// Gösterim formatı: 16-bit hex değer → 4 hex basamak
//   Örnek: port[0] = 55 (0x0037) → "  37" (sağda)
//
// Çıkışlar (aktif-düşük):
//   seg[6:0] = {CA, CB, CC, CD, CE, CF, CG}
//   an[3:0]  = anode seçimi (aktif-düşük: 0=seçili)
// ============================================================

module seg7_ctrl (
    input  wire        clk,      // 100 MHz
    input  wire        rst_n,
    input  wire [15:0] value,    // gösterilecek 16-bit değer
    output reg  [6:0]  seg,      // segment sürücüler (aktif-düşük)
    output reg  [3:0]  an,       // anode seçimi (aktif-düşük)
    output wire        dp         // ondalık nokta (kapalı)
);

    assign dp = 1'b1;  // ondalık nokta yok

    // ─── Tarama Sayacı ───────────────────────────────────
    // 100 MHz / 100000 = 1 kHz → her 1ms bir digit değişir
    reg [16:0] scan_ctr;
    reg [1:0]  digit_sel;  // 0=sağ, 3=sol

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            scan_ctr  <= 17'd0;
            digit_sel <= 2'd0;
        end else begin
            if (scan_ctr == 17'd99999) begin
                scan_ctr  <= 17'd0;
                digit_sel <= digit_sel + 2'd1;
            end else begin
                scan_ctr <= scan_ctr + 17'd1;
            end
        end
    end

    // ─── Nibble Seçimi ───────────────────────────────────
    reg [3:0] nibble;
    always @(*) begin
        case (digit_sel)
            2'd0: nibble = value[3:0];    // en sağ basamak
            2'd1: nibble = value[7:4];
            2'd2: nibble = value[11:8];
            2'd3: nibble = value[15:12];  // en sol basamak
            default: nibble = 4'd0;
        endcase
    end

    // ─── Anode Seçimi (aktif-düşük) ──────────────────────
    always @(*) begin
        case (digit_sel)
            2'd0: an = 4'b1110;  // digit 0 (sağ)
            2'd1: an = 4'b1101;  // digit 1
            2'd2: an = 4'b1011;  // digit 2
            2'd3: an = 4'b0111;  // digit 3 (sol)
            default: an = 4'b1111;
        endcase
    end

    // ─── Hex → 7-Segment Dönüşümü ────────────────────────
    // Segment sırası: {g, f, e, d, c, b, a} (aktif-düşük)
    //
    //   aaa
    //  f   b
    //  f   b
    //   ggg
    //  e   c
    //  e   c
    //   ddd
    //
    always @(*) begin
        case (nibble)
            //                  gfedcba
            4'h0: seg = 7'b1000000;  // 0
            4'h1: seg = 7'b1111001;  // 1
            4'h2: seg = 7'b0100100;  // 2
            4'h3: seg = 7'b0110000;  // 3
            4'h4: seg = 7'b0011001;  // 4
            4'h5: seg = 7'b0010010;  // 5
            4'h6: seg = 7'b0000010;  // 6
            4'h7: seg = 7'b1111000;  // 7
            4'h8: seg = 7'b0000000;  // 8
            4'h9: seg = 7'b0010000;  // 9
            4'hA: seg = 7'b0001000;  // A
            4'hB: seg = 7'b0000011;  // b
            4'hC: seg = 7'b1000110;  // C
            4'hD: seg = 7'b0100001;  // d
            4'hE: seg = 7'b0000110;  // E
            4'hF: seg = 7'b0001110;  // F
            default: seg = 7'b1111111; // hepsi kapalı
        endcase
    end

endmodule
