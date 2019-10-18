CXXFLAGS = -std=c++17 -Wall -Wextra -O2

.PHONY: all
all:

### Tests ###
tests.bin: tests.cpp ropts.h external/doctest.h Makefile
	$(CXX) $(CXXFLAGS) -o $@ $<
TO_CLEAN += tests.bin

.PHONY: test
test: tests.bin
	./$<

### Clean ###
.PHONY: clean
clean:
	$(RM) $(TO_CLEAN)