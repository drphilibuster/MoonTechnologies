# Rebate's MIDIverb machine, run without Rack against the real firmware.
#
# The ROMs are Alesis's and are never in this repository: point MIDIVERB_ROMS at a
# directory holding the CPU image (MVOP) and a DSP image (MVOBJ and/or MIDIFEX);
# they are recognised by CRC, so the names do not matter. Without it every test
# prints SKIP and passes, so `make test` stays green on a machine that has no
# ROMs -- CI included.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g

CORE := ../../src/Rebate/Mcs51.hpp ../../src/Rebate/Machine.hpp ../../src/Rebate/Analog.hpp

all: test_machine

test: test_machine
	./test_machine

test_machine: test_machine.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ test_machine.cpp

clean:
	rm -rf test_machine test_machine.dSYM
