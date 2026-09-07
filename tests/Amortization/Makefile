# Amortization's two tanks and its mode collider, tested without Rack.
#
# Tanks.hpp is self-contained -- <cmath>, <cstddef>, <cstdint>, <vector> and a
# local clampf, nothing else -- so it needs no <rack.hpp> shim. `make test` at
# the repo root runs this. A header left out of CORE is one whose edits do not
# rebuild the test, and a negative control against a stale binary passes for the
# wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Amortization/Tanks.hpp

all: test_tanks

test: test_tanks
	./test_tanks

test_tanks: test_tanks.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_tanks.cpp

clean:
	rm -rf test_tanks *.dSYM

.PHONY: all test clean
