#include <stdio.h>
#include <crtdbg.h>
#include <process.h>
#include <WinSock2.h>
#include <windows.h>

#include "Logger.h"

#include "CLockFreeQueue.h"
#include "CLockFreeStack.h"
#include "RingBuffer.h"
#include "Session.h"
#include "IHandler.h"
#include "LanServer.h"
#include "Packet.h"

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"LoggerMt.lib")

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


#define RECV_COMPL 1000
#define SEND_COMPL 1001

#define LOGIN_PAYLOAD ((ULONGLONG)0x7fffffffffffffff)

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

	hListenSock_ = socket(AF_INET, SOCK_STREAM, 0);
	if (hListenSock_== INVALID_SOCKET)
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
	retval = bind(hListenSock_, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind OK");

	// listen
	retval = listen(hListenSock_, SOMAXCONN);
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
	setsockopt(hListenSock_, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"linger() OK");
#endif

#ifdef ZERO_BYTE_SEND
	DWORD dwSendBufSize = 0;
	setsockopt(hListenSock_, SOL_SOCKET, SO_SNDBUF, (char*)&dwSendBufSize, sizeof(dwSendBufSize));
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Zerobyte Send OK");
#endif

	hIOCPWorkerThreadArr_ = new HANDLE[si.dwNumberOfProcessors * 2];
	for (DWORD i = 0; i < si.dwNumberOfProcessors * 2; ++i)
	{
		hIOCPWorkerThreadArr_[i] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, 0, nullptr);
		if (!hIOCPWorkerThreadArr_[i])
		{
			LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE WorkerThread Fail ErrCode : %u", WSAGetLastError());
			__debugbreak();
		}
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE IOCP WorkerThread OK Num : %u!", si.dwNumberOfProcessors);

	// 상위 17비트를 못쓰고 상위비트가 16개 이하가 되는날에는 뻑나라는 큰그림이다.
	if (!CAddressTranslator::CheckMetaCntBits())
		__debugbreak();

	pSessionArr_ = new Session[dwMaxSession];
	lMaxSession_ = dwMaxSession;

	for (int i = dwMaxSession - 1; i >= 0; --i)
	{
		DisconnectStack_.Push(i);
	}

	hAcceptThread_ = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, nullptr);
	if (!hAcceptThread_)
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread Fail ErrCode : %u", WSAGetLastError());
		__debugbreak();
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");
	return 0;
}

__forceinline void ClearPacket(Session* pSession)
{
	DWORD dwSendBufNum = _InterlockedExchange(&pSession->lSendBufNum, 0);
	for (DWORD i = 0; i < dwSendBufNum; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			Packet::Free(pPacket);
		}
	}
}

__forceinline void ReleaseSendFailPacket(Session* pSession)
{
	DWORD dwSendBufNum = _InterlockedExchange(&pSession->lSendBufNum, 0);

	for (DWORD i = 0; i < dwSendBufNum; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			Packet::Free(pPacket);
		}
	}

	for (DWORD i = 0; pSession->sendPacketQ.num_; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ.Dequeue().value();
		if (pPacket->DecrementRefCnt() == 0)
		{
			Packet::Free(pPacket);
		}
	}
}


unsigned __stdcall LanServer::IOCPWorkerThread(LPVOID arg)
{
	LanServer* pLanServer = (LanServer*)arg;
	while (1)
	{
		WSAOVERLAPPED* pOverlapped = nullptr;
		DWORD dwNOBT = 0;
		Session* pSession = nullptr;
		BOOL bGQCSRet = GetQueuedCompletionStatus(pLanServer->hcp_, &dwNOBT, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);
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
			pLanServer->ReleaseSession(pSession);
	}
}

unsigned __stdcall LanServer::AcceptThread(LPVOID arg)
{
	SOCKET clientSock;
	SOCKADDR_IN clientAddr;
	int addrlen;
	LanServer* pLanServer = (LanServer*)arg;
	addrlen = sizeof(clientAddr);

	while (1)
	{
		clientSock = accept(pLanServer->hListenSock_, (SOCKADDR*)&clientAddr, &addrlen);

		if (clientSock == INVALID_SOCKET)
		{
			DWORD dwErrCode = WSAGetLastError();
			if (dwErrCode != WSAEINTR && dwErrCode != WSAENOTSOCK)
			{
				__debugbreak();
			}
			return 0;
		}


		if (!pLanServer->OnConnectionRequest())
		{
			closesocket(clientSock);
			continue;
		}

		InterlockedIncrement((LONG*)&pLanServer->lAcceptTPS_);
		InterlockedIncrement((LONG*)&pLanServer->lSessionNum_);

		short idx = pLanServer->DisconnectStack_.Pop().value();
		Session* pSession = pLanServer->pSessionArr_ + idx;
		pSession->Init(clientSock, g_ullID, idx);

		CreateIoCompletionPort((HANDLE)pSession->sock, pLanServer->hcp_, (ULONG_PTR)pSession, 0);
		++g_ullID;

		InterlockedIncrement(&pSession->IoCnt);
		pLanServer->OnAccept(pSession->id);
		pLanServer->RecvPost(pSession);
		if (InterlockedDecrement(&pSession->IoCnt) == 0)
			pLanServer->ReleaseSession(pSession);

	}
}

BOOL LanServer::OnConnectionRequest()
{
	if (lSessionNum_ + 1 >= lMaxSession_)
		return FALSE;

	return TRUE;
}

void* LanServer::OnAccept(ID id)
{
	SmartPacket smartPacket;
	*smartPacket.GetPacket() << LOGIN_PAYLOAD;
	SendPacket(id, smartPacket);
	return nullptr;
}

void LanServer::OnRecv(ID id, Packet* pPacket)
{
	ULONGLONG ullPayLoad;
	*pPacket >> ullPayLoad;

	SmartPacket sendPacket;
	*sendPacket.GetPacket() << ullPayLoad;
	SendPacket(id, sendPacket);
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

void LanServer::Stop()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);

	closesocket(hListenSock_);
	WaitForSingleObject(hAcceptThread_, INFINITE);

	for (int i = 0; i < lMaxSession_; ++i)
	{
		CancelIoEx((HANDLE)pSessionArr_[i].sock, nullptr);
	}

	while (InterlockedExchange(&lSessionNum_, lSessionNum_) > 0);

	for (DWORD i = 0; i < si.dwNumberOfProcessors * 2; ++i)
	{
		PostQueuedCompletionStatus(hcp_, 0, 0, 0);
	}

	WaitForMultipleObjects(si.dwNumberOfProcessors * 2, hIOCPWorkerThreadArr_, TRUE, INFINITE);
	for (DWORD i = 0; i < si.dwNumberOfProcessors * 2; ++i)
	{
		CloseHandle(hIOCPWorkerThreadArr_[i]);
	}
	delete[] hIOCPWorkerThreadArr_;

	for (DWORD i = 0; i < si.dwNumberOfProcessors * 2; ++i)
	{
		if (InterlockedExchange((LONG*)&pSessionArr_[i].bUsing, pSessionArr_[i].bUsing) == TRUE)
			__debugbreak();
	}
	delete[] pSessionArr_;
	CloseHandle(hcp_);
	WSACleanup();
}

void LanServer::SendPacket(ID id, SmartPacket& sendPacket)
{
	Session* pSession = pSessionArr_ + GET_SESSION_INDEX(id);
	*(NET_HEADER*)sendPacket.GetPacket()->pBuffer_ = sendPacket.GetPacket()->GetUsedDataSize();
	sendPacket.GetPacket()->IncreaseRefCnt();
	pSession->sendPacketQ.Enqueue(sendPacket.GetPacket());
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
	iRecvRet = WSARecv(pSession->sock, wsa, 2, nullptr, &flags, &(pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		InterlockedDecrement(&(pSession->IoCnt));
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
	DWORD dwBufferNum = pSession->sendPacketQ.num_;
	if (dwBufferNum == 0)
	{
		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		return TRUE;
	}

	WSABUF wsa[50];
	DWORD i;
	for (i = 0; i < 50 && i < dwBufferNum; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ.Dequeue().value();
		wsa[i].buf = (char*)pPacket->pBuffer_;
		wsa[i].len = pPacket->GetUsedDataSize() + sizeof(NET_HEADER);
		pSession->pSendPacketArr[i] = pPacket;
	}

	InterlockedExchange(&pSession->lSendBufNum, i);
	InterlockedAdd(&lSendTPS_, i);
	InterlockedIncrement(&pSession->IoCnt);
	ZeroMemory(&(pSession->sendOverlapped), sizeof(WSAOVERLAPPED));
	int iSendRet = WSASend(pSession->sock, wsa, i, nullptr, 0, &(pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
			return TRUE;

		InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
		InterlockedDecrement(&(pSession->IoCnt));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

void LanServer::ReleaseSession(Session* pSession)
{
	ReleaseSendFailPacket(pSession);
	closesocket(pSession->sock);
	InterlockedExchange((LONG*)&pSession->bUsing, FALSE);
	if (pSession->sendPacketQ.num_ > 0)
		__debugbreak();

	DisconnectStack_.Push((short)(pSession - pSessionArr_));
	InterlockedIncrement(&lDisconnectTPS_);
	InterlockedDecrement(&lSessionNum_);
}

void LanServer::RecvProc(Session* pSession, int numberOfByteTransferred)
{
	SHORT shHeader;
	Packet* pPacket = Packet::Alloc();
	pSession->recvRB.MoveInPos(numberOfByteTransferred);
	while (1)
	{
		if (pSession->recvRB.Peek((char*)&shHeader, sizeof(shHeader)) == 0)
			break;

		if (pSession->recvRB.GetUseSize() < sizeof(shHeader) + shHeader)
			break;

		pSession->recvRB.Dequeue(pPacket->GetBufferPtr(), sizeof(shHeader) + shHeader);
		pPacket->MoveWritePos(sizeof(shHeader) + shHeader);
		pPacket->MoveReadPos(sizeof(shHeader));
		OnRecv(pSession->id, pPacket);
		pPacket->Clear();
		InterlockedIncrement(&lRecvTPS_);
	}
	Packet::Free(pPacket);
	RecvPost(pSession);
}

void LanServer::SendProc(Session* pSession, DWORD dwNumberOfBytesTransferred)
{
	ClearPacket(pSession);
	InterlockedExchange((LONG*)&pSession->bSendingInProgress, FALSE);
	if (pSession->sendPacketQ.num_ > 0)
		SendPost(pSession);
}






