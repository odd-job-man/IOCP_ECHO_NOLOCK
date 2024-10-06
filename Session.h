#pragma once
#define GET_SESSION_INDEX(id) (id.ullId & 0xFFFF)
#define MAKE_SESSION_INDEX(Ret,ullID,index)\
do{\
Ret.ullId = ullID << 16;\
Ret.ullId ^= index;\
}while(0)\

union ID
{
	ULONGLONG ullId;
};

class Packet;

struct Session
{
	SOCKET sock;
	ID id;
	BOOL bSendingInProgress;
	BOOL bUsing;
	LONG lSendBufNum;
	LONG IoCnt;
	WSAOVERLAPPED recvOverlapped;
	WSAOVERLAPPED sendOverlapped;
	CLockFreeQueue<Packet*> sendPacketQ;
	Packet* pSendPacketArr[50];
	RingBuffer recvRB;
	BOOL Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx);
#ifdef IO_RET
	ULONGLONG ullSend;
	ULONGLONG ullRecv;
#endif
};
