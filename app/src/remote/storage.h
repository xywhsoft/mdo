#ifndef MDO_REMOTE_STORAGE_H
#define MDO_REMOTE_STORAGE_H

#include <xsbase.h>
#include "../account/internal.h"

#define MDO_REMOTE_CONFIG_PATH "config/remote.json"
typedef struct MdoRemoteConfig {
    char Name[97];
    uint64 MemberId;
    bool AllowRemote;
} MdoRemoteConfig;
typedef struct MdoRemoteIdentity {
    uint64 MemberId;
    char Id[33];
    char Secret[65];
    bool Persistent;
} MdoRemoteIdentity;

/* Caller serializes storage mutations. Missing config is off and needs no
 * writes; corrupt config fails closed. One sealed identity per account keeps
 * a stable device ID without sharing its secret across account switches. */
bool MdoRemoteConfigLoad(MdoRemoteConfig* Config);
bool MdoRemoteConfigSave(const MdoRemoteConfig* Config);
/* Create is only true for an explicit enable action. Existing unreadable
 * credentials are never replaced silently. Unsupported secure storage allows
 * a temporary in-memory identity; its enabled flag must not be persisted. */
bool MdoRemoteIdentityLoad(uint64 MemberId, bool Create, MdoRemoteIdentity* Identity);
void MdoRemoteIdentityClear(MdoRemoteIdentity* Identity);

#endif
