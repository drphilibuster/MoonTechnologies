# SixFigures' reverse-avalanche relaxation oscillator, tested without Rack.
#
# Avalanche.hpp includes only <cmath>. It is listed in CORE: a header left out is one whose
# edits do not rebuild the test, and a suite that does not rebuild passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/SixFigures/Avalanche.hpp

all: test_avalanche

test: test_avalanche
	./test_avalanche

test_avalanche: test_avalanche.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_avalanche.cpp

clean:
	rm -rf test_avalanche *.dSYM

.PHONY: all test clean
