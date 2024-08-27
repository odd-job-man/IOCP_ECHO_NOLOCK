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
	virtual void SendPacket(ID id, Packet* pPacket);
	virtual BOOL OnConnectionRequest();
	virtual void* OnAccept(ID id);
	virtual void OnRecv(ID id, Packet* pPacket);
	void Monitoring();
private:
	friend unsigned __stdcall AcceptThread(LPVOID arg);
	friend unsigned __stdcall IOCPWorkerThread(LPVOID arg);
	int SessionNum_ = 0;
	DWORD dwMaxSession_;
	Session* pSessionArr_;
	Stack DisconnectStack_;
	CRITICAL_SECTION stackLock_;
	HANDLE hcp_;
	virtual BOOL RecvPost(Session* pSession);
	virtual BOOL SendPost(Session* pSession);
	virtual void ReleaseSession(Session* pSession);

	// Monitoring º¯¼ö
	// Accept
	DWORD dwAcceptTPS_ = 0;

	// Recv (Per MSG)
	DWORD dwRecvTPS_ = 0;

	// Send (Per MSG)
	DWORD dwSendTPS_ = 0;

	// Disconnect
	DWORD dwDisconnectTPS_ = 0;

};
