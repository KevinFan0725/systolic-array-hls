#include "systolic_4x4_kernel.h"

void systolic_core(
    data_t A_local[M_TILE][KMAX],
    data_t B_local[KMAX][N_TILE],
    acc_t  C_local[M_TILE][N_TILE],
    int K
) {
    acc_t  psum [M_TILE][N_TILE];   // per-PE accumulator (stationary)
    data_t a_reg[M_TILE][N_TILE];   // A pipeline register, flows right
    data_t b_reg[M_TILE][N_TILE];   // B pipeline register, flows down

    // Split into 16 independent registers so all PEs run in parallel.
#pragma HLS ARRAY_PARTITION variable=psum  complete dim=0
#pragma HLS ARRAY_PARTITION variable=a_reg complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_reg complete dim=0

    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            psum [i][j] = 0;
            a_reg[i][j] = 0;
            b_reg[i][j] = 0;
        }
    }

    for (int t = 0; t < K + M_TILE + N_TILE - 2; t++) {
#pragma HLS LOOP_TRIPCOUNT min=10 max=134 avg=134
#pragma HLS PIPELINE II=1

        data_t a_in[M_TILE];
        data_t b_in[N_TILE];
#pragma HLS ARRAY_PARTITION variable=a_in complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_in complete dim=0

        // Inject A from the left edge, skewed by the row index.
        for (int i = 0; i < M_TILE; i++) {
#pragma HLS UNROLL
            a_in[i] = ((t - i) >= 0 && (t - i) < K) ? A_local[i][t - i] : (data_t)0;
        }

        // Inject B from the top edge, skewed by the column index.
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            b_in[j] = ((t - j) >= 0 && (t - j) < K) ? B_local[t - j][j] : (data_t)0;
        }

        // Two-phase update. The new values go to a_next/b_next, NOT straight
        // back into a_reg/b_reg -- otherwise PE(i,j+1) would read the value
        // PE(i,j) produced in this same iteration, collapsing all four columns
        // into one combinational chain and destroying II=1.
        data_t a_next[M_TILE][N_TILE];
        data_t b_next[M_TILE][N_TILE];
#pragma HLS ARRAY_PARTITION variable=a_next complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_next complete dim=0

        for (int i = 0; i < M_TILE; i++) {
            for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
                // Column 0 takes the fresh injection, others take the left neighbour.
                data_t a_val = (j == 0) ? a_in[i] : a_reg[i][j - 1];
                // Row 0 takes the fresh injection, others take the neighbour above.
                data_t b_val = (i == 0) ? b_in[j] : b_reg[i - 1][j];

                psum[i][j] += (acc_t)a_val * (acc_t)b_val;

                a_next[i][j] = a_val;   // pass right
                b_next[i][j] = b_val;   // pass down
            }
        }

        // Phase 2: commit.
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

// extern "C" keeps the symbol unmangled so Vitis can find the kernel entry.
extern "C" void systolic_kernel(
    const wide_t* A,
    const wide_t* B,
    wide_t*       C,
    int           K
) {
// Ports are declared 512 bits wide so every AXI beat carries 32 operands.
// Letting HLS infer widening from a data_t* port fails here: the row stride
// depends on the runtime K, so it can prove neither alignment nor a compatible
// stride and falls back to one 16-bit element per cycle.
#pragma HLS INTERFACE m_axi port=A offset=slave bundle=gmem0 depth=16
#pragma HLS INTERFACE m_axi port=B offset=slave bundle=gmem1 depth=16
#pragma HLS INTERFACE m_axi port=C offset=slave bundle=gmem2 depth=1

#pragma HLS INTERFACE s_axilite port=A      bundle=control
#pragma HLS INTERFACE s_axilite port=B      bundle=control
#pragma HLS INTERFACE s_axilite port=C      bundle=control
#pragma HLS INTERFACE s_axilite port=K      bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    data_t A_local[M_TILE][KMAX];
    data_t B_local[KMAX][N_TILE];
    acc_t  C_local[M_TILE][N_TILE];

// Banking has to absorb a whole 512-bit beat per cycle: 32 elements land at
// once, and a single BRAM has only two ports.
//   A: one bank per row (dim=1) x 32 banks across K, so the 32 elements of a
//      beat (k = w*32 .. w*32+31) hit 32 distinct banks.
//   B: a beat covers 8 consecutive k rows x 4 columns, so bank on k modulo 8
//      (dim=1) and split the 4 columns (dim=2) -> again 32 distinct banks.
#pragma HLS ARRAY_PARTITION variable=A_local complete dim=1
#pragma HLS ARRAY_PARTITION variable=A_local cyclic factor=32 dim=2
#pragma HLS ARRAY_PARTITION variable=B_local cyclic factor=8  dim=1
#pragma HLS ARRAY_PARTITION variable=B_local complete dim=2
#pragma HLS ARRAY_PARTITION variable=C_local complete dim=0

    const int words_b = (K * N_TILE + E_PER_W - 1) / E_PER_W;   // beats for all of B

    // Reading row by row costs a fresh AXI transaction per row (~64 cycles of
    // latency each). A is padded to M_TILE*KMAX, so the whole tile is one
    // contiguous run of beats: issue it as a single burst and pay that latency
    // once. For K < KMAX this moves padding too, but 16 beats is cheaper than
    // four round trips.
    for (int w = 0; w < M_TILE * WORDS_ROW; w++) {
#pragma HLS PIPELINE II=1
        wide_t beat = A[w];
        const int row = w / WORDS_ROW;
        const int seg = w % WORDS_ROW;
        for (int e = 0; e < E_PER_W; e++) {
#pragma HLS UNROLL
            A_local[row][seg * E_PER_W + e] =
                (data_t)(ap_int<16>)beat.range(e * 16 + 15, e * 16);
        }
    }

    for (int w = 0; w < words_b; w++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=16 avg=16
#pragma HLS PIPELINE II=1
        wide_t beat = B[w];
        for (int e = 0; e < E_PER_W; e++) {
#pragma HLS UNROLL
            B_local[w * (E_PER_W / N_TILE) + e / N_TILE][e % N_TILE] =
                (data_t)(ap_int<16>)beat.range(e * 16 + 15, e * 16);
        }
    }

    systolic_core(A_local, B_local, C_local, K);


    wide_t out = 0;
    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            out.range((i * N_TILE + j) * 32 + 31, (i * N_TILE + j) * 32) =
                (ap_uint<32>)C_local[i][j];
        }
    }
    C[0] = out;
}
