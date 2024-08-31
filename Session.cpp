#include <WinSock2.h>
#include "Session.h"

BOOL Session::Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx)
{
    sock = clientSock;
    bSendingInProgress = FALSE;
    _InterlockedExchange((LONG*)&bUsing, TRUE);
    MAKE_SESSION_INDEX(id, ullClientID, shIdx);
    IoCnt = 0;
    lSendBufNum = 0;
    recvRB.ClearBuffer();
    sendRB.ClearBuffer();
#ifdef IO_RET
    ullRecv = ullSend = 0;
#endif
    return TRUE;
}
