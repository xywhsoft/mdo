#ifndef MDO_REMOTE_BRIDGE_H
#define MDO_REMOTE_BRIDGE_H

#include "loopback.h"
#include "receipts.h"
#include "net.h"

typedef struct MdoRemoteBridge MdoRemoteBridge;
typedef bool (*MdoRemoteBridgeEmit)(xbytesview Envelope, void* Data);
typedef bool (*MdoRemoteBridgeRoutedEmit)(uint64 Route, xbytesview Envelope, void* Data);
typedef xvalue* (*MdoRemoteBridgeOffer)(cstr Peer, bool ReadOnly, void* Data);

/* One bridge per ServiceInit generation, retained across relay reconnects.
 * Peer/Input/Pump calls belong to the socket owner. HTTP workers own only
 * immutable copied requests and enqueue responses; they never send sockets. */
/* Net is borrowed until Destroy has joined all HTTP/live workers. */
MdoRemoteBridge* MdoRemoteBridgeCreate(XS_ServerInfo* Server, MdoRemoteNet* Net);
bool MdoRemoteBridgePeerOpen(MdoRemoteBridge* Bridge, cstr Peer, bool ReadOnly, xcancel* ConnectionCancel);
void MdoRemoteBridgeDirect(MdoRemoteBridge* Bridge, MdoRemoteBridgeOffer Offer, void* Data);
void MdoRemoteBridgeRendezvous(MdoRemoteBridge* Bridge, MdoRemoteBridgeOffer Offer, void* Data);
/* Internal monotonic route tags: zero is relay. A retired tag is never reused;
 * late output stays on its admission route and is dropped after teardown. */
uint64 MdoRemoteBridgeRoute(MdoRemoteBridge* Bridge);
void MdoRemoteBridgeRouteClose(MdoRemoteBridge* Bridge, uint64 Route);
bool MdoRemoteBridgeControl(MdoRemoteBridge* Bridge, cstr Peer, const xvalue* Value);
cstr MdoRemoteBridgeRuntime(MdoRemoteBridge* Bridge);
void MdoRemoteBridgePeerClose(MdoRemoteBridge* Bridge, cstr Peer);
void MdoRemoteBridgeDisconnect(MdoRemoteBridge* Bridge);
bool MdoRemoteBridgeInput(MdoRemoteBridge* Bridge, cstr Peer, bool Binary, xbytesview Message);
bool MdoRemoteBridgeInputRouted(MdoRemoteBridge* Bridge, cstr Peer, uint64 Route, bool Binary, xbytesview Message);
bool MdoRemoteBridgePump(MdoRemoteBridge* Bridge, MdoRemoteBridgeEmit Emit, void* Data);
bool MdoRemoteBridgePumpRouted(MdoRemoteBridge* Bridge, MdoRemoteBridgeRoutedEmit Emit, void* Data);
void MdoRemoteBridgeDestroy(MdoRemoteBridge* Bridge);

#endif
