# The shared quantizer, tested without Rack.
#
# Quantizer.hpp is self-contained -- <cmath>, <cstdio>, <string> and nothing
# else -- so it needs no <rack.hpp> shim. `make test` at the repo root runs
# this. A header left out of CORE is one whose edits do not rebuild the test,
# and a negative control against a stale binary passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Quantizer.hpp

all: test_quantizer

test: test_quantizer
	./test_quantizer

test_quantizer: test_quantizer.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_quantizer.cpp

clean:
	rm -rf test_quantizer *.dSYM

.PHONY: all test clean
