#include "systolic_4x4_kernel.h"

void systolic_core(
    data_t A_local[M_TILE][KMAX],
    data_t B_local[KMAX][N_TILE],
    acc_t  C_local[M_TILE][N_TILE],
    int K
) {
    acc_t psum[M_TILE][N_TILE]; //每个PE的partial sum
    data_t a_reg[M_TILE][N_TILE]; //A在阵列里往右流
    data_t b_reg[M_TILE][N_TILE]; //B在阵列里往下流
    
    //把二维数组拆成16个独立寄存器 让16个PE可以完全并行
#pragma HLS ARRAY_PARTITION variable=psum complete dim=0 
#pragma HLS ARRAY_PARTITION variable=a_reg complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_reg complete dim=0
  

    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            psum[i][j] = 0;
            a_reg[i][j] = 0;
            b_reg[i][j] = 0;
        }
    }

    for (int t = 0; t < K + M_TILE + N_TILE - 2; t++) {
#pragma HLS PIPELINE II=1

        data_t a_in[M_TILE];
        data_t b_in[N_TILE];

#pragma HLS ARRAY_PARTITION variable=a_in complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_in complete dim=0

        for (int i = 0; i < M_TILE; i++) {
#pragma HLS UNROLL
            if ((t - i) >= 0 && (t - i) < K)
                a_in[i] = A_local[i][t - i];
            else
                a_in[i] = 0;
        }

        for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
            if ((t - j) >= 0 && (t - j) < K)
                b_in[j] = B_local[t - j][j];
            else
                b_in[j] = 0;
        }

        data_t a_next[M_TILE][N_TILE];
        data_t b_next[M_TILE][N_TILE];

#pragma HLS ARRAY_PARTITION variable=a_next complete dim=0
#pragma HLS ARRAY_PARTITION variable=b_next complete dim=0

        for (int i = 0; i < M_TILE; i++) {
            for (int j = 0; j < N_TILE; j++) {
#pragma HLS UNROLL
                data_t a_val = (j == 0) ? a_in[i] : a_reg[i][j - 1];
                data_t b_val = (i == 0) ? b_in[j] : b_reg[i - 1][j];

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

// extern "C" 是告诉编译器：
// 这个函数名不要做C++的名字修饰
// 这样Vitis才能找到这个函数作为kernel入口
extern "C" void systolic_kernel(
    const data_t* A,  // 指针：指向HBM里A矩阵的地址
    const data_t* B,  // 指针：指向HBM里B矩阵的地址
    acc_t* C,         // 指针：指向HBM里C矩阵的地址（输出）
    int K             // 普通参数：矩阵的K维度
) {

// ===== 接口声明 =====

// m_axi = Memory AXI，就是连接HBM的高速数据总线
// port=A：A这个指针走这条总线
// bundle=gmem0：连到HBM的第0个bank（不同bank可以同时读，更快）
#pragma HLS INTERFACE m_axi port=A offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=B offset=slave bundle=gmem1  // B走另一个bank
#pragma HLS INTERFACE m_axi port=C offset=slave bundle=gmem2  // C又一个bank

// s_axilite = 慢速控制总线，用来传"参数"而不是"数据"
// host通过这条总线告诉kernel：A在哪个地址、K等于多少
#pragma HLS INTERFACE s_axilite port=A      bundle=control
#pragma HLS INTERFACE s_axilite port=B      bundle=control
#pragma HLS INTERFACE s_axilite port=C      bundle=control
#pragma HLS INTERFACE s_axilite port=K      bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
// port=return：kernel跑完后通过这里通知host"我跑完了"

// ===== 片上缓存（BRAM）=====
// 这三个数组不在HBM里，在FPGA芯片内部的BRAM里
// BRAM很小（几MB）但访问速度极快（1个时钟周期）
// HBM很大（8GB）但相对慢
    data_t A_local[M_TILE][KMAX];  // 片上A的缓存，只存当前tile
    data_t B_local[KMAX][N_TILE];  // 片上B的缓存
    acc_t  C_local[M_TILE][N_TILE];// 片上C的缓存

// ARRAY_PARTITION：把数组拆成独立寄存器
// dim=1：沿第1维（K维）完全拆开
// 这样systolic array的每个PE可以同时访问不同的K元素
#pragma HLS ARRAY_PARTITION variable=A_local complete dim=1
#pragma HLS ARRAY_PARTITION variable=B_local complete dim=2
#pragma HLS ARRAY_PARTITION variable=C_local complete dim=0

// ===== 第一步：从HBM搬数据到片上BRAM =====
// 就像工厂进货：从仓库（HBM）搬到车间（BRAM）
    for (int i = 0; i < M_TILE; i++) {      // 4行
        for (int k = 0; k < K; k++) {
#pragma HLS PIPELINE II=1
            A_local[i][k] = A[i * KMAX + k]; // HBM → BRAM
        }
    }

    for (int k = 0; k < K; k++) {
        for (int j = 0; j < N_TILE; j++) {   // 4列
#pragma HLS PIPELINE II=1
            B_local[k][j] = B[k * N_TILE + j]; // HBM → BRAM
        }
    }

// ===== 第二步：调用systolic_core计算 =====
// 数据已经在BRAM里了，让脉动阵列算
    systolic_core(A_local, B_local, C_local, K);

// ===== 第三步：把结果从BRAM写回HBM =====
// 就像工厂出货：从车间（BRAM）送到仓库（HBM）
    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
#pragma HLS PIPELINE II=1
            C[i * N_TILE + j] = C_local[i][j]; // BRAM → HBM
        }
    }
}
