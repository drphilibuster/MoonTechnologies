# Ledger, tested without Rack.
#
# test_mod covers Modulation.hpp: CV spans, the mod matrix, ROOT, pitch standards
# and the poly hubs. test_fx covers the effects, the event voices and sources,
# and fuzzes the chains for hanging notes. test_midi covers Midi.hpp -- the output
# ports, taps, live input, clock out, recording's arithmetic -- and the two effects that
# send MIDI, and fuzzes those for MIDI notes left sounding. test_song covers Song.hpp:
# loops in master ticks, the row syncs, the song's order. test_pattern covers Pattern.hpp and Player.hpp, and the hook
# that lets Shoal's walker drive a pattern track.
#
# test_module runs the Module itself (LedgerModule.hpp) sample by sample. It links
# libRack, so it needs a Rack checkout whose library loads -- ../Rack beside this repo
# (the SDK's copy is link-only) -- and is skipped, saying so, without one.
#
# test_golden builds the original Shoal (vendor/shoal/shoal.cpp, against the
# vendored distingNT API header) next to Ledger's port and compares them frame by
# frame. `make test` at the repo root runs this. A header left out of CORE is one
# whose edits do not rebuild the tests, and a negative control against a stale
# binary passes for the wrong reason: list them all.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
UPSTREAM_FLAGS := -std=gnu++11 -O2 -g -w -I../../vendor/distingNT_API/include

CORE := ../../src/Ledger/Shoal.hpp \
        ../../src/Ledger/Modulation.hpp \
        ../../src/Ledger/Pattern.hpp \
        ../../src/Ledger/Player.hpp \
        ../../src/Ledger/Events.hpp \
        ../../src/Ledger/Voices.hpp \
        ../../src/Ledger/Effects.hpp \
        ../../src/Ledger/Midi.hpp \
        ../../src/Ledger/Song.hpp

UPSTREAM := upstream.cpp upstream.hpp ../../vendor/shoal/shoal.cpp \
            ../../vendor/distingNT_API/include/distingnt/api.h

TESTS := test_golden test_mod test_pattern test_fx test_midi test_song

RACK_DIR ?= $(firstword $(wildcard ../../../Rack-SDK ../../../../Rack-SDK $(HOME)/Rack-SDK) ../../../Rack-SDK)
# a libRack that loads, for test_module to run against
RACK_RUN ?= $(firstword $(wildcard ../../../Rack/libRack.dylib ../../../Rack/libRack.so))
MODULE_CORE := $(CORE) ../../src/Ledger/LedgerModule.hpp ../../src/Ledger/LedgerUndo.hpp ../../src/plugin.hpp

all: $(TESTS)

test: $(TESTS)
	./test_golden
	./test_mod
	./test_pattern
	./test_fx
	./test_midi
	./test_song
ifneq ($(RACK_RUN),)
	$(MAKE) test_module
	DYLD_LIBRARY_PATH="$(dir $(RACK_RUN))" LD_LIBRARY_PATH="$(dir $(RACK_RUN))" ./test_module
else
	@echo "SKIP test_module: no loadable libRack (a Rack checkout at ../Rack)"
endif

test_fx: test_fx.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_fx.cpp

test_midi: test_midi.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_midi.cpp

test_song: test_song.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_song.cpp

test_module: test_module.cpp $(MODULE_CORE)
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -Wno-deprecated-declarations -I$(RACK_DIR)/include \
		-I$(RACK_DIR)/dep/include -o $@ test_module.cpp -L$(RACK_DIR) -lRack

upstream.o: $(UPSTREAM)
	$(CXX) $(UPSTREAM_FLAGS) $(SAN) -c -o $@ upstream.cpp

test_golden: test_golden.cpp upstream.o upstream.hpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_golden.cpp upstream.o

test_mod: test_mod.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_mod.cpp

test_pattern: test_pattern.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_pattern.cpp

clean:
	rm -rf $(TESTS) test_module *.o *.dSYM

.PHONY: all test clean
