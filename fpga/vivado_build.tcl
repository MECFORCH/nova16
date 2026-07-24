# ============================================================
# NOVA-16 CPU — Vivado Build Scripti
#
# Kullanım (Vivado Tcl Console veya komut satırı):
#   vivado -mode batch -source vivado_build.tcl
#
# Çıktı:
#   nova16_fpga/nova16_fpga.runs/impl_1/nova16_top.bit
#
# Hedef kart: Digilent Basys3 (Xilinx Artix-7 XC7A35T-1CPG236C)
# ============================================================

# ─── Proje Oluştur ─────────────────────────────────────────
set proj_name "nova16_fpga"
set proj_dir  [file normalize [file dirname [info script]]]

# Varsa eski projeyi sil
if {[file exists $proj_dir/$proj_name]} {
    file delete -force $proj_dir/$proj_name
}

create_project $proj_name $proj_dir/$proj_name \
    -part xc7a35tcpg236-1

# ─── Kaynak Dosyaları ───────────────────────────────────────
set src_dir [file dirname [info script]]
set top_dir [file join $src_dir ..]   ;# nova16/

# Verilog kaynakları
add_files -norecurse [list \
    [file join $src_dir nova16_top.v]    \
    [file join $src_dir nova16_bram.v]   \
    [file join $src_dir seg7_ctrl.v]     \
    [file join $top_dir cpu_synth.v]     \
]

# Üst modül
set_property top nova16_top [current_fileset]

# Kısıtlama dosyası
add_files -fileset constrs_1 -norecurse \
    [file join $src_dir basys3.xdc]

# ─── Sentez ────────────────────────────────────────────────
puts "==> Sentez başlıyor..."
launch_runs synth_1 -jobs 4
wait_on_run synth_1

if {[get_property PROGRESS [get_runs synth_1]] != "100%"} {
    error "HATA: Sentez başarısız!"
}
puts "==> Sentez tamamlandı."

# ─── Implementasyon ─────────────────────────────────────────
puts "==> Implementasyon başlıyor..."
launch_runs impl_1 -jobs 4
wait_on_run impl_1

if {[get_property PROGRESS [get_runs impl_1]] != "100%"} {
    error "HATA: Implementasyon başarısız!"
}
puts "==> Implementasyon tamamlandı."

# ─── Bitstream Üret ─────────────────────────────────────────
puts "==> Bitstream üretiliyor..."
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1

set bit_file [file join $proj_dir $proj_name \
    ${proj_name}.runs impl_1 nova16_top.bit]

if {[file exists $bit_file]} {
    puts "==> BAŞARILI! Bitstream: $bit_file"
    puts ""
    puts "FPGA'ya yüklemek için:"
    puts "  open_hw_manager"
    puts "  connect_hw_server"
    puts "  open_hw_target"
    puts "  program_hw_devices -bitfile $bit_file"
} else {
    error "HATA: Bitstream dosyası bulunamadı!"
}

# ─── Timing Raporu ──────────────────────────────────────────
open_run impl_1
report_timing_summary -file [file join $proj_dir timing_report.txt]
report_utilization    -file [file join $proj_dir util_report.txt]

puts ""
puts "Raporlar:"
puts "  Timing   : [file join $proj_dir timing_report.txt]"
puts "  Kullanım : [file join $proj_dir util_report.txt]"
