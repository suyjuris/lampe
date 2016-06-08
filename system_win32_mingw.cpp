#include "global.hpp"
#include "system.hpp"

#include <stdlib.h>
#include <windows.h>
#include <fstream>

namespace jup {

// see header
void sleep(int milliseconds) {
    Sleep(milliseconds);
}

// see header
bool is_debugged() {
    return IsDebuggerPresent();
}

// see header
void stop_abort_from_printing() {
    // This would be the more proper way, but I can't get mingw to link a recent
    // version of msvcr without making pthreads segfault.
    //_set_abort_behavior(0, _WRITE_ABORT_MSG);
    
    CloseHandle(GetStdHandle(STD_ERROR_HANDLE));
}

// see header
bool file_exists(Buffer_view path) {
    // Copied from stackoverflow.com/questions/3828835
    DWORD attrib = GetFileAttributes(path.c_str());
    
    return attrib != INVALID_FILE_ATTRIBUTES
        and not (attrib & FILE_ATTRIBUTE_DIRECTORY);
}

// see header
void cancel_blocking_io(std::thread& thread) {
    // Very dirty. Assume that threads are implemented using some kind of
    // pthread variant (which is true under mingw) and call an internal function
    // to get the Windows handle.
    assert( CancelSynchronousIo(pthread_gethandle(thread.native_handle()))
            or GetLastError() == ERROR_NOT_FOUND );
}

void Process::init(const char* cmdline, const char* dir) {
    assert(cmdline);
    assert(!*this);

    HANDLE stdin_read = 0,  stdin_write = 0;
    HANDLE stdout_read = 0, stdout_write = 0;
    
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;

    assert(CreatePipe(&stdin_read,  &stdin_write,  &sa, 0));
    assert(CreatePipe(&stdout_read, &stdout_write, &sa, 0));
    
    assert(SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0));
    assert(SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0));

    STARTUPINFO startup_info;
    memset(&proc_info,    0, sizeof(proc_info   ));
    memset(&startup_info, 0, sizeof(startup_info));

    startup_info.cb = sizeof(STARTUPINFO);
    //si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup_info.hStdError = stdout_write;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdInput = stdin_read;
    startup_info.dwFlags |= STARTF_USESTDHANDLES;

    assert(CreateProcess(0, const_cast<LPSTR>(cmdline), nullptr, nullptr, true,
                         0, nullptr, dir, &startup_info, &proc_info));

    write = stdin_write;
    read  = stdout_read;
    
    worker = std::thread(&fillBuffer, this);

    valid = true;
}

void Process::close() {
    if (!*this) return;

    valid = false;
    TerminateProcess(proc_info.hProcess, 0);
    assert( CloseHandle(proc_info.hProcess) );
    assert( CloseHandle(proc_info.hThread) );

    cancel_blocking_io(worker);
    
    worker.join();
}

void Process::send(Buffer_view buf) {
    assert(*this);
    
    int done = 0;
    while (done < buf.size()) {
        DWORD len;
        if (WriteFile(write, buf.data() + done, buf.size() - done, &len, 0)) {
            done += len;
        } else  {
            auto code = GetLastError();
            if (code == ERROR_BROKEN_PIPE) {
                // The handle has been closed
                close();
                return;
            } else {
                assert(false);
            }
        }
    }
}

void Process::fillBuffer() {
    static char buf[1024];
    
    while (true) {
        DWORD len;
        if (!ReadFile(read, &buf, sizeof(buf) - 1, &len, 0)) {
            auto code = GetLastError();
            if (code == ERROR_BROKEN_PIPE) {
                // The handle has been closed
                close();
                return;
            } else if (code == ERROR_OPERATION_ABORTED) {
                // We are closing down
                return;
            } else {
                assert(false);
            }
        }
        if (write_to_stdout) {
            jout.write(buf, len);
            jout.flush();
        }
        if (write_to_buffer) {
            std::lock_guard<std::mutex> {mutex};
            buffer.append(buf, len);
        }
    }
}

int Process::getMsg(Buffer* into) {
    assert(*this);
    assert(into);

    std::lock_guard<std::mutex> {mutex};

    // Find the end of the first line
    int i = 0;
    while (i < buffer.size() and buffer[i] != '\n') ++i;
    if (i == buffer.size()) return 0;
    ++i;
    
    into->append(buffer.data(), i);
    into->append("", 1);
    buffer.pop_front(i);
    
    return i;
}

void Process::waitFor(const char* pattern) {
    assert(*this);
    
    Buffer buf;
    while (true) {
        buf.reset();
        while (getMsg(&buf) == 0) sleep(50);
        if (std::strstr(buf.data(), pattern)) return;
    }
}

} /* end of namespace jup */
