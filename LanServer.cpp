#include "IHandler.h"
#include "LanServer.h"
#include "Packet.h"
#include <process.h>
#include "Logger.h"
#include <iostream>
#include <crtdbg.h>

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"LoggerMt.lib")

#define GQCSRET
#define SENDRECV
#define WILL_SEND
#define WILL_RECV
#define SESSION_DELETE

int g_iCount = 0;

ULONGLONG g_recv = 0;
ULONGLONG g_send = 0;

unsigned __stdcall IOCPWorkerThread(LPVOID arg);
unsigned __stdcall AcceptThread(LPVOID arg);

ULONGLONG g_ullID;

#define ACCEPT
#define SERVERPORT 6000

#define ZERO_BYTE_SEND
#define LINGER

SOCKET g_ListenSock;

#define RECV_COMPL 1000
#define SEND_COMPL 1001

BOOL LanServer::Start(DWORD dwMaxSession)
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_WARN, GetStdHandle(STD_OUTPUT_HANDLE));

	int retval;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp Fail ErrCode : %u",WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp OK!");
	// NOCT에 0들어가면 논리프로세서 수만큼을 설정함
	hcp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (!hcp_)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"CreateIoCompletionPort Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Create IOCP OK!");

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	HANDLE hIOCPWorkerThread;
	HANDLE hAcceptThread;

	hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, CREATE_SUSPENDED, nullptr);
	if (!hAcceptThread)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");


	g_ListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_ListenSock == INVALID_SOCKET)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET OK");

	// bind
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = bind(g_ListenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind OK");

	// listen
	retval = listen(g_ListenSock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen() OK");

#ifdef LINGER
	linger linger;
	linger.l_linger = 0;
	linger.l_onoff = 1;
	setsockopt(g_ListenSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"linger() OK");
#endif

#ifdef ZERO_BYTE_SEND
	DWORD dwSendBufSize = 0;
	setsockopt(g_ListenSock, SOL_SOCKET, SO_SNDBUF, (char*)&dwSendBufSize, sizeof(dwSendBufSize));
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Zerobyte Send OK");
#endif

	pSessionArr_ = new Session[dwMaxSession];
	lMaxSession_ = dwMaxSession;
	DisconnectStack_.Init(dwMaxSession, sizeof(SHORT));
	for (int i = dwMaxSession - 1; i >= 0; --i)
		DisconnectStack_.Push((void**)&i);
	InitializeCriticalSection(&stackLock_);

	hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, nullptr);
	if (!hAcceptThread)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");

	for (DWORD i = 0; i < si.dwNumberOfProcessors * 2; ++i)
	{
		hIOCPWorkerThread = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, 0, nullptr);
		if (!hIOCPWorkerThread)
		{
			LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE WorkerThread Fail ErrCode : %u", WSAGetLastError());
			__debugbreak();
		}
		CloseHandle(hIOCPWorkerThread);
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE IOCP WorkerThread OK Num : %u!", si.dwNumberOfProcessors);
	return 0;
}

__forceinline void ClearPacket(Session* pSession)
{
	DWORD dwSendBufNum = _InterlockedExchange(&pSession->lSendBufNum, 0);
	for (DWORD i = 0; i < dwSendBufNum; ++i)
	{
		Packet* pPacket;
		pSession->sendRB.Dequeue((char*)&pPacket, sizeof(Packet*));
		delete pPacket;
	}
}

__forceinline void ReleaseSendFailPacket(Session* pSession)
{
	DWORD dwSendBufNum = pSession->sendRB.GetUseSize() / sizeof(Packet*);
	for (DWORD i = 0; i < dwSendBufNum; ++i)
	{
		Packet* pPacket;
		pSession->sendRB.Dequeue((char*)&pPacket, sizeof(Packet*));
		delete pPacket;
	}
}

unsigned __stdcall IOCPWorkerThread(LPVOID arg)
{
	LanServer* pLanServer = (LanServer*)arg;
	while (1)
	{
		WSAOVERLAPPED* pOverlapped = nullptr;
		DWORD dwNOBT = 0;
		Session* pSession = nullptr;
		BOOL bGQCSRet = GetQueuedCompletionStatus(pLanServer->hcp_, &dwNOBT, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

#ifdef GQCSRET
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, GQCS return Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
		do
		{
			if (!pOverlapped && !dwNOBT && !pSession)
				return 0;

			//정상종료
			if (bGQCSRet && dwNOBT == 0)
				break;

			//비정상 종료
			//로깅을 하려햇으나 GQCS 에서 WSAGetLastError 값을 64로 덮어 써버린다.
			//따라서 WSASend나 WSARecv, 둘 중 하나가 바로 실패하는 경우에만 로깅하는것으로...
			if (!bGQCSRet && pOverlapped)
				break;

			if (&pSession->recvOverlapped == pOverlapped)
				pLanServer->RecvProc(pSession, dwNOBT);
			else
				pLanServer->SendProc(pSession, dwNOBT);

		} while (0);

		if (InterlockedDecrement(&pSession->IoCnt) == 0)
		{
			pLanServer->ReleaseSession(pSession);
		}
	}
}

unsigned __stdcall AcceptThread(LPVOID arg)
{
	SOCKET clientSock;
	SOCKADDR_IN clientAddr;
	int addrlen;
	LanServer* pLanServer = (LanServer*)arg;
	addrlen = sizeof(clientAddr);

	while (1)
	{
		clientSock = accept(g_ListenSock, (SOCKADDR*)&clientAddr, &addrlen);

		if (clientSock == INVALID_SOCKET)
		{
			DWORD dwErrCode = WSAGetLastError();
			__debugbreak();
		}


		if (!pLanServer->OnConnectionRequest())
		{
			closesocket(clientSock);
			continue;
		}

		InterlockedIncrement((LONG*)&pLanServer->lAcceptTPS_);
		InterlockedIncrement((LONG*)&pLanServer->lSessionNum_);

		SHORT shIndex;
		EnterCriticalSection(&pLanServer->stackLock_);
		pLanServer->DisconnectStack_.Pop((void**)&shIndex);
		LeaveCriticalSection(&pLanServer->stackLock_);
		Session* pSession = pLanServer->pSessionArr_ + shIndex;
		pSession->Init(clientSock, g_ullID, shIndex);
		++g_ullID;
#ifdef ACCEPT
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Accept! Thread ID : %u Session ID : %llu", GetCurrentThreadId(), g_ullID);
#endif
		CreateIoCompletionPort((HANDLE)pSession->sock, pLanServer->hcp_, (ULONG_PTR)pSession, 0);

		Packet* pLoginPacket = new Packet;
		(*pLoginPacket) << (ULONGLONG)0x7fffffffffffffff;
		*(NET_HEADER*)(pLoginPacket->pBuffer_) = pLoginPacket->GetUsedDataSize();
		pSession->sendRB.Enqueue((char*)&pLoginPacket, sizeof(Packet*));
		
		if (!pLanServer->SendPost(pSession))
		{
			pLanServer->ReleaseSession(pSession);
			continue;
		}

		/*
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001948]  Accept! Thread ID : 7412 Session ID : 216
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001949]  Thread ID : 7412, SendPost WSASend Session ID : 1125899906842839, IoCount : 1
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001950]  Thread ID : 7412, RecvPost WSARecv Session ID : 1125899906842839, IoCount : 2
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001951]  Thread ID : 1644, GQCS return Session ID : 1125899906842839, IoCount : 2
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001952]  Thread ID : 7412, WSARecv Fail Session ID : 1125899906842839, IoCount : 1
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001954]  Thread ID : 7412, Delete Session : 1125899906842839, IoCnt : 1
		[DEBUG]  [2024-08-29 20:50:51 / DEBUG  / 000001953]  Thread ID : 1644, Send Complete Session ID : 1125899906842839, IoCnt : 1
		SendPost는 성공하고 RecvPost가 바로 실패하는 경우 기존 코드에서는 SendPost가 없어서 바로 실패하자마자 Release 햇엇다. IoCnt는 무조건 0이엇으므로,
		하지만 이제는 확인하지않으면 IoCnt가 1인데 삭제하고, Send완료통지 처리에서 댕글링 포인터를 건드려서 문제가 생기는 경우가 발생해서 이 경우에도 체크 한다. 
		*/
		do
		{
			if (pLanServer->RecvPost(pSession))
				break;

			if (InterlockedExchange(&pSession->IoCnt, pSession->IoCnt) != 0)
				break;

			pLanServer->ReleaseSession(pSession);
		} while (0);
	}
	return 0;
}

BOOL LanServer::OnConnectionRequest()
{
	if (lSessionNum_ + 1 >= lMaxSession_)
		return FALSE;

	return TRUE;
}

void* LanServer::OnAccept(ID id)
{
	return nullptr;
}

void LanServer::OnRecv(ID id, Packet* pPacket)
{
	ULONGLONG ullPayLoad;
	(*pPacket) >> ullPayLoad;

	Packet* pSendPacket = new Packet;
	pSendPacket->Clear();
	(*pSendPacket) << ullPayLoad;
	SendPacket(id, pSendPacket);
}

void LanServer::Monitoring()
{
	printf(
		"Accept TPS: %d\n"
		"Disconnect TPS: %d\n"
		"Recv Msg TPS: %d\n"
		"Send Msg TPS: %d\n"
		"User Num : %d\n\n",
		lAcceptTPS_, lDisconnectTPS_, lRecvTPS_, lSendTPS_, lSessionNum_);

#ifdef _DEBUG
	if (!lSessionNum_)
		_CrtDumpMemoryLeaks();
#endif

	lAcceptTPS_ = lDisconnectTPS_ = lRecvTPS_ = lSendTPS_ = 0;
}

void LanServer::SendPacket(ID id, Packet* pPacket)
{
	Session* pSession = &pSessionArr_[GET_SESSION_INDEX(id)];
	*(NET_HEADER*)pPacket->pBuffer_ = pPacket->GetUsedDataSize();
	pSession->sendRB.Enqueue((const char*)&pPacket, sizeof(pPacket));
	SendPost(pSession);
}

BOOL LanServer::RecvPost(Session* pSession)
{
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
	InterlockedIncrement(&pSession->IoCnt);
#ifdef WILL_RECV
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, RecvPost WSARecv Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
	iRecvRet = WSARecv(pSession->sock, wsa, 2, nullptr, &flags, &(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		InterlockedDecrement(&(pSession->IoCnt));
#ifdef WILL_RECV
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, WSARecv Fail Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
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

	// SendPacket에서 in을 옮겨서 UseSize가 0보다 커진시점에서 Send완료통지가 도착해서 Out을 옮기고 플래그 해제 Recv완료통지 스레드가 먼저 SendPost에 도달해 플래그를 선점한경우 UseSize가 0이나온다.
	// 여기서 flag를 다시 FALSE로 바꾸어주지 않아서 멈춤발생
	int out = pSession->sendRB.iOutPos_;
	int in = pSession->sendRB.iInPos_;
	int iUseSize;
	GetUseSize_MACRO(in, out, iUseSize);

	if (iUseSize == 0)
	{
		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		return TRUE;
	}

	WSABUF wsa[50];
	DWORD i;
	DWORD dwBufferNum = iUseSize / sizeof(Packet*);
	for (i = 0; i < 50 && i < dwBufferNum; ++i)
	{
		Packet* pPacket;
		pSession->sendRB.PeekAt((char*)&pPacket, out, sizeof(Packet*));
		wsa[i].buf = (char*)pPacket;
		wsa[i].len = pPacket->GetNetUseSize();
		MoveInOrOutPos_MACRO(out, sizeof(Packet*));
	}

	InterlockedExchange(&pSession->lSendBufNum, i);
	InterlockedAdd(&lSendTPS_, i);
	InterlockedIncrement(&pSession->IoCnt);
	ZeroMemory(&(pSession->sendOverlapped), sizeof(WSAOVERLAPPED));
#ifdef WILL_SEND 
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, SendPost WSASend Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
	int iSendRet = WSASend(pSession->sock, wsa, i, nullptr, 0, &(pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		InterlockedDecrement(&(pSession->IoCnt));
#ifdef WILL_SEND 
		LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, WSASend Fail Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
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
#ifdef SESSION_DELETE 
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Delete Session : %llu, IoCnt : %d",
		GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange(&pSession->IoCnt, pSession->IoCnt));
#endif
	ReleaseSendFailPacket(pSession);
	closesocket(pSession->sock);
	pSession->bUsing = FALSE;
	SHORT shIndex = (SHORT)(pSession - pSessionArr_);
	if (pSession->sendRB.GetUseSize() > 0)
		__debugbreak();

#ifdef _DEBUG
	_ASSERTE(_CrtCheckMemory());
#endif
	EnterCriticalSection(&stackLock_);
	DisconnectStack_.Push((void**)&shIndex);
	LeaveCriticalSection(&stackLock_);
	InterlockedIncrement(&lDisconnectTPS_);
	if (InterlockedDecrement(&lSessionNum_) < 0)
		__debugbreak();
}

void LanServer::RecvProc(Session* pSession, DWORD dwNumberOfByteTransferred)
{
#ifdef SENDRECV
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Recv Complete Session ID : %llu, IoCnt : %d",
		GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange(&pSession->IoCnt, pSession->IoCnt));
#endif
	SHORT shHeader;
	Packet packet;
#ifdef IO_RET
	ULONGLONG ret = InterlockedAdd64((LONG64*)&pSession->ullRecv, dwNOBT);
	LOG_ASYNC(L"Session ID : %llu, Recv Amount : %u", pSession->id.ullId, dwNOBT);
#endif
	pSession->recvRB.MoveInPos(dwNumberOfByteTransferred);
	while (1)
	{
		if (pSession->recvRB.Peek((char*)&shHeader, sizeof(shHeader)) == 0)
			break;

		if (pSession->recvRB.GetUseSize() < sizeof(shHeader) + shHeader)
			break;

		pSession->recvRB.Dequeue(packet.GetBufferPtr(), sizeof(shHeader) + shHeader);
		packet.MoveWritePos(sizeof(shHeader) + shHeader);
		packet.MoveReadPos(sizeof(shHeader));
		OnRecv(pSession->id, &packet);
		packet.Clear();
		InterlockedIncrement(&lRecvTPS_);
	}
	RecvPost(pSession);
}

void LanServer::SendProc(Session* pSession, DWORD dwNumberOfBytesTransferred)
{
#ifdef SENDRECV
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Send Complete Session ID : %llu, IoCnt : %d",
		GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange(&pSession->IoCnt, pSession->IoCnt));
#endif

#ifdef IO_RET
	ULONGLONG ret = InterlockedAdd64((LONG64*)&pSession->ullRecv, dwNOBT);
	LOG_ASYNC(L"Session ID : %llu, Send Amount : %u", pSession->id.ullId, dwNOBT);
#endif
	ClearPacket(pSession);
	InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);

	if (pSession->sendRB.GetUseSize() > 0)
		SendPost(pSession);
}



