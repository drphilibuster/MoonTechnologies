# Depreciation's host-side tests: the ROM identification (SHA-256, known-dump table,
# set assembly), run without Rack.
#
# The ROMs are Lexicon's and are never in this repository: point PCM70_ROMS at a
# directory holding the dumps and the identification is also run on the real
# files. Without it those checks print SKIP and pass, so `make test` stays green
# on a machine that has no ROMs -- CI included.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g

CORE := ../../src/Depreciation/Roms.hpp
MACHINE := ../../src/Pcm70Machine.hpp ../../src/Pcm70Locate.hpp ../../src/Pcm70Drive.hpp ../../src/Pcm70BatRam.hpp ../../src/Pcm70Hsp.hpp fixtures.hpp hsp_ref.hpp ../../vendor/z80/z80.c ../../vendor/z80/z80.h
CC ?= cc

all: test_roms test_machine test_hsp test_analog test_voice test_control test_panel

test: test_roms test_machine test_hsp test_analog test_voice test_control test_panel
	./test_roms
	./test_machine
	./test_hsp
	./test_analog
	./test_voice
	./test_control
	./test_panel

test_roms: test_roms.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ test_roms.cpp

z80.o: ../../vendor/z80/z80.c ../../vendor/z80/z80.h
	$(CC) -O2 -Wall -c -o $@ ../../vendor/z80/z80.c

test_machine: test_machine.cpp $(CORE) $(MACHINE) z80.o
	$(CXX) $(CXXFLAGS) -o $@ test_machine.cpp z80.o

test_hsp: test_hsp.cpp $(CORE) $(MACHINE) z80.o
	$(CXX) $(CXXFLAGS) -o $@ test_hsp.cpp z80.o

test_voice: test_voice.cpp fixtures.hpp $(MACHINE) ../../src/Pcm70Analog.hpp ../../src/Pcm70Voice.hpp z80.o
	$(CXX) $(CXXFLAGS) -o $@ test_voice.cpp z80.o

voice_ir: voice_ir.cpp fixtures.hpp $(MACHINE) ../../src/Pcm70Analog.hpp ../../src/Pcm70Voice.hpp z80.o
	$(CXX) $(CXXFLAGS) -o $@ voice_ir.cpp z80.o

test_control: test_control.cpp fixtures.hpp $(MACHINE) ../../src/Pcm70Control.hpp z80.o
	$(CXX) $(CXXFLAGS) -o $@ test_control.cpp z80.o

test_panel: test_panel.cpp fixtures.hpp $(MACHINE) ../../src/Pcm70Analog.hpp ../../src/Pcm70Voice.hpp ../../src/Pcm70Panel.hpp z80.o
	$(CXX) $(CXXFLAGS) -o $@ test_panel.cpp z80.o

test_analog: test_analog.cpp ../../src/Pcm70Analog.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_analog.cpp

clean:
	rm -rf test_roms test_roms.dSYM test_machine test_machine.dSYM test_hsp test_hsp.dSYM test_analog test_analog.dSYM test_voice test_voice.dSYM test_control test_control.dSYM test_panel test_panel.dSYM voice_ir voice_ir.dSYM z80.o
