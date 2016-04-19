# lampe
Library for Agent Manipulation and Planning Efficiently

## Description
Allows interfacing with the (MASSim Server)[https://multiagentcontest.org/].

## System requirements
This project currently targets Microsoft Windows 64-bit. The sockets are the only non-portable part.
The library used for parsing xml is [pugixml](http://pugixml.org/), it is included in the sourcecode.

## Compile (using MinGW)
Build this project by a simple
  
    make

This requires only the default windows headers and libraries to be installed and produces an `jup.exe` as output.

