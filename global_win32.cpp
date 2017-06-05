
#include "server.hpp"

namespace jup {

std::ostream& jout = std::cout;
std::ostream& jerr = std::cerr;

bool program_closing = false;

void debug_break() {}

class MyStackWalker : public StackWalker {
    using StackWalker::StackWalker;
protected:
    void OnSymInit(LPCSTR szSearchPath, DWORD symOptions, LPCSTR szUserName) override {}
    void OnLoadModule(LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size, DWORD result,
        LPCSTR symType, LPCSTR pdbName, ULONGLONG fileVersion) override {}
    
    void OnCallstackEntry(CallstackEntryType eType, CallstackEntry& entry) override {
        if (eType == lastEntry || entry.offset == 0) return;
        CHAR buffer[STACKWALK_MAX_NAMELEN];
        
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
    }
    
    void OnOutput(LPCSTR szText) override {
        jerr << "  " << szText;
    }
    void OnDbgHelpErr(LPCSTR szFuncName, DWORD gle, DWORD64 addr) override {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%p", (void const*)addr);
        jerr << "Error: " << szFuncName << " at " << buf << '\n';
        write_last_errmsg();
    }
};

void _assert(c_str expr_str, c_str file, int line) {
    jerr << "\nError: Assertion failed. File: " << file << ", Line " << line
         << "\n\nExpression: " << expr_str << "\n\nStack trace:\n";
    
    MyStackWalker sw;
    sw.ShowCallstack();
    die();
}

void die() {
    std::raise(SIGABRT);
}

} /* end of namespace jup */

