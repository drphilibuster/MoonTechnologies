# Contagion's Virus C, run without Rack against the real firmware.
#
# The OS image is Access's and is never in this repository: point VIRUS_ROMS at a
# directory holding a Virus A/B/C 512 KB flash dump (any name ending .bin, any case).
# Without it every test prints SKIP and passes, so `make test` stays green on a machine
# that has no ROMs -- CI included. The DSP emulator (vendor/dsp56300) is compiled once
# into objects here; the first build takes a few minutes.
CXX ?= c++
V := ../../vendor
FLAGS := -std=gnu++17 -O2 -g -DDSP56300_DEBUGGER=0 -DASMJIT_STATIC -I$(V)/dsp56300
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
FLAGS += -mmacosx-version-min=10.12
else
FLAGS += -pthread -fconstexpr-loop-limit=10000000
endif

DSP_SRC := $(filter-out %test.cpp %tests.cpp,$(wildcard $(V)/dsp56300/dsp56kEmu/*.cpp $(V)/dsp56300/dsp56kBase/*.cpp)) \
           $(wildcard $(V)/dsp56300/asmjit/core/*.cpp $(V)/dsp56300/asmjit/x86/*.cpp $(V)/dsp56300/asmjit/arm/*.cpp) \
           $(V)/gearmulator/hardwareLib/hd44780.cpp $(V)/gearmulator/hardwareLib/lcdfonts.cpp
DSP_OBJ := $(patsubst $(V)/%.cpp,obj/%.o,$(DSP_SRC))

CORE := ../../src/Contagion/Mcs515.hpp ../../src/Contagion/VirusC.hpp ../../src/Contagion/VirusC.cpp

all: test_machine

test: test_machine
	./test_machine

obj/%.o: $(V)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -w -c -o $@ $<

obj/VirusC.o: $(CORE)
	@mkdir -p obj
	$(CXX) $(FLAGS) -Wall -Wextra -Wno-unused-parameter -c -o $@ ../../src/Contagion/VirusC.cpp

test_machine: test_machine.cpp obj/VirusC.o $(DSP_OBJ)
	$(CXX) $(FLAGS) -Wall -o $@ test_machine.cpp obj/VirusC.o $(DSP_OBJ)

clean:
	rm -rf test_machine test_machine.dSYM obj
