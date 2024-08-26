#pragma once
#include "Session.h"
#include "IHandler.h"
#include "Stack.h"
#include <unordered_map>

unsigned __stdcall AcceptThread(LPVOID arg);
unsigned __stdcall IOCPWorkerThread(LPVOID arg);


class LanServer : public IHandler
{
public:
	//virtual void Stop();
	//virtual int GetSessionCount();
	//virtual BOOL Disconnect(ULONGLONG ullID);
	//virtual void OnRelease(ULONGLONG ullID);
	virtual BOOL Start(DWORD dwMaxSession);
	virtual void SendPacket(ULONGLONG ullID, Packet* pPacket);
	virtual BOOL OnConnectionRequest();
	virtual void* OnAccept(ULONGLONG ullID);
	virtual void OnRecv(ULONGLONG ullID, Packet* pPacket);
private:
	friend unsigned __stdcall AcceptThread(LPVOID arg);
	friend unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	int SessionNum_ = 0;
	DWORD dwMaxSession_;
	DWORD dwAcceptTPS = 0;
	DWORD dwRecvTPS = 0;
	DWORD dwSendTPS = 0;
	Session* pSessionArr_;
	Stack DisconnectStack_;
	CRITICAL_SECTION stackLock_;
	HANDLE hcp_;
	virtual BOOL RecvPost(Session* pSession);
	virtual BOOL SendPost(Session* pSession);
	virtual void ReleaseSession(Session* pSession);
};
