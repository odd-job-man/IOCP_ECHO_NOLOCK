#include "Session.h"

BOOL Session::Init(SOCKET clientSock, ULONGLONG ullClientID, SHORT shIdx)
{
    sock = clientSock;
    bSendingInProgress = FALSE;
    bUsing = TRUE;
    MAKE_SESSION_INDEX(id, ullClientID, shIdx);
    IoCnt = 0;
    recvRB.ClearBuffer();
    sendRB.ClearBuffer();
#ifdef IO_RET
    ullRecv = ullSend = 0;
#endif
    return TRUE;
}
