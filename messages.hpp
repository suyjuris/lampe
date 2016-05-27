#pragma once

#include "buffer.hpp"
#include "objects.hpp"
#include "sockets.hpp"

namespace jup {

struct Message {
	enum Type: char {
		AUTH_REQUEST,
		AUTH_RESPONSE,
		SIM_START,
		SIM_END,
		BYE,
		REQUEST_ACTION,
		ACTION,
		TYPE_MESSAGE_COUNT
	};

	char type = -1;
};

struct Message_Client2Server: Message {
	
};

struct Message_Server2Client: Message {
	u64 timestamp;
};

struct Message_Auth_Request: Message_Client2Server {
	/**
	 * This uses strings for now, as this message is only sent a handfull of
	 * times and not performance critical.
	 */
	Message_Auth_Request(std::string user, std::string pass):
		username{user}, password{pass} { type = AUTH_REQUEST; }
	
	std::string username, password;				 
};

struct Message_Action: Message_Client2Server {
    static constexpr int Type_value = ACTION;
    
	/**
	 * action is an arbitrary subclass of Action, it is copied to the end of the
	 *  Buffer.
	 */
	template <typename T>
	Message_Action(u16 id, T const& action, Buffer* containing):
		id{id}, action{action, containing} { type = Type_value; }

	u16 id;
	Flat_ref<Action> action;
};

struct Message_Auth_Response: Message_Server2Client {
    static constexpr int Type_value = AUTH_RESPONSE;
	Message_Auth_Response() { type = Type_value; }
	
	bool succeeded;
};

struct Message_Sim_Start: Message_Server2Client {
    static constexpr int Type_value = SIM_START;
	Message_Sim_Start() { type = Type_value; }
	
	Simulation simulation;
};

struct Message_Sim_End: Message_Server2Client {
    static constexpr int Type_value = SIM_END;
	Message_Sim_End() { type = Type_value; }

	u8 ranking;
	u32 score;
};

struct Message_Bye: Message_Server2Client {
    static constexpr int Type_value = BYE;
	Message_Bye() { type = Type_value; }
};

struct Message_Request_Action: Message_Server2Client {
    static constexpr int Type_value = REQUEST_ACTION;
	Message_Request_Action() { type = Type_value; }

	Perception perception;
};

/**
 * Performs various initlization functions. Call before get_next_message or send_message.
 */
void init_messages();

u8 register_id(Buffer_view str);

/**
 * Return the id of the string. The empty string is guaranteed to have the id
 * 0. Different strings of different domains may have the same id (currently
 * they don't). If the str is not mapped, the behaviour is undefined.
 */
u8 get_id_from_string(Buffer_view str);


/**
 * Writes the next message in the Socket into the end of the Buffer. Returns the
 * type of the Message read. Blocks.
 */
u8 get_next_message(Socket& sock, Buffer* into);

/**
 * Writes the next message in the Socket into the end of the Buffer. Returns the
 * Message if it is of the specified type, else the behaviour is undefined.
 */
template <typename T>
T& get_next_message_ref(Socket& sock, Buffer* into) {
    assert(into);

    int pos = into->size();
    auto type = get_next_message(sock, into);
    assert(type == T::Type_value);
    return into->get<T>(pos);
}

/**
 * Send a message into the socket.
 */
void send_message(Socket& sock, Message_Auth_Request const& mess);
void send_message(Socket& sock, Message_Action const& mess);

} /* end of namespace jup */
