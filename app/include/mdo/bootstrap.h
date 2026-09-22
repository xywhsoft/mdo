#ifndef MDO_BOOTSTRAP_H
#define MDO_BOOTSTRAP_H

#include <xsbase.h>
#include <xwork.h>

#include "config.h"
#include "home.h"

typedef enum MdoBootstrapStage {
    MDO_BOOTSTRAP_EMPTY = 0,
    MDO_BOOTSTRAP_HOME_READY,
    MDO_BOOTSTRAP_CONFIG_READY,
    MDO_BOOTSTRAP_RUNTIME_READY,
    MDO_BOOTSTRAP_FAILED
} MdoBootstrapStage;

typedef struct MdoBootstrapSnapshot {
    uint32 Size;
    MdoBootstrapStage Stage;
    bool Ready;
    size_t DefaultsBytes;
    MdoConfigSnapshot Config;
    MdoHomeSnapshot Home;
    const char* Message;
} MdoBootstrapSnapshot;

bool MdoBootstrapInit(XS_HostInfo* pHost);
void MdoBootstrapUnit(void);
bool MdoBootstrapGetSnapshot(MdoBootstrapSnapshot* pSnapshot);
xwork_runtime* MdoBootstrapRuntime(void); /* borrowed */

#endif
