#include <xsbase.h>

#include "../api/internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/remote.h"

static uint64 g_MdoServiceRetryTimer;
static unsigned g_MdoServiceRetryCount;
enum { MDO_SERVICE_READY, MDO_SERVICE_WAITING, MDO_SERVICE_FAILED };
/* Request workers can observe the deferred owner-timer initialization. */
static xatomic32 g_MdoServiceState;

static bool MdoServiceHomeBusy(void)
{
    const xerror* error = xrtGetError();
    const char* domain = error ? xrtErrorDomain(error) : NULL;
    return domain && xrtErrorKind(error) == XERR_AGAIN && !strcmp(domain,"mdo.home");
}

static void MdoServiceRetry(void* User)
{
    XS_HostInfo* host = User;
    g_MdoServiceRetryTimer = 0u;
    if (xrtAtomic32Load(&g_MdoServiceState,XMEMORY_ACQUIRE) != MDO_SERVICE_WAITING) return;
    if (MdoBootstrapInit(host)) {
        (void)MdoRemoteInit(host->Server);
        xrtAtomic32Store(&g_MdoServiceState,MDO_SERVICE_READY,XMEMORY_RELEASE);
        return;
    }
    if (MdoServiceHomeBusy() && ++g_MdoServiceRetryCount < 100u) {
        MdoBootstrapUnit();
        g_MdoServiceRetryTimer = xsTimerAfter(host,100u,MdoServiceRetry,host);
        if (g_MdoServiceRetryTimer) return;
    }
    xrtAtomic32Store(&g_MdoServiceState,MDO_SERVICE_FAILED,XMEMORY_RELEASE);
    fprintf(stderr,"[mdo] Home handover failed; restart required\n");
}

bool ServiceSwap(XS_HostInfo* Host, xvalue** Shared)
{
    (void)Host;
    if (!Shared) return false;
    *Shared = xrtValueObject();
    /* Only a snapshot: old routes, producers and exclusive Home lease stay
     * usable until xs publishes the candidate and retires the old generation. */
    return *Shared && MdoAccountSetBool(*Shared,"mdo_home_handover",
        xrtAtomic32Load(&g_MdoServiceState,XMEMORY_ACQUIRE) == MDO_SERVICE_WAITING ||
        MdoBootstrapRuntime() != NULL);
}

void ServiceInit(XS_HostInfo* pHost)
{
    xvalue* shared = xsSwapTake(pHost);
    bool handover = false;
    (void)xrtValueGetBool(xrtValueObjectGet(shared,XRT_STR_LITERAL("mdo_home_handover")),&handover);
    xrtValueRelease(shared);
    bool ready = MdoBootstrapInit(pHost);
    if (!ready && handover && MdoServiceHomeBusy()) {
        /* Candidate Init precedes old Unit. The owner timer runs only after
         * publication; acquire the same exclusive lease after old Unit closes
         * it, without allowing two generations to write Home concurrently. */
        MdoBootstrapUnit();
        xrtAtomic32Store(&g_MdoServiceState,MDO_SERVICE_WAITING,XMEMORY_RELEASE);
        g_MdoServiceRetryTimer = xsTimerAfter(pHost,100u,MdoServiceRetry,pHost);
        if (!g_MdoServiceRetryTimer) {
            xrtAtomic32Store(&g_MdoServiceState,MDO_SERVICE_FAILED,XMEMORY_RELEASE);
            fprintf(stderr,"[mdo] cannot schedule Home handover; restart required\n");
        }
    }
    (void)MdoApiInit();
    if (ready) (void)MdoRemoteInit(pHost->Server);
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    xrtAtomic32Store(&g_MdoServiceState,MDO_SERVICE_FAILED,XMEMORY_RELEASE);
    if (g_MdoServiceRetryTimer) (void)xsTimerCancel(g_MdoServiceRetryTimer);
    g_MdoServiceRetryTimer = 0u;
    MdoRemoteUnit();
    MdoApiUnit();
    MdoBootstrapUnit();
    MdoApiLiveRelease();
}

XS_RequestResult RequestProc(XS_HttpReq* pRequest)
{
    int state = xrtAtomic32Load(&g_MdoServiceState,XMEMORY_ACQUIRE);
    if (state != MDO_SERVICE_READY && pRequest && pRequest->head) {
        MdoApiContext context = {0}; context.Request = pRequest;
        if (xrtHttpTargetParse(pRequest->head->Method,pRequest->head->Target,&context.Target) &&
            MdoApiPath(context.Target.Path)) {
            MdoApiRequestId(context.RequestId);
            (void)MdoApiReplyError(&context,503u,
                state == MDO_SERVICE_WAITING ? "home_reload_pending" : "home_reload_failed",
                state == MDO_SERVICE_WAITING ?
                "The previous generation is releasing its Home lease; retry shortly" :
                "Home handover did not finish; restart the application",NULL);
            return XS_OK;
        }
    }
    return MdoApiRequest(pRequest);
}
