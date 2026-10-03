# Obfuscation's allpass matrix, tested without Rack. Matrix.hpp is pure DSP.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Obfuscation/Matrix.hpp

all: test_matrix

test: test_matrix
	./test_matrix

test_matrix: test_matrix.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_matrix.cpp

clean:
	rm -rf test_matrix *.dSYM

.PHONY: all test clean
