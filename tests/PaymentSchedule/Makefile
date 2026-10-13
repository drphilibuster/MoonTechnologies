# Payment Schedule's drum sequencer, tested without Rack -- and the Baby8 / 4031
# circuit it replaced (Sequencer.hpp and the two chip headers), which the module no
# longer uses but which stay in the tree, and stay tested, until they are removed.
#
# Both are pure C++ -- no Rack types -- so there is no <rack.hpp> shim. `make
# test` at the repo root runs this. A header left out of CORE is one whose edits do not
# rebuild the test, and a negative control against a stale binary passes for the wrong
# reason: list them all.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/PaymentSchedule/DrumSeq.hpp
RETIRED := ../../src/PaymentSchedule/Sequencer.hpp \
           ../../src/Cd4017.hpp \
           ../../src/Cd4031.hpp

all: test_drumseq test_sequencer

test: test_drumseq test_sequencer
	./test_drumseq
	./test_sequencer

test_drumseq: test_drumseq.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_drumseq.cpp

test_sequencer: test_sequencer.cpp $(RETIRED)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_sequencer.cpp

clean:
	rm -rf test_drumseq test_sequencer *.dSYM

.PHONY: all test clean
