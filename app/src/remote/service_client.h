#ifndef MDO_REMOTE_SERVICE_CLIENT_H
#define MDO_REMOTE_SERVICE_CLIENT_H

#include "storage.h"
#include "net.h"

#define MDO_REMOTE_RELAY_PROTOCOL "xadmin.device-relay.v1"
#define MDO_REMOTE_RELAY_PATH "/api/v1/devices/connect"
#define MDO_REMOTE_RELAY_HEADER 21u
#define MDO_REMOTE_RELAY_PAYLOAD 262123u

/* Calls only the fixed first-party device service, off a network worker.
 * Lease belongs to the caller throughout the call. No automatic write retry. */
/* Release returned tickets with MdoAccountSecretValueRelease. */
xvalue* MdoRemoteServiceCall(cstr Path, cstr Method, const xvalue* Body,
    const MdoAccountLease* Lease, uint16* Status);
MdoRemoteSocket* MdoRemoteServiceConnect(MdoRemoteNet* Net,
    const xvalue* Ticket, const MdoAccountLease* Lease, uint16* Status);

#endif
