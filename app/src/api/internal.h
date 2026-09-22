#ifndef MDO_API_INTERNAL_H
#define MDO_API_INTERNAL_H

#include <xsbase.h>

#include "../../include/mdo/api.h"

#define MDO_API_RESPONSE_MAX_BYTES (256u * 1024u)
#define MDO_API_REQUEST_ID_CAPACITY 40u

typedef struct MdoApiContext {
    XS_HttpReq* Request;
    xhttptarget Target;
    char RequestId[MDO_API_REQUEST_ID_CAPACITY];
} MdoApiContext;

typedef bool (*MdoApiRouteProc)(MdoApiContext* pContext);

bool MdoApiReplySuccessTake(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, cstr Allow);
bool MdoApiReplyError(MdoApiContext* pContext, uint16 Status, cstr Code,
    cstr Message, cstr Allow);
bool MdoApiReplyOptions(MdoApiContext* pContext, cstr Allow);

bool MdoApiBootstrapRoute(MdoApiContext* pContext);

#endif
