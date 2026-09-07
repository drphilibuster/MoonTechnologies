# Toll's modal voice, tested without Rack.
#
# Voice.hpp and the shared src/Drum.hpp are pure DSP -- no Rack types, no
# allocation, no I/O -- so they need no <rack.hpp> shim and compile against the
# host toolchain as they stand. `make test` at the repo root runs this. Both
# headers are listed below: one left out of CORE is one whose edits do not
# rebuild the test, and a suite that does not rebuild passes for the wrong
# reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Toll/Voice.hpp ../../src/Drum.hpp ../../src/DspCache.hpp

all: test_voice

test: test_voice
	./test_voice

test_voice: test_voice.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_voice.cpp

clean:
	rm -rf test_voice *.dSYM

.PHONY: all test clean
