# Retroactive's permutation core, tested without Rack.
#
# WindowPermuter.hpp is Rack-free C++11 that allocates nothing on the audio
# thread, so it compiles straight against a host compiler. `make test` is what
# CI runs.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer
# The ring is sentinel-filled under test: ASan catches out-of-bounds, but not
# uninitialised reads, and MSan is unavailable on macOS.
DEFS := -DRETROACTIVE_RING_FILL=-12345.f

CORE := ../../src/Retroactive/dsp/WindowPermuter.hpp

all: test_permuter

test: test_permuter
	./test_permuter

test_permuter: test_permuter.cpp wav.hpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) $(DEFS) -o $@ test_permuter.cpp

# No sanitizers, release fill -- used for rendering long WAVs quickly.
render_permuter: test_permuter.cpp wav.hpp $(CORE)
	$(CXX) -std=c++11 -O2 -Wall -Wextra -o $@ test_permuter.cpp

clean:
	rm -rf test_permuter render_permuter *.dSYM out

.PHONY: all test clean
