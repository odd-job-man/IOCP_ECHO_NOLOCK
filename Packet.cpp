#include <windows.h>
#include "Packet.h"
#include "FreeList.h"

static FreeList freeList;

void InitProc(void* pData)
{
    Packet* pPacket = (Packet*)pData;
    pPacket->front_ = Packet::NET_HEADER_SIZE;
    pPacket->rear_ = Packet::NET_HEADER_SIZE;
    pPacket->pBuffer_ = new char[Packet::DEFAULT_SIZE];
}

Packet* Packet::Alloc()
{
    Packet* pPacket = (Packet*)::Alloc(&freeList);
    pPacket->Clear();
    return pPacket;
}

void Packet::MemPoolInit()
{
    ::Init(&freeList, sizeof(Packet), FALSE, InitProc, NULL);
}

void Packet::Free(Packet* pPacket)
{
    ::Free(&freeList, pPacket);
}

void Packet::ReleasePacketPool()
{
    ::Clear(&freeList);
}
