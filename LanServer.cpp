#include "IHandler.h"
#include "LanServer.h"
#include "Packet.h"
#include <process.h>

#include "Logger.h"
#include <iostream>



#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"LoggerMt.lib")

//#define GQCSRET
//#define SENDRECV
//#define WILL_SEND
//#define WILL_RECV
//#define SESSION_DELETE

int g_iCount = 0;



unsigned __stdcall IOCPWorkerThread(LPVOID arg);
unsigned __stdcall AcceptThread(LPVOID arg);

ULONGLONG g_ullID;

#define SERVERPORT 6000

SOCKET g_ListenSock;

BOOL LanServer::Start()
{
	int retval;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		__debugbreak();
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp OK!");
	// NOCT에 0들어가면 논리프로세서 수만큼을 설정함
	hcp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (!hcp_)
		__debugbreak();
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Create IOCP OK!");

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	HANDLE hIOCPWorkerThread;
	HANDLE hAcceptThread;

	hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, CREATE_SUSPENDED, nullptr);
	if (!hAcceptThread)
		__debugbreak();
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");

	for (DWORD i = 0; i < si.dwNumberOfProcessors; ++i)
	{
		hIOCPWorkerThread = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, 0, nullptr);
		if (!hIOCPWorkerThread)
			__debugbreak();
		CloseHandle(hIOCPWorkerThread);
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE IOCP WorkerThread OK Num : %u!", si.dwNumberOfProcessors);

	g_ListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_ListenSock == INVALID_SOCKET)
		__debugbreak();
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET OK");

	// bind
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = bind(g_ListenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
		__debugbreak();
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind OK");

	// listen
	retval = listen(g_ListenSock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		__debugbreak();
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen() OK");

	linger linger;
	linger.l_linger = 0;
	linger.l_onoff = 1;
	setsockopt(g_ListenSock, IPPROTO_TCP, SO_LINGER, (char*)&linger, sizeof(linger));

	ResumeThread(hAcceptThread);

	InitializeSRWLock(&SessionUMapLock_);
	return 0;
}

unsigned __stdcall IOCPWorkerThread(LPVOID arg)
{
	LanServer* pLanServer;
	Session* pSession;
	WSAOVERLAPPED* pOverlapped;
	//클라이언트 정보 얻기
	SOCKADDR_IN clientaddr;
	int addrlen = sizeof(clientaddr);
	DWORD dwNOBT;
	BOOL bGQCSRet;

	pLanServer = (LanServer*)arg;

	while (1)
	{
		pOverlapped = nullptr;
		dwNOBT = 0;
		pSession = nullptr;
		bGQCSRet = GetQueuedCompletionStatus(pLanServer->hcp_, &dwNOBT, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

#ifdef GQCSRET
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, GQCS return Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->ullID, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif

		if (!pOverlapped && !dwNOBT && !pSession)
			return 0;

		// 정상종료
		if (bGQCSRet && dwNOBT == 0)
			goto lb_next;

		// 비정상 종료
		// 로깅을 하려햇으나 GQCS 에서 WSAGetLastError 값을 64로 덮어 써버린다.
		// 따라서 WSASend나 WSARecv, 둘 중 하나가 바로 실패하는 경우에만 로깅하는것으로...
		if (!bGQCSRet && pOverlapped)
			goto lb_next;

		if (&pSession->recvOverlapped == pOverlapped)
		{
#ifdef SENDRECV
			LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Recv Complete Session ID : %llu", GetCurrentThreadId(), pSession->ullID);
#endif
			SHORT shHeader;
			Packet pckt;

			pSession->recvRB.MoveInPos(dwNOBT);
			while (1)
			{
				int iPeekRet = pSession->recvRB.Peek((char*)&shHeader, sizeof(shHeader));
				if (iPeekRet == 0)
					break;

				int iUseSize = pSession->recvRB.GetUseSize();
				if (iUseSize < sizeof(shHeader) + shHeader)
					break;

				pSession->recvRB.MoveOutPos(sizeof(shHeader));
				pSession->recvRB.Dequeue(pckt.GetBufferPtr(), shHeader);
				pckt.MoveWritePos(shHeader);
				pLanServer->OnRecv(pSession->ullID, &pckt);
				pckt.Clear();
			}
			pLanServer->RecvPost(pSession);
		}
		else
		{
			pSession->sendRB.MoveOutPos(dwNOBT);
#ifdef SENDRECV
			LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Send Complete Session ID : %llu", GetCurrentThreadId(), pSession->ullID);
#endif
			InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);

			if (pSession->sendRB.GetUseSize() > 0)
				pLanServer->SendPost(pSession);
		}
	lb_next:
		int ioCnt = InterlockedDecrement((LONG*)&pSession->IoCnt);
		//LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, IoCnt Decre Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->ullID, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
		if (ioCnt == 0)
			pLanServer->ReleaseSession(pSession);
	}
}

unsigned __stdcall AcceptThread(LPVOID arg)
{
	SOCKET clientSock;
	SOCKADDR_IN clientAddr;
	int addrlen;
	LanServer* pLanServer = (LanServer*)arg;
	Session* pSession;
	addrlen = sizeof(clientAddr);

	while (1)
	{
		clientSock = accept(g_ListenSock, (SOCKADDR*)&clientAddr, &addrlen);

		if (clientSock == INVALID_SOCKET)
			__debugbreak();

		if (!pLanServer->OnConnectionRequest())
		{
			closesocket(clientSock);
			continue;
		}

		pSession = new Session;
		pSession->Init(clientSock, g_ullID);
		CreateIoCompletionPort((HANDLE)pSession->sock, pLanServer->hcp_, (ULONG_PTR)pSession, 0);

		AcquireSRWLockExclusive(&pLanServer->SessionUMapLock_);
		pLanServer->sessionUMap_.insert(std::pair<ULONGLONG, Session*>{g_ullID, pSession});
		ReleaseSRWLockExclusive(&pLanServer->SessionUMapLock_);
		++g_ullID;

		// 맨처음 WSARecv 부터 실패한 경우
		if (!pLanServer->RecvPost(pSession))
			pLanServer->ReleaseSession(pSession);
	}
	return 0;
}


#define MAX_SESSION 1000
BOOL LanServer::OnConnectionRequest()
{
	if (SessionNum_ + 1 >= MAX_SESSION)
		return FALSE;


	return TRUE;
}

void* LanServer::OnAccept(ULONGLONG ullID)
{
	return nullptr;
}

void LanServer::OnRecv(ULONGLONG ullID, Packet* pPacket)
{
	ULONGLONG ullPayLoad;
	Packet sendPacket;
	(*pPacket) >> ullPayLoad;

	sendPacket << ullPayLoad;
	SendPacket(ullID, &sendPacket);
}

void LanServer::SendPacket(ULONGLONG ullID, Packet* pPacket)
{
	AcquireSRWLockShared(&SessionUMapLock_);
	auto iter = sessionUMap_.find(ullID);
	Session* pSession = iter->second;
	ReleaseSRWLockShared(&SessionUMapLock_);

	SHORT shHeader = pPacket->GetUsedDataSize();
	pSession->sendRB.Enqueue((const char*)&shHeader, sizeof(shHeader));
	pSession->sendRB.Enqueue(pPacket->GetBufferPtr(), shHeader);
	SendPost(pSession);
}

BOOL LanServer::RecvPost(Session* pSession)
{
	int IoCnt;
	DWORD flags;
	int iRecvRet;
	DWORD dwErrCode;
	WSABUF wsa[2];

	wsa[0].buf = pSession->recvRB.GetWriteStartPtr();
	wsa[0].len = pSession->recvRB.DirectEnqueueSize();
	wsa[1].buf = pSession->recvRB.Buffer_;
	wsa[1].len = pSession->recvRB.GetFreeSize() - wsa[0].len;

	ZeroMemory(&pSession->recvOverlapped, sizeof(WSAOVERLAPPED));
	flags = 0;
	InterlockedIncrement((LONG*)&(pSession->IoCnt));
#ifdef WILL_RECV
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, RecvPost WSARecv Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->ullID, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
	iRecvRet = WSARecv(pSession->sock, wsa, 2, nullptr, &flags, &(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		IoCnt = InterlockedDecrement((LONG*)&(pSession->IoCnt));
#ifdef WILL_RECV
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, WSARecv Fail Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->ullID, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

BOOL LanServer::SendPost(Session* pSession)
{
	// 현재 값을 TRUE로 바꾼다. 원래 TRUE엿다면 반환값이 TRUE일것이며 그렇다면 현재 SEND 진행중이기 때문에 그냥 빠저나간다
	// 이 조건문의 위치로 인하여 Out은 바뀌지 않을것임이 보장된다.
	// 하지만 SendPost 실행주체가 Send완료통지 스레드인 경우에는 in의 위치는 SendPacket으로 인해서 바뀔수가 있다.
	// iUseSize를 구하는 시점에서의 DirectDequeueSize의 값이 달라질수있다.
	if (InterlockedExchange((LONG*)&pSession->bSendingInProgress, TRUE) == TRUE)
		return TRUE;

	WSABUF wsa[2];
	int iUseSize = pSession->sendRB.GetUseSize();
	int iDirectDeqSize = pSession->sendRB.DirectDequeueSize();
	int iBufLen = 0;

	// SendPacket에서 in을 옮겨서 UseSize가 0보다 커진시점에서 Send완료통지가 도착해서 Out을 옮기고 플래그 해제 Recv완료통지 스레드가 먼저 SendPost에 도달해 플래그를 선점한경우 UseSize가 0이나온다.
	// 여기서 flag를 다시 FALSE로 바꾸어주지 않아서 멈춤발생
	if (iUseSize == 0)
	{
		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		return TRUE;
	}
	
	if (iUseSize <= iDirectDeqSize)
	{
		wsa[0].buf = pSession->sendRB.GetReadStartPtr();
		wsa[0].len = iUseSize;
		iBufLen = 1;
	}
	else
	{
		wsa[0].buf = pSession->sendRB.GetReadStartPtr();
		wsa[0].len = iDirectDeqSize;
		wsa[1].buf = pSession->sendRB.Buffer_;
		wsa[1].len = iUseSize - wsa[0].len;
		iBufLen = 2;
	}

	ZeroMemory(&(pSession->sendOverlapped), sizeof(WSAOVERLAPPED));
	InterlockedIncrement((LONG*)&pSession->IoCnt);
#ifdef WILL_SEND 
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, SendPost WSASend Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->ullID, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
	int iSendRet = WSASend(pSession->sock, wsa, iBufLen, nullptr, 0, &(pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		InterlockedDecrement((LONG*)&(pSession->IoCnt));
#ifdef WILL_SEND 
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, WSASend Fail Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->ullID, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

void LanServer::ReleaseSession(Session* pSession)
{
	AcquireSRWLockExclusive(&SessionUMapLock_);
	sessionUMap_.erase(pSession->ullID);
	closesocket(pSession->sock);
#ifdef SESSION_DELETE 
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Delete Session Thread ID :%u Session ID : %llu", GetCurrentThreadId(), pSession->ullID);
#endif
	delete pSession;
	ReleaseSRWLockExclusive(&SessionUMapLock_);
}

