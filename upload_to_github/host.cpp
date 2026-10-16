#include <iostream>
#include <vector>
#include <cstdint>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./host <path_to_xclbin>" << std::endl;
        return 1;
    }

    // Must match systolic_4x4_kernel.h
    const int M_TILE = 4;
    const int N_TILE = 4;
    const int KMAX   = 128;
    const int K      = 128;

    std::string xclbin_path = argv[1];

    std::cout << "[HOST] Open device" << std::endl;
    auto device = xrt::device(0);

    std::cout << "[HOST] Load xclbin: " << xclbin_path << std::endl;
    auto uuid = device.load_xclbin(xclbin_path);

    // Name must match the extern "C" entry point in the kernel.
    std::cout << "[HOST] Open kernel: systolic_kernel" << std::endl;
    auto kernel = xrt::kernel(device, uuid, "systolic_kernel");

    // The kernel reads 512-bit beats. A is padded to KMAX per row so the whole
    // tile is one contiguous burst; allocate the padded size regardless of K.
    size_t size_A = (size_t)M_TILE * KMAX   * sizeof(int16_t);   // 1024 B = 16 beats
    size_t size_B = (size_t)KMAX   * N_TILE * sizeof(int16_t);   // 1024 B = 16 beats
    size_t size_C = (size_t)M_TILE * N_TILE * sizeof(int32_t);   //   64 B =  1 beat

    auto bo_A = xrt::bo(device, size_A, kernel.group_id(0));   // gmem0
    auto bo_B = xrt::bo(device, size_B, kernel.group_id(1));   // gmem1
    auto bo_C = xrt::bo(device, size_C, kernel.group_id(2));   // gmem2

    auto A_ptr = bo_A.map<int16_t*>();
    auto B_ptr = bo_B.map<int16_t*>();
    auto C_ptr = bo_C.map<int32_t*>();

    std::cout << "[HOST] Fill input data (K = " << K << ")" << std::endl;
    for (size_t i = 0; i < size_A / sizeof(int16_t); i++) A_ptr[i] = 0;
    for (size_t i = 0; i < size_B / sizeof(int16_t); i++) B_ptr[i] = 0;
    for (int i = 0; i < M_TILE * N_TILE; i++)             C_ptr[i] = 0;

    // Asymmetric test data. An identity-matrix test would pass even if only
    // the diagonal PEs worked, so use values that depend on both indices.
    for (int i = 0; i < M_TILE; i++)
        for (int k = 0; k < K; k++)
            A_ptr[i * KMAX + k] = (int16_t)((i * 7 + k * 3) % 11 - 5);  // padded stride

    for (int k = 0; k < K; k++)
        for (int j = 0; j < N_TILE; j++)
            B_ptr[k * N_TILE + j] = (int16_t)((k * 5 + j * 2) % 9 - 4);

    // CPU reference
    std::vector<int32_t> ref(M_TILE * N_TILE, 0);
    for (int i = 0; i < M_TILE; i++)
        for (int j = 0; j < N_TILE; j++) {
            int32_t acc = 0;
            for (int k = 0; k < K; k++)
                acc += (int32_t)A_ptr[i * KMAX + k] * (int32_t)B_ptr[k * N_TILE + j];
            ref[i * N_TILE + j] = acc;
        }

    std::cout << "[HOST] Sync input to device" << std::endl;
    bo_A.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    bo_B.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // Argument order must match the kernel signature: (A, B, C, K)
    std::cout << "[HOST] Launch kernel" << std::endl;
    auto run = kernel(bo_A, bo_B, bo_C, K);
    run.wait();

    std::cout << "[HOST] Sync output from device" << std::endl;
    bo_C.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    std::cout << "[HOST] Check results:" << std::endl;
    bool pass = true;
    for (int i = 0; i < M_TILE; i++) {
        for (int j = 0; j < N_TILE; j++) {
            int32_t expected = ref[i * N_TILE + j];
            int32_t got      = C_ptr[i * N_TILE + j];
            std::cout << got << "\t";
            if (got != expected) {
                pass = false;
                std::cerr << "\nMISMATCH at C[" << i << "][" << j << "]"
                          << " expected=" << expected
                          << " got=" << got << std::endl;
            }
        }
        std::cout << std::endl;
    }

    std::cout << (pass ? "[HOST] PASS" : "[HOST] FAIL") << std::endl;
    return pass ? 0 : 1;
}
