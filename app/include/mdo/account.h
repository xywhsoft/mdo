#ifndef MDO_ACCOUNT_H
#define MDO_ACCOUNT_H

#include <xsbase.h>
#include <xwork.h>

/* Login and search share one native service authority. This build-time
 * default can be replaced in isolated test compositions, never in settings. */
#ifndef MDO_ACCOUNT_SERVICE_ORIGIN
#define MDO_ACCOUNT_SERVICE_ORIGIN "https://ai.xywhsoft.com"
#endif

/* One authority for website credentials, shared by all agents and background
 * tasks. Snapshots contain display data only, never bearer/refresh tokens. */
typedef struct MdoAccountLease {
    char* AccessToken;
    xcancel* Cancel;
    xcancelwatch* Watch;
    uint64 Generation;
    bool Managed;
} MdoAccountLease;

bool MdoAccountInit(void);
void MdoAccountUnit(void);
xvalue* MdoAccountSnapshot(void);
bool MdoAccountLoginStart(cstr LocalOrigin, bool Remember, xvalue** PublicResult);
bool MdoAccountLoginPassword(cstr Identifier, cstr Password, bool Remember);
bool MdoAccountLoginCancel(void);
bool MdoAccountCallback(cstr State, cstr Code, cstr Error);
bool MdoAccountRefresh(void);
bool MdoAccountLogout(void);
bool MdoAccountSkipSearch(uint64 Id);
cstr MdoAccountSearchEndpoint(void);
bool MdoAccountAcquire(cstr Query, const xwork_tool_context* Context,
    MdoAccountLease* Lease);
void MdoAccountRelease(MdoAccountLease* Lease);
bool MdoAccountRejectAccess(const MdoAccountLease* Lease);
void MdoAccountSearchStatus(uint16 Status);
bool MdoAccountOpenWebsite(cstr Path);

#endif
