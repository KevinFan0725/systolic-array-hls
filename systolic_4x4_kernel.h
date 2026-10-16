#pragma once
#include <ap_int.h>

constexpr int M_TILE = 4;
constexpr int N_TILE = 4;
constexpr int KMAX   = 128;           // on-chip buffer depth (max K accepted)

typedef ap_int<16>  data_t;           // 16-bit signed operands
typedef ap_int<32>  acc_t;            // 32-bit signed accumulator
typedef ap_uint<512> wide_t;          // one full AXI beat

constexpr int E_PER_W   = 512 / 16;   // 32 int16 per beat
constexpr int WORDS_ROW = KMAX / E_PER_W;   // 4 beats per padded A row


extern "C" void systolic_kernel(
    const wide_t* A,
    const wide_t* B,
    wide_t*       C,
    int           K
);
