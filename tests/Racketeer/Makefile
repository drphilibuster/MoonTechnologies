# Racketeer's PT2399 delay loop, tested without Rack.
#
# Pt2399Loop.hpp is self-contained -- <cmath>, <cstdint>, <cstring>, <vector>
# and nothing else -- so it needs no <rack.hpp> shim at all. `make test` at the
# repo root runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Racketeer/Pt2399Loop.hpp

all: test_loop

test: test_loop
	./test_loop

test_loop: test_loop.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_loop.cpp

clean:
	rm -rf test_loop *.dSYM

.PHONY: all test clean
