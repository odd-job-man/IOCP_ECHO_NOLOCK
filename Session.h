#pragma once
union ID
{
	ULONGLONG ullId;
};

class Packet;

struct Session
{
	static constexpr LONG RELEASE_FLAG = 0x80000000;
	SOCKET sock_;
	ID id_;
	LONG lSendBufNum_;
	WSAOVERLAPPED recvOverlapped;
	WSAOVERLAPPED sendOverlapped;
	LONG IoCnt_;
	CLockFreeQueue<Packet*> sendPacketQ_;
	BOOL bSendingInProgress_;
	Packet* pSendPacketArr_[50];
	RingBuffer recvRB_;
	BOOL Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx);
	inline static size_t GET_SESSION_INDEX(ID id)
	{
		return id.ullId & 0xFFFF;
	}

#ifdef IO_RET
	ULONGLONG ullSend;
	ULONGLONG ullRecv;
#endif
};
