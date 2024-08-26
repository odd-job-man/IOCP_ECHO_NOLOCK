#include "LanServer.h"
#include "Logger.h"

int main()
{
	LanServer ls;
	BOOL bShutDown = FALSE;
	ls.Start();
	//LOG_ASYNC_INIT();
	LOG(L"SYSTEM", SYSTEM, CONSOLE, L"Server StartUp()!");
	//INCREASE_LOG_LEVEL();
	while (!bShutDown)
	{
		if (GetAsyncKeyState(VK_SPACE & 0x01))
		{
			bShutDown = TRUE;
		}
	}
	//CLEAR_LOG_ASYNC();
	return 0;

}