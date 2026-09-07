# Collusion's coupled-oscillator engine, tested without Rack.
#
# Swarm.hpp includes nothing but <cmath> and <cstdint> -- no Rack, not even
# `clamp`, which it defines for itself -- so the signal path under test is the
# shipping one and no <rack.hpp> shim is needed. `make test` at the repo root
# runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Collusion/Swarm.hpp

all: test_swarm

test: test_swarm
	./test_swarm

test_swarm: test_swarm.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_swarm.cpp

clean:
	rm -rf test_swarm *.dSYM

.PHONY: all test clean
