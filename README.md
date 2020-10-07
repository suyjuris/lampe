# lampe
Library for Agent Manipulation and Planning Efficiently

## Description
Allows interfacing with the [MASSim Server](https://multiagentcontest.org/) natively from C++ and implements the agents used by us in the 2016 and 2017 MAPC.

## System requirements
This project currently targets Windows 64-bit, using a recent version of g++ (we use 5.3.0) via the mingw64 project. The non-portable parts are marked with `_win32` for the use of the Windows API and `_mingw` for the dependence on the internals of the MinGW `std::thread` implementation.

The library used for parsing xml is [pugixml](http://pugixml.org/), it is included in the sourcecode.

## Compile (using MinGW)
Build this project by a simple
  
    make

This requires only the default windows headers and libraries to be installed and produces an `jup.exe` as output.
