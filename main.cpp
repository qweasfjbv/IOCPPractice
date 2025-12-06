#include "IOCompletionPort.h"

const UINT16 SERVER_PORT = 61394;
const UINT16 MAX_CLIENT = 100;

int main() {

	IOCompletionPort ioCompletionPort;

	ioCompletionPort.InitSocket();

	ioCompletionPort.BindandListen(SERVER_PORT);

	ioCompletionPort.StartServer(MAX_CLIENT);

	LOG_INFO("WAIT FOR ANY KEY....");
	getchar();

	ioCompletionPort.DestroyThread();

	return 0;
}