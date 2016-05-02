#include "global.hpp"
#include "sockets.hpp"
#include "messages.hpp"
#include "system.hpp"
#include <conio.h>

jup::Process *server;



int main() {

	server = new jup::Process("bash --login");
	
	server->send(jup::Buffer_view{ "cd /d/lampe/sim/massim/scripts/\n./startServer.sh\n0\n" });

	server->waitFor("[ NORMAL ]  ##   InetSocketListener created");


	jup::init_messages();
	jup::Socket_context scon;
	jup::Socket sock{ "localhost", "12300" };
	assert(sock);
	jup::send_message(sock, jup::Message_Auth_Request{ "a1", "1" });

	server->waitFor("[ NORMAL ]  ##   Please press ENTER to start the tournament");

	getch();

	server->send("\n");
	return 0;
}

