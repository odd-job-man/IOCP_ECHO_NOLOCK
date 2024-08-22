#include "LanServer.h"
#include "Logger.h"

int main()
{
	LanServer ls;
	BOOL bShutDown = FALSE;
	ls.Start();
	LOG(L"SYSTEM", SYSTEM, CONSOLE, L"Server StartUp()!");
	//INCREASE_LOG_LEVEL();
	while (!bShutDown)
	{
		//Sleep(20);
		if (GetAsyncKeyState(VK_SPACE & 0x01))
		{
			bShutDown = TRUE;
		}
	}
	return 0;

}