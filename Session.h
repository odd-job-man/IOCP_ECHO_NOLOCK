#pragma once
#include <WinSock2.h>
#include <windows.h>
#include "RingBuffer.h"

#define IO_RET
struct Session
{
	SOCKET sock;
	ULONGLONG ullID;
	BOOL bSendingInProgress;
	int IoCnt;
	WSAOVERLAPPED recvOverlapped;
	WSAOVERLAPPED sendOverlapped;
	RingBuffer recvRB;
	RingBuffer sendRB;
	BOOL Init(SOCKET clientSock, ULONGLONG ullClientID);
#ifdef IO_RET
	ULONGLONG ullSend;
	ULONGLONG ullRecv;
#endif
};
