#pragma once

#include <cstdlib>
#include <windows.h>
#include <thread>
#include <sstream>
#include "buffer.hpp"
#include <mutex>


namespace jup {

	class Process {
	private:
		HANDLE r, w;
		std::thread t;
		Buffer b;
		std::mutex m;
	protected:

		void fillBuffer();

	public:
		// start new process and initialize pipelines
		Process(const char* cmdline);

		// send data to process
		void send(Buffer_view data);

		int getMsg(Buffer* into);

		void waitFor(const char* pattern);

	};

}


