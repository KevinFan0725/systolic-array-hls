#pragma once //the header can be include once
#include <ap_int.h>

constexpr int M_TILE = 4;
constexpr int N_TILE = 4;
constexpr int KMAX   = 128;

typedef ap_int<16> data_t;
typedef ap_int<32> acc_t;

void systolic_core(
    data_t A_local[M_TILE][KMAX],
    data_t B_local[KMAX][N_TILE],
    acc_t  C_local[M_TILE][N_TILE],
    int K
);

extern "C" void systolic_kernel( //make the function be used as kernel
    const data_t* A,
    const data_t* B,
    acc_t* C,
    int K
);
