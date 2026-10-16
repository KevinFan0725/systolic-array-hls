# 4×4 Systolic Array GEMM on FPGA (Vitis HLS)

An output-stationary 4×4 systolic array for `C = A × B`, written in C++ for
Vitis HLS and targeting Alveo HBM cards over XRT.

## Design

16 processing elements in a 4×4 grid. Each PE owns one accumulator for
`C[i][j]` and never moves it — only the operands move:

```
            B (flows down, 1 hop/cycle)
            |    |    |    |
       A -- PE - PE - PE - PE
  (flows -- PE - PE - PE - PE
   right)-- PE - PE - PE - PE
       A -- PE - PE - PE - PE
```

Operands are **skewed on injection** so the two values meeting at `PE(i,j)` in
any cycle always carry the same `k`:

| injection | delay to reach PE(i,j) | k index on arrival |
|---|---|---|
| `a_in[i] = A[i][t-i]` | `j` cycles (one per column hop) | `t - i - j` |
| `b_in[j] = B[t-j][j]` | `i` cycles (one per row hop) | `t - i - j` |

The loop runs `K + M_TILE + N_TILE - 2` cycles. On the last one (`t = K+5`) the
far corner `PE(3,3)` sees `k = K-1`, so the array fills and drains exactly.

Each cycle updates in **two phases** (`a_next`/`b_next`, then commit). Writing
`a_reg[i][j] = a_val` directly would let `PE(i,j+1)` read the value `PE(i,j)`
produced in the *same* C iteration, collapsing all four columns into one
combinational chain and destroying `II=1`.

## Memory layout

The kernel reads 512-bit AXI beats, so the host lays the operands out to match:

| buffer | layout | size |
|---|---|---|
| `A` | `M_TILE` rows, each **padded to `KMAX`** int16 | 1024 B = 16 beats |
| `B` | `KMAX × N_TILE` int16, contiguous | 1024 B = 16 beats |
| `C` | `M_TILE × N_TILE` int32 | 64 B = 1 beat |

Padding A's rows to `KMAX` makes the whole tile one contiguous run, so it loads
as a single burst instead of one transaction per row.

## Results

C-synthesis and C/RTL co-simulation, Vitis HLS 2022.2, 300 MHz target
(3.33 ns). `xcu280-fsvh2892-2L-e` and `xcu55c-fsvh2892-2L-e` give **identical**
numbers — they are the same die.

### Timing

| | |
|---|---|
| Target period | 3.33 ns (300 MHz) |
| Estimated critical path | **2.431 ns** |
| Estimated Fmax | **411.4 MHz** |
| Initiation interval, systolic loop | **1** (one `k` step per cycle) |

> The report prints a top-level slack of `-0.00`. That is
> `3.33 − 2.431 − 0.90`, where 0.90 ns is the default HLS clock uncertainty
> (27% of the target). The critical path itself clears 300 MHz with 0.9 ns to
> spare; the uncertainty margin is what lands it on zero.

### Resources

| Resource | Used | Device | Utilization |
|---|---|---|---|
| DSP | 16 | 9024 | ~0% |
| LUT | 60,159 | 1,303,680 | 4% |
| FF | 21,431 | 2,607,360 | ~0% |
| BRAM_18K | 0 | 4032 | 0% |
| URAM | 0 | 960 | 0% |

One DSP per PE, exactly as intended. The staging buffers mapped to registers
and LUTRAM rather than BRAM at these sizes.

### Measured latency (C/RTL co-simulation, Verilog, PASS)

| K | cycles | @300 MHz |
|---|---|---|
| 4 | 274 | 0.91 µs |
| 128 | 384 | 1.28 µs |

Verified element-by-element against a CPU reference GEMM at K = 4, 16, 32, 100
and 128. K=100 is deliberately not a multiple of 32, so it exercises the
partially-filled tail beat.

## What the memory path cost, and what fixing it bought

The first working version passed its ports as `data_t*` and let HLS infer the
burst. It did not widen them:

```
* Inferred Bursts and Widening Missed
| m_axi_gmem0 | A | Start index of the access is unaligned |
| m_axi_gmem1 | B | Start index of the access is unaligned |
```

`A[i * K + k]` has a runtime `K`, so HLS can prove neither that each row starts
on a 512-bit boundary nor that the stride is compatible, and fell back to
**16 bits per beat** — 512 cycles to move 512 operands, against 137 cycles of
actual arithmetic. Padding the rows to `KMAX` alone did not help; HLS simply
changed its complaint to `Stride is incompatible`, because only `K` of each
`KMAX`-element row was being read.

What worked was declaring the ports `ap_uint<512>` and unpacking by hand. That
forces the issue, but it moves the problem on-chip: 32 operands now arrive per
cycle, and a BRAM has two ports, so the staging buffers have to be banked to
absorb them (`A_local` 4×32 banks, `B_local` 8×4).

| | baseline | wide ports | change |
|---|---|---|---|
| cycles @ K=128 | 884 | **384** | **2.3× faster** |
| LUT | 14,377 | **60,159** | 4.2× more |
| DSP | 16 | 16 | — |
| Fmax | 411.4 MHz | 411.4 MHz | unchanged |
| MAC utilisation | 14.5% | **33.3%** | |

The banking muxes, the 32-way unpack and the variable row index are what cost
the LUTs. On this device it is an easy trade — 1% to 4% of a part that has
1.3 M LUTs — but on a design that already fills the fabric it would not be.

Timing did not move, which is the useful part: the added logic is wide and
shallow, so it does not lengthen the critical path.

Remaining overhead at K=128 is roughly 384 − 137 = 247 cycles, most of it AXI
read latency that a `DATAFLOW` region could hide by overlapping the loads with
the compute.

## Build

```bash
make csynth     # C-sim + C-synthesis: correctness, timing, resources (~30 s)
make            # full bitstream: .xo -> .xclbin + host (hours)
make run        # run on hardware
```

`make csynth` runs C-simulation, C-synthesis **and** C/RTL co-simulation, and
needs only Vitis HLS. Override the target part:

```bash
SYN_PART=xcu55c-fsvh2892-2L-e SYN_PERIOD=3.0 vitis_hls -f run_csynth.tcl
```

## Files

| file | role |
|---|---|
| `systolic_4x4_kernel.cpp` | the kernel: PE array + wide-beat AXI wrapper |
| `systolic_4x4_kernel.h` | tile sizes and data types |
| `tb_systolic.cpp` | testbench, checks against a CPU GEMM |
| `host.cpp` | XRT host: allocate, transfer, launch, verify |
| `run_csynth.tcl` | C-sim + C-synthesis script |
| `Makefile` | build flow |

Numbers above are Vitis HLS estimates plus co-simulation cycle counts, not
post-route results; a full `v++ -t hw` implementation would refine the timing
and resource figures.
