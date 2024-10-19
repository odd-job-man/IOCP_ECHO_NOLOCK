#include <WinSock2.h>

#include "CLockFreeQueue.h"

#include "RingBuffer.h"

#include "Session.h"
BOOL Session::Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx)
{
    sock_ = clientSock;
    bSendingInProgress_ = FALSE;
    id_.ullId = ((ullClientID << 16) ^ shIdx);
    lSendBufNum_ = 0;
    recvRB_.ClearBuffer();
    return TRUE;
}
