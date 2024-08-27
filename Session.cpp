#include "Session.h"

BOOL Session::Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx)
{
    sock = clientSock;
    bSendingInProgress = FALSE;
    id.ullId = ullClientID;
    id.sh[3] = shIdx;
    IoCnt = 0;
    recvRB.ClearBuffer();
    sendRB.ClearBuffer();
#ifdef IO_RET
    ullRecv = ullSend = 0;
#endif
    return TRUE;
}
