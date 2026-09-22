#include <xsbase.h>

#include "../../include/mdo/version.h"

/* MDO-0 only establishes the process entry and source boundary. Persistent
 * state and the long-lived xwork runtime are initialized by MDO-1. */
void ServiceInit(XS_HostInfo* pHost)
{
    (void)pHost;
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
}
