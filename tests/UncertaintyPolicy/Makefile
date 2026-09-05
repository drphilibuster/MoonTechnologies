# Uncertainty Policy's host-testable parts.
#
#   make test   the movement statistic -- Rack-free, runs anywhere, and is what
#               CI runs
#   make all    that plus the label parser, which needs a real Rack (below)
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

# The label parser speaks jansson, which lives inside libRack. Linking the SDK's
# own copy builds but will not run -- macOS refuses to load it into an unsigned
# binary -- so this uses the installed Rack, which is signed. Override RACK_APP
# if yours is elsewhere. This is why test_labels is not part of `make test`:
# it needs Rack installed, and CI has no Rack.
RACK_APP ?= /Applications/VCV Rack 2 Pro.app
RACK_LIB := $(RACK_APP)/Contents/Resources
JANSSON := -I../../../Rack-SDK/dep/include -L"$(RACK_LIB)" -lRack
RACKLIB := DYLD_LIBRARY_PATH="$(RACK_LIB)"

all: test
	$(MAKE) test_labels
	$(RACKLIB) ./test_labels

test: test_motion
	./test_motion

test_motion: test_motion.cpp ../../src/UncertaintyPolicy/Motion.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_motion.cpp

test_labels: test_labels.cpp ../../src/UncertaintyPolicy/LabelParse.hpp
	$(CXX) $(CXXFLAGS) $(SAN) $(JANSSON) -o $@ test_labels.cpp

clean:
	rm -rf test_motion test_labels *.dSYM

.PHONY: all test clean
