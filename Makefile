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

.PHONY: all lint test benchmark gpu-lint gpu-test gpu-benchmark chisel-test chisel-generate chisel-benchmark clean

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
	cp tb/chisel_benchmark.cpp /tmp/chisel_benchmark.cpp
	env CCACHE_DISABLE=1 verilator --cc build/chisel-generated/DataCoalescingSystem.sv --exe /tmp/chisel_benchmark.cpp --build --top-module DataCoalescingSystem --Mdir /tmp/chisel_dco_obj -CFLAGS "-O2 -std=c++17"
	/tmp/chisel_dco_obj/VDataCoalescingSystem

clean:
	rm -rf build
	rm -rf $(STAGE_DIR)
	rm -rf $(GPU_STAGE_DIR)
