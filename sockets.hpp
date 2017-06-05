
#pragma once


#include "buffer.hpp"

namespace jup {

/**
 * Manages any initialization and uninitialization necessary to create
 * Sockets. Exactly one of these must exist in an enclosing scope of all
 * Sockets.
 */
struct Socket_context {
	Socket_context();
	~Socket_context();
};

/**
 * Abstract a single Socket. This is owning, meaning that the Socket is created
 * on construction and released on destruction. Also, copy semantics are not
 * supported. Move semantics could be supported (and currently are not).
 *
 * Sockets may be invalid! When any operation fails, a valid socket becomes
 * invalid. Either check that and recover gracefully, or risk as assertion the
 * next time you try to do something with the socket.
 */
struct Socket {
    Socket() {}
    
	/**
	 * Constructs the socket. Any errors are printed as warnings to the
	 * console. If that happens, the resulting socket is not valid. Check that.
	 *
	 * address and port MUST BE zero-terminated strings.
	 */
	Socket(Buffer_view /* c_str */ address, Buffer_view /* c_str */ port) {
        init(address, port);
    }
    void init(Buffer_view /* c_str */ address, Buffer_view /* c_str */ port);

	
	/**
	 * Make the socket invalid, release any resources. If the socket already was
	 * invalid, do nothing.
	 */
	~Socket() { close(); }
	void close();

	// operators and move semantics belong here if needed
	
	/**
	 * Whether the socket is valid
	 */
	bool is_valid() const { return initialized; }
	operator bool() const { return initialized and not err; }

	/**
	 * Send data over the socket.
	 */
	void send(Buffer_view data);

	/**
	 * Recieve data currently from the socket into the buffer. Blocks. Returns
	 * the total amount of data read. May resize the buffer.
	 */
	int recv(Buffer* into);

    /**
     * Returns an integer uniquely identifying the socket. No other guarantees are made.
     */
    int get_id() const;

    
	bool initialized = false;
    bool err = false;
	
	// Only guaranteed to contain valid data when the socket is valid is set,
	// else UNDEFINED. The contents are implementation defined.
	char data[16] = {0};
};
	
} /* end of namespace jup */
