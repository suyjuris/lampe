#include "global.hpp"
#include "system.hpp"
#include <windows.h>
#include <fstream>

namespace jup {

	Process::Process(const char* cmdline) {
		HANDLE ird = 0, iwr = 0, ord = 0, owr = 0;
		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(SECURITY_ATTRIBUTES);
		sa.bInheritHandle = true;
		sa.lpSecurityDescriptor = NULL;
		assert(CreatePipe(&ord, &owr, &sa, FILE_FLAG_OVERLAPPED));
		assert(SetHandleInformation(ord, HANDLE_FLAG_INHERIT, 0));
		assert(CreatePipe(&ird, &iwr, &sa, 0));
		assert(SetHandleInformation(iwr, HANDLE_FLAG_INHERIT, 0));

		PROCESS_INFORMATION pi;
		STARTUPINFO si;

		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));

		si.cb = sizeof(STARTUPINFO);
		si.hStdError = owr;
		si.hStdOutput = owr;
		si.hStdInput = ird;
		si.dwFlags |= STARTF_USESTDHANDLES;

		assert(CreateProcess(0, const_cast<LPSTR>(cmdline), 0, 0, true, 0, 0, 0, &si, &pi));
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		w = iwr;
		r = ord;

		t = std::thread(&fillBuffer, this);
	}

	void Process::send(Buffer_view buf) {
		DWORD n;
		while(!WriteFile(w, buf.data(), buf.size(), &n, 0))
			;
	}

	void Process::fillBuffer() {
		DWORD len;
		char c[1024];
		while (true) {
			ReadFile(r, &c, 1023, &len, 0);
			m.lock();
			b.append(c, len);
			m.unlock();
		}
	}

	int Process::getMsg(Buffer* into) {
		u16 copied = 0;
		m.lock();
		for (u16 i = 0; i < b.size(); i++) {
			if (b.data()[i] == '\n') {
				into->append(b.data(), ++i);
				b.pop_front(i);
				copied = i;
				break;
			}
		}
		m.unlock();
		return copied;
	}

}
