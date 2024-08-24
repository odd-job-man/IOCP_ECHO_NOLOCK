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
	// NOCT�� 0���� �����μ��� ����ŭ�� ������
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
	//Ŭ���̾�Ʈ ���� ���
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

//#ifdef GQCSRET
//		LOG_ASYNC(L"GQCS return Session ID : %llu", pSession->ullID);
//#endif

		if (!pOverlapped && !dwNOBT && !pSession)
			return 0;


		if (&pSession->recvOverlapped == pOverlapped)
		{
#ifdef SENDRECV
			LOG_ASYNC(L"Recv Complete Session ID : %llu", pSession->ullID);
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
				if (!pLanServer->OnRecv(pSession->ullID, &pckt))
				{
					pLanServer->ReleaseSession(pSession);
					goto lb_next;
				}
				pckt.Clear();
			}
			if (!pLanServer->RecvPost(pSession))
			{
				pLanServer->ReleaseSession(pSession);
				goto lb_next;
			}
		}
		else
		{
			int in_prev = pSession->sendRB.iInPos_;
			int out_prev = pSession->sendRB.iOutPos_;
			pSession->sendRB.MoveOutPos(dwNOBT);
			int in_after = pSession->sendRB.iInPos_;
			int out_after = pSession->sendRB.iOutPos_;

			if (out_after - in_after > 0 && out_after - in_after < 1000)
				__debugbreak();

#ifdef SENDRECV
			LOG_ASYNC(L"Send Complete Session ID : %llu\nMoveOutPos SendRB in %d -> %d, out %d -> %d NOBT : %llu", pSession->ullID, in_prev, in_after, out_prev, out_after, dwNOBT);
#endif
			InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);

			int iUseSize = pSession->sendRB.GetUseSize();
			if (iUseSize <= 0)
			{
				//printf("1\n");
				goto lb_next;
			}

			if (!pLanServer->SendPost(pSession))
			{
				//printf("2\n");
				pLanServer->ReleaseSession(pSession);
				goto lb_next;
			}
		}
	lb_next:;
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
			continue;

		if (!pLanServer->OnConnectionRequest())
		{
			closesocket(clientSock);
			continue;
		}
		++pLanServer->dwAcceptTPS;

		pSession = new Session;
		pSession->sock = clientSock;
		pSession->bSendingInProgress = FALSE;
		pSession->ullID = g_ullID;
		pSession->dwIoCount = 0;
		pSession->recvRB.iInPos_ = pSession->recvRB.iOutPos_ = 0;
		pSession->sendRB.iInPos_ = pSession->sendRB.iOutPos_ = 0;
		CreateIoCompletionPort((HANDLE)pSession->sock, pLanServer->hcp_, (ULONG_PTR)pSession, 0);

		AcquireSRWLockExclusive(&pLanServer->SessionUMapLock_);
		pLanServer->sessionUMap_.insert(std::pair<ULONGLONG, Session*>{g_ullID, pSession});
		ReleaseSRWLockExclusive(&pLanServer->SessionUMapLock_);
		++g_ullID;

		// onAccept();
		if (!pLanServer->RecvPost(pSession))
		{

		}
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

BOOL LanServer::OnRecv(ULONGLONG ullID, Packet* pPacket)
{
	ULONGLONG ullPayLoad;
	Packet sendPacket;
	(*pPacket) >> ullPayLoad;

	sendPacket << ullPayLoad;
	return SendPacket(ullID, &sendPacket);
}

BOOL LanServer::SendPacket(ULONGLONG ullID, Packet* pPacket)
{
	AcquireSRWLockShared(&SessionUMapLock_);
	auto iter = sessionUMap_.find(ullID);
	Session* pSession = iter->second;
	ReleaseSRWLockShared(&SessionUMapLock_);

	SHORT shHeader = pPacket->GetUsedDataSize();
	//LOG_ASYNC(L"SendPacket sendRB Enqeueue Before in : %d, out : %d", pSession->sendRB.iInPos_, pSession->sendRB.iOutPos_);
	pSession->sendRB.Enqueue((const char*)&shHeader, sizeof(shHeader));
	pSession->sendRB.Enqueue(pPacket->GetBufferPtr(), shHeader);
	//LOG_ASYNC(L"SendPacket sendRB Enqeueue After in : %d, out : %d", pSession->sendRB.iInPos_, pSession->sendRB.iOutPos_);
	if(pSession->sendRB.iInPos_ == 0 && pSession->sendRB.iOutPos_ == 0)
		__debugbreak();
	return SendPost(pSession);
}


BOOL LanServer::RecvPost(Session* pSession)
{
	DWORD dwRefCnt;
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
	InterlockedIncrement(&(pSession->dwIoCount));
#ifdef WILL_RECV
	LOG_ASYNC(L"RecvPost WSARecv Session ID : %llu", pSession->ullID);
#endif
	iRecvRet = WSARecv(pSession->sock, wsa, 2, nullptr, &flags, &(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			goto lb_release;

		InterlockedDecrement(&(pSession->dwIoCount));
		dwRefCnt = InterlockedDecrement(&(pSession->dwIoCount));

		if (dwRefCnt > 0)
			goto lb_release;

		if (dwErrCode == WSAECONNRESET)
			goto lb_disconnect;

		//logging
	lb_disconnect:
		return FALSE;
	}
lb_release:
	return TRUE;
}

BOOL LanServer::SendPost(Session* pSession)
{
	//LOG_ASYNC(L"Appear At SendPost");
	// ���� ���� TRUE�� �ٲ۴�. ���� TRUE���ٸ� ��ȯ���� TRUE�ϰ��̸� �׷��ٸ� ���� SEND �������̱� ������ �׳� ����������
	// �� ���ǹ��� ��ġ�� ���Ͽ� Out�� �ٲ��� ���������� ����ȴ�.
	// ������ SendPost ������ü�� Send�Ϸ����� �������� ��쿡�� in�� ��ġ�� SendPacket���� ���ؼ� �ٲ���� �ִ�.
	// iUseSize�� ���ϴ� ���������� DirectDequeueSize�� ���� �޶������ִ�.
	if (InterlockedExchange((LONG*)&pSession->bSendingInProgress, TRUE) == TRUE)
		return TRUE;

	WSABUF wsa[2];
	int iUseSize = pSession->sendRB.GetUseSize();
	int iDirectDeqSize = pSession->sendRB.DirectDequeueSize();
	int iBufLen = 0;
	int iSendRet;
	DWORD dwIoCount;

	// SendPacket���� in�� �Űܼ� UseSize�� 0���� Ŀ���������� Send�Ϸ������� �����ؼ� Out�� �ű�� �÷��� ���� Recv�Ϸ����� �����尡 ���� SendPost�� ������ �÷��׸� �����Ѱ�� UseSize�� 0�̳��´�.
	// ���⼭ flag�� �ٽ� FALSE�� �ٲپ����� �ʾƼ� ����߻�
	if (iUseSize == 0)
	{
		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		return TRUE;
	}
	
	if (iUseSize <= iDirectDeqSize)
	{
		//if (iUseSize != iDirectDeqSize)
		//	__debugbreak();

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
	dwIoCount = InterlockedIncrement(&pSession->dwIoCount);
#ifdef WILL_SEND 
	LOG_ASYNC(L"SendPost WSASend Session ID : %llu %d Bytes Send", pSession->ullID, iUseSize);
#endif
	iSendRet = WSASend(pSession->sock, wsa, iBufLen, nullptr, 0, &(pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			return TRUE;
		}
		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		InterlockedDecrement(&(pSession->dwIoCount));
		dwIoCount = InterlockedDecrement(&(pSession->dwIoCount));

		if (dwIoCount > 0)
		{
			return TRUE;
		}

		if (dwErrCode == WSAECONNRESET)
			goto lb_disconnect;

		LOG(L"Disconnect", ERR, TEXTFILE, L"WSASend() Fail Client Disconnect By ErrCode : %u", dwErrCode);
	lb_disconnect:
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
	LOG(L"DEBUG", TEXTFILE, CONSOLE, L"Delete Session Thread ID :%u Session ID : %llu", GetCurrentThreadId(), pSession->ullID);
#endif
	delete pSession;
	ReleaseSRWLockExclusive(&SessionUMapLock_);
}

