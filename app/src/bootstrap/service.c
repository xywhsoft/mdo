#include <xsbase.h>

#include "../api/internal.h"
#include "../../include/mdo/bootstrap.h"

void ServiceInit(XS_HostInfo* pHost)
{
    (void)MdoBootstrapInit(pHost);
    (void)MdoApiInit();
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    MdoApiUnit();
    MdoBootstrapUnit();
    MdoApiLiveRelease();
}

XS_RequestResult RequestProc(XS_HttpReq* pRequest)
{
    return MdoApiRequest(pRequest);
}
