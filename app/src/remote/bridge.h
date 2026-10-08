#ifndef MDO_REMOTE_BRIDGE_H
#define MDO_REMOTE_BRIDGE_H

#include "loopback.h"
#include "receipts.h"
#include "net.h"

typedef struct MdoRemoteBridge MdoRemoteBridge;
typedef bool (*MdoRemoteBridgeEmit)(xbytesview Envelope, void* Data);
typedef xvalue* (*MdoRemoteBridgeOffer)(cstr Peer, bool ReadOnly, void* Data);

/* One bridge per ServiceInit generation, retained across relay reconnects.
 * Peer/Input/Pump calls belong to the socket owner. HTTP workers own only
 * immutable copied requests and enqueue responses; they never send sockets. */
/* Net is borrowed until Destroy has joined all HTTP/live workers. */
MdoRemoteBridge* MdoRemoteBridgeCreate(XS_ServerInfo* Server, MdoRemoteNet* Net);
bool MdoRemoteBridgePeerOpen(MdoRemoteBridge* Bridge, cstr Peer, bool ReadOnly, xcancel* ConnectionCancel);
void MdoRemoteBridgeDirect(MdoRemoteBridge* Bridge, MdoRemoteBridgeOffer Offer, void* Data);
cstr MdoRemoteBridgeRuntime(MdoRemoteBridge* Bridge);
void MdoRemoteBridgePeerClose(MdoRemoteBridge* Bridge, cstr Peer);
void MdoRemoteBridgeDisconnect(MdoRemoteBridge* Bridge);
bool MdoRemoteBridgeInput(MdoRemoteBridge* Bridge, cstr Peer, bool Binary, xbytesview Message);
bool MdoRemoteBridgePump(MdoRemoteBridge* Bridge, MdoRemoteBridgeEmit Emit, void* Data);
void MdoRemoteBridgeDestroy(MdoRemoteBridge* Bridge);

#endif
