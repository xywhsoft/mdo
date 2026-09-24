#include <string.h>

#include "internal.h"
#include "../../include/mdo/sessions.h"

static bool MdoApiTodoId(xstrview View, char* Output, size_t Capacity)
{
    size_t i;
    if ( View.Size == 0u || View.Size >= Capacity || View.Data[0] == '.' )
        return false;
    for ( i = 0u; i < View.Size; ++i ) {
        unsigned char Byte = (unsigned char)View.Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             Byte == '.' ) continue;
        return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

bool MdoApiTodoRoute(MdoApiContext* Context)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoSession* Session;
    xwork_error Error;
    xvalue* Data = NULL;
    if ( Context->ParamCount != 2u ||
         !MdoApiTodoId(Context->Params[0], ProjectId,
            sizeof(ProjectId)) ||
         !MdoApiTodoId(Context->Params[1], SessionId,
            sizeof(SessionId)) )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return MdoApiReplyError(Context, 404u,
        "session_not_found", "The requested session does not exist", NULL);
    MdoSessionRelease(Session);
    if ( !MdoSessionTodoLoad(ProjectId, SessionId, &Data) )
        return MdoApiReplyError(Context, 503u, "todo_unavailable",
            "The session plan could not be read", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
