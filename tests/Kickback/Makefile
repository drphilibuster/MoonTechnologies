# Kickback's nine percussion voices and its pattern engine, tested without Rack.
#
# Drum.hpp (shared, at src/), Voices.hpp and Payroll.hpp are pure DSP -- no Rack types, no
# allocation, no I/O -- so they need no <rack.hpp> shim at all; they compile
# against the host toolchain as they stand. `make test` at the repo root runs
# this. All three are listed below: a header left out of CORE is one whose
# edits do not rebuild the test, and a negative control against a stale binary
# passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Drum.hpp ../../src/Kickback/Voices.hpp ../../src/Kickback/Bridge808.hpp ../../src/Kickback/Smurf.hpp \
        ../../src/Kickback/Payroll.hpp ../../src/DspCache.hpp \
        ../../src/Kickback/AvalancheNoise.hpp ../../src/Microplasma.hpp ../../src/Mna.hpp ../../src/Cd40106.hpp ../../src/Vactrol.hpp

all: test_voices test_avalanche test_microplasma test_xor test_vactrol_snare

test: test_voices test_avalanche test_microplasma test_xor test_vactrol_snare
	./test_voices
	./test_avalanche
	./test_microplasma
	./test_xor
	./test_vactrol_snare

test_voices: test_voices.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_voices.cpp

test_xor: test_xor.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_xor.cpp

test_vactrol_snare: test_vactrol_snare.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_vactrol_snare.cpp

test_avalanche: test_avalanche.cpp ../../src/Kickback/AvalancheNoise.hpp ../../src/Microplasma.hpp ../../src/Mna.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_avalanche.cpp

test_microplasma: test_microplasma.cpp ../../src/Kickback/AvalancheNoise.hpp ../../src/Microplasma.hpp ../../src/Mna.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_microplasma.cpp

clean:
	rm -rf test_vactrol_snare test_microplasma test_voices test_avalanche test_xor render_kick *.dSYM

.PHONY: all test clean

# Audition/measurement tool (not a test): renders each kick model to WAV and prints its
# pitch track. No sanitizers, so it is quick. `make render_kick && ./render_kick out`.
render_kick: render_kick.cpp $(CORE) ../Retroactive/wav.hpp
	$(CXX) -std=c++11 -O2 -Wall -Wextra -o $@ render_kick.cpp
