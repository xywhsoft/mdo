#ifndef MDO_REMOTE_BRIDGE_H
#define MDO_REMOTE_BRIDGE_H

#include "loopback.h"
#include "receipts.h"

typedef struct MdoRemoteBridge MdoRemoteBridge;
typedef bool (*MdoRemoteBridgeEmit)(xbytesview Envelope, void* Data);

/* One bridge per ServiceInit generation, retained across relay reconnects.
 * Peer/Input/Pump calls belong to the socket owner. HTTP workers own only
 * immutable copied requests and enqueue responses; they never send sockets. */
MdoRemoteBridge* MdoRemoteBridgeCreate(XS_ServerInfo* Server);
bool MdoRemoteBridgePeerOpen(MdoRemoteBridge* Bridge, cstr Peer, bool ReadOnly, xcancel* ConnectionCancel);
void MdoRemoteBridgePeerClose(MdoRemoteBridge* Bridge, cstr Peer);
void MdoRemoteBridgeDisconnect(MdoRemoteBridge* Bridge);
bool MdoRemoteBridgeInput(MdoRemoteBridge* Bridge, cstr Peer, bool Binary, xbytesview Message);
bool MdoRemoteBridgePump(MdoRemoteBridge* Bridge, MdoRemoteBridgeEmit Emit, void* Data);
void MdoRemoteBridgeDestroy(MdoRemoteBridge* Bridge);

#endif
