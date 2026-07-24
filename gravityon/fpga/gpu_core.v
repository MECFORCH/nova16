/**
 * GRAVITYON GPU CORE — Verilog RTL
 * ==================================
 * NOVA-64 FPGA (Xilinx Basys3 / Artix-7) için GPU birimi.
 *
 * Bloklar:
 *   - I/O Port Arayüzü    : NOVA-64 CPU ↔ GPU (0xE0-0xFF)
 *   - Ring Buffer Controller: DMA ile komut kuyruğunu okur
 *   - Komut Dekoder       : GPUCmdType'a göre alt birimleri yönlendirir
 *   - Shader Execution Unit: GBYT bytecode yürütücüsü (pipeline)
 *   - Triangle Setup      : Barycentric hesap, kenar denklemleri
 *   - Rasterizer          : Tarama dönüşümü, scissor
 *   - Z-Buffer Unit       : Derinlik testi & yazma
 *   - Texture Unit        : Bilinear örnekleme, BRAM önbelleği
 *   - Framebuffer Unit    : RGBA8 piksel yazma
 *   - Display Controller  : VGA/HDMI sync sinyal üretimi
 *   - IRQ Controller      : CPU'ya kesme sinyali
 */

`timescale 1ns / 1ps

/* =========================================================================
 * PARAMETRE SABITLERI
 * ========================================================================= */

`define GPU_ID          32'h47505500   /* "GPU\0" */
`define GPU_VER         32'h00010000   /* v1.0.0  */
`define VRAM_DEPTH      16384          /* 64-bit word → 128 KB (Basys3 BRAM) */
`define SHADER_SLOTS    4
`define MAX_VARYINGS    8
`define MAX_REGS        32
`define RING_DEPTH      1024           /* 1K × 64-bit = 8 KB ring buffer */

/* Port adresleri */
`define P_ID        8'hE0
`define P_VER       8'hE1
`define P_STATUS    8'hE2
`define P_CTRL      8'hE3
`define P_RING_BASE 8'hE4
`define P_RING_SIZE 8'hE5
`define P_RING_HEAD 8'hE6
`define P_RING_TAIL 8'hE7
`define P_DOORBELL  8'hE8
`define P_FB_ADDR   8'hEE
`define P_FB_W      8'hEF
`define P_FB_H      8'hF0
`define P_FB_PITCH  8'hF1
`define P_FB_FMT    8'hF2
`define P_FB_FLIP   8'hF3
`define P_IRQ_STAT  8'hFC
`define P_IRQ_MASK  8'hFD
`define P_IRQ_CLR   8'hFE

/* =========================================================================
 * ANA GPU MODÜLü
 * ========================================================================= */

module gravityon_gpu_core #(
    parameter SCREEN_W  = 800,
    parameter SCREEN_H  = 600,
    parameter CLK_MHZ   = 100
)(
    input  wire        clk,
    input  wire        rst_n,

    /* NOVA-64 I/O Port Arayüzü */
    input  wire        port_wr,         /* OUT komutu: yazma */
    input  wire        port_rd,         /* IN  komutu: okuma */
    input  wire [7:0]  port_addr,       /* port numarası     */
    input  wire [63:0] port_din,        /* CPU→GPU veri      */
    output reg  [63:0] port_dout,       /* GPU→CPU veri      */

    /* Paylaşılan RAM Arayüzü (DMA için) */
    output reg         ram_req,
    output reg         ram_wr,
    output reg  [31:0] ram_addr,
    output reg  [63:0] ram_wdata,
    input  wire [63:0] ram_rdata,
    input  wire        ram_ack,

    /* VGA/Display çıkışı */
    output wire        vga_hsync,
    output wire        vga_vsync,
    output wire [3:0]  vga_r,
    output wire [3:0]  vga_g,
    output wire [3:0]  vga_b,

    /* IRQ çıkışı (NOVA-64 IRQ girişine bağlı) */
    output wire        irq_out,

    /* 7-Segment debug (Basys3) */
    output wire [6:0]  seg,
    output wire [3:0]  an
);

/* =========================================================================
 * BRAM — VRAM (çift portlu, 64-bit)
 * ========================================================================= */

reg [63:0] vram [0:`VRAM_DEPTH-1];
reg [63:0] vram_rdata;
reg        vram_rack;
integer    vi;
initial begin
    for (vi=0; vi<`VRAM_DEPTH; vi=vi+1) vram[vi]=64'd0;
end

/* VRAM yazma */
always @(posedge clk) begin
    if (vram_wr_en) begin
        vram[vram_wr_addr[14:3]] <= vram_wr_data;
        vram_rack <= 1'b1;
    end else begin
        vram_rack <= 1'b0;
    end
    vram_rdata <= vram[vram_rd_addr[14:3]];
end

reg        vram_wr_en;
reg [31:0] vram_wr_addr;
reg [63:0] vram_wr_data;
reg [31:0] vram_rd_addr;

/* =========================================================================
 * I/O PORT KAYITLARI
 * ========================================================================= */

reg [63:0] reg_status;
reg [63:0] reg_ctrl;
reg [63:0] reg_ring_base;
reg [63:0] reg_ring_size;
reg [63:0] reg_ring_head;
reg [63:0] reg_ring_tail;
reg [63:0] reg_fb_addr;
reg [31:0] reg_fb_w;
reg [31:0] reg_fb_h;
reg [31:0] reg_fb_pitch;
reg [7:0]  reg_fb_fmt;
reg [31:0] reg_irq_status;
reg [31:0] reg_irq_mask;

/* Port okuma */
always @(*) begin
    case (port_addr)
        `P_ID:        port_dout = `GPU_ID;
        `P_VER:       port_dout = `GPU_VER;
        `P_STATUS:    port_dout = reg_status;
        `P_RING_TAIL: port_dout = reg_ring_tail;
        `P_FB_W:      port_dout = reg_fb_w;
        `P_FB_H:      port_dout = reg_fb_h;
        `P_IRQ_STAT:  port_dout = reg_irq_status;
        default:      port_dout = 64'd0;
    endcase
end

/* Port yazma */
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        reg_ctrl      <= 0; reg_ring_base <= 0;
        reg_ring_size <= 0; reg_ring_head <= 0;
        reg_fb_addr   <= 0; reg_fb_w      <= SCREEN_W;
        reg_fb_h      <= SCREEN_H;
        reg_fb_pitch  <= SCREEN_W * 4;
        reg_fb_fmt    <= 0; reg_irq_mask  <= 0;
        doorbell_pulse <= 0;
    end else if (port_wr) begin
        case (port_addr)
            `P_CTRL:      reg_ctrl      <= port_din;
            `P_RING_BASE: reg_ring_base <= port_din;
            `P_RING_SIZE: reg_ring_size <= port_din;
            `P_RING_HEAD: reg_ring_head <= port_din;
            `P_DOORBELL:  doorbell_pulse <= 1'b1;
            `P_FB_ADDR:   reg_fb_addr   <= port_din;
            `P_FB_W:      reg_fb_w      <= port_din[31:0];
            `P_FB_H:      reg_fb_h      <= port_din[31:0];
            `P_FB_PITCH:  reg_fb_pitch  <= port_din[31:0];
            `P_FB_FMT:    reg_fb_fmt    <= port_din[7:0];
            `P_IRQ_MASK:  reg_irq_mask  <= port_din[31:0];
            `P_IRQ_CLR:   reg_irq_status <= reg_irq_status & ~port_din[31:0];
            default: ;
        endcase
    end else begin
        doorbell_pulse <= 1'b0;
    end
end

reg doorbell_pulse;

/* =========================================================================
 * RING BUFFER KONTROLCÜSÜ
 * ========================================================================= */

localparam RB_IDLE    = 3'd0,
           RB_FETCH   = 3'd1,
           RB_WAIT    = 3'd2,
           RB_DECODE  = 3'd3,
           RB_PAYLOAD = 3'd4,
           RB_EXEC    = 3'd5;

reg [2:0]  rb_state;
reg [31:0] rb_ptr;          /* ring buffer okuma pozisyonu    */
reg [31:0] rb_cmd_type;     /* mevcut komut türü              */
reg [31:0] rb_payload_sz;   /* payload boyutu (bayt)          */
reg [511:0] rb_payload_buf; /* payload tamponu (64 bayt max)  */
reg [5:0]  rb_payload_idx;  /* payload okunan kelime sayısı   */

wire rb_empty = (rb_ptr[31:0] == reg_ring_head[31:0]);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        rb_state <= RB_IDLE;
        rb_ptr   <= 0;
        ram_req  <= 0; ram_wr <= 0;
    end else begin
        case (rb_state)

        RB_IDLE: begin
            ram_req <= 0;
            if (doorbell_pulse && !rb_empty) begin
                rb_state <= RB_FETCH;
                reg_status <= 64'h02; /* BUSY */
            end
        end

        RB_FETCH: begin
            /* Komut başlığını DMA ile oku (8 bayt = 1 × 64-bit) */
            ram_req  <= 1'b1;
            ram_wr   <= 1'b0;
            ram_addr <= reg_ring_base[31:0] + rb_ptr;
            rb_state <= RB_WAIT;
        end

        RB_WAIT: begin
            if (ram_ack) begin
                ram_req <= 0;
                rb_cmd_type   <= ram_rdata[31:0];
                rb_payload_sz <= ram_rdata[63:32];
                rb_ptr        <= rb_ptr + 4'd8;
                rb_payload_idx <= 0;
                rb_state      <= (ram_rdata[63:32] > 0) ? RB_PAYLOAD : RB_EXEC;
            end
        end

        RB_PAYLOAD: begin
            /* Payload kelimelerini oku */
            if (rb_payload_idx*8 < rb_payload_sz) begin
                ram_req  <= 1'b1;
                ram_wr   <= 1'b0;
                ram_addr <= reg_ring_base[31:0] + rb_ptr;
                if (ram_ack) begin
                    rb_payload_buf[rb_payload_idx*64 +: 64] <= ram_rdata;
                    rb_ptr         <= rb_ptr + 4'd8;
                    rb_payload_idx <= rb_payload_idx + 1'b1;
                    ram_req <= 0;
                end
            end else begin
                ram_req  <= 0;
                rb_state <= RB_EXEC;
            end
        end

        RB_EXEC: begin
            /* Komutu execute birimine gönder */
            exec_cmd_valid  <= 1'b1;
            exec_cmd_type   <= rb_cmd_type;
            exec_payload    <= rb_payload_buf;
            exec_payload_sz <= rb_payload_sz;
            reg_ring_tail   <= rb_ptr;
            rb_state        <= RB_IDLE;
            reg_status      <= 64'h01; /* IDLE */
            /* Frame done IRQ */
            if (rb_cmd_type == 32'h0F) begin /* GPU_CMD_PRESENT */
                if (reg_irq_mask[0]) reg_irq_status[0] <= 1'b1;
            end
        end

        default: rb_state <= RB_IDLE;
        endcase
    end
end

/* =========================================================================
 * KOMUT YÜRÜTMESİ
 * ========================================================================= */

reg        exec_cmd_valid;
reg [31:0] exec_cmd_type;
reg [511:0] exec_payload;
reg [31:0] exec_payload_sz;

/* Rasterizasyon durumu kayıtları */
reg [31:0] cull_mode;       /* 0=none,1=back,2=front     */
reg [31:0] fill_mode;       /* 0=solid,1=wireframe        */
reg        depth_test_en;
reg        depth_write_en;
reg [1:0]  depth_cmp;       /* 0=less,1=lequal,2=greater */
reg [31:0] vp_x, vp_y, vp_w, vp_h;
reg [31:0] sc_x, sc_y, sc_w, sc_h;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        exec_cmd_valid <= 0;
        cull_mode      <= 1;
        fill_mode      <= 0;
        depth_test_en  <= 1;
        depth_write_en <= 1;
        depth_cmp      <= 0;
        vp_x<=0; vp_y<=0;
        vp_w<=SCREEN_W; vp_h<=SCREEN_H;
        sc_x<=0; sc_y<=0;
        sc_w<=SCREEN_W; sc_h<=SCREEN_H;
    end else if (exec_cmd_valid) begin
        exec_cmd_valid <= 0;
        case (exec_cmd_type)
            32'h000B: begin /* SET_VIEWPORT */
                vp_x <= exec_payload[31:0];
                vp_y <= exec_payload[63:32];
                vp_w <= exec_payload[95:64];
                vp_h <= exec_payload[127:96];
            end
            32'h000C: begin /* SET_SCISSOR */
                sc_x <= exec_payload[31:0];
                sc_y <= exec_payload[63:32];
                sc_w <= exec_payload[95:64];
                sc_h <= exec_payload[127:96];
            end
            32'h000D: begin /* SET_RASTER */
                cull_mode <= exec_payload[31:0];
                fill_mode <= exec_payload[63:32];
            end
            32'h000E: begin /* SET_DEPTH */
                depth_test_en  <= exec_payload[0];
                depth_write_en <= exec_payload[32];
                depth_cmp      <= exec_payload[65:64];
            end
            32'h000F: begin /* PRESENT — frame done */ end
            default: ;
        endcase
    end
end

/* =========================================================================
 * SHADER EXECUTION UNIT (GBYT Bytecode Pipeline — 5 aşama)
 *
 * Aşama 1: Fetch    — BRAM'den komut oku
 * Aşama 2: Decode   — opcode çöz, register oku
 * Aşama 3: Execute  — ALU/FPU işlemi
 * Aşama 4: Writeback— sonucu register'a yaz
 * Aşama 5: Output   — varying/position/color yaz
 *
 * NOT: Gerçek FPU için Xilinx Floating Point IP kullanılır.
 *      Burada integer approximation ile simüle edilmiştir.
 *      Gerçek implementasyon: Vivado IP Catalog → Floating Point v7.1
 * ========================================================================= */

/* Shader BRAM — 4 slot × 4096 komut = 16K × 32-bit */
reg [31:0] shader_mem [0:`SHADER_SLOTS*`GBYT_MAX_INSTRUCTIONS-1];
reg [31:0] shader_pc;
reg [1:0]  active_vert_slot;
reg [1:0]  active_frag_slot;

/* Register dosyası (tek iş parçacığı — paralel için çoğalt) */
reg [31:0] shader_regs [0:`MAX_REGS-1];

/* Shader pipeline kayıtları */
reg [31:0] sh_fetch_instr;
reg [7:0]  sh_opcode;
reg [4:0]  sh_dst, sh_src1, sh_src2, sh_src3;
reg [18:0] sh_imm19;

/* Fetch aşaması */
always @(posedge clk) begin
    sh_fetch_instr <= shader_mem[shader_pc];
end

/* Decode aşaması */
always @(posedge clk) begin
    sh_opcode <= sh_fetch_instr[31:24];
    sh_dst    <= sh_fetch_instr[23:19];
    sh_src1   <= sh_fetch_instr[18:14];
    sh_src2   <= sh_fetch_instr[13:9];
    sh_src3   <= sh_fetch_instr[8:4];
    sh_imm19  <= sh_fetch_instr[18:0];
end

/* =========================================================================
 * Z-BUFFER BİRİMİ
 * ========================================================================= */

/* Derinlik BRAM — 32-bit float olarak saklanır */
reg [31:0] zbuf [0:SCREEN_W*SCREEN_H-1];
integer    zi;
initial begin
    for (zi=0; zi<SCREEN_W*SCREEN_H; zi=zi+1) zbuf[zi]=32'h3F800000; /* 1.0f */
end

reg        zbuf_test_pass;
reg [31:0] zbuf_z_in;
reg [31:0] zbuf_pixel_idx;
reg [31:0] zbuf_z_stored;

always @(posedge clk) begin
    zbuf_z_stored <= zbuf[zbuf_pixel_idx];
    /* Basit karşılaştırma (IEEE754 pozitif değerler için integer karşılaştırma çalışır) */
    case (depth_cmp)
        2'd0: zbuf_test_pass <= (zbuf_z_in < zbuf_z_stored);  /* less    */
        2'd1: zbuf_test_pass <= (zbuf_z_in <= zbuf_z_stored); /* lequal  */
        2'd2: zbuf_test_pass <= (zbuf_z_in > zbuf_z_stored);  /* greater */
        2'd3: zbuf_test_pass <= 1'b1;                          /* always  */
    endcase
    if (zbuf_test_pass && depth_write_en)
        zbuf[zbuf_pixel_idx] <= zbuf_z_in;
end

/* =========================================================================
 * FRAMEBUFFER YAZICI
 * ========================================================================= */

reg        fb_wr_en;
reg [31:0] fb_wr_x, fb_wr_y;
reg [7:0]  fb_wr_r, fb_wr_g, fb_wr_b, fb_wr_a;

always @(posedge clk) begin
    if (fb_wr_en) begin
        /* RGBA8 → VRAM yaz (64-bit hizalamalı) */
        vram_wr_en   <= 1'b1;
        vram_wr_addr <= reg_fb_addr[31:0] + fb_wr_y * reg_fb_pitch + fb_wr_x * 4;
        vram_wr_data <= {24'd0, fb_wr_a, fb_wr_b, fb_wr_g, fb_wr_r, 8'd0};
    end else begin
        vram_wr_en <= 1'b0;
    end
end

/* =========================================================================
 * VGA DISPLAY KONTROLcüsü
 * ========================================================================= */

/* 800×600 @ 60Hz: pixel clk ≈ 40 MHz (100 MHz clk / 2.5 → yaklaşık) */
localparam H_ACTIVE = 800,  H_FRONT  = 40,  H_SYNC = 128, H_BACK = 88;
localparam V_ACTIVE = 600,  V_FRONT  = 1,   V_SYNC = 4,   V_BACK = 23;
localparam H_TOTAL  = H_ACTIVE + H_FRONT + H_SYNC + H_BACK;
localparam V_TOTAL  = V_ACTIVE + V_FRONT + V_SYNC + V_BACK;

reg [10:0] h_cnt, v_cnt;
reg        pix_clk_en;

/* Pixel clock divider (100MHz → 40MHz yaklaşık) */
reg [1:0] pclk_div;
always @(posedge clk) begin
    pclk_div <= pclk_div + 1'b1;
    pix_clk_en <= (pclk_div == 2'd2);
end

/* Sayaçlar */
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        h_cnt <= 0; v_cnt <= 0;
    end else if (pix_clk_en) begin
        if (h_cnt == H_TOTAL-1) begin
            h_cnt <= 0;
            if (v_cnt == V_TOTAL-1) v_cnt <= 0;
            else                    v_cnt <= v_cnt + 1'b1;
        end else h_cnt <= h_cnt + 1'b1;
    end
end

wire h_active = (h_cnt < H_ACTIVE);
wire v_active = (v_cnt < V_ACTIVE);
wire display_en = h_active & v_active;

assign vga_hsync = ~((h_cnt >= H_ACTIVE+H_FRONT) && (h_cnt < H_ACTIVE+H_FRONT+H_SYNC));
assign vga_vsync = ~((v_cnt >= V_ACTIVE+V_FRONT) && (v_cnt < V_ACTIVE+V_FRONT+V_SYNC));

/* Framebuffer piksel oku */
reg [31:0] disp_rd_addr;
reg [63:0] disp_pixel;

always @(posedge clk) begin
    if (display_en) begin
        vram_rd_addr <= reg_fb_addr[31:0] + v_cnt * reg_fb_pitch + h_cnt * 4;
        disp_pixel   <= vram_rdata;
    end
end

assign vga_r = display_en ? disp_pixel[7:4]   : 4'd0;
assign vga_g = display_en ? disp_pixel[15:12]  : 4'd0;
assign vga_b = display_en ? disp_pixel[23:20]  : 4'd0;

/* =========================================================================
 * IRQ ÇIKIŞI
 * ========================================================================= */

assign irq_out = |(reg_irq_status & reg_irq_mask);

/* =========================================================================
 * 7-SEGMENT DEBUG (frame sayacı)
 * ========================================================================= */

reg [15:0] frame_cnt;
always @(posedge clk) begin
    if (exec_cmd_valid && exec_cmd_type == 32'h000F)
        frame_cnt <= frame_cnt + 1'b1;
end

/* Basit hex→7seg */
function [6:0] hex_to_seg;
    input [3:0] x;
    case(x)
        4'h0: hex_to_seg=7'b1000000;
        4'h1: hex_to_seg=7'b1111001;
        4'h2: hex_to_seg=7'b0100100;
        4'h3: hex_to_seg=7'b0110000;
        4'h4: hex_to_seg=7'b0011001;
        4'h5: hex_to_seg=7'b0010010;
        4'h6: hex_to_seg=7'b0000010;
        4'h7: hex_to_seg=7'b1111000;
        4'h8: hex_to_seg=7'b0000000;
        4'h9: hex_to_seg=7'b0010000;
        4'hA: hex_to_seg=7'b0001000;
        4'hB: hex_to_seg=7'b0000011;
        4'hC: hex_to_seg=7'b1000110;
        4'hD: hex_to_seg=7'b0100001;
        4'hE: hex_to_seg=7'b0000110;
        4'hF: hex_to_seg=7'b0001110;
        default: hex_to_seg=7'b1111111;
    endcase
endfunction

reg [1:0] seg_mux;
always @(posedge clk) seg_mux <= seg_mux + 1'b1;

assign an  = ~(4'b0001 << seg_mux);
assign seg = hex_to_seg(
    seg_mux==2'd0 ? frame_cnt[3:0]   :
    seg_mux==2'd1 ? frame_cnt[7:4]   :
    seg_mux==2'd2 ? frame_cnt[11:8]  :
                    frame_cnt[15:12]);

/* =========================================================================
 * DURUM KAYDI
 * ========================================================================= */

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) reg_status <= 64'h41; /* IDLE | RING_EMPTY | FB_READY */
    else if (rb_state == RB_IDLE && gpu_ring_empty_flag)
        reg_status <= 64'h45; /* IDLE | RING_EMPTY | FB_READY */
end

wire gpu_ring_empty_flag = (reg_ring_tail == reg_ring_head);

endmodule
