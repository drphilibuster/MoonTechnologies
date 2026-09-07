# The Chebyshev harmonic exciter, tested without Rack.
#
# Chebyshev.hpp is self-contained -- <cmath>, <cstddef> and nothing else -- so
# it needs no <rack.hpp> shim. `make test` at the repo root runs this. A header
# left out of CORE is one whose edits do not rebuild the test, and a negative
# control against a stale binary passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Dependents/Chebyshev.hpp

all: test_cheb

test: test_cheb
	./test_cheb

test_cheb: test_cheb.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_cheb.cpp

clean:
	rm -rf test_cheb *.dSYM

.PHONY: all test clean
