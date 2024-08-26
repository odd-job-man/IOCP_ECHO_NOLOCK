#include "Session.h"

BOOL Session::Init(SOCKET clientSock, ULONGLONG ullClientID)
{
    sock = clientSock;
    bSendingInProgress = FALSE;
    ullID = ullClientID;
    IoCnt = 0;
    recvRB.ClearBuffer();
    sendRB.ClearBuffer();
    return TRUE;
}
