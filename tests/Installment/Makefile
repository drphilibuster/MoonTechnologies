# Installment's CURVE shaper, tested without Rack.
#
# Curve.hpp is pure DSP -- no Rack types, no state, no I/O -- so it needs no
# <rack.hpp> shim and compiles against the host toolchain as it stands. `make
# test` at the repo root runs this. The header is listed below: one left out of
# CORE is one whose edits do not rebuild the test, and a suite that does not
# rebuild passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Installment/Curve.hpp

all: test_curve

test: test_curve
	./test_curve

test_curve: test_curve.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_curve.cpp

clean:
	rm -rf test_curve *.dSYM

.PHONY: all test clean
