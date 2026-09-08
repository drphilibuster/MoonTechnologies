# Transmittal's encoder command line, pacing and scaler, plus the video bus,
# tested without Rack.
#
# Neither header reaches the SDK -- Encoder.hpp is strings and arithmetic and
# VideoBus.hpp is a mutex and a map -- so both compile against the host
# toolchain as they stand. `make test` at the repo root runs this. Both are
# listed below: one left out of CORE is one whose edits do not rebuild the
# test, and a suite that does not rebuild passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Transmittal/Encoder.hpp ../../src/VideoBus.hpp

all: test_encoder

test: test_encoder
	./test_encoder

test_encoder: test_encoder.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_encoder.cpp

clean:
	rm -rf test_encoder *.dSYM

.PHONY: all test clean
