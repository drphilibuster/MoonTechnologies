# Accrual (the 4069 VCO): the audio-rate model against the whole netlist solved by brute force,
# the exponential pair and the PNP against independent oracles, tested without Rack.
#
# Vco.hpp, Cd4069.hpp and Mna.hpp are Rack-free, so `make test` at the repo root runs this on a
# machine with neither Rack nor the SDK. Every header the test includes is listed: one left out
# of CORE is one whose edits do not rebuild the test, and a suite that does not rebuild passes
# for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Accrual/Vco.hpp ../../src/Cd4069.hpp ../../src/Mna.hpp Fullnet.hpp

all: test_vco

test: test_vco
	./test_vco

test_vco: test_vco.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_vco.cpp

clean:
	rm -rf test_vco *.dSYM

.PHONY: all test clean
