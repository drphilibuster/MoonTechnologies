# Racketeer's PT2399 delay loop, tested without Rack.
#
# Pt2399Loop.hpp and src/Pt2399.hpp are self-contained -- <cmath>, <cstdint>,
# <cstring>, <vector> and nothing else -- so it needs no <rack.hpp> shim at all. `make test` at the
# repo root runs this.
CXX ?= c++
# -ffp-contract=off: T7 compares two spellings of one update bit for bit, so the
# compiler may not fuse a multiply-add in one and not the other.
CXXFLAGS := -std=c++11 -O2 -ffp-contract=off -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Racketeer/Pt2399Loop.hpp ../../src/Pt2399.hpp ../../src/DspCache.hpp

all: test_loop

test: test_loop
	./test_loop

test_loop: test_loop.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_loop.cpp

clean:
	rm -rf test_loop *.dSYM

.PHONY: all test clean
