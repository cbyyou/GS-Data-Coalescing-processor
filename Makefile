VERILATOR ?= verilator
TOP       := data_coalescing_system
RTL       := rtl/data_coalescer.sv rtl/simple_memory_core.sv rtl/data_coalescing_system.sv
TB        := tb/benchmark.cpp
STAGE_DIR := /tmp/data_coalescer_build_$(shell id -u)
OBJ_DIR   := $(STAGE_DIR)/obj_dir
GPU_TOP       := simple_gpu_system
GPU_RTL       := rtl/data_coalescer.sv rtl/simple_simt_core.sv rtl/simple_gpu_system.sv
GPU_TB        := tb/gpu_benchmark.cpp
GPU_STAGE_DIR := /tmp/simple_gpu_build_$(shell id -u)
GPU_OBJ_DIR   := $(GPU_STAGE_DIR)/obj_dir
CHISEL_STAGE_DIR := /tmp/chisel_dco_build_$(shell id -u)
CHISEL_OBJ_DIR   := $(CHISEL_STAGE_DIR)/obj_dir
DIFF_STAGE_DIR   := /tmp/chisel_diff_build_$(shell id -u)
DIFF_OBJ_DIR     := $(DIFF_STAGE_DIR)/obj_dir

.PHONY: all lint test benchmark parameter-sweep gpu-lint gpu-test gpu-benchmark chisel-test chisel-generate chisel-benchmark chisel-diff clean

all: test

lint:
	$(VERILATOR) --lint-only --Wall -Wno-fatal --top-module $(TOP) $(RTL)

$(OBJ_DIR)/V$(TOP): $(RTL) $(TB) Makefile
	mkdir -p build
	mkdir -p $(STAGE_DIR)
	cp $(TB) $(STAGE_DIR)/benchmark.cpp
	env CCACHE_DISABLE=1 $(VERILATOR) --cc $(RTL) --exe $(STAGE_DIR)/benchmark.cpp --build --top-module $(TOP) \
		--Mdir $(OBJ_DIR) -CFLAGS "-O2 -std=c++17"

test benchmark: $(OBJ_DIR)/V$(TOP)
	$(OBJ_DIR)/V$(TOP)

parameter-sweep: $(OBJ_DIR)/V$(TOP)
	$(OBJ_DIR)/V$(TOP) --sweep

gpu-lint:
	$(VERILATOR) --lint-only --Wall -Wno-fatal --top-module $(GPU_TOP) $(GPU_RTL)

$(GPU_OBJ_DIR)/V$(GPU_TOP): $(GPU_RTL) $(GPU_TB) Makefile
	mkdir -p build
	mkdir -p $(GPU_STAGE_DIR)
	cp $(GPU_TB) $(GPU_STAGE_DIR)/gpu_benchmark.cpp
	env CCACHE_DISABLE=1 $(VERILATOR) --cc $(GPU_RTL) --exe $(GPU_STAGE_DIR)/gpu_benchmark.cpp --build \
		--top-module $(GPU_TOP) --Mdir $(GPU_OBJ_DIR) -CFLAGS "-O2 -std=c++17"

gpu-test gpu-benchmark: $(GPU_OBJ_DIR)/V$(GPU_TOP)
	$(GPU_OBJ_DIR)/V$(GPU_TOP)

chisel-test:
	cd chisel && sbt --batch test

chisel-generate:
	mkdir -p build/chisel-generated
	cd chisel && sbt --batch "runMain dco.Generate ../build/chisel-generated"

chisel-benchmark: chisel-generate
	mkdir -p $(CHISEL_STAGE_DIR)
	cp tb/chisel_benchmark.cpp $(CHISEL_STAGE_DIR)/
	env CCACHE_DISABLE=1 $(VERILATOR) --cc build/chisel-generated/DataCoalescingSystem.sv \
		--exe $(CHISEL_STAGE_DIR)/chisel_benchmark.cpp --build \
		--top-module DataCoalescingSystem --Mdir $(CHISEL_OBJ_DIR) \
		-CFLAGS "-O2 -std=c++17"
	$(CHISEL_OBJ_DIR)/VDataCoalescingSystem

chisel-diff: chisel-generate
	mkdir -p $(DIFF_STAGE_DIR)
	cp tb/golden_coalescer.hpp $(DIFF_STAGE_DIR)/
	cp tb/golden_coalescer.cpp $(DIFF_STAGE_DIR)/
	cp tb/coalescer_diff.cpp $(DIFF_STAGE_DIR)/
	env CCACHE_DISABLE=1 $(VERILATOR) --cc build/chisel-generated/DataCoalescer.sv \
		--exe $(DIFF_STAGE_DIR)/coalescer_diff.cpp \
		      $(DIFF_STAGE_DIR)/golden_coalescer.cpp --build \
		--top-module DataCoalescer --Mdir $(DIFF_OBJ_DIR) \
		-CFLAGS "-O2 -std=c++17"
	$(DIFF_OBJ_DIR)/VDataCoalescer

clean:
	rm -rf build
	rm -rf $(STAGE_DIR)
	rm -rf $(GPU_STAGE_DIR)
	rm -rf $(CHISEL_STAGE_DIR)
	rm -rf $(DIFF_STAGE_DIR)
