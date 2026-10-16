#!/bin/bash
# Run on yangzi (requires USC VPN):
#   scp -r Matrix_multi_kernel jiakang@10.136.200.74:~/
#   ssh jiakang@10.136.200.74 'cd ~/Matrix_multi_kernel && bash run_csynth.sh'
set -e

source /tools/Xilinx/Vivado/2022.2/settings64.sh
source /tools/Xilinx/Vitis_HLS/2022.2/settings64.sh

vitis_hls -f run_csynth.tcl

R=csynth_systolic/solution1/syn/report
echo
echo "==================== README numbers ===================="
sed -n '/Timing/,/Latency/p'                 "$R/systolic_kernel_csynth.rpt" | head -10
sed -n '/Utilization Estimates/,/^ *$/p'     "$R/systolic_kernel_csynth.rpt" | head -25
grep -h "Estimated Fmax" ../csynth.log csynth.log 2>/dev/null | tail -1
echo "========================================================"
