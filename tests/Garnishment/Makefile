# Garnishment's VCA circuits, and the I AM O JFET stage's circuit solver, tested without Rack.
#
# Vca.hpp is self-contained -- <algorithm>, <cmath> and nothing else. It was
# split out of the module for exactly this: `make test` is deliberately SDK-free
# so CI can run it on a machine with neither Rack nor the SDK.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

IAMO := ../../src/Garnishment/IAmO.hpp ../../src/Mna.hpp
CORE := ../../src/Garnishment/Vca.hpp $(IAMO)

all: test_vca test_iamo

test: test_vca test_iamo
	./test_vca
	./test_iamo

test_vca: test_vca.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_vca.cpp

test_iamo: test_iamo.cpp $(IAMO)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_iamo.cpp

clean:
	rm -rf test_vca test_iamo *.dSYM

.PHONY: all test clean
