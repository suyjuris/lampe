
# Generic C++ Makefile

TARGET = jup
LIBS = -lWs2_32 -static-libstdc++ -static-libgcc -static
CXX = g++
CXXFLAGS = -g -Wall -Werror -pedantic -fmax-errors=2
CPPFLAGS = -include global.hpp -std=c++1y
LDFLAGS  = -Wall
EXEEXT = .exe

.PHONY: default all clean test
.SUFFIXES:

all: default

SOURCES = $(wildcard *.c) $(wildcard *.cpp)
OBJECTS = $(SOURCES:.cpp=.o)
HEADERS = $(wildcard *.h) $(wildcard *.hpp)
DEPS    = $(SOURCES:.cpp=.d)

%.d: %.cpp $(HEADERS)
	@set -e; $(CXX) -MM $(CPPFLAGS) $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@;

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

default: $(TARGET)

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o *.d *~
	-rm -f $(TARGET)$(EXEEXT)
