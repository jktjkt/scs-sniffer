CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Werror

# The tests are static_asserts: compiling the file is running them.
test:
	$(CXX) $(CXXFLAGS) -fsyntax-only scs_decoder_test.cpp
	$(CXX) $(CXXFLAGS) -fsyntax-only scs_protocol_test.cpp
	@echo "all SCS tests passed"

.PHONY: test
