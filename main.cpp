#include <WinSock2.h>
#include <windows.h>
#include <stdio.h>
#include "Stack.h"
#include "IHandler.h"
#include "LanServer.h"
#include "Logger.h"
#define MAX_SESSION 3000

int main()
{
	LanServer ls;
	BOOL bShutDown = FALSE;
	BOOL bStopMode = FALSE;
	LOG(L"SYSTEM", SYSTEM, CONSOLE, L"Server StartUp()!");
	ls.Start(MAX_SESSION);
	while (!bShutDown)
	{
		Sleep(1000);
		// Stop상태가 아니면서 ESC가 눌렷으면 STOP으로 바꾸고 서버를 멈춘다
		if (!bStopMode && GetAsyncKeyState(VK_SPACE) & 0x01)
		{
			bStopMode = TRUE;
			ls.Stop();
		}

		// stop 상태이면서 SPACE가 눌렷으면 START상태로 재시작한다.
		if (bStopMode && GetAsyncKeyState(VK_SPACE) & 0x01)
		{
			bStopMode = FALSE;
			LOG(L"SYSTEM", SYSTEM, CONSOLE, L"Server StartUp()!");
			ls.Start(MAX_SESSION);
		}

		if(bStopMode)
			printf("Is Stop Press SPACE To Restart!\n");
		else
			printf("Is Running Press SPACE To Stop!\n");


		ls.Monitoring();

		if (GetAsyncKeyState(VK_BACK) & 0x01)
		{
			if(!bStopMode)
				ls.Stop();
			bShutDown = TRUE;
		}
	}
	return 0;

}