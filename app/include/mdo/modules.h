#ifndef MDO_MODULES_H
#define MDO_MODULES_H

#include <xsbase.h>
#include <xwork.h>

#include "../../generated/module-sdk/mdo/module.h"

typedef struct MdoModuleCatalog MdoModuleCatalog;
typedef struct MdoModuleDiagnostics MdoModuleDiagnostics;

typedef enum MdoModuleKind {
    MDO_MODULE_TOOLS = 1,
    MDO_MODULE_AGENTS,
    MDO_MODULE_SUBAGENTS
} MdoModuleKind;

typedef enum MdoModuleDiagnosticStage {
    MDO_MODULE_DIAGNOSTIC_DISCOVERY = 1,
    MDO_MODULE_DIAGNOSTIC_READ,
    MDO_MODULE_DIAGNOSTIC_COMPILE,
    MDO_MODULE_DIAGNOSTIC_ENTRY,
    MDO_MODULE_DIAGNOSTIC_REGISTER,
    MDO_MODULE_DIAGNOSTIC_VALIDATE,
    MDO_MODULE_DIAGNOSTIC_PUBLISH
} MdoModuleDiagnosticStage;

typedef struct MdoModuleInfo {
    uint32 Size;
    uint64 Generation;
    MdoModuleKind Kind;
    bool External;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* Version;
    const char* SourcePath;
    const char* SourceHash;
    mdo_capabilities Capabilities;
    size_t ToolCount;
    size_t AgentCount;
} MdoModuleInfo;

typedef struct MdoModuleToolInfo {
    uint32 Size;
    uint64 Generation;
    const char* ModuleId;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* ParametersJson;
    mdo_tool_effects Effects;
    mdo_tool_flags Flags;
    const char* SerialGroup;
    const char* PermissionResource;
    size_t MaxResultBytes;
} MdoModuleToolInfo;

typedef struct MdoModuleAgentInfo {
    uint32 Size;
    uint64 Generation;
    const char* ModuleId;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* Model;
    const char* ReasoningEffort;
    const char* SystemPrompt;
    const char* PermissionProfile;
    const char* const* Tools;
    size_t ToolCount;
    const char* const* Skills;
    size_t SkillCount;
    mdo_tool_effects AllowedEffects;
    mdo_agent_flags Flags;
    uint64 ContextWindowTokens;
    uint64 MaxInputTokens;
    uint32 MaxOutputTokens;
    uint32 MaxTurns;
    uint32 TimeoutMilliseconds;
    size_t MaxFinalBytes;
    uint32 MaxDepth;
} MdoModuleAgentInfo;

typedef struct MdoModuleDiagnosticInfo {
    uint32 Size;
    MdoModuleDiagnosticStage Stage;
    const char* SourcePath;
    const char* SourceHash;
    const char* Message;
} MdoModuleDiagnosticInfo;

bool MdoModuleManagerInit(xwork_runtime* pRuntime);
void MdoModuleManagerUnit(void);
bool MdoModuleManagerReload(void);
uint64 MdoModuleManagerGeneration(void);

MdoModuleCatalog* MdoModuleCatalogSnapshot(void);
MdoModuleCatalog* MdoModuleCatalogRef(MdoModuleCatalog* pCatalog);
void MdoModuleCatalogRelease(MdoModuleCatalog* pCatalog);
size_t MdoModuleCatalogModuleCount(const MdoModuleCatalog* pCatalog);
size_t MdoModuleCatalogToolCount(const MdoModuleCatalog* pCatalog);
size_t MdoModuleCatalogAgentCount(const MdoModuleCatalog* pCatalog);
bool MdoModuleCatalogModuleAt(const MdoModuleCatalog* pCatalog,
    size_t iIndex, MdoModuleInfo* pInfo);
bool MdoModuleCatalogToolAt(const MdoModuleCatalog* pCatalog,
    size_t iIndex, MdoModuleToolInfo* pInfo);
bool MdoModuleCatalogAgentAt(const MdoModuleCatalog* pCatalog,
    size_t iIndex, MdoModuleAgentInfo* pInfo);

MdoModuleDiagnostics* MdoModuleDiagnosticsSnapshot(void);
MdoModuleDiagnostics* MdoModuleDiagnosticsRef(
    MdoModuleDiagnostics* pDiagnostics);
void MdoModuleDiagnosticsRelease(MdoModuleDiagnostics* pDiagnostics);
size_t MdoModuleDiagnosticsCount(const MdoModuleDiagnostics* pDiagnostics);
bool MdoModuleDiagnosticsAt(const MdoModuleDiagnostics* pDiagnostics,
    size_t iIndex, MdoModuleDiagnosticInfo* pInfo);

#endif
