#ifndef MDO_WEB_H
#define MDO_WEB_H

#include <xsbase.h>
#include <xwork.h>

#define MDO_WEB_TRANSPORT_VERSION 1u

typedef bool (*MdoWebFetchProc)(void* pContext,
    const XS_FetchRequest* pRequest, XS_FetchResponse* pResponse);
typedef void (*MdoWebFetchResponseUnitProc)(void* pContext,
    XS_FetchResponse* pResponse);

/* Testable transport seam. Production uses xsFetch; injected transports must
 * honor the same ownership contract and release responses in ResponseUnit. */
typedef struct MdoWebTransport {
    uint32 Size;
    uint32 Version;
    void* Context;
    MdoWebFetchProc Fetch;
    MdoWebFetchResponseUnitProc ResponseUnit;
} MdoWebTransport;

typedef struct MdoWebSnapshot {
    uint32 Size;
    uint64 Generation;
    bool Enabled;
    size_t DocumentCount;
    size_t MaxDocuments;
    uint64 RequestsCompleted;
    uint64 RequestsFailed;
} MdoWebSnapshot;

bool MdoWebManagerInit(xwork_runtime* pRuntime);
bool MdoWebManagerInitWithTransport(xwork_runtime* pRuntime,
    const MdoWebTransport* pTransport);
void MdoWebManagerUnit(void);
bool MdoWebManagerReload(void);
bool MdoWebManagerGetSnapshot(MdoWebSnapshot* pSnapshot);

#endif
