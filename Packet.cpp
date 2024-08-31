#include <WinSock2.h>
#include <windows.h>
#include "FreeList.h"
#include "Session.h"
#include "IHandler.h"
#include "Stack.h"
#include "LanServer.h"
#include "Packet.h"

static FreeList<Packet> freeList{ false,0 };

Packet* Packet::Alloc()
{
    Packet* pPacket = freeList.Alloc();
    pPacket->Clear();
    return pPacket;
}

void Packet::Free(Packet* pPacket)
{
    freeList.Free(pPacket);
}

void Packet::ReleasePacketPool()
{
    freeList.ReleaseAll();
}
