CXXFLAGS = -std=c++17 -Wall -Wextra

.PHONY: all
all:

# TODO temporary
ropts.o: ropts.cpp ropts.h Makefile
	$(CXX) -c $(CXXFLAGS) -O2 -g -o $@ $<
TO_CLEAN += ropts.o

### Tests ###
tests.bin: tests.cpp ropts.o ropts.h external/doctest.h Makefile
	$(CXX) $(CXXFLAGS) -O2 -g -o $@ tests.cpp ropts.o
TO_CLEAN += tests.bin

.PHONY: test
test: tests.bin
	./$<

### Clean ###
.PHONY: clean
clean:
	$(RM) $(TO_CLEAN)
