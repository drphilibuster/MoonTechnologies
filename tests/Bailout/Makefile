# Bailout's routing, tested without Rack.
#
# Bus.hpp is pure DSP -- no Rack types, no state, no I/O -- so it needs no
# <rack.hpp> shim and compiles against the host toolchain as it stands. `make
# test` at the repo root runs this. The header is listed below: one left out of
# CORE is one whose edits do not rebuild the test, and a suite that does not
# rebuild passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Bailout/Bus.hpp

all: test_bus

test: test_bus
	./test_bus

test_bus: test_bus.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_bus.cpp

clean:
	rm -rf test_bus *.dSYM

.PHONY: all test clean
