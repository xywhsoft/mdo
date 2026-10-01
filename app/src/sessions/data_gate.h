#ifndef MDO_SESSION_DATA_GATE_H
#define MDO_SESSION_DATA_GATE_H

#include <xwork.h>

typedef struct MdoSessionDataLease MdoSessionDataLease;
typedef enum MdoSessionDataMode {
    MDO_SESSION_DATA_WRITE = 1,
    MDO_SESSION_DATA_CAPTURE
} MdoSessionDataMode;

/* Manager lifecycle only, with acquisition stopped. Outstanding leases pin
 * their closed registry until release, including late callback owners. */
bool MdoSessionDataInit(void);
void MdoSessionDataUnit(void);

/* Writers may nest independent shared leases; capture is exclusive and never
 * waits for a writer. Acquire before taking a storage/bridge lock. A lease
 * covers a complete operation, not just its individual atomic file writes.
 * Conservative native aliases share a key without rewriting durable IDs. */
MdoSessionDataLease* MdoSessionDataAcquire(const char* ProjectId,
    const char* SessionId, MdoSessionDataMode Mode, xwork_error* Error);
void MdoSessionDataRelease(MdoSessionDataLease* Lease);

#endif
