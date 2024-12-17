#include <WinSock2.h>
#include <WS2tcpip.h>
#include <locale>
#include <process.h>
#include "LanServer.h"
#include "Parser.h"
#include "Assert.h"
#include "Timer.h"
#pragma comment(lib,"LoggerMt.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib,"TextParser.lib")
#pragma comment(lib,"Winmm.lib")



template<typename T>
__forceinline T& IGNORE_CONST(const T& value)
{
	return const_cast<T&>(value);
}

LanServer::LanServer()
{
	std::locale::global(std::locale(""));
	char* pStart;
	char* pEnd;
	PARSER psr = CreateParser(L"ServerConfig.txt");

	WCHAR ipStr[16];
	GetValue(psr, L"BIND_IP", (PVOID*)&pStart, (PVOID*)&pEnd);
	unsigned long long stringLen = (pEnd - pStart) / sizeof(WCHAR);
	wcsncpy_s(ipStr, _countof(ipStr) - 1, (const WCHAR*)pStart, stringLen);
	// Null terminated String 으로 끝내야 InetPtonW쓸수잇음
	ipStr[stringLen] = 0;


	ULONG ip;
	InetPtonW(AF_INET, ipStr, &ip);
	GetValue(psr, L"BIND_PORT", (PVOID*)&pStart, nullptr);
	short SERVER_PORT = (short)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"IOCP_WORKER_THREAD", (PVOID*)&pStart, nullptr);
	IGNORE_CONST(IOCP_WORKER_THREAD_NUM_) = (DWORD)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"IOCP_ACTIVE_THREAD", (PVOID*)&pStart, nullptr);
	IGNORE_CONST(IOCP_ACTIVE_THREAD_NUM_) = (DWORD)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"IS_ZERO_BYTE_SEND", (PVOID*)&pStart, nullptr);
	int bZeroByteSend = _wtoi((LPCWSTR)pStart);

	GetValue(psr, L"SESSION_MAX", (PVOID*)&pStart, nullptr);
	IGNORE_CONST(maxSession_) = (LONG)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"TIME_OUT_MILLISECONDS", (PVOID*)&pStart, nullptr);
	IGNORE_CONST(TIME_OUT_MILLISECONDS_) = (LONG)_wtoi((LPCWSTR)pStart);

	GetValue(psr, L"TIME_OUT_CHECK_INTERVAL", (PVOID*)&pStart, nullptr);
	IGNORE_CONST(TIME_OUT_CHECK_INTERVAL_) = (ULONGLONG)_wtoi((LPCWSTR)pStart);

	WSADATA wsa;
	ASSERT_NON_ZERO_LOG(WSAStartup(MAKEWORD(2, 2), &wsa), L"WSAStartUp Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp OK!");

	hcp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, IOCP_ACTIVE_THREAD_NUM_);
	ASSERT_NULL_LOG(hcp_, "CreateIoCompletionPort Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Create IOCP OK!");

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	hListenSock_ = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_INVALID_SOCKET_LOG(hListenSock_, "MAKE Listen SOCKET Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET OK");

	// bind
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.S_un.S_addr = ip;
	serveraddr.sin_port = htons(SERVER_PORT);
	ASSERT_SOCKET_ERROR_LOG(bind(hListenSock_, (SOCKADDR*)&serveraddr, sizeof(serveraddr)),"bind Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind OK");

	ASSERT_SOCKET_ERROR_LOG(listen(hListenSock_, SOMAXCONN), "listen Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen() OK");

	linger linger;
	linger.l_linger = 0;
	linger.l_onoff = 1;
	ASSERT_ZERO_LOG(setsockopt(hListenSock_, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)),"Linger Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"linger() OK");

	if (bZeroByteSend == 1)
	{
		DWORD dwSendBufSize = 0;
		ASSERT_ZERO_LOG(setsockopt(hListenSock_, SOL_SOCKET, SO_SNDBUF, (char*)&dwSendBufSize, sizeof(dwSendBufSize)), "MAKE SNDBUF 0 Fail");
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"ZeroByte Send OK");
	}
	else
	{
		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"NO ZeroByte Send");
	}

	hIOCPWorkerThreadArr_ = new HANDLE[IOCP_WORKER_THREAD_NUM_];
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
	{
		hIOCPWorkerThreadArr_[i] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, CREATE_SUSPENDED, nullptr);
		ASSERT_ZERO_LOG(hIOCPWorkerThreadArr_[i], "MAKE WorkerThread Fail ErrCode");
	}
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE IOCP WorkerThread OK Num : %u!", si.dwNumberOfProcessors);

	// 상위 17비트를 못쓰고 상위비트가 16개 이하가 되는날에는 뻑나라는 큰그림이다.
	if (!CAddressTranslator::CheckMetaCntBits())
		__debugbreak();

	pSessionArr_ = new LanSession[maxSession_];
	for (int i = maxSession_ - 1; i >= 0; --i)
		DisconnectStack_.Push(i);

	hAcceptThread_ = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, CREATE_SUSPENDED, nullptr);
	ASSERT_ZERO_LOG(hAcceptThread_, "MAKE AcceptThread Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");

	Timer::Init();
}

void LanServer::SendPacket(ULONGLONG id, SmartPacket& sendPacket)
{
	LanSession* pSession = pSessionArr_ + LanSession::GET_SESSION_INDEX(id);
	LONG IoCnt = InterlockedIncrement(&pSession->IoCnt_);
	// 이미 RELEASE 진행중이거나 RELEASE된 경우
	if ((IoCnt & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE 완료후 다시 세션에 대한 초기화가 완료된경우 즉 재활용
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	sendPacket->SetHeader<Lan>();
	sendPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(sendPacket.GetPacket());
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void LanServer::SendPacket(ULONGLONG id, Packet* pPacket)
{
	LanSession* pSession = pSessionArr_ + LanSession::GET_SESSION_INDEX(id);
	LONG IoCnt = InterlockedIncrement(&pSession->IoCnt_);
	// 이미 RELEASE 진행중이거나 RELEASE된 경우
	if ((IoCnt & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE 완료후 다시 세션에 대한 초기화가 완료된경우 즉 재활용
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	pPacket->SetHeader<Lan>();
	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

void LanServer::Disconnect(ULONGLONG id)
{
	LanSession* pSession = pSessionArr_ + LanSession::GET_SESSION_INDEX(id);
	LONG IoCnt = InterlockedIncrement(&pSession->IoCnt_);


	// RELEASE진행중 혹은 진행완료
	if ((IoCnt & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE후 재활용까지 되엇을때
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// Disconnect 1회 제한
	if ((BOOL)InterlockedExchange((LONG*)&pSession->bDisconnectCalled_, TRUE) == TRUE)
	{
		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// 여기 도달햇다면 같은 세션에 대해서 RELEASE 조차 호출되지 않은상태임이 보장된다
	CancelIoEx((HANDLE)pSession->sock_, nullptr);

	// CancelIoEx호출로 인해서 RELEASE가 호출되엇어야 햇지만 위에서의 InterlockedIncrement 때문에 호출이 안된 경우 업보청산
	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
		ReleaseSession(pSession);
}

BOOL LanServer::RecvPost(LanSession* pSession)
{
	WSABUF wsa[2];
	wsa[0].buf = pSession->recvRB_.GetWriteStartPtr();
	wsa[0].len = pSession->recvRB_.DirectEnqueueSize();
	wsa[1].buf = pSession->recvRB_.Buffer_;
	wsa[1].len = pSession->recvRB_.GetFreeSize() - wsa[0].len;

	ZeroMemory(&pSession->recvOverlapped, sizeof(WSAOVERLAPPED));
	pSession->recvOverlapped.why = OVERLAPPED_REASON::RECV;
	DWORD flags = 0;
	InterlockedIncrement(&pSession->IoCnt_);
	int iRecvRet = WSARecv(pSession->sock_, wsa, 2, nullptr, &flags, (LPWSAOVERLAPPED) & (pSession->recvOverlapped), nullptr);
	if (iRecvRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, nullptr);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->IoCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
	return 0;
}

BOOL LanServer::SendPost(LanSession* pSession)
{
	DWORD dwBufferNum;
	while (1)
	{
		if (pSession->sendPacketQ_.GetSize() <= 0)
			return FALSE;

		// 현재 값을 TRUE로 바꾼다. 원래 TRUE엿다면 반환값이 TRUE일것이며 그렇다면 현재 SEND 진행중이기 때문에 그냥 빠저나간다
		// 이 조건문의 위치로 인하여 Out은 바뀌지 않을것임이 보장된다.
		// 하지만 SendPost 실행주체가 Send완료통지 스레드인 경우에는 in의 위치는 SendPacket으로 인해서 바뀔수가 있다.
		// iUseSize를 구하는 시점에서의 DirectDequeueSize의 값이 달라질수있다.
		if (InterlockedExchange((LONG*)&pSession->bSendingInProgress_, TRUE) == TRUE)
			return TRUE;

		// SendPacket에서 in을 옮겨서 UseSize가 0보다 커진시점에서 Send완료통지가 도착해서 Out을 옮기고 플래그 해제 Recv완료통지 스레드가 먼저 SendPost에 도달해 플래그를 선점한경우 UseSize가 0이나온다.
		// 여기서 flag를 다시 FALSE로 바꾸어주지 않아서 멈춤발생
		dwBufferNum = pSession->sendPacketQ_.GetSize();

		if (dwBufferNum <= 0)
			InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
		else
			break;
	}

	WSABUF wsa[50];
	DWORD i;
	for (i = 0; i < 50 && i < dwBufferNum; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
		wsa[i].buf = (char*)pPacket->pBuffer_;
		wsa[i].len = pPacket->GetUsedDataSize() + sizeof(Packet::LanHeader);
		pSession->pSendPacketArr_[i] = pPacket;
	}

	InterlockedExchange(&pSession->lSendBufNum_, i);
	InterlockedAdd64(&sendTPS_, i);
	InterlockedIncrement(&pSession->IoCnt_);
	ZeroMemory(&(pSession->sendOverlapped.overlapped), sizeof(WSAOVERLAPPED));
	pSession->sendOverlapped.why = OVERLAPPED_REASON::SEND;
	int iSendRet = WSASend(pSession->sock_, wsa, i, nullptr, 0, (LPWSAOVERLAPPED) & (pSession->sendOverlapped), nullptr);
	if (iSendRet == SOCKET_ERROR)
	{
		DWORD dwErrCode = WSAGetLastError();
		if (dwErrCode == WSA_IO_PENDING)
		{
			if (pSession->bDisconnectCalled_ == TRUE)
			{
				CancelIoEx((HANDLE)pSession->sock_, nullptr);
				return FALSE;
			}
			return TRUE;
		}

		InterlockedDecrement(&(pSession->IoCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

void LanServer::ReleaseSession(LanSession* pSession)
{
	if (InterlockedCompareExchange(&pSession->IoCnt_, LanSession::RELEASE_FLAG | 0, 0) != 0)
		return;

	// Release 될 Session의 직렬화 버퍼 정리
	for (LONG i = 0; i < pSession->lSendBufNum_; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}


	LONG size = pSession->sendPacketQ_.GetSize();
	for (LONG i = 0; i < size; ++i)
	{
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	closesocket(pSession->sock_);
	if (pSession->sendPacketQ_.GetSize() > 0)
		__debugbreak();

	// OnRelease와 idx 푸시 순서가 바뀌어서 JOB_OnRelease 도착 이전에 새로운 플레이어에 대해 JOB_On_ACCEPT가 중복으로 도착햇음
	OnRelease(pSession->id_);
	DisconnectStack_.Push((short)(pSession - pSessionArr_));
	InterlockedIncrement(&disconnectTPS_);
	InterlockedDecrement(&lSessionNum_);
}

void LanServer::RecvProc(LanSession* pSession, DWORD dwNumberOfBytesTransferred)
{
	using LanHeader = Packet::LanHeader;
	pSession->recvRB_.MoveInPos(dwNumberOfBytesTransferred);
	while (1)
	{
		LanHeader header;
		if (pSession->recvRB_.Peek((char*)&header, sizeof(LanHeader)) == 0)
			break;

		if (pSession->recvRB_.GetUseSize() < sizeof(LanHeader) + header.payloadLen_)
		{
			if (header.payloadLen_ > BUFFER_SIZE)
			{
				Disconnect(pSession->id_);
				return;
			}
			break;
		}

		pSession->recvRB_.MoveOutPos(sizeof(LanHeader));

		Packet* pPacket = PACKET_ALLOC(Lan);
		pSession->recvRB_.Dequeue(pPacket->GetPayloadStartPos<Lan>(), header.payloadLen_);
		pPacket->MoveWritePos(header.payloadLen_);
		memcpy(pPacket->pBuffer_, &header, sizeof(Packet::LanHeader));

		pSession->lastRecvTime = GetTickCount64();
		InterlockedIncrement64(&recvTPS_);
		OnRecv(pSession->id_, pPacket);
	}
	RecvPost(pSession);
}

void LanServer::SendProc(LanSession* pSession, DWORD dwNumberOfBytesTransferred)
{
	LONG sendBufNum = InterlockedExchange(&pSession->lSendBufNum_, 0);
	for (LONG i = 0; i < sendBufNum; ++i)
	{
		Packet* pPacket = pSession->pSendPacketArr_[i];
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	InterlockedExchange((LONG*)&pSession->bSendingInProgress_, FALSE);
	SendPost(pSession);
}

void LanServer::ProcessTimeOut()
{
	ULONGLONG currentTime = GetTickCount64();
	for (int i = 0; i < maxSession_; ++i)
	{
		LanSession* pSession = pSessionArr_ + i;

		__faststorefence();
		ULONGLONG id = pSession->id_;
		if ((pSession->IoCnt_ & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
			continue;

		if (currentTime < pSessionArr_[i].lastRecvTime + TIME_OUT_MILLISECONDS_)
			continue;

		Disconnect(id);
	}
}

unsigned __stdcall LanServer::AcceptThread(LPVOID arg)
{
	SOCKET clientSock;
	SOCKADDR_IN clientAddr;
	int addrlen = sizeof(clientAddr);
	LanServer* pLanServer = (LanServer*)arg;

	while (1)
	{
		clientSock = accept(pLanServer->hListenSock_, (SOCKADDR*)&clientAddr, &addrlen);
		InterlockedIncrement((LONG*)&pLanServer->acceptCounter_);

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

		InterlockedIncrement((LONG*)&pLanServer->lSessionNum_);

		short idx = pLanServer->DisconnectStack_.Pop().value();
		LanSession* pSession = pLanServer->pSessionArr_ + idx;
		pSession->Init(clientSock, pLanServer->ullIdCounter, idx);

		CreateIoCompletionPort((HANDLE)pSession->sock_, pLanServer->hcp_, (ULONG_PTR)pSession, 0);
		++pLanServer->ullIdCounter;

		InterlockedIncrement(&pSession->IoCnt_);
		InterlockedAnd(&pSession->IoCnt_, ~LanSession::RELEASE_FLAG);

		pLanServer->OnAccept(pSession->id_);
		pLanServer->RecvPost(pSession);

		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			pLanServer->ReleaseSession(pSession);
	}
	return 0;
}

unsigned __stdcall LanServer::IOCPWorkerThread(LPVOID arg)
{
	LanServer* pLanServer = (LanServer*)arg;
	while (1)
	{
		MYOVERLAPPED* pOverlapped = nullptr;
		DWORD dwNOBT = 0;
		LanSession* pSession = nullptr;
		bool bContinue = false;
		BOOL bGQCSRet = GetQueuedCompletionStatus(pLanServer->hcp_, &dwNOBT, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);
		do
		{
			if (!pOverlapped && !dwNOBT && !pSession)
				return 0;

			//정상종료
			if (bGQCSRet && dwNOBT == 0)
				break;

			if (!bGQCSRet && pOverlapped)
				break;

			switch (pOverlapped->why)
			{
			case OVERLAPPED_REASON::SEND:
				pLanServer->SendProc(pSession, dwNOBT);
				break;

			case OVERLAPPED_REASON::RECV:
				pLanServer->RecvProc(pSession, dwNOBT);
				break;

			case OVERLAPPED_REASON::TIMEOUT:
				pLanServer->ProcessTimeOut();
				bContinue = true;
				break;

			case OVERLAPPED_REASON::SEND_POST_FRAME: // 안씀
				break;

			case OVERLAPPED_REASON::SEND_ACCUM: // 안씀
				break;

			case OVERLAPPED_REASON::UPDATE:
				((UpdateBase*)(pSession))->Update();
				bContinue = true;
				break;

			case OVERLAPPED_REASON::POST:
				pLanServer->OnPost(pSession);
				bContinue = true;
				break;

			case OVERLAPPED_REASON::SEND_WORKER:
				pLanServer->SendPost(pSession);
				InterlockedExchange((LONG*)&pSession->bSendingAtWorker_, FALSE);
				break;

			default:
				__debugbreak();
			}

		} while (0);

		if (bContinue)
			continue;

		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
			pLanServer->ReleaseSession(pSession);
	}
	return 0;
}
