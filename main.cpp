#include <WinSock2.h>
#include <windows.h>
#include <stdio.h>
#include "IHandler.h"
#include "FreeList.h"
#include "LFStack.h"
#include "LanServer.h"
#include "Logger.h"
#include <timeapi.h>
#pragma comment(lib,"Winmm.lib")
#define MAX_SESSION 3000

int main()
{
	LanServer ls;
	BOOL bShutDown = FALSE;
	BOOL bStopMode = FALSE;
	LOG(L"SYSTEM", SYSTEM, CONSOLE, L"Server StartUp()!");
	ls.Start(MAX_SESSION);
	timeBeginPeriod(1);
	while (!bShutDown)
	{
		Sleep(1000);
		ls.Monitoring();
	}
	return 0;

}