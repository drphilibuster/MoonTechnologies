# Projection's band analysis and its three renderers, tested without Rack.
#
# Neither header reaches the SDK: the FFT that feeds Bands.hpp is Rack's and
# lives in the .cpp, precisely so the part with opinions in it can be tested
# against the host toolchain. `make test` at the repo root runs this. Both are
# listed below: one left out of CORE is one whose edits do not rebuild the test,
# and a suite that does not rebuild passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Projection/Bands.hpp ../../src/Projection/Render.hpp

all: test_projection

test: test_projection
	./test_projection

test_projection: test_projection.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_projection.cpp

clean:
	rm -rf test_projection *.dSYM

.PHONY: all test clean
