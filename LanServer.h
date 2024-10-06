#pragma once

unsigned __stdcall AcceptThread(LPVOID arg);
unsigned __stdcall IOCPWorkerThread(LPVOID arg);


class Stack; 
class SmartPacket;

class LanServer : public IHandler
{
public:
	virtual BOOL Start(DWORD dwMaxSession);
	virtual void SendPacket(ID id, SmartPacket& sendPacket);
	virtual BOOL OnConnectionRequest();
	virtual void* OnAccept(ID id);
	virtual void OnRecv(ID id, Packet* pPacket);
	void Monitoring();
	virtual void Stop();
	static unsigned __stdcall AcceptThread(LPVOID arg);
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
private:
	// Monitoring 변수
	// 일부러 캐시라인 벌림
	// Accept
	LONG lAcceptTPS_ = 0;

	LONG lSessionNum_ = 0;
	LONG lMaxSession_;
	Session* pSessionArr_;
	CLockFreeStack<short> DisconnectStack_;
	HANDLE hcp_;
	HANDLE hAcceptThread_;
	HANDLE* hIOCPWorkerThreadArr_;
	SOCKET hListenSock_;
	virtual BOOL RecvPost(Session* pSession);
	virtual BOOL SendPost(Session* pSession);
	virtual void ReleaseSession(Session* pSession);
	void RecvProc(Session* pSession, int numberOfBytesTransferred);
	void SendProc(Session* pSession, DWORD dwNumberOfBytesTransferred);
	friend class Packet;

	// Monitoring 변수
	// Recv (Per MSG)
	alignas(64) LONG lRecvTPS_ = 0;

	// Send (Per MSG)
	alignas(64) LONG lSendTPS_ = 0;

	// Disconnect
	alignas(64) LONG lDisconnectTPS_ = 0;
};
