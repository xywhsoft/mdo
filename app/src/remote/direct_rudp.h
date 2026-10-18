#ifndef MDO_REMOTE_DIRECT_RUDP_H
#define MDO_REMOTE_DIRECT_RUDP_H
#include "bridge.h"
#include "rendezvous.h"
#if defined(XS_USE_XRUDP)
typedef struct MdoDirectRudp MdoDirectRudp;
MdoDirectRudp* MdoDirectRudpCreate(MdoRemoteNet* Net, MdoRemoteBridge* Bridge, uint64 Owner, xcancel* Cancel);
void MdoDirectRudpDestroy(MdoDirectRudp* Direct);
void MdoDirectRudpPeerClose(MdoDirectRudp* Direct, cstr Peer);
bool MdoDirectRudpInput(MdoDirectRudp* Direct, cstr Peer, bool Binary, xbytesview Bytes, bool* Handled);
bool MdoDirectRudpPoll(MdoDirectRudp* Direct);
bool MdoDirectRudpEmit(MdoDirectRudp* Direct, uint64 Route, xbytesview Envelope, bool* Handled);
#endif
#endif
