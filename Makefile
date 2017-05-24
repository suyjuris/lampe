
# Generic C++ Makefile

TARGET = jup
LIBS = -lWs2_32 -static-libstdc++ -static-libgcc -static
CXX = g++
CXXFLAGS = -g -Wall -Werror -pedantic -fmax-errors=2
CPPFLAGS = -std=c++1y
LDFLAGS  = -Wall
EXEEXT = .exe
TMPDIR = build_files
PRE_HEADER = $(TMPDIR)/global.hpp.gch

.PHONY: default all clean test
.SUFFIXES:

all: default

SOURCES = $(wildcard *.c) $(wildcard *.cpp)
OBJECTS = $(SOURCES:%.cpp=$(TMPDIR)/%.o)
HEADERS = $(wildcard *.h) $(wildcard *.hpp)
DEPS    = $(SOURCES:%.cpp=$(TMPDIR)/%.d)

$(PRE_HEADER): global.hpp
	@mkdir -p $(TMPDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(TMPDIR)/%.d: %.cpp $(HEADERS)
	@mkdir -p $(TMPDIR)
	@set -e; $(CXX) -MM $(CPPFLAGS) $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@;

$(TMPDIR)/%.o: %.cpp $(PRE_HEADER)
	@mkdir -p $(TMPDIR)
	$(CXX) $(CPPFLAGS) -I $(TMPDIR) -include global.hpp $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

default: $(TARGET)

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o *.d *~
	-rm -f $(TARGET)$(EXEEXT)
	-rm -f $(TMPDIR)/*
	-rmdir $(TMPDIR)
