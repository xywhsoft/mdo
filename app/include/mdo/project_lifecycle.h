#ifndef MDO_PROJECT_LIFECYCLE_H
#define MDO_PROJECT_LIFECYCLE_H

#include <xsbase.h>
#include <xwork.h>

typedef struct MdoProjectLease MdoProjectLease;

typedef enum MdoProjectLeaseMode {
    MDO_PROJECT_LEASE_SHARED = 1,
    MDO_PROJECT_LEASE_EXCLUSIVE
} MdoProjectLeaseMode;

/* Init/Unit run on the host lifecycle thread with new acquirers stopped. Unit
 * closes acquisition; outstanding leases keep their registry alive so their
 * final callback-owner releases remain safe even after Unit or a new Init. */
bool MdoProjectLifecycleInit(void);
void MdoProjectLifecycleUnit(void);

/* Nonblocking. Shared leases cover operations and complete object lifetimes;
 * exclusive acquisition fails with XWORK_ERROR_CONTEXT while any shared or
 * exclusive owner exists. Acquire before manager locks. Ref is an atomic pin
 * of an already-held lease and may be used under a manager lock. Ref does not
 * create another reader; exclusion lasts until the final Release.
 *
 * IDs use a conservative ASCII case fold and omit trailing dots in the gate
 * key, protecting aliases on case-insensitive/native Windows filesystems.
 * Paths and durable IDs are never rewritten. Distinct Linux case variants
 * therefore share exclusion but retain separate data. No file is created.
 * A lease is only a coordination primitive; a purge transaction must enlist
 * every affected writer and validate its inventory before moving any data. */
MdoProjectLease* MdoProjectLeaseAcquire(const char* ProjectId,
    MdoProjectLeaseMode Mode, xwork_error* Error);
MdoProjectLease* MdoProjectLeaseRef(MdoProjectLease* Lease);
void MdoProjectLeaseRelease(MdoProjectLease* Lease);

/* The caller must own a live reference. Checks the current registry, project
 * gate key and exact mode without acquiring a second lease; closed/old
 * registries never authorize a new transaction. Does not create files. */
bool MdoProjectLeaseProtects(const MdoProjectLease* Lease,
    const char* ProjectId, MdoProjectLeaseMode Mode);

#endif
