#pragma once
#include <WinSock2.h>
#include <windows.h>
#include "RingBuffer.h"

//#define IO_RET

union ID
{
	ULONGLONG ullId;
	struct
	{
		short sh[4];
	};
};

struct Session
{
	SOCKET sock;
	ID id;
	BOOL bSendingInProgress;
	int IoCnt;
	WSAOVERLAPPED recvOverlapped;
	WSAOVERLAPPED sendOverlapped;
	RingBuffer recvRB;
	RingBuffer sendRB;
	BOOL Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx);
#ifdef IO_RET
	ULONGLONG ullSend;
	ULONGLONG ullRecv;
#endif
};
