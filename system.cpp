#include "global.hpp"
#include "system.hpp"
#include <windows.h>
#include <fstream>

namespace jup {

void sleep(int milliseconds) {
    Sleep(milliseconds);
}

bool is_debugged() {
    return IsDebuggerPresent();
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

    PROCESS_INFORMATION pi;
    STARTUPINFO si;

    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));

    si.cb = sizeof(STARTUPINFO);
    //si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdError = stdout_write;
    si.hStdOutput = stdout_write;
    si.hStdInput = stdin_read;
    si.dwFlags |= STARTF_USESTDHANDLES;

    assert(CreateProcess(0, const_cast<LPSTR>(cmdline), nullptr, nullptr, true,
                         0, nullptr, dir, &si, &pi));

    write = stdin_write;
    read  = stdout_read;
    proc_info = pi;

    worker = std::thread(&fillBuffer, this);

    valid = true;
}

void Process::close() {
    if (!*this) return;

    valid = false;
    TerminateProcess(proc_info.hProcess, 0);
    CloseHandle(proc_info.hProcess);
    CloseHandle(proc_info.hThread);
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
