# ==============================
# 配置区域（到yanzi后改这里）
# ==============================
TARGET   := hw
PLATFORM := /opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm
KERNEL   := systolic_kernel
XO       := $(KERNEL).xo
XCLBIN   := $(KERNEL).xclbin
HOST_EXE := host

VPP      := v++   # Vitis的编译器，专门编译FPGA代码
CXX      := g++
XILINX_XRT ?= /opt/xilinx/xrt

CXXFLAGS := -std=c++17 -O2
LDFLAGS  := -lxrt_coreutil -lxrt_core -luuid

# ==============================
# 编译步骤
# ==============================

# 步骤1：kernel.cpp → .xo
$(XO): systolic_4x4_kernel.cpp
	$(VPP) -c -t $(TARGET) \
		--platform $(PLATFORM) \
		-k $(KERNEL) \
		-o $@ $<

# 步骤2：.xo → .xclbin
$(XCLBIN): $(XO)
	$(VPP) -l -t $(TARGET) \
		--platform $(PLATFORM) \
		-o $@ $<

# 步骤3：host.cpp → 可执行文件
$(HOST_EXE): host.cpp
	$(CXX) $(CXXFLAGS) \
		-I$(XILINX_XRT)/include \
		-L$(XILINX_XRT)/lib \
		$< -o $@ $(LDFLAGS)

# 一键编译全部
all: $(XCLBIN) $(HOST_EXE)

# 运行
run: $(HOST_EXE) $(XCLBIN)
	./$(HOST_EXE) $(XCLBIN)

clean:
	rm -f $(XO) $(XCLBIN) $(HOST_EXE)

.PHONY: all run clean
