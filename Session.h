#pragma once
#include <WinSock2.h>
#include <windows.h>
#include "RingBuffer.h"

struct Session
{
	SOCKET sock;
	ULONGLONG ullID;
	BOOL bSendingInProgress;
	void* pClient;
	DWORD dwIoCount;
	WSAOVERLAPPED recvOverlapped;
	WSAOVERLAPPED sendOverlapped;
	RingBuffer recvRB;
	RingBuffer sendRB;
};
