# Calculation's phrase counter, tested without Rack.
#
# Core.hpp is pure logic -- no Rack types -- so it compiles against the host
# toolchain as it stands. `make test` at the repo root runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Calculation/Core.hpp

all: test_core

test: test_core
	./test_core

test_core: test_core.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_core.cpp

clean:
	rm -rf test_core *.dSYM

.PHONY: all test clean
