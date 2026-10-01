# AuditLogic's INSTALLMENTS divider chip (CD4024B), tested without Rack.
#
# Cd4024.hpp includes nothing but <cstdint>. It is listed in CORE: a header left out
# is one whose edits do not rebuild the test, and a suite that does not rebuild
# passes for the wrong reason.
CXX ?= c++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -g
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE := ../../src/Cd4024.hpp

all: test_cd4024

test: test_cd4024
	./test_cd4024

test_cd4024: test_cd4024.cpp $(CORE)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ test_cd4024.cpp

clean:
	rm -rf test_cd4024 *.dSYM

.PHONY: all test clean
