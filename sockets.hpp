
#pragma once

#include <vector>

#include "buffer.hpp"

namespace jup {

struct Socket_context {
	Socket_context();
	~Socket_context();
};

struct Socket {
	Socket(std::string address, std::string port);
	~Socket();

	// operators and move semantics belong here if needed
	
	// Whether the socket is valid
	operator bool() const {return initialized;}
	// Make the socket invalid
	void close();

	void send(Buffer_view data);
	int recv(Buffer* into);

	bool initialized = false;
	// Only guaranteed to contain valid data when initialized is set, else UNDEFINED
	char data[8] = {0};
};
	
} /* end of namespace jup */
