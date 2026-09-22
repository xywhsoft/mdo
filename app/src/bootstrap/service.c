#include <xsbase.h>

#include "../../include/mdo/bootstrap.h"

void ServiceInit(XS_HostInfo* pHost)
{
    (void)MdoBootstrapInit(pHost);
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    MdoBootstrapUnit();
}
