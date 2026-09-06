# Kickback's eight percussion voices, tested without Rack.
#
# Voices.hpp is pure DSP -- no Rack types, no allocation, no I/O -- so it needs
# no <rack.hpp> shim at all; it compiles against the host toolchain as it
# stands. `make test` at the repo root runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Kickback/Voices.hpp ../../src/DspCache.hpp

all: test_voices

test: test_voices
	./test_voices

test_voices: test_voices.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_voices.cpp

clean:
	rm -rf test_voices *.dSYM

.PHONY: all test clean
