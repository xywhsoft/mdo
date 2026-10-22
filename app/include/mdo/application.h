#ifndef MDO_APPLICATION_H
#define MDO_APPLICATION_H
#include <xsbase.h>
#include <stdio.h>
#include <string.h>
#include "../../generated/module-sdk/mdo/sdk.h"
typedef struct MdoApplicationInvocation {
    xcancel* Cancel;
    xvalue* Actions;
    unsigned Depth;
} MdoApplicationInvocation;
const mdo_application_v1* MdoApplicationApi(void);
void MdoApplicationContext(mdo_sdk_context_v1*,MdoApplicationInvocation*,cstr Extension,
    cstr Invocation,cstr Project,cstr Session,cstr Run,cstr Workspace,uint64 Deadline);
bool MdoApplicationInit(XS_ServerInfo*);
void MdoApplicationUnit(void);
xvalue* MdoApplicationCatalog(void);
mdo_result MdoApplicationCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*);
/* Captured product routes are generated from the production router, never a
 * separately maintained allowlist. */
typedef struct MdoApplicationRoute {const char *Id,*Method,*Path,*Domain;} MdoApplicationRoute;
typedef struct MdoApplicationResponse {
    uint32 Status;
    char *Body,*ContentType,*Revision;
    size_t Bytes;
} MdoApplicationResponse;
#define MDO_APPLICATION_RESPONSE_LIMIT (96u*1024u*1024u)
XS_ServerInfo* MdoApplicationServer(void); /* borrowed until the SDK is drained */
bool MdoApiApplicationInvoke(const mdo_sdk_context_v1*,cstr Method,cstr Target,
    const xvalue* Body,const xvalue* Headers,xbytesview Binary,MdoApplicationResponse*,mdo_error_v1*);
bool MdoApplicationRouteInvoke(const mdo_sdk_context_v1*,const MdoApplicationRoute*,const xvalue*,xvalue**,mdo_error_v1*);
void MdoApplicationEvent(cstr Topic,const mdo_sdk_context_v1*,const xvalue*);
xvalue* MdoApplicationEvents(const xvalue*,mdo_error_v1*);
bool MdoApplicationCustomCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool* Found);
xvalue* MdoApplicationCustomCatalog(void);
bool MdoApplicationHooks(const mdo_sdk_context_v1*,cstr,xvalue**,mdo_error_v1*);
static inline bool MdoApplicationError(mdo_error_v1* E,cstr Code,cstr Message,cstr Stage,uint32 Status,bool Uncertain){
    if(E){memset(E,0,sizeof(*E));E->Size=sizeof(*E);snprintf(E->Code,sizeof(E->Code),"%s",Code?Code:"sdk_error");
        snprintf(E->Message,sizeof(E->Message),"%s",Message?Message:"");snprintf(E->Stage,sizeof(E->Stage),"%s",Stage?Stage:"");
        E->Status=Status;E->MayHaveCompleted=Uncertain;E->Retryable=Status==429||Status==503;}
    return false;
}
#endif
