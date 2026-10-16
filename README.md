# 4×4 Systolic Array GEMM on FPGA (Vitis HLS)

Output-stationary systolic array for C = A·B, targeting AMD Alveo with HBM.

## Design
- 16 PEs (4×4), fully partitioned registers → 16 MACs/cycle at II=1
- Skewed wavefront feeding: row i and column j delayed by i and j cycles
- Three independent HBM bundles (gmem0/1/2) for A, B, C
- On-chip BRAM staging before compute, writeback after

## Performance
- K + 6 cycles for a 4×4×K tile (II=1)
- [填实测的时钟和资源]

## Build
make -f Makefile
