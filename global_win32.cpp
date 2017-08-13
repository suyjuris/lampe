
#include "server.hpp"
#include "utilities.hpp"

#include "stack_walker_win32.hpp"

namespace jup {

std::ostream& jout = std::cout;
std::ostream& jerr = std::cerr;

bool program_closing = false;

void debug_break() {}

Buffer_view jup_exec(jup_str cmd) {
    FILE* pipe = _popen(cmd.c_str(), "r");
    assert_errno(pipe);

    tmp_alloc_buffer().reset();
    tmp_alloc_buffer().reserve_space(256);
    while (not std::feof(pipe)) {
        if (std::fgets(tmp_alloc_buffer().end(), tmp_alloc_buffer().capacity(), pipe)) {
            tmp_alloc_buffer().addsize(std::strlen(tmp_alloc_buffer().end()));
        }
    }
    _pclose(pipe);
    return tmp_alloc_buffer();
}

void err_msg(c_str msg, int err) {
    int l = std::strlen(msg);
    while (l and (msg[l-1] == '\n' or msg[l-1] == '\x0d')) --l;
    jerr << "Error: ";
    jerr.write(msg, l);
    jerr << " (" << err << ")\n";
}

void win_last_errmsg() {
    auto err = GetLastError();
    char* msg = nullptr;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&msg,
        0,
        nullptr
    );
    err_msg(msg, err);
}

void print_symbol(char const* exe, u64 offset) {
    // replace jup.exe with build_files/jup.exe
    int index = -1;
    for (int i = 0; i+7 <= (int)std::strlen(exe); ++i) {
        if (i+11<= (int)std::strlen(exe) and std::strncmp(exe + i, "build_files", 11) == 0) {
            break;
        }
        if (std::strncmp(exe + i, "jup.exe", 7) == 0) {
            index = i;
        }
    }
    jup_str cmdline;
    if (index == -1) {
        cmdline = jup_printf("addr2line -C -f -e %s 0x%I64x", exe, offset);
    } else {
        char* exe_ = (char*)alloca(index);
        std::memcpy(exe_, exe, index);
        exe_[index] = 0;
        cmdline = jup_printf("addr2line -C -f -e %sbuild_files/jup.exe 0x%I64x", exe_, offset);
    }
    
    auto str = jup_exec(cmdline);
    if (not str.size()) return;
    if (str[0] == '?') return;

    char* dem_name = (char*)alloca(str.size());
    char* fil_name = (char*)alloca(str.size());
    char* fii_name = (char*)alloca(str.size());
    int line;
    std::sscanf(str.c_str(), "%[^\n]\n%[^\\/]%[^:]:%d\n", dem_name, fil_name, fii_name, &line);

    std::memcpy(fil_name + std::strlen(fil_name), fii_name, std::strlen(fii_name) + 1);

    for (int j = std::strlen(dem_name) - 1; j >= 0; --j) {
        if (dem_name[j] == '(') dem_name[j] = 0;
    }

    if (std::strcmp(dem_name, "jup::print_stacktrace") == 0) return;
    if (std::strcmp(dem_name, "jup::die") == 0) return;
    if (std::strcmp(dem_name, "jup::_assert_fail") == 0) return;
    if (std::strcmp(dem_name, "StackWalker::ShowCallstack") == 0) return;
        
    if (line == -1) {
        jerr << "  (filename not available): " << dem_name << '\n';
    } else {
        char pwd [MAX_PATH];
        _getcwd(pwd, sizeof(pwd)); // Ignore error, since we are crashing anyways
        if (std::strncmp(pwd, fil_name, std::strlen(pwd)) == 0 and std::strlen(pwd) > 1) {
            fil_name += std::strlen(pwd) - 1;
            fil_name[0] = '.';
        }
        jerr << "  " << fil_name << ":" << line << ": " << dem_name << '\n';
    }
}

class MyStackWalker : public StackWalker {
    using StackWalker::StackWalker;
protected:
    void OnSymInit(LPCSTR szSearchPath, DWORD symOptions, LPCSTR szUserName) override {}
    void OnLoadModule(LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size, DWORD result,
        LPCSTR symType, LPCSTR pdbName, ULONGLONG fileVersion) override {}
    
    void OnCallstackEntry(CallstackEntryType eType, CallstackEntry& entry) override {
        if (eType == lastEntry || entry.offset == 0) return;
#if 1
        print_symbol(entry.loadedImageName, entry.offset);
#else   
        CHAR buffer[STACKWALK_MAX_NAMELEN];

        if (std::strcmp(entry.name, "ShowCallstack") == 0) return;
        if (std::strcmp(entry.name, "_assert_fail")  == 0) return;
        if (std::strcmp(entry.name, "die")  == 0) return;
        
        if (entry.name[0] == 0)
            strcpy_s(entry.name, "(function-name not available)");
        if (entry.undName[0] != 0)
            strcpy_s(entry.name, entry.undName);
        if (entry.undFullName[0] != 0)
            strcpy_s(entry.name, entry.undFullName);
        if (entry.lineFileName[0] == 0) {
            strcpy_s(entry.lineFileName, "(filename not available)");
            if (entry.moduleName[0] == 0)
                strcpy_s(entry.moduleName, "(module-name not available)");
            _snprintf_s(buffer, STACKWALK_MAX_NAMELEN, "%p (%s): %s: %s\n", (LPVOID)entry.offset,
                entry.moduleName, entry.lineFileName, entry.name);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%s:%d: %s\n",
                entry.lineFileName, (int)entry.lineNumber, entry.name);
        }
        OnOutput(buffer);
#endif
    }
    
    void OnOutput(LPCSTR szText) override {
        jerr << "  " << szText;
    }
    void OnDbgHelpErr(LPCSTR szFuncName, DWORD gle, DWORD64 addr) override {
        //char buf[256];
        //std::snprintf(buf, sizeof(buf), "%p", (void const*)addr);
        //jerr << "Error: " << szFuncName << " at " << buf << '\n';
        //win_last_errmsg();
    }
};

void _assert_fail(c_str expr_str, c_str file, int line) {
    jerr << "\nError: Assertion failed. File: " << file << ", Line " << line
         << "\n\nExpression: " << expr_str << "\n";
    
    die();
}

void _assert_errno_fail(c_str expr_str, c_str file, int line) {
    auto err = errno;
    char const* msg = std::strerror(err);
    err_msg(msg, err);
    _assert_fail(expr_str, file, line);
}

void _assert_win_fail(c_str expr_str, c_str file, int line) {
    win_last_errmsg();
    _assert_fail(expr_str, file, line);
}

void die(c_str msg, int err) {
    err_msg(msg, err);
    die();
}

void die(bool show_stacktrace) {
    if (show_stacktrace) {
        jerr << "\nStack trace:\n";
        MyStackWalker sw;
        sw.ShowCallstack();
    }
    
    // This would be the more proper way, but I can't get mingw to link a recent
    // version of msvcr without recompiling a lot of stuff.
    //_set_abort_behavior(0, _WRITE_ABORT_MSG);
    CloseHandle(GetStdHandle(STD_ERROR_HANDLE));
    
    std::abort();
}

} /* end of namespace jup */

