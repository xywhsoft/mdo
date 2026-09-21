#include <xsbase.h>

#include <stdio.h>

void ServiceInit(XS_HostInfo* pHost)
{
    (void)pHost;
    printf("[packed-startup-smoke] service initialized\n");
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    printf("[packed-startup-smoke] service released\n");
}
