# Diversified's 106 programs, swept for NaN and runaway feedback without Rack.
#
# rack.hpp here supplies the two things the DSP headers want from Rack -- clamp
# and the resampler pair -- from the real SDK headers, so the signal path under
# test is the shipping one. `make test` at the repo root runs this.
#
# RACK_DIR is only needed for those headers; nothing links against libRack.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

RACK_DIR ?= $(firstword \
	$(wildcard ../../../Rack-SDK ../../../../Rack-SDK $(HOME)/Rack-SDK) \
	../../../Rack-SDK)

CORE := ../../src/Diversified/Dsp99.hpp \
        ../../src/Diversified/MiawFx.hpp \
        ../../src/Diversified/Primitives.hpp \
        ../../src/Pt2399.hpp \
        ../../src/Adc0809.hpp \
        ../../src/TaxBracket/Ladder.hpp

all: test_programs test_pt2399_boards test_bitcrusher

test: test_programs test_pt2399_boards test_bitcrusher
	./test_programs
	./test_pt2399_boards
	./test_bitcrusher

# -I. first, so <rack.hpp> resolves to the shim beside this file rather than
# the SDK's, which needs nanovg and will not link.
test_programs: test_programs.cpp rack.hpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -I. -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-o $@ test_programs.cpp

test_pt2399_boards: test_pt2399_boards.cpp rack.hpp $(CORE)
	$(CXX) $(CXXFLAGS) -O2 -I. -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-o $@ test_pt2399_boards.cpp

test_bitcrusher: test_bitcrusher.cpp rack.hpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -I. -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-o $@ test_bitcrusher.cpp

clean:
	rm -rf test_programs test_pt2399_boards test_bitcrusher *.dSYM

.PHONY: all test clean
