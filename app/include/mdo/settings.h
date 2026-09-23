#ifndef MDO_SETTINGS_H
#define MDO_SETTINGS_H

#include <xsbase.h>

#include "config.h"

typedef enum MdoSettingsStatus {
    MDO_SETTINGS_STATUS_OK = 0,
    MDO_SETTINGS_STATUS_ARGUMENT,
    MDO_SETTINGS_STATUS_CONFLICT,
    MDO_SETTINGS_STATUS_VALIDATION,
    MDO_SETTINGS_STATUS_PERSISTENCE,
    MDO_SETTINGS_STATUS_RUNTIME_REJECTED,
    MDO_SETTINGS_STATUS_ROLLBACK_FAILED,
    MDO_SETTINGS_STATUS_UNAVAILABLE
} MdoSettingsStatus;

typedef struct MdoSettingsResult {
    uint32 Size;
    MdoSettingsStatus Status;
    MdoConfigDomain Domain;
    bool Changed;
    bool Restored;
    uint64 PreviousRevision;
    uint64 Revision;
    uint64 ModelGeneration;
    uint64 WebGeneration;
    uint64 ScheduleGeneration;
    char Message[256];
} MdoSettingsResult;

typedef struct MdoSettingsServiceSnapshot {
    uint32 Size;
    bool Initialized;
    bool Degraded;
    uint64 Transactions;
    uint64 Rollbacks;
    char LastError[256];
} MdoSettingsServiceSnapshot;

bool MdoSettingsServiceInit(void);
void MdoSettingsServiceUnit(void);
bool MdoSettingsServiceGetSnapshot(MdoSettingsServiceSnapshot* Snapshot);

/* ExpectedRevision is mandatory for external mutations. UINT64_MAX is
 * reserved for controlled bootstrap or migration callers. */
bool MdoSettingsApply(MdoConfigDomain Domain, xstrview Document,
    uint64 ExpectedRevision, MdoSettingsResult* Result);
bool MdoSettingsRestore(MdoConfigDomain Domain, uint64 ExpectedRevision,
    MdoSettingsResult* Result);

#endif
