#pragma once
#include <WinSock2.h>
#include <windows.h>
#include "Packet.h"

#include "Session.h"
class IHandler
{
public:
	virtual BOOL Start() = 0;
	//virtual void Stop() = 0;
	//virtual int GetSessionCount() = 0;
	//virtual BOOL Disconnect(ULONGLONG ullID) = 0;
	virtual BOOL SendPacket(ULONGLONG ullID, Packet* pPacket) = 0;
	virtual BOOL OnConnectionRequest() = 0;
	virtual void* OnAccept(ULONGLONG ullID) = 0;
	virtual BOOL OnRecv(ULONGLONG ullID, Packet* pPacket) = 0;
	//virtual void OnRelease(ULONGLONG ullID) = 0;
private:
	virtual BOOL SendPost(Session* pSession) = 0;
	virtual BOOL RecvPost(Session* pSession) = 0;
	virtual void ReleaseSession(Session* pSession) = 0;
};
