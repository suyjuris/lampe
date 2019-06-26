#pragma once

#include "buffer.hpp"

namespace jup {

/**
 * Sleep for approximately this amount of time.
 */
void sleep(int milliseconds);

/**
 * Returns whether there currently is a debugger attached to the program.
 */
bool is_debugged();

/**
 * On Windows, abort prints the annoying "The application has...". This causes
 * it to shut up.
 */
void stop_abort_from_printing();

/**
 * Returns whether a file exists.
 */
bool file_exists(Buffer_view path);

/**
 * This cancels any pending IO operations of the thread.
 */
void cancel_blocking_io(std::thread& thread);

/**
 * Write the last error to stderr
 */
void write_last_errmsg();

/**
 * Return the width of the terminal currently used. This may not always work correctly.
 */
int get_terminal_width();

/**
 * Return the time that has passed since some point (which can be set with init_elapsed_time)
 */
double elapsed_time();

/**
 * Set the time point elapsed_time is relative to, such that calling elapsed_time immediately
 * afterwards would yield val.
 */
void init_elapsed_time(double val = 0);

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
    /**
     * read is the pipe we are reading from
     */
    HANDLE read, write;
    PROCESS_INFORMATION proc_info;
    Buffer buffer;         
    std::thread worker;    
    std::mutex mutex;      
    bool valid;            
};

} /* end of namespace jup */


