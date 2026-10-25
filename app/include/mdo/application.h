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
    char TraceId[33], CallId[33], ParentCallId[33]; /* invocation-local; never caller supplied */
    void* Catalog; /* retained module generation for an asynchronous invocation */
    char WriteToken[64]; /* original HTTP admission generation, never returned */
} MdoApplicationInvocation;
const mdo_application_v1* MdoApplicationApi(void);
void MdoApplicationContext(mdo_sdk_context_v1*,MdoApplicationInvocation*,cstr Extension,
    cstr Invocation,cstr Project,cstr Session,cstr Run,cstr Workspace,uint64 Deadline);
bool MdoApplicationInit(XS_ServerInfo*);
void MdoApplicationUnit(void);
bool MdoApplicationChangesInit(void);
void MdoApplicationChangesUnit(void);
void MdoApplicationCaptureChanges(cstr Project,cstr Session,cstr Workspace);
xvalue* MdoApplicationCatalog(void);
/* Product handlers own their lifecycle coordination, including exclusive
 * purge/restore operations. Native/custom jobs pin their invocation project. */
bool MdoApplicationProductService(cstr Method);
xvalue* MdoApplicationRuntimeCatalog(void);
bool MdoApplicationRuntimeCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
bool MdoApplicationJobsInit(void);
void MdoApplicationJobsStop(void);
void MdoApplicationJobsUnit(void);
xvalue* MdoApplicationJobsCatalog(void);
bool MdoApplicationJobsCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
bool MdoApplicationStreamsInit(void);
void MdoApplicationStreamsStop(void);
void MdoApplicationStreamsUnit(void);
xvalue* MdoApplicationStreamsCatalog(void);
bool MdoApplicationStreamsCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
bool MdoApplicationTerminalsInit(void);
void MdoApplicationTerminalsStop(void);
void MdoApplicationTerminalsUnit(void);
bool MdoApplicationTerminalsProjectBusy(cstr Project);
xvalue* MdoApplicationTerminalsCatalog(void);
bool MdoApplicationTerminalsCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
bool MdoApplicationPluginsInit(void);
void MdoApplicationPluginsUnit(void);
xvalue* MdoApplicationPluginsCatalog(void);
bool MdoApplicationPluginsCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
/* Internal store transaction reservation. Begin/End enclose filesystem journal
 * publication and rollback; Publish mounts only verified immutable revisions. */
bool MdoApplicationPluginsStoreBegin(xvalue**,char*,size_t);
bool MdoApplicationPluginsStorePublish(const xvalue*,char*,size_t);
void MdoApplicationPluginsStoreEnd(void);
bool MdoApplicationUiInit(void);
bool MdoApplicationWorkbenchInit(void);
void MdoApplicationWorkbenchUnit(void);
xvalue* MdoApplicationWorkbenchCatalog(void);
bool MdoApplicationWorkbenchCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
void MdoApplicationUiUnit(void);
xvalue* MdoApplicationUiCatalog(void);
bool MdoApplicationUiCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*,bool*);
mdo_result MdoApplicationCall(const mdo_sdk_context_v1*,cstr,const xvalue*,xvalue**,mdo_error_v1*);
/* Captured product routes are generated from the production router, never a
 * separately maintained allowlist. */
typedef struct MdoApplicationRoute {const char *Id,*Method,*Path,*Domain,*InputSchema,*OutputSchema;} MdoApplicationRoute;
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
