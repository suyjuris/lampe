#pragma once

#include <cstdlib>
#include <windows.h>
#include <thread>
#include <sstream>
#include "buffer.hpp"
#include <mutex>


namespace jup {

/**
 * Sleep for approximately this amount of time.
 */
void sleep(int milliseconds);

/**
 * Returns whether there currently is a debugger attached to the program.
 */
bool is_debugged();

class Process {
public:
    bool write_to_buffer = true;
    bool write_to_stdout = false;

    Process(): valid{false} {}
    Process(char const* cmdline, char const* dir = nullptr) { init(cmdline); }
    ~Process() { close(); }
    
    void init(char const* cmdline, char const* dir = nullptr);
    void close();

    // send data to process
    void send(Buffer_view data);

    void fillBuffer();

    int getMsg(Buffer* into);

    void waitFor(char const* pattern);

    operator bool() const { return valid; }
    
private:
    HANDLE read, write;
    PROCESS_INFORMATION proc_info;
    Buffer buffer;         
    std::thread worker;    
    std::mutex mutex;      
    bool valid;            
};

} /* end of namespace jup */


