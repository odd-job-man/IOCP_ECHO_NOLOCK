#include <WinSock2.h>
#include <WS2tcpip.h>
#include <locale>
#include <process.h>
#include "LanServer.h"
#include "Parser.h"
#include "Assert.h"
#include "Scheduler.h"
#include "DBWriteThreadBase.h"
#pragma comment(lib,"LoggerMt.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib,"TextParser.lib")
#pragma comment(lib,"Winmm.lib")



template<typename T>
__forceinline T& IGNORE_CONST(const T& value)
{
	return const_cast<T&>(value);
}

LanServer::LanServer(const WCHAR* pConfigFileName)
	:hShutDownEvent_{ CreateEvent(NULL,FALSE,FALSE,NULL) }
{
	std::locale::global(std::locale(""));
	char* pStart;
	char* pEnd;
	PARSER psr = CreateParser(pConfigFileName);

	WCHAR ipStr[16];
	GetValue(psr, L"BIND_IP", (PVOID*)&pStart, (PVOID*)&pEnd);
	unsigned long long stringLen = (pEnd - pStart) / sizeof(WCHAR);
	wcsncpy_s(ipStr, _countof(ipStr) - 1, (const WCHAR*)pStart, stringLen);
	// Null terminated String ���� ������ InetPtonW��������
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
	ASSERT_NULL_LOG(hcp_, L"CreateIoCompletionPort Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Create IOCP OK!");

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	hListenSock_ = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_INVALID_SOCKET_LOG(hListenSock_, L"MAKE Listen SOCKET Fail");
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
	ASSERT_NON_ZERO_LOG(setsockopt(hListenSock_, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)),"Linger Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"linger() OK");

	if (bZeroByteSend == 1)
	{
		DWORD dwSendBufSize = 0;
		ASSERT_NON_ZERO_LOG(setsockopt(hListenSock_, SOL_SOCKET, SO_SNDBUF, (char*)&dwSendBufSize, sizeof(dwSendBufSize)), "MAKE SNDBUF 0 Fail");
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

	// ���� 17��Ʈ�� ������ ������Ʈ�� 16�� ���ϰ� �Ǵ³����� ������� ū�׸��̴�.
	ASSERT_FALSE_LOG(CAddressTranslator::CheckMetaCntBits(), L"LockFree 17bits Over");

	pSessionArr_ = new LanSession[maxSession_];
	for (int i = maxSession_ - 1; i >= 0; --i)
		DisconnectStack_.Push(i);

	hAcceptThread_ = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, CREATE_SUSPENDED, nullptr);
	ASSERT_ZERO_LOG(hAcceptThread_, L"MAKE AcceptThread Fail");
	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");

	Scheduler::Init();
}

void LanServer::SendPacket(ULONGLONG id, SmartPacket& sendPacket)
{
	LanSession* pSession = pSessionArr_ + LanSession::GET_SESSION_INDEX(id);
	LONG IoCnt = InterlockedIncrement(&pSession->refCnt_);
	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	sendPacket->SetHeader<Lan>();
	sendPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(sendPacket.GetPacket());
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

void LanServer::SendPacket(ULONGLONG id, Packet* pPacket)
{
	LanSession* pSession = pSessionArr_ + LanSession::GET_SESSION_INDEX(id);
	LONG IoCnt = InterlockedIncrement(&pSession->refCnt_);
	// �̹� RELEASE �������̰ų� RELEASE�� ���
	if ((IoCnt & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE �Ϸ��� �ٽ� ���ǿ� ���� �ʱ�ȭ�� �Ϸ�Ȱ�� �� ��Ȱ��
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	pPacket->SetHeader<Lan>();
	pPacket->IncreaseRefCnt();
	pSession->sendPacketQ_.Enqueue(pPacket);
	SendPost(pSession);

	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

void LanServer::Disconnect(ULONGLONG id)
{
	LanSession* pSession = pSessionArr_ + LanSession::GET_SESSION_INDEX(id);
	LONG IoCnt = InterlockedIncrement(&pSession->refCnt_);

	// RELEASE������ Ȥ�� ����Ϸ�
	if ((IoCnt & LanSession::RELEASE_FLAG) == LanSession::RELEASE_FLAG)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// RELEASE�� ��Ȱ����� �Ǿ�����
	if (id != pSession->id_)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// Disconnect 1ȸ ����
	if ((BOOL)InterlockedExchange((LONG*)&pSession->bDisconnectCalled_, TRUE) == TRUE)
	{
		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			ReleaseSession(pSession);
		return;
	}

	// ���� �����޴ٸ� ���� ���ǿ� ���ؼ� RELEASE ���� ȣ����� ������������ ����ȴ�
	CancelIoEx((HANDLE)pSession->sock_, nullptr);

	// CancelIoExȣ��� ���ؼ� RELEASE�� ȣ��Ǿ���� ������ �������� InterlockedIncrement ������ ȣ���� �ȵ� ��� ����û��
	if (InterlockedDecrement(&pSession->refCnt_) == 0)
		ReleaseSession(pSession);
}

void LanServer::WaitUntilShutDown()
{
	WaitForSingleObject(hShutDownEvent_, INFINITE);
	ShutDown();
}

const WCHAR* LanServer::GetSessionIP(ULONGLONG id)
{
	return (pSessionArr_ + LanSession::GET_SESSION_INDEX(id))->ip_;
}

const USHORT LanServer::GetSessionPort(ULONGLONG id)
{
	return (pSessionArr_ + LanSession::GET_SESSION_INDEX(id))->port_;
}

void LanServer::ShutDown()
{
	// ��Ŀ�����忡�� ȣ���Ѱ�� �ȵ�, ��Ŀ������ RequestShutDown�� ȣ���ؾ���
	HANDLE hDebug = GetCurrentThread();
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
	{
		if (hIOCPWorkerThreadArr_[i] == hDebug)
		{
			LOG(L"ERROR", ERR, CONSOLE, L"WORKER Call Shutdown Must Have To Call RequestShutDown", Packet::packetPool_.capacity_, Packet::packetPool_.size_);
			LOG(L"ERROR", ERR, TEXTFILE, L"WORKER Call Shutdown Must Have To Call RequestShutDown", Packet::packetPool_.capacity_, Packet::packetPool_.size_);
			__debugbreak();
		}
	}
	// ���������� �ݾƼ� Accept�� ���´�
	closesocket(hListenSock_);
	WaitForSingleObject(hAcceptThread_, INFINITE);
	CloseHandle(hAcceptThread_);

	//���� 0�ɶ����� ������
	while (InterlockedXor(&lSessionNum_, 0) != 0)
	{
		for (int i = 0; i < maxSession_; ++i)
		{
			CancelIoEx((HANDLE)pSessionArr_[i].sock_, nullptr);
			InterlockedExchange((LONG*)&pSessionArr_[i].bDisconnectCalled_, TRUE);
		}
	}

	// ���̻� PQCS�� ������ �����Ƿ� UpdateBase* �� PQCS�� ��°��� �������� Timer�����带 �����Ѵ�
	// ���� ��������, �ݼ����� ������ ���� Ÿ�̸ӽ����带 �����Ű�� �ڵ��� �ݴ��� Wait_Failed�� ���� ����ó���� �����������μ� �ߺ�ȣ���� ����Ѵ�
	// �ֳ��ϸ� ������ Ȥ�� �ݼ��� ���� �ϳ��� �մ� ������ ��쿡 �����ڵ带 ���� ���ؼ���
	Scheduler::Release_SchedulerThread();

	// ������ DB� ���� �ܿ����� ó���� PQCS���� ���⼭ ���
	OnLastTaskBeforeAllWorkerThreadEndBeforeShutDown();

	// ��Ŀ�����带 �����ϱ����� PQCS�� ��� ����Ѵ�
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
		PostQueuedCompletionStatus(hcp_, 0, 0, 0);

	WaitForMultipleObjects(IOCP_WORKER_THREAD_NUM_, hIOCPWorkerThreadArr_, TRUE, INFINITE);
	OnResourceCleanAtShutDown();

	CloseHandle(hcp_);
	for (DWORD i = 0; i < IOCP_WORKER_THREAD_NUM_; ++i)
		CloseHandle(hIOCPWorkerThreadArr_[i]);
	delete[] pSessionArr_;
	CloseHandle(hShutDownEvent_);
}

void LanServer::RequestShutDown()
{
	SetEvent(hShutDownEvent_);
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
	InterlockedIncrement(&pSession->refCnt_);
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

		InterlockedDecrement(&(pSession->refCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

BOOL LanServer::SendPost(LanSession* pSession)
{
	DWORD dwBufferNum;
	while (1)
	{
		if (pSession->sendPacketQ_.GetSize() <= 0)
			return FALSE;

		// ���� ���� TRUE�� �ٲ۴�. ���� TRUE���ٸ� ��ȯ���� TRUE�ϰ��̸� �׷��ٸ� ���� SEND �������̱� ������ �׳� ����������
		// �� ���ǹ��� ��ġ�� ���Ͽ� Out�� �ٲ��� ���������� ����ȴ�.
		// ������ SendPost ������ü�� Send�Ϸ����� �������� ��쿡�� in�� ��ġ�� SendPacket���� ���ؼ� �ٲ���� �ִ�.
		// iUseSize�� ���ϴ� ���������� DirectDequeueSize�� ���� �޶������ִ�.
		if (InterlockedExchange((LONG*)&pSession->bSendingInProgress_, TRUE) == TRUE)
			return TRUE;

		// SendPacket���� in�� �Űܼ� UseSize�� 0���� Ŀ���������� Send�Ϸ������� �����ؼ� Out�� �ű�� �÷��� ���� Recv�Ϸ����� �����尡 ���� SendPost�� ������ �÷��׸� �����Ѱ�� UseSize�� 0�̳��´�.
		// ���⼭ flag�� �ٽ� FALSE�� �ٲپ����� �ʾƼ� ����߻�
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
#pragma warning(disable : 26815)
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
#pragma warning(default: 26815)
		wsa[i].buf = (char*)pPacket->pBuffer_;
		wsa[i].len = pPacket->GetUsedDataSize() + sizeof(Packet::LanHeader);
		pSession->pSendPacketArr_[i] = pPacket;
	}

	InterlockedExchange(&pSession->lSendBufNum_, i);
	InterlockedAdd64(&sendTPS_, i);
	InterlockedIncrement(&pSession->refCnt_);
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

		InterlockedDecrement(&(pSession->refCnt_));
		if (dwErrCode == WSAECONNRESET)
			return FALSE;

		LOG(L"Disconnect", ERR, TEXTFILE, L"Client Disconnect By ErrCode : %u", dwErrCode);
		return FALSE;
	}
	return TRUE;
}

void LanServer::ReleaseSession(LanSession* pSession)
{
	if (InterlockedCompareExchange(&pSession->refCnt_, LanSession::RELEASE_FLAG | 0, 0) != 0)
		return;

	// TimeOut������ �̸� ����
	pSession->lastRecvTime = 0;

	// Release �� Session�� ����ȭ ���� ����
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
#pragma warning(disable : 26815)
		Packet* pPacket = pSession->sendPacketQ_.Dequeue().value();
#pragma warning(default : 26815)
		if (pPacket->DecrementRefCnt() == 0)
		{
			PACKET_FREE(pPacket);
		}
	}

	closesocket(pSession->sock_);
	if (pSession->sendPacketQ_.GetSize() > 0)
		__debugbreak();

	// OnRelease�� idx Ǫ�� ������ �ٲ� JOB_OnRelease ���� ������ ���ο� �÷��̾ ���� JOB_On_ACCEPT�� �ߺ����� ��������
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

		WCHAR ip[16];
		InetNtop(AF_INET, &clientAddr.sin_addr, ip, _countof(ip));
		USHORT port = ntohs(clientAddr.sin_port);

		if (!pLanServer->OnConnectionRequest(ip, port))
		{
			closesocket(clientSock);
			continue;
		}

		// maxSession ��ŭ�����ؼ� �ε��� �ڸ������� ��������
		auto&& opt = pLanServer->DisconnectStack_.Pop();
		if (!opt.has_value())
		{
			closesocket(clientSock);
			continue;
		}
		short idx = opt.value();

		InterlockedIncrement((LONG*)&pLanServer->lSessionNum_);
		LanSession* pSession = pLanServer->pSessionArr_ + idx;
		pSession->Init(clientSock, pLanServer->ullIdCounter, idx);

		CreateIoCompletionPort((HANDLE)pSession->sock_, pLanServer->hcp_, (ULONG_PTR)pSession, 0);
		++pLanServer->ullIdCounter;

		// ip�� ��Ʈ�� ���ǿ� ����
		memcpy(pSession->ip_, ip, sizeof(WCHAR) * 16);
		pSession->port_ = port;

		InterlockedIncrement(&pSession->refCnt_);
		InterlockedAnd(&pSession->refCnt_, ~LanSession::RELEASE_FLAG);

		pLanServer->OnAccept(pSession->id_);
		pLanServer->RecvPost(pSession);

		if (InterlockedDecrement(&pSession->refCnt_) == 0)
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

			//��������
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
				bContinue = true;
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

			case OVERLAPPED_REASON::CONNECT: // �Ⱦ� (���� Ŭ�󿡼���)
				break;

			case OVERLAPPED_REASON::DB_WRITE:
				((DBWriteThreadBase*)pSession)->ProcessDBWrite();
				bContinue = true;
				break;

			default:
				__debugbreak();
			}

		} while (0);

		if (bContinue)
			continue;

		if (InterlockedDecrement(&pSession->refCnt_) == 0)
			pLanServer->ReleaseSession(pSession);
	}
	return 0;
}
