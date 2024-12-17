#pragma once
#include "Packet.h"
#include "RingBuffer.h"
#include "MYOVERLAPPED.h"

class Packet;
struct LanSession
{
	static constexpr LONG RELEASE_FLAG = 0x80000000;
	SOCKET sock_;
	ULONGLONG id_;
	ULONGLONG lastRecvTime;
	LONG lSendBufNum_;
	BOOL bDisconnectCalled_;
	MYOVERLAPPED recvOverlapped;
	MYOVERLAPPED sendOverlapped;
	LONG IoCnt_;
	CLockFreeQueue<Packet*> sendPacketQ_;
	BOOL bSendingInProgress_;
	BOOL bSendingAtWorker_;
	Packet* pSendPacketArr_[50];
	RingBuffer recvRB_;
	BOOL Init(SOCKET clientSock, ULONGLONG ullClientId, SHORT shIdx);

	LanSession()
		:IoCnt_{ LanSession::RELEASE_FLAG | 0 }
	{}

	inline static short GET_SESSION_INDEX(ULONGLONG id)
	{
		return id & 0xFFFF;
	}
};
