#include <xsbase.h>

#include "../../include/mdo/api.h"
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
}

XS_RequestResult RequestProc(XS_HttpReq* pRequest)
{
    return MdoApiRequest(pRequest);
}
