// C-simulation / co-simulation testbench for the wide-port kernel.

#include "systolic_4x4_kernel.h"
#include <cstdio>
#include <cstring>

static wide_t Abuf[M_TILE * WORDS_ROW];                 // 16 beats
static wide_t Bbuf[KMAX * N_TILE / E_PER_W];            // 16 beats
static wide_t Cbuf[1];

static void poke(wide_t* buf, int idx, int val) {       // write element idx
    buf[idx / E_PER_W].range((idx % E_PER_W) * 16 + 15,
                             (idx % E_PER_W) * 16) = (ap_uint<16>)(ap_int<16>)val;
}
static int peek32(wide_t* buf, int idx) {               // read int32 element idx
    return (int)(ap_int<32>)buf[0].range(idx * 32 + 31, idx * 32);
}

static int run_case(int K, const char* label) {
    for (int i = 0; i < M_TILE * WORDS_ROW; i++)      Abuf[i] = 0;
    for (int i = 0; i < KMAX * N_TILE / E_PER_W; i++) Bbuf[i] = 0;
    Cbuf[0] = 0;

    int Aref[M_TILE][KMAX], Bref[KMAX][N_TILE];
    for (int i = 0; i < M_TILE; i++)
        for (int k = 0; k < K; k++) {
            int v = (i * 7 + k * 3) % 11 - 5;
            Aref[i][k] = v;
            poke(Abuf, i * KMAX + k, v);        // row stride is the padded KMAX
        }
    for (int k = 0; k < K; k++)
        for (int j = 0; j < N_TILE; j++) {
            int v = (k * 5 + j * 2) % 9 - 4;
            Bref[k][j] = v;
            poke(Bbuf, k * N_TILE + j, v);
        }

    systolic_kernel(Abuf, Bbuf, Cbuf, K);

    int bad = 0;
    for (int i = 0; i < M_TILE; i++)
        for (int j = 0; j < N_TILE; j++) {
            long ref = 0;
            for (int k = 0; k < K; k++) ref += (long)Aref[i][k] * (long)Bref[k][j];
            long got = (long)peek32(Cbuf, i * N_TILE + j);
            if (got != ref) {
                if (bad < 8) printf("  MISMATCH C[%d][%d] expected %ld got %ld\n", i, j, ref, got);
                bad++;
            }
        }
    printf("[%s] K=%-4d  mismatches = %d  %s\n", label, K, bad, bad ? "FAIL" : "PASS");
    return bad;
}

int main() {
    int bad = 0;
    bad += run_case(4,   "min");
    bad += run_case(16,  "mid");
    bad += run_case(32,  "aln");
    bad += run_case(100, "odd"); 
    bad += run_case(128, "max");
    printf("%s\n", bad ? "CSIM FAIL" : "CSIM PASS");
    return bad ? 1 : 0;
}
