# SixFigures' DSP, tested without Rack: the reverse-avalanche relaxation oscillator and the
# CD40106B Schmitt inverter / astable behind the 40106 core (and Kickback's XOR snare).
#
# Avalanche.hpp and Cd40106.hpp include only <cmath>. Both are listed in CORE: a header left out is
# one whose edits do not rebuild the test, and a suite that does not rebuild passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/SixFigures/Avalanche.hpp ../../src/SixFigures/AvalancheBoard.hpp ../../src/Vactrol.hpp ../../src/Microplasma.hpp ../../src/Cd40106.hpp ../../src/SixFigures/Schmitt.hpp ../../src/SixFigures/Cd4046.hpp

all: test_avalanche test_strike test_board test_cd40106 test_cd4046

test: test_avalanche test_strike test_board test_cd40106 test_cd4046
	./test_avalanche
	./test_strike
	./test_board
	./test_cd40106
	./test_cd4046

test_avalanche: test_avalanche.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_avalanche.cpp

test_strike: test_strike.cpp $(CORE) ../../src/Microplasma.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_strike.cpp

test_board: test_board.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_board.cpp

test_cd40106: test_cd40106.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_cd40106.cpp

test_cd4046: test_cd4046.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_cd4046.cpp

clean:
	rm -rf test_strike test_board test_avalanche test_cd40106 test_cd4046 *.dSYM

.PHONY: all test clean
