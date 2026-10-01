# Payment Schedule's counter and tap loop, tested without Rack.
#
# Sequencer.hpp and the two chip headers under it are pure C++ -- no Rack types --
# so there is no <rack.hpp> shim. `make test` at the repo root runs this. A header
# left out of CORE is one whose edits do not rebuild the test, and a negative
# control against a stale binary passes for the wrong reason: list them all.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/PaymentSchedule/Sequencer.hpp \
        ../../src/Cd4017.hpp \
        ../../src/Cd4031.hpp

all: test_sequencer

test: test_sequencer
	./test_sequencer

test_sequencer: test_sequencer.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_sequencer.cpp

clean:
	rm -rf test_sequencer *.dSYM

.PHONY: all test clean
