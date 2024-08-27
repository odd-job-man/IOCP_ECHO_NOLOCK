#pragma once
#include <WinSock2.h>
#include <windows.h>
#include "RingBuffer.h"

//#define IO_RET

#define GET_SESSION_INDEX(id) (id.sh[3])
#define MAKE_SESSION_INDEX(Ret,ullID,index)\
do{\
	Ret.ullId = ullID;\
	Ret.sh[3] = index;\
}while(0)\

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
	BOOL bUsing;
	DWORD dwSendBufNum;
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
