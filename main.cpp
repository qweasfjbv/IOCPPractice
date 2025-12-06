#include "EchoServer.h"

const UINT16 SERVER_PORT = 61394;
const UINT16 MAX_CLIENT = 100;

int main() {

	EchoServer server;

	server.InitSocket();

	server.BindandListen(SERVER_PORT);

	server.Run(MAX_CLIENT);

	LOG_INFO("WAIT FOR ANY KEY....");
	getchar();

	server.End();

	return 0;
}