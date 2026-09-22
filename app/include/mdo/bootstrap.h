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
    MDO_BOOTSTRAP_SKILLS_READY,
    MDO_BOOTSTRAP_MCP_READY,
    MDO_BOOTSTRAP_MODULES_READY,
    MDO_BOOTSTRAP_FAILED
} MdoBootstrapStage;

typedef struct MdoBootstrapSnapshot {
    uint32 Size;
    MdoBootstrapStage Stage;
    bool Ready;
    size_t DefaultsBytes;
    uint64 SkillGeneration;
    size_t SkillCount;
    size_t SkillDiagnosticCount;
    uint64 McpGeneration;
    size_t McpServerCount;
    size_t McpDiagnosticCount;
    uint64 ModuleGeneration;
    size_t ModuleCount;
    size_t ModuleToolCount;
    size_t ModuleAgentCount;
    size_t ModuleDiagnosticCount;
    MdoConfigSnapshot Config;
    MdoHomeSnapshot Home;
    const char* Message;
} MdoBootstrapSnapshot;

bool MdoBootstrapInit(XS_HostInfo* pHost);
void MdoBootstrapUnit(void);
bool MdoBootstrapGetSnapshot(MdoBootstrapSnapshot* pSnapshot);
xwork_runtime* MdoBootstrapRuntime(void); /* borrowed */

#endif
