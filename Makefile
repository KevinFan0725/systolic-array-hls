# ==============================
# Configuration
# ==============================
TARGET   := hw
PLATFORM := /opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm
KERNEL   := systolic_kernel
XO       := $(KERNEL).xo
XCLBIN   := $(KERNEL).xclbin
HOST_EXE := host

# Vitis compiler (FPGA side)
VPP      := v++
CXX      := g++
XILINX_XRT ?= /opt/xilinx/xrt

CXXFLAGS := -std=c++17 -O2
LDFLAGS  := -lxrt_coreutil -lxrt_core -luuid

# ==============================
# Targets
# ==============================
# 'all' must stay first so a bare `make` builds everything.
all: $(XCLBIN) $(HOST_EXE)

# Step 1: kernel.cpp -> .xo
$(XO): systolic_4x4_kernel.cpp systolic_4x4_kernel.h
	$(VPP) -c -t $(TARGET) \
		--platform $(PLATFORM) \
		-k $(KERNEL) \
		-o $@ $<

# Step 2: .xo -> .xclbin
$(XCLBIN): $(XO)
	$(VPP) -l -t $(TARGET) \
		--platform $(PLATFORM) \
		-o $@ $<

# Step 3: host.cpp -> executable
$(HOST_EXE): host.cpp
	$(CXX) $(CXXFLAGS) \
		-I$(XILINX_XRT)/include \
		-L$(XILINX_XRT)/lib \
		$< -o $@ $(LDFLAGS)

run: $(HOST_EXE) $(XCLBIN)
	./$(HOST_EXE) $(XCLBIN)

# Fast correctness + resource check, no bitstream (minutes, not hours).
csynth:
	vitis_hls -f run_csynth.tcl

clean:
	rm -f $(XO) $(XCLBIN) $(HOST_EXE)
	rm -rf csynth_systolic .Xil *.log *.jou

.PHONY: all run csynth clean
