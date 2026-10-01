# Amortization's circuit -- the Verbtronic schematic, solved -- tested without Rack.
#
# Verbtronic.hpp is self-contained -- <cmath>, <cstddef>, <cstdint>, <vector>
# local clampf, nothing else -- so it needs no <rack.hpp> shim. `make test` at
# the repo root runs this. A header left out of CORE is one whose edits do not
# rebuild the test, and a negative control against a stale binary passes for the
# wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Amortization/Verbtronic.hpp

all: test_verbtronic

test: test_verbtronic
	./test_verbtronic

test_verbtronic: test_verbtronic.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_verbtronic.cpp

clean:
	rm -rf test_verbtronic *.dSYM

.PHONY: all test clean
