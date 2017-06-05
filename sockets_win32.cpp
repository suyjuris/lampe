
#include "sockets.hpp"
#include "system.hpp"

// This is the Windows Implementation of the sockets.hpp sockets.

namespace jup {

Socket_context::Socket_context() {
	WSADATA wsaData;
	assert(WSAStartup(0x0202, &wsaData) == 0);
}
Socket_context::~Socket_context() {
	WSACleanup();
}

struct Socket_win32_data {
    SOCKET sock;
    int id;
};

static int socket_id_counter = 0;

/**
 * Helper, does some casting and asserting
 */
Socket_win32_data& get_sock(Socket const& sock) {
	static_assert(sizeof(sock.data) >= sizeof(Socket_win32_data),
				  "sock.data is not big enough");
	return *((Socket_win32_data*)sock.data);
}

// see header
void Socket::init(Buffer_view address, Buffer_view port) {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    auto code = getaddrinfo(address.c_str(), port.c_str(), &hints, &result);
    if (code) return;

	addrinfo* ptr = result;
	while (ptr) {
		auto sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (sock == INVALID_SOCKET) {
			jerr << "Warning: Error at socket(): " << WSAGetLastError() << '\n';
		} else {
			auto code = connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen);
			if (code == SOCKET_ERROR) {
				closesocket(sock);
				jerr << "Warning: Unable to connect to server!\n";
			} else {
				get_sock(*this) = {sock, ++socket_id_counter};
				break;
			}
		}
		
		ptr = ptr->ai_next;
	}
	
	freeaddrinfo(result);
	if (not ptr) return;

	initialized = true;
    err = false;
}

// see header
void Socket::close() {
	if (!initialized) return;
	closesocket(get_sock(*this).sock);
	initialized = false;
}

// see header
void Socket::send(Buffer_view buf) {
	assert(initialized);
	
	auto code = ::send(get_sock(*this).sock, buf.data(), buf.size(), 0);
	if (code == SOCKET_ERROR) {
		jerr << "Warning: send failed: " << WSAGetLastError() << '\n';
        err = true;
		close();
	}
}

// see header
int Socket::recv(Buffer* into) {
	assert(into);
	assert(initialized);

	int total_count = 0;
	while(true) {
		into->reserve_space(256);
		auto result = ::recv(get_sock(*this).sock, into->end(), into->space(), 0);
		
		if (result < 0) {
            // This is dirty. When the program is closing down, there could be
            // an error here due to the server being killed. This is a sleep we
            // should not wake up from.
            if (program_closing) {
                sleep(1000); assert(false);
            }
            
			jerr << "Warning: recv failed: " << WSAGetLastError() << '\n';
			close();
            err = true;
			return total_count;
		} else if (result == 0) {
            jerr << "Warning: recv returned 0\n";
			close();
			return total_count;
        } else if (result < into->space()) {
			into->addsize(result);
			total_count += result;
			return total_count;
		} else if (result == into->space()) {
			into->addsize(result);
			total_count += result;
		} else {
			assert(false);
		}
	}
}

int Socket::get_id() const {
    return get_sock(*const_cast<Socket*>(this)).id;
}

} /* end of namespace jup */
    
int socketsMain() {
	jup::Socket_context context;

	jup::Socket sock {"localhost", "12300"};

	std::string sendmsg {"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?><message type=\"auth-request\"><authentication password=\"1\" username=\"a1\"/></message>"};
	sock.send(sendmsg);
	sock.send({"", 1});
	
	jup::Buffer buffer;
	sock.recv(&buffer);
	jup::jout << buffer.data() << '\n';
	
    return 0;
}
