#include <iostream>
#include <vector>
#include <cstdint>

// XRT头文件（在yanzi服务器上才有）
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./host <path_to_xclbin>" << std::endl;
        return 1;
    }

    // kernel参数（必须和kernel.h一致）
    const int M_TILE = 4;
    const int N_TILE = 4;
    const int K      = 4;  // 先测最小的K

    std::string xclbin_path = argv[1];

    // 1.打开FPGA设备
    std::cout << "[HOST] Open device" << std::endl;
    auto device = xrt::device(0);

    // 2.加载xclbin
    std::cout << "[HOST] Load xclbin: " << xclbin_path << std::endl;
    auto uuid = device.load_xclbin(xclbin_path);

    // 3.找到kernel（名字必须和kernel.cpp里extern "C"函数名一致）
    std::cout << "[HOST] Open kernel: systolic_kernel" << std::endl;
    auto kernel = xrt::kernel(device, uuid, "systolic_kernel");

    // 4.在HBM上分配buffer
    // ap_int<16> = 2字节, ap_int<32> = 4字节
    size_t size_A = M_TILE * K * sizeof(int16_t);
    size_t size_B = K * N_TILE * sizeof(int16_t);
    size_t size_C = M_TILE * N_TILE * sizeof(int32_t);

    auto bo_A = xrt::bo(device, size_A, kernel.group_id(0)); // gmem0
    auto bo_B = xrt::bo(device, size_B, kernel.group_id(1)); // gmem1
    auto bo_C = xrt::bo(device, size_C, kernel.group_id(2)); // gmem2

    // 5.拿到host侧可写的指针
    auto A_ptr = bo_A.map<int16_t*>();
    auto B_ptr = bo_B.map<int16_t*>();
    auto C_ptr = bo_C.map<int32_t*>();

    // 6.填入测试数据：A=单位矩阵，B=单位矩阵，C应该=单位矩阵
    std::cout << "[HOST] Fill input data" << std::endl;
    for (int i = 0; i < M_TILE; i++)
        for (int k = 0; k < K; k++)
            A_ptr[i * K + k] = (i == k) ? 1 : 0;

    for (int k = 0; k < K; k++)
        for (int j = 0; j < N_TILE; j++)
            B_ptr[k * N_TILE + j] = (k == j) ? 1 : 0;

    for (int i = 0; i < M_TILE * N_TILE; i++)
        C_ptr[i] = 0;

    // 7.把数据从CPU内存同步到FPGA的HBM
    std::cout << "[HOST] Sync input to device" << std::endl;
    bo_A.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_B.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // 8.启动kernel
    // 参数顺序必须和kernel函数签名一致：(A, B, C, K)
    std::cout << "[HOST] Launch kernel" << std::endl;
    auto run = kernel(bo_A, bo_B, bo_C, K);
    run.wait(); // 等kernel跑完

    // 9.把结果从FPGA的HBM读回CPU
    std::cout << "[HOST] Sync output from device" << std::endl;
    bo_C.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    // 10.验证结果
    std::cout << "[HOST] Check results:" << std::endl;
    bool pass = true;
    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
            int32_t expected = (i == j) ? 1 : 0;
            int32_t got = C_ptr[i * N_TILE + j];
            std::cout << got << " ";
            if (got != expected) {
                pass = false;
                std::cerr << "\nMISMATCH at C[" << i << "][" << j << "]"
                          << " expected=" << expected
                          << " got=" << got << std::endl;
            }
        }
        std::cout << std::endl;
    }

    std::cout << (pass ? "[HOST] PASS ✓" : "[HOST] FAIL ✗") << std::endl;
    return pass ? 0 : 1;
}
