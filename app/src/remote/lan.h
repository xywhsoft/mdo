#ifndef MDO_REMOTE_LAN_H
#define MDO_REMOTE_LAN_H
#include "bridge.h"
typedef struct MdoLan MdoLan;
/* Entire listener, ephemeral identity and grants belong to one authenticated
 * relay generation. Disconnect/revoke closes direct peers before its workers. */
MdoLan* MdoLanCreate(MdoRemoteNet* Net, MdoRemoteBridge* Bridge, xcancel* Cancel);
void MdoLanDestroy(MdoLan* Lan);
bool MdoLanPoll(MdoLan* Lan);
void MdoLanPeerClose(MdoLan* Lan, cstr Peer);
bool MdoLanEmit(MdoLan* Lan, xbytesview Envelope, bool* Handled);
xvalue* MdoLanOffer(cstr Peer, bool ReadOnly, void* Data);
bool MdoLanGatewayInit(XS_ServerInfo* Server);
void MdoLanGatewayUnit(void);
bool MdoLanGatewayAccept(XS_HttpReq* Request);
#endif
