#include "LanServer.h"
#include "Logger.h"
#define MAX_SESSION 3000

int main()
{
	LanServer ls;
	BOOL bShutDown = FALSE;
	ls.Start(MAX_SESSION);
	LOG(L"SYSTEM", SYSTEM, CONSOLE, L"Server StartUp()!");
	while (!bShutDown)
	{
		Sleep(1000);
		if (GetAsyncKeyState(VK_SPACE & 0x01))
			bShutDown = TRUE;

		ls.Monitoring();
	}
	return 0;

}