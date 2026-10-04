#include <xsbase.h>

#include "../api/internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/remote.h"

void ServiceInit(XS_HostInfo* pHost)
{
    (void)MdoBootstrapInit(pHost);
    (void)MdoApiInit();
    (void)MdoRemoteInit(pHost->Server);
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    MdoRemoteUnit();
    MdoApiUnit();
    MdoBootstrapUnit();
    MdoApiLiveRelease();
}

XS_RequestResult RequestProc(XS_HttpReq* pRequest)
{
    return MdoApiRequest(pRequest);
}
