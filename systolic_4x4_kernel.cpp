#include "systolic_4x4_kernel.h"

// Output-stationary 4x4 systolic array.
// A flows left-to-right, B flows top-to-bottom, partial sums stay in place.
void systolic_core(
    data_t A_local[M_TILE][KMAX],
    data_t B_local[KMAX][N_TILE],
    acc_t  C_local[M_TILE][N_TILE],
    int K
) {
    acc_t  psum[M_TILE][N_TILE];    // per-PE accumulator
    data_t a_reg[M_TILE][N_TILE];   // A value held by each PE this cycle
    data_t b_reg[M_TILE][N_TILE];   // B value held by each PE this cycle

    // Split into 16 independent registers so all PEs read and write in one cycle.
#pragma HLS ARRAY_PARTITION variable=psum  complete dim=0
#pragma HLS ARRAY_PARTITION variable=a_reg complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_reg complete dim=0

    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            psum[i][j]  = 0;
            a_reg[i][j] = 0;
            b_reg[i][j] = 0;
        }
    }

    // Skewed wavefront: row i and column j enter the array i and j cycles late,
    // so the array drains after K + M_TILE + N_TILE - 2 steps.
    for (int t = 0; t < K + M_TILE + N_TILE - 2; t++) {
#pragma HLS PIPELINE II=1

        data_t a_in[M_TILE];
        data_t b_in[N_TILE];
#pragma HLS ARRAY_PARTITION variable=a_in complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_in complete dim=0

        for (int i = 0; i < M_TILE; i++) {
#pragma HLS UNROLL
            a_in[i] = ((t - i) >= 0 && (t - i) < K) ? A_local[i][t - i] : (data_t)0;
        }

        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            b_in[j] = ((t - j) >= 0 && (t - j) < K) ? B_local[t - j][j] : (data_t)0;
        }

        // Two-phase update: every PE must read its neighbours' values from the
        // previous cycle, so new values go to a_next/b_next and are committed
        // after all PEs have read.
        data_t a_next[M_TILE][N_TILE];
        data_t b_next[M_TILE][N_TILE];
#pragma HLS ARRAY_PARTITION variable=a_next complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_next complete dim=0

        for (int i = 0; i < M_TILE; i++) {
            for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
                data_t a_val = (j == 0) ? a_in[i] : a_reg[i][j - 1];   // from the left
                data_t b_val = (i == 0) ? b_in[j] : b_reg[i - 1][j];   // from above

                psum[i][j] += (acc_t)a_val * (acc_t)b_val;

                a_next[i][j] = a_val;
                b_next[i][j] = b_val;
            }
        }

        for (int i = 0; i < M_TILE; i++) {
            for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
                a_reg[i][j] = a_next[i][j];
                b_reg[i][j] = b_next[i][j];
            }
        }
    }

    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            C_local[i][j] = psum[i][j];
        }
    }
}


extern "C" void systolic_kernel(
    const data_t* A,
    const data_t* B,
    acc_t* C,
    int K
) {
    // A, B and C get separate bundles so their transfers do not serialise.
#pragma HLS INTERFACE m_axi port=A offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=B offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=C offset=slave bundle=gmem2

#pragma HLS INTERFACE s_axilite port=A      bundle=control
#pragma HLS INTERFACE s_axilite port=B      bundle=control
#pragma HLS INTERFACE s_axilite port=C      bundle=control
#pragma HLS INTERFACE s_axilite port=K      bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    data_t A_local[M_TILE][KMAX];
    data_t B_local[KMAX][N_TILE];
    acc_t  C_local[M_TILE][N_TILE];

    // A_local splits along the row dimension and B_local along the column
    // dimension, so the array can read one element per row and per column
    // in the same cycle.
#pragma HLS ARRAY_PARTITION variable=A_local complete dim=1
#pragma HLS ARRAY_PARTITION variable=B_local complete dim=2
#pragma HLS ARRAY_PARTITION variable=C_local complete dim=0

    for (int i = 0; i < M_TILE; i++) {
        for (int k = 0; k < K; k++) {
#pragma HLS PIPELINE II=1
            A_local[i][k] = A[i * KMAX + k];
        }
    }

    for (int k = 0; k < K; k++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS PIPELINE II=1
            B_local[k][j] = B[k * N_TILE + j];
        }
    }

    systolic_core(A_local, B_local, C_local, K);

    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS PIPELINE II=1
            C[i * N_TILE + j] = C_local[i][j];
        }
    }
}
