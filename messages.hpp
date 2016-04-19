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
	Message_Auth_Request(std::string user, std::string pass):
		username{user}, password{pass} { type = AUTH_REQUEST; }
	
	std::string username, password;				 
};

struct Message_Auth_Response: Message_Server2Client {
	Message_Auth_Response() { type = AUTH_RESPONSE; }
	
	bool succeeded = false;
};

struct Message_Sim_Start: Message_Server2Client {
	Message_Sim_Start() { type = SIM_START; }
	
	Simulation simulation;
};

void init_messages();
void get_next_message(Socket& sock, Buffer* into);
void send_message(Socket& sock, Message_Auth_Request const& mess);

} /* end of namespace jup */
