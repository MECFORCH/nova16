// ============================================================
// NOVA-64 Block RAM — Program + Veri Belleği
// 65536 × 64-bit kelime, senkron okuma/yazma
// Xilinx Artix-7 Block RAM'e otomatik eşlenir (Vivado).
//
// NOT: Bu dosya sadece donanım tanımıdır.
// İçine program gömülmez. Program yükleme ayrı aşamadır:
//   → Vivado "Memory Editor" ile .mem dosyası
//   → Ya da JTAG üzerinden bootloader
// ============================================================

module nova32_bram (
    input  wire        clk,
    input  wire [15:0] addr,
    input  wire        we,
    input  wire [63:0] wdata,
    output reg  [63:0] rdata
);

    // 64K × 64-bit bellek
    // Xilinx Vivado bunu Block RAM'e sentezler
    (* ram_style = "block" *)
    reg [63:0] mem [0:65535];

    // Senkron okuma/yazma (1 döngü gecikme — CPU durum makinesiyle uyumlu)
    always @(posedge clk) begin
        if (we)
            mem[addr] <= wdata;
        rdata <= mem[addr];
    end

endmodule
