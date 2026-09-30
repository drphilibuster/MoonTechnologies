# Apportionment's DP/4 machine, run without Rack against the real firmware.
#
# The ROMs are Ensoniq's and are never in this repository: point DP4_ROMS at a
# directory holding the OS and UCODE EPROM images (any names containing "os"
# and "ucode"). Without it every test prints SKIP and passes, so `make test`
# stays green on a machine that has no ROMs -- CI included.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g

CORE := ../../src/Apportionment/Machine.hpp \
        ../../src/Apportionment/Router.hpp \
        ../../src/Apportionment/M6803.hpp \
        ../../src/Apportionment/Esp.hpp

all: test_machine

test: test_machine
	./test_machine

M6803.o: ../../src/Apportionment/M6803.cpp ../../src/Apportionment/M6803.hpp $(wildcard ../../vendor/mame/m6800/*)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

Esp.o: ../../src/Apportionment/Esp.cpp ../../src/Apportionment/Esp.hpp $(wildcard ../../vendor/mame/es5510/*)
	$(CXX) $(CXXFLAGS) -std=c++14 -Wno-deprecated-declarations -Wno-unused-parameter -Wno-sign-compare -c -o $@ $<

test_machine: test_machine.cpp M6803.o Esp.o $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ test_machine.cpp M6803.o Esp.o

clean:
	rm -f test_machine *.o
