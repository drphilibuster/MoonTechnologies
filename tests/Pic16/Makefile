# The PIC16F684 emulator, tested without Rack.
#
# Pic16f684.hpp is pure C++, so there is no <rack.hpp> shim. `make test` at the repo
# root runs this. test_core needs nothing but the header. test_varimode drives the
# Day 10 quantizer firmware and so needs it: set VARIMODE_HEX to the .HEX (and,
# optionally, VARIMODE_ASM to its source, which adds an independent oracle); without
# it that test prints SKIP. The firmware is never in this repository.
#
# A header left out of CORE is one whose edits do not rebuild the test, and a negative
# control against a stale binary passes for the wrong reason: list them all.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Pic16f684.hpp \
        ../../src/PaymentSchedule/Varimode.hpp \
        ../../src/PaymentSchedule/VarimodeFirmware.hpp

FW := ../../firmware/varimode/varimode_fixed

all: test_core test_varimode

test: test_core test_varimode asm-test firmware-check
	./test_core
	./test_varimode

# The assembler's own tests.
asm-test:
	python3 test_asm.py

# The committed .hex and generated header are what the assembly assembles to. If this fails, run
# `make firmware` at the repo root and commit the result.
firmware-check:
	python3 ../../tools/pic16asm.py $(FW).asm -q -o fw_check.hex --cpp fw_check.hpp VarimodeFixed
	diff -q fw_check.hex $(FW).hex
	diff -q fw_check.hpp ../../src/PaymentSchedule/VarimodeFirmware.hpp
	rm -f fw_check.hex fw_check.hpp

test_core: test_core.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_core.cpp

test_varimode: test_varimode.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_varimode.cpp

clean:
	rm -rf test_core test_varimode fw_check.hex fw_check.hpp *.dSYM

.PHONY: all test clean asm-test firmware-check
