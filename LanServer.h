#pragma once
#include "Session.h"
#include "IHandler.h"
#include <unordered_map>

unsigned __stdcall AcceptThread(LPVOID arg);
unsigned __stdcall IOCPWorkerThread(LPVOID arg);


class LanServer : public IHandler
{
public:
	virtual BOOL Start();
	//virtual void Stop();
	//virtual int GetSessionCount();
	//virtual BOOL Disconnect(ULONGLONG ullID);
	virtual BOOL SendPacket(ULONGLONG ullID, Packet* pPacket);
	virtual BOOL OnConnectionRequest();
	virtual void* OnAccept(ULONGLONG ullID);
	virtual BOOL OnRecv(ULONGLONG ullID, Packet* pPacket);
	//virtual void OnRelease(ULONGLONG ullID);
private:
	friend unsigned __stdcall AcceptThread(LPVOID arg);
	friend unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	int SessionNum_ = 0;
	DWORD dwAcceptTPS = 0;
	DWORD dwRecvTPS = 0;
	DWORD dwSendTPS = 0;
	HANDLE hcp_;
	std::unordered_map<ULONGLONG, Session*> sessionUMap_;
	SRWLOCK SessionUMapLock_;
	virtual BOOL RecvPost(Session* pSession);
	virtual BOOL SendPost(Session* pSession);
	virtual void ReleaseSession(Session* pSession);
};
