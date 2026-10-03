# The vactrol model (src/Vactrol.hpp), tested without Rack.
# The header is listed in CORE so an edit to it rebuilds the test.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Vactrol.hpp

all: test_vactrol test_parts

test: test_vactrol test_parts
	./test_vactrol
	./test_parts

test_vactrol: test_vactrol.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_vactrol.cpp

test_parts: test_parts.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_parts.cpp

clean:
	rm -rf test_vactrol test_parts *.dSYM

.PHONY: all test clean
