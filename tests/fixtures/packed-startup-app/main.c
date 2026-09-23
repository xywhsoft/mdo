#include <xsbase.h>

#include <stdio.h>

void ServiceInit(XS_HostInfo* pHost)
{
    FILE* pMarker;
    (void)pHost;
    pMarker = fopen("startup-ready.marker", "wb");
    if (pMarker != NULL) {
        (void)fwrite("service-initialized\n", 1u, 20u, pMarker);
        (void)fclose(pMarker);
    }
    printf("[packed-startup-smoke] service initialized\n");
}

void ServiceUnit(XS_HostInfo* pHost)
{
    (void)pHost;
    printf("[packed-startup-smoke] service released\n");
}
