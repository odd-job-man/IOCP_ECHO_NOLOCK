
#include <WinSock2.h>

#include "CLockFreeQueue.h"
#include "CLockFreeStack.h"
#include "RingBuffer.h"
#include "Session.h"
#include "IHandler.h"
#include "LanServer.h"

#include "Packet.h"
#include "CLockFreeObjectPool.h"

CLockFreeObjectPool<Packet, false> g_pool;

Packet* Packet::Alloc()
{
	Packet* pPacket = g_pool.Alloc();
	pPacket->Clear();
	return pPacket;
}

void Packet::Free(Packet* pPacket)
{
	g_pool.Free(pPacket);
}
