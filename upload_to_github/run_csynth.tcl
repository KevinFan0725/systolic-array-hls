# C-simulation + C-synthesis for the 4x4 systolic array.
#   usage:  vitis_hls -f run_csynth.tcl

set part   [expr {[info exists ::env(SYN_PART)]   ? $::env(SYN_PART)   : "xcu280-fsvh2892-2L-e"}]
set period [expr {[info exists ::env(SYN_PERIOD)] ? $::env(SYN_PERIOD) : "3.33"}]

puts "=== systolic_kernel : $part @ ${period} ns ==="

open_project  -reset csynth_systolic
set_top       systolic_kernel
add_files     systolic_4x4_kernel.cpp
add_files -tb tb_systolic.cpp
open_solution -reset solution1 -flow_target vitis
set_part      $part
create_clock  -period $period -name default
csim_design
csynth_design
cosim_design
exit
