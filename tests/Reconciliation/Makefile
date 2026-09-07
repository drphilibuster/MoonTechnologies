# Reconciliation's just-intonation tables and search, tested without Rack.
#
# Tuning.hpp includes nothing but <cmath> and <cstdint>, so the tables and the
# search under test are the shipping ones and no <rack.hpp> shim is needed.
# `make test` at the repo root runs this.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Reconciliation/Tuning.hpp

all: test_tuning

test: test_tuning
	./test_tuning

test_tuning: test_tuning.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_tuning.cpp

clean:
	rm -rf test_tuning *.dSYM

.PHONY: all test clean
