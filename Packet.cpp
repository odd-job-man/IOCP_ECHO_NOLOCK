#include <WinSock2.h>
#include <windows.h>
#include "FreeList.h"
#include "Packet.h"
#include "Session.h"
#include "IHandler.h"
#include "Stack.h"
#include "LanServer.h"

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
