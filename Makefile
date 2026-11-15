VERILATOR ?= verilator
TOP := axi_lite_firewall
RTL := rtl/axi_lite_firewall.sv
.PHONY: all test lint waves clean
all: test
lint:
	$(VERILATOR) --lint-only --assert -Wall --top-module $(TOP) $(RTL)
build/V$(TOP): $(RTL) sim/test_firewall.cpp
	mkdir -p build
	$(VERILATOR) --cc --exe --build -j 2 --assert --trace -Wall --top-module $(TOP) --Mdir build -CFLAGS '-std=c++17' $(RTL) $(abspath sim/test_firewall.cpp)
test: lint build/V$(TOP)
	./build/V$(TOP) 1
	./build/V$(TOP) 42
	./build/V$(TOP) 2026
waves: build/V$(TOP)
	./build/V$(TOP) 1 --trace
clean:
	rm -rf build
