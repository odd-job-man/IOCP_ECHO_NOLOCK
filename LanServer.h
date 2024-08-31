#pragma once

unsigned __stdcall AcceptThread(LPVOID arg);
unsigned __stdcall IOCPWorkerThread(LPVOID arg);


class Stack; 

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
	LONG lSessionNum_ = 0;
	LONG lMaxSession_;
	Session* pSessionArr_;
	Stack DisconnectStack_;
	CRITICAL_SECTION stackLock_;
	HANDLE hcp_;
	virtual BOOL RecvPost(Session* pSession);
	virtual BOOL SendPost(Session* pSession);
	virtual void ReleaseSession(Session* pSession);
	void RecvProc(Session* pSession, DWORD dwNumberOfBytesTransferred);
	void SendProc(Session* pSession, DWORD dwNumberOfBytesTransferred);
	char* GetNetBufferPtr(Packet* pPacket);

	// Monitoring º¯¼ö
	// Accept
	LONG lAcceptTPS_ = 0;

	// Recv (Per MSG)
	LONG lRecvTPS_ = 0;

	// Send (Per MSG)
	LONG lSendTPS_ = 0;

	// Disconnect
	LONG lDisconnectTPS_ = 0;
};
