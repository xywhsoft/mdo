#ifndef MDO_REMOTE_NET_H
#define MDO_REMOTE_NET_H

#include <xsbase.h>

/* Internal outbound transport, never a browser-facing arbitrary URL API.
 * A retained host server owns Engine. All socket calls belong to one service
 * thread; other threads enqueue work or request cancellation. No app callback
 * is installed on an xrt network worker or retained beyond Poll. */
typedef struct MdoRemoteNet {
    xnetengine* Engine;
    xnetresolver* Resolver;
    xtlscontext* Tls;
    xtlsverifier* Verifier;
} MdoRemoteNet;

typedef struct MdoRemoteSocketConfig {
    cstr Host;
    uint16 Port;
    bool Secure;
    cstr Path;
    cstr Origin;
    cstr Protocols;
    cstr Protocol; /* Required selected protocol, not the credential offer. */
    size_t MessageLimit;
} MdoRemoteSocketConfig;

typedef struct MdoRemoteSocket MdoRemoteSocket;
typedef bool (*MdoRemoteMessageProc)(bool Binary, xbytesview Message, void* Data);

/* Trust is NULL for platform system roots. A supplied store is copied by the
 * verifier, useful for private/test authorities; no verification bypass. */
bool MdoRemoteNetInit(MdoRemoteNet* Net, xnetengine* Engine, const xx509store* Trust);
void MdoRemoteNetUnit(MdoRemoteNet* Net); /* After all sockets are destroyed. */
MdoRemoteSocket* MdoRemoteSocketOpen(MdoRemoteNet* Net,
    const MdoRemoteSocketConfig* Config, xcancel* Cancel, uint16* HttpStatus);
bool MdoRemoteSocketSend(MdoRemoteSocket* Socket, bool Binary, xbytesview Data);
/* Nonblocking when idle; bounded I/O while data is available. Messages borrow
 * Socket storage for the callback only. False is terminal; never reuse. */
bool MdoRemoteSocketPoll(MdoRemoteSocket* Socket, MdoRemoteMessageProc Proc, void* Data);
uint16 MdoRemoteSocketCloseCode(const MdoRemoteSocket* Socket);
void MdoRemoteSocketDestroy(MdoRemoteSocket* Socket);

#endif
