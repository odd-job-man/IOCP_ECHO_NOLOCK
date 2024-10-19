//#include <Winsock2.h>
//#include <windows.h>
//#include <process.h>
//#include "CLockFreeQueue.h"
//#include "RingBuffer.h"
//#include "Session.h"
//#include "IHandler.h"
//#include "NetServer.h"
//#include "Logger.h"
//#include "Packet.h"
//#define SERVERPORT 6000
//
//#define LINGER
//#define ZERO_BYTE_SEND
//
//BOOL NetServer::Start(DWORD dwMaxSession)
//{
//	int retval;
//	WSADATA wsa;
//	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
//	{
//		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp Fail ErrCode : %u", WSAGetLastError());
//		__debugbreak();
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"WSAStartUp OK!");
//	// NOCT에 0들어가면 논리프로세서 수만큼을 설정함
//	hcp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
//	if (!hcp_)
//	{
//		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"CreateIoCompletionPort Fail ErrCode : %u", WSAGetLastError());
//		__debugbreak();
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Create IOCP OK!");
//
//	SYSTEM_INFO si;
//	GetSystemInfo(&si);
//
//	hListenSock_ = socket(AF_INET, SOCK_STREAM, 0);
//	if (hListenSock_ == INVALID_SOCKET)
//	{
//		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET Fail ErrCode : %u", WSAGetLastError());
//		__debugbreak();
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE Listen SOCKET OK");
//
//	// bind
//	SOCKADDR_IN serveraddr;
//	ZeroMemory(&serveraddr, sizeof(serveraddr));
//	serveraddr.sin_family = AF_INET;
//	serveraddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
//	serveraddr.sin_port = htons(SERVERPORT);
//	retval = bind(hListenSock_, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
//	if (retval == SOCKET_ERROR)
//	{
//		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind Fail ErrCode : %u", WSAGetLastError());
//		__debugbreak();
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"bind OK");
//
//	// listen
//	retval = listen(hListenSock_, SOMAXCONN);
//	if (retval == SOCKET_ERROR)
//	{
//		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen Fail ErrCode : %u", WSAGetLastError());
//		__debugbreak();
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"listen() OK");
//
//#ifdef LINGER
//	linger linger;
//	linger.l_linger = 0;
//	linger.l_onoff = 1;
//	setsockopt(hListenSock_, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"linger() OK");
//#endif
//
//#ifdef ZERO_BYTE_SEND
//	DWORD dwSendBufSize = 0;
//	setsockopt(hListenSock_, SOL_SOCKET, SO_SNDBUF, (char*)&dwSendBufSize, sizeof(dwSendBufSize));
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"Zerobyte Send OK");
//#endif
//
//	hIOCPWorkerThreadArr_ = new HANDLE[si.dwNumberOfProcessors * 2];
//	for (DWORD i = 0; i < si.dwNumberOfProcessors * 2; ++i)
//	{
//		hIOCPWorkerThreadArr_[i] = (HANDLE)_beginthreadex(NULL, 0, IOCPWorkerThread, this, 0, nullptr);
//		if (!hIOCPWorkerThreadArr_[i])
//		{
//			LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE WorkerThread Fail ErrCode : %u", WSAGetLastError());
//			__debugbreak();
//		}
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE IOCP WorkerThread OK Num : %u!", si.dwNumberOfProcessors);
//
//	// 상위 17비트를 못쓰고 상위비트가 16개 이하가 되는날에는 뻑나라는 큰그림이다.
//	if (!CAddressTranslator::CheckMetaCntBits())
//		__debugbreak();
//
//	pSessionArr_ = new Session[dwMaxSession];
//	lMaxSession_ = dwMaxSession;
//
//	for (int i = dwMaxSession - 1; i >= 0; --i)
//	{
//		DisconnectStack_.Push(i);
//	}
//
//	hAcceptThread_ = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, nullptr);
//	if (!hAcceptThread_)
//	{
//		LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread Fail ErrCode : %u", WSAGetLastError());
//		__debugbreak();
//	}
//	LOG(L"ONOFF", SYSTEM, TEXTFILE, L"MAKE AccpetThread OK!");
//	return 0;
//}
//
//void NetServer::SendPacket(ID id, SmartPacket& sendPacket)
//{
//	Session* pSession = pSessionArr_ + Session::GET_SESSION_INDEX(id);
//	long IoCnt = InterlockedIncrement(&pSession->IoCnt_);
//
//	if ((IoCnt & Session::RELEASE_FLAG) == Session::RELEASE_FLAG)
//	{
//		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
//		{
//			ReleaseSession(pSession);
//			return;
//		}
//	}
//
//	// 세션에 대한 초기화가 완료된경우 재활용 
//	if (id.ullId != pSession->id_.ullId)
//	{
//		if (InterlockedDecrement(&pSession->IoCnt_) == 0)
//		{
//			ReleaseSession(pSession);
//			return;
//		}
//	}
//
//	// 이부분 다름
//	sendPacket->SetHeader<Net>();
//	sendPacket->IncreaseRefCnt();
//	pSession->sendPacketQ_.Enqueue(sendPacket.GetPacket());
//	SendPost(pSession);
//	if (InterlockedDecrement(&pSession->IoCnt_) == 0)
//	{
//		ReleaseSession(pSession);
//	}
//}
//
//void NetServer::RecvProc(Session* pSession, int numberOfBytesTransferred)
//{
//	using NetHeader = Packet::NetHeader;
//
//	NetHeader header;
//	Packet* pPacket = Packet::Alloc<Net>();
//	pSession->recvRB_.MoveInPos(numberOfBytesTransferred);
//	while (1)
//	{
//		if (pSession->recvRB_.Peek((char*)&header, sizeof(NetHeader)) == 0)
//			break;
//
//		if (pSession->recvRB_.GetUseSize() < sizeof(NetHeader) + header.payloadLen_)
//			break;
//
//		pSession->recvRB_.Dequeue(pPacket->GetPayloadStartPos<Net>(), sizeof(NetHeader) + header.payloadLen_);
//		pPacket->MoveWritePos(sizeof(NetHeader) + header.payloadLen_);
//		pPacket->MoveReadPos(sizeof(NetHeader));
//		OnRecv(pSession->id_, pPacket);
//		pPacket->Clear<Lan>();
//		InterlockedIncrement(&lRecvTPS_);
//	}
//
//}
