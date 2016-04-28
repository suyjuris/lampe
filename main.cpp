#include "global.hpp"
#include "sockets.hpp"
#include "messages.hpp"
#include "system.hpp"
#include <conio.h>

bool findBeginning(const char* c, const char* p) {
	int i = 0;
	while (p[i] != 0) {
		if (p[i] != c[i])
			return false;
		i++;
	}
	return true;
}

int main() {

	jup::Process server("bash --login");
	
	server.send(jup::Buffer_view{ "cd /d/lampe/sim/massim/scripts/\n./startServer.sh\n0\n" });

	jup::Buffer read;
	do {
		read.reset();
		while (server.getMsg(&read) == 0) {
		}
		read.append("\0", 1);
		jup::jout << read.data();
	} while (!findBeginning(read.data(), "[ NORMAL ]  ##   InetSocketListener created"));


	jup::init_messages();
	jup::Socket_context scon;
	jup::Socket sock{ "localhost", "12300" };
	assert(sock);
	jup::send_message(sock, jup::Message_Auth_Request{ "a1", "1" });


	do {
		read.reset();
		while (server.getMsg(&read) == 0)
			;
		read.append("\0", 1);
		jup::jout << read.data();
	} while (!findBeginning(read.data(), "[ NORMAL ]  ##   Please press ENTER to start the tournament"));

	getch();

	server.send("\n");

	while (true) {
		read.reset();
		while (server.getMsg(&read) == 0)
			;
		read.append("\0", 1);
		jup::jout << read.data();
	}

	return 0;
}

