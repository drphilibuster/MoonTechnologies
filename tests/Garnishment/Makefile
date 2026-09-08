# Garnishment's VCA circuits, tested without Rack.
#
# Vca.hpp is self-contained -- <algorithm>, <cmath> and nothing else. It was
# split out of the module for exactly this: `make test` is deliberately SDK-free
# so CI can run it on a machine with neither Rack nor the SDK.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Garnishment/Vca.hpp

all: test_vca

test: test_vca
	./test_vca

test_vca: test_vca.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_vca.cpp

clean:
	rm -rf test_vca *.dSYM

.PHONY: all test clean
