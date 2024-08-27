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

ULONGLONG g_recv = 0;
ULONGLONG g_send = 0;


unsigned __stdcall IOCPWorkerThread(LPVOID arg);
unsigned __stdcall AcceptThread(LPVOID arg);

ULONGLONG g_ullID;

#define SERVERPORT 6000

#define ZERO_BYTE_SEND
#define LINGER

SOCKET g_ListenSock;

BOOL LanServer::Start(DWORD dwMaxSession)
{
	int retval;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp Fail ErrCode : %u",WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp OK!");
	// NOCT�� 0���� �����μ��� ����ŭ�� ������
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
	dwMaxSession_ = dwMaxSession;
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
	DWORD dwSendBufNum = _InterlockedExchange((LONG*)&pSession->dwSendBufNum, 0);
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

		if (!pOverlapped && !dwNOBT && !pSession)
			return 0;

		 //��������
		if (bGQCSRet && dwNOBT == 0)
			goto lb_next;

		 //������ ����
		 //�α��� �Ϸ������� GQCS ���� WSAGetLastError ���� 64�� ���� �������.
		 //���� WSASend�� WSARecv, �� �� �ϳ��� �ٷ� �����ϴ� ��쿡�� �α��ϴ°�����...
		if (!bGQCSRet && pOverlapped)
			goto lb_next;

		if (&pSession->recvOverlapped == pOverlapped)
		{
#ifdef SENDRECV
			LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Recv Complete Session ID : %llu", GetCurrentThreadId(), pSession->id.ullId);
#endif
			SHORT shHeader;
			Packet pckt;
#ifdef IO_RET
			ULONGLONG ret = InterlockedAdd64((LONG64*)&pSession->ullRecv, dwNOBT);
			LOG_ASYNC(L"Session ID : %llu, Recv Amount : %u", pSession->id.ullId, dwNOBT);
#endif
			pSession->recvRB.MoveInPos(dwNOBT);
			while (1)
			{
				if (pSession->recvRB.Peek((char*)&shHeader, sizeof(shHeader)) == 0)
					break;

				if (pSession->recvRB.GetUseSize() < sizeof(shHeader) + shHeader)
					break;

				pSession->recvRB.Dequeue(pckt.GetBufferPtr(), sizeof(shHeader) + shHeader);
				pckt.MoveWritePos(sizeof(shHeader) + shHeader);
				pckt.MoveReadPos(sizeof(shHeader));
				pLanServer->OnRecv(pSession->id, &pckt);
				pckt.Clear();
				InterlockedIncrement((LONG*)&pLanServer->dwRecvTPS_);
			}
			pLanServer->RecvPost(pSession);
		}
		else
		{
#ifdef IO_RET
			ULONGLONG ret = InterlockedAdd64((LONG64*)&pSession->ullRecv, dwNOBT);
			LOG_ASYNC(L"Session ID : %llu, Send Amount : %u", pSession->id.ullId, dwNOBT);
#endif
			ClearPacket(pSession);
		#ifdef SENDRECV
			LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, Send Complete Session ID : %llu", GetCurrentThreadId(), pSession->id.ullId);
#endif
			InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);

			if (pSession->sendRB.GetUseSize() > 0)
				pLanServer->SendPost(pSession);
		}
	lb_next:
		if (InterlockedDecrement((LONG*)&pSession->IoCnt) == 0)
		{
			ClearPacket(pSession);
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

		InterlockedIncrement((LONG*)&pLanServer->dwAcceptTPS_);
		InterlockedIncrement((LONG*)&pLanServer->dwSessionNum_);

		SHORT shIndex;
		EnterCriticalSection(&pLanServer->stackLock_);
		pLanServer->DisconnectStack_.Pop((void**)&shIndex);
		LeaveCriticalSection(&pLanServer->stackLock_);
		Session* pSession = pLanServer->pSessionArr_ + shIndex;
		pSession->Init(clientSock, g_ullID, shIndex);
		CreateIoCompletionPort((HANDLE)pSession->sock, pLanServer->hcp_, (ULONG_PTR)pSession, 0);
		++g_ullID;

		// ��ó�� WSARecv ���� ������ ���
		if (!pLanServer->RecvPost(pSession))
			pLanServer->ReleaseSession(pSession);
	}
	return 0;
}


BOOL LanServer::OnConnectionRequest()
{
	if (dwSessionNum_ + 1 >= dwMaxSession_)
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
		"Send Msg TPS: %d\n\n", dwAcceptTPS_, dwDisconnectTPS_, dwRecvTPS_, dwSendTPS_);
	dwAcceptTPS_ = dwDisconnectTPS_ = dwRecvTPS_ = dwSendTPS_ = 0;
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
	InterlockedIncrement((LONG*)&(pSession->IoCnt));
#ifdef WILL_RECV
	LOG(L"DEBUG", DEBUG, TEXTFILE, L"Thread ID : %u, RecvPost WSARecv Session ID : %llu, IoCount : %d", GetCurrentThreadId(), pSession->id.ullId, InterlockedExchange((LONG*)&pSession->IoCnt, pSession->IoCnt));
#endif
	iRecvRet = WSARecv(pSession->sock, wsa, 2, nullptr, &flags, &(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		InterlockedDecrement((LONG*)&(pSession->IoCnt));
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
	// ���� ���� TRUE�� �ٲ۴�. ���� TRUE���ٸ� ��ȯ���� TRUE�ϰ��̸� �׷��ٸ� ���� SEND �������̱� ������ �׳� ����������
	// �� ���ǹ��� ��ġ�� ���Ͽ� Out�� �ٲ��� ���������� ����ȴ�.
	// ������ SendPost ������ü�� Send�Ϸ����� �������� ��쿡�� in�� ��ġ�� SendPacket���� ���ؼ� �ٲ���� �ִ�.
	// iUseSize�� ���ϴ� ���������� DirectDequeueSize�� ���� �޶������ִ�.
	if (InterlockedExchange((LONG*)&pSession->bSendingInProgress, TRUE) == TRUE)
		return TRUE;

	// SendPacket���� in�� �Űܼ� UseSize�� 0���� Ŀ���������� Send�Ϸ������� �����ؼ� Out�� �ű�� �÷��� ���� Recv�Ϸ����� �����尡 ���� SendPost�� ������ �÷��׸� �����Ѱ�� UseSize�� 0�̳��´�.
	// ���⼭ flag�� �ٽ� FALSE�� �ٲپ����� �ʾƼ� ����߻�
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

	InterlockedExchange((LONG*)&pSession->dwSendBufNum, i);
	InterlockedAdd((LONG*)&dwSendTPS_, i);
	InterlockedIncrement((LONG*)&pSession->IoCnt);
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
		InterlockedDecrement((LONG*)&(pSession->IoCnt));
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
	LOG_ASYNC(L"Delete Session : %llu", pSession->id.ullId);
#endif
	closesocket(pSession->sock);
	pSession->bUsing = FALSE;
	SHORT shIndex = (SHORT)(pSession - pSessionArr_);

	EnterCriticalSection(&stackLock_);
	DisconnectStack_.Push((void**)&shIndex);
	LeaveCriticalSection(&stackLock_);
	InterlockedIncrement((LONG*)&dwDisconnectTPS_);
	InterlockedDecrement((LONG*)&dwSessionNum_);
}

