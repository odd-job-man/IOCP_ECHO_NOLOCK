#pragma once
#include <Windows.h>
#include "CLockFreeStack.h"
#include "Packet.h"
#include "LanSession.h"

class SmartPacket;

class LanServer
{
public:
	LanServer();
	void SendPacket(ULONGLONG id, SmartPacket& sendPacket);
	void SendPacket(ULONGLONG id, Packet* pPacket);
	virtual BOOL OnConnectionRequest() = 0;
	virtual void* OnAccept(ULONGLONG id) = 0;
	virtual void OnRelease(ULONGLONG id) = 0;
	virtual void OnRecv(ULONGLONG id, Packet* pPacket) = 0;
	virtual void OnError(ULONGLONG id, int errorType, Packet* pRcvdPacket) = 0;
	virtual void OnPost(void* order) = 0;
	void Disconnect(ULONGLONG id);
protected:
	const DWORD IOCP_WORKER_THREAD_NUM_ = 0;
	const DWORD IOCP_ACTIVE_THREAD_NUM_ = 0;
	const LONG TIME_OUT_MILLISECONDS_ = 0;
	const ULONGLONG TIME_OUT_CHECK_INTERVAL_ = 0;
	ULONGLONG ullIdCounter = 0;
	LanSession* pSessionArr_;
	CLockFreeStack<short> DisconnectStack_;
	HANDLE hcp_;
	HANDLE hAcceptThread_;
	HANDLE* hIOCPWorkerThreadArr_;
	SOCKET hListenSock_;

	static inline const MYOVERLAPPED OnPostOverlapped{ OVERLAPPED{},OVERLAPPED_REASON::POST };

	BOOL RecvPost(LanSession* pSession);
	BOOL SendPost(LanSession* pSession);
	void ReleaseSession(LanSession* pSession);
	void RecvProc(LanSession* pSession, DWORD dwNumberOfBytesTransferred);
	void SendProc(LanSession* pSession, DWORD dwNumberOfBytesTransferred);
	void ProcessTimeOut();

	static unsigned __stdcall AcceptThread(LPVOID arg);
	static unsigned __stdcall IOCPWorkerThread(LPVOID arg);
public:
	alignas(64) ULONGLONG acceptTotal_ = 0;
	alignas(64) LONG64 recvTPS_ = 0;
	alignas(64) LONG64 sendTPS_ = 0;
	alignas(64) ULONGLONG disconnectTPS_ = 0;
	alignas(64) ULONGLONG acceptCounter_ = 0;
	alignas(64) LONG lSessionNum_ = 0;
protected:
	const LONG maxSession_ = 0;
};
