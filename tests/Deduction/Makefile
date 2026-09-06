# Deduction's six filter cores, tested without Rack.
#
# Filters.hpp includes nothing but <cmath> and reaches for exactly one thing
# from Rack -- `clamp` -- which the test defines itself, so no <rack.hpp> shim
# is needed. `make test` at the repo root runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Deduction/Filters.hpp

all: test_filters

test: test_filters
	./test_filters

test_filters: test_filters.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_filters.cpp

clean:
	rm -rf test_filters *.dSYM

.PHONY: all test clean
