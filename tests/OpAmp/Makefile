# The behavioural op-amps (TL07x, LM741, LM358), tested without Rack.
#
# OpAmp.hpp includes nothing but <cmath>. It is listed in CORE: a header left out is one
# whose edits do not rebuild the test, and a suite that does not rebuild passes for the
# wrong reason. (Negative controls: rm -f test_opamp before each rebuild, see CLAUDE.md.)
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/OpAmp.hpp
MIX := ../../src/OpAmp.hpp ../../src/MixerStages.hpp

all: test_opamp test_mixstage

test: test_opamp test_mixstage
	./test_opamp
	./test_mixstage

test_opamp: test_opamp.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_opamp.cpp

test_mixstage: test_mixstage.cpp $(MIX)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_mixstage.cpp

clean:
	rm -rf test_opamp test_mixstage *.dSYM

.PHONY: all test clean
