# Nordic Banking's Nord Lead 2X, run without Rack against the real firmware.
#
# The OS image is Clavia's and is never in this repository: point NL2X_ROMS at a directory
# holding a Nord Lead 2X 512 KB OS image (any name). Without it every test prints SKIP and
# passes, so `make test` stays green on a machine that has no ROMs -- CI included. The DSP and
# 68k emulators (vendor/) are compiled once into objects here; the first build takes minutes.
CXX ?= c++
CC ?= cc
V := ../../vendor
INC := -I$(V)/dsp56300 -I$(V)/gearmulator
FLAGS := -std=gnu++17 -O2 -g -DDSP56300_DEBUGGER=0 -DASMJIT_STATIC $(INC)
CFLAGS := -O2 -g $(INC)
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
FLAGS += -mmacosx-version-min=10.13
CFLAGS += -mmacosx-version-min=10.13
else
FLAGS += -pthread -fconstexpr-loop-limit=10000000
endif

DSP_SRC := $(filter-out %test.cpp %tests.cpp,$(wildcard $(V)/dsp56300/dsp56kEmu/*.cpp $(V)/dsp56300/dsp56kBase/*.cpp)) \
           $(wildcard $(V)/dsp56300/asmjit/core/*.cpp $(V)/dsp56300/asmjit/x86/*.cpp $(V)/dsp56300/asmjit/arm/*.cpp)
N2X_SRC := $(wildcard $(V)/gearmulator/mc68k/*.cpp $(V)/gearmulator/n2xLib/*.cpp) \
           $(V)/gearmulator/hardwareLib/i2c.cpp $(V)/gearmulator/hardwareLib/i2cFlash.cpp $(V)/gearmulator/hardwareLib/sciMidi.cpp \
           $(V)/gearmulator/baseLib/filesystem.cpp \
           $(V)/gearmulator/synthLib/midiBufferParser.cpp $(V)/gearmulator/synthLib/deviceException.cpp $(V)/gearmulator/synthLib/os.cpp
M68K_SRC := $(V)/gearmulator/mc68k/Musashi/m68kcpu.c $(V)/gearmulator/mc68k/Musashi/m68kops.c $(V)/gearmulator/mc68k/Musashi/m68kdasm.c
OBJ := $(patsubst $(V)/%.cpp,obj/%.o,$(DSP_SRC) $(N2X_SRC)) $(patsubst $(V)/%.c,obj/%.o,$(M68K_SRC))

CORE := ../../src/NordicBanking/Nord2x.hpp ../../src/NordicBanking/Nord2x.cpp ../../src/NordicBanking/PanelMap.hpp

all: test_machine

test: test_machine
	./test_machine

obj/%.o: $(V)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -w -c -o $@ $<

obj/%.o: $(V)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c -o $@ $<

obj/Nord2x.o: $(CORE)
	@mkdir -p obj
	$(CXX) $(FLAGS) -Wall -Wextra -Wno-unused-parameter -c -o $@ ../../src/NordicBanking/Nord2x.cpp

test_machine: test_machine.cpp obj/Nord2x.o $(OBJ) $(CORE)
	$(CXX) $(FLAGS) -Wall -o $@ test_machine.cpp obj/Nord2x.o $(OBJ)

clean:
	rm -rf test_machine test_machine.dSYM obj
