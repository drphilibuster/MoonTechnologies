# Repossession's child-process launcher, tested without Rack.
#
# Process.hpp needs five rack::system calls and a handful of jansson handles;
# rack.hpp here supplies exactly those and nothing else, so what is under test
# is the real launcher. `make test` at the repo root runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer
# Rack defines this in the real build; without it defaultToolDirs() compiles its
# no-platform branch, which is not the code that ships.
DEFS := -DARCH_MAC

CORE := ../../src/Repossession/Process.hpp

all: test_process test_windows

test: test_process test_windows
	./test_process
	./test_windows

# -I. first, so <rack.hpp> resolves to the stub beside this file.
test_process: test_process.cpp rack.hpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) $(DEFS) -I. -o $@ test_process.cpp

test_windows: test_windows.cpp rack.hpp ../../src/Repossession/Windows.hpp
	$(CXX) $(CXXFLAGS) $(SAN) $(DEFS) -I. -o $@ test_windows.cpp

clean:
	rm -rf test_process test_windows *.dSYM

.PHONY: all test clean
