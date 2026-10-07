#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libtcc.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/agent_file.h"
#include "../../include/mdo/tool_catalog.h"

#define MDO_MODULE_SOURCE_LIMIT (1024u * 1024u)
#define MDO_MODULE_HEADER_LIMIT (512u * 1024u)
#define MDO_MODULE_COUNT_LIMIT 128u
#define MDO_MODULE_TOOL_LIMIT 256u
#define MDO_MODULE_AGENT_LIMIT 128u
#define MDO_MODULE_DEPENDENCY_LIMIT 64u
#define MDO_MODULE_AGENT_TOOL_LIMIT 256u
#define MDO_MODULE_AGENT_SKILL_LIMIT 128u
#define MDO_MODULE_ID_LIMIT 96u
#define MDO_MODULE_NAME_LIMIT 256u
#define MDO_MODULE_DESCRIPTION_LIMIT 4096u
#define MDO_MODULE_SCHEMA_LIMIT (128u * 1024u)
#define MDO_MODULE_PROMPT_LIMIT (256u * 1024u)
#define MDO_MODULE_ERROR_LIMIT 1024u
#define MDO_MODULE_SOURCE "mdo.modules"
#define MDO_MODULE_COMPILE_OPTIONS "-std=c11 -Wall -Wextra -Werror"
#define MDO_MODULE_COMPILE_ID "c11|-Wall|-Wextra|-Werror|restricted-v1"

typedef struct MdoModuleGeneration MdoModuleGeneration;

typedef struct MdoModuleToolBinding {
    MdoModuleGeneration* Owner;
    char* Id;
    char* Name;
    char* Description;
    char* ParametersJson;
    mdo_tool_effects Effects;
    mdo_tool_flags Flags;
    char* SerialGroup;
    char* PermissionResource;
    size_t MaxResultBytes;
    void* UserData;
    mdo_tool_describe_permissions_v1 DescribePermissions;
    mdo_tool_execute_v1 Execute;
    mdo_tool_cancel_v1 Cancel;
} MdoModuleToolBinding;

typedef struct MdoModuleAgentBinding {
    MdoModuleGeneration* Owner;
    char* Id;
    char* Name;
    char* Description;
    char* Model;
    char* ReasoningEffort;
    char* SystemPrompt;
    char* PermissionProfile;
    char** Tools;
    size_t ToolCount;
    char** Skills;
    size_t SkillCount;
    uint64 ContextWindowTokens;
    uint64 MaxInputTokens;
    uint32 MaxOutputTokens;
    uint32 MaxTurns;
    uint32 TimeoutMilliseconds;
    size_t MaxFinalBytes;
    mdo_tool_effects AllowedEffects;
    uint32 MaxDepth;
    mdo_agent_flags Flags;
    void* UserData;
    mdo_agent_acquire_v1 Acquire;
    mdo_agent_release_v1 Release;
} MdoModuleAgentBinding;

struct MdoModuleGeneration {
    xatomic32 Refs;
    uint64 Generation;
    MdoModuleKind Kind;
    bool External;
    TCCState* Tcc;
    bool Profile;
    bool UseCode;
    char* Id;
    char* Name;
    char* Description;
    char* Version;
    char* SourcePath;
    char SourceHash[65];
    char SourceRevision[65];
    mdo_capabilities Capabilities;
    char** Dependencies;
    size_t DependencyCount;
    MdoModuleToolBinding* Tools;
    size_t ToolCount;
    size_t ToolCapacity;
    MdoModuleAgentBinding* Agents;
    size_t AgentCount;
    size_t AgentCapacity;
    void* ModuleData;
    mdo_module_unregister_v1 Unregister;
    bool UnregisterReady;
    mdo_host_core_v1 HostCore;
    mdo_host_services_v1 HostServices;
    char CompilerLog[4096];
    size_t CompilerLogLength;
};

struct MdoModuleCatalog {
    xatomic32 Refs;
    uint64 Generation;
    MdoModuleGeneration** Modules;
    size_t ModuleCount;
    MdoModuleToolBinding** Tools;
    size_t ToolCount;
    MdoModuleAgentBinding** Agents;
    size_t AgentCount;
};

typedef struct MdoModuleDiagnosticEntry {
    MdoModuleDiagnosticStage Stage;
    char* SourcePath;
    char* SourceHash;
    char* Message;
} MdoModuleDiagnosticEntry;

struct MdoModuleDiagnostics {
    xatomic32 Refs;
    MdoModuleDiagnosticEntry* Entries;
    size_t Count;
    size_t Capacity;
};

typedef struct MdoModuleSource {
    MdoModuleKind Kind;
    bool External;
    bool Declarative;
    char* VirtualPath;
    char* RelativePath;
} MdoModuleSource;

typedef struct MdoModuleRegistrarContext {
    MdoModuleGeneration* Generation;
    bool Failed;
    char Message[MDO_MODULE_ERROR_LIMIT];
} MdoModuleRegistrarContext;

typedef struct MdoModuleState {
    xmutex* Lock;
    xwork_runtime* Runtime;
    MdoModuleCatalog* Catalog;
    MdoModuleDiagnostics* Diagnostics;
    uint64 NextGeneration;
    bool Initialized;
} MdoModuleState;

static MdoModuleState g_MdoModules;

static void MdoModulesSetError(cstr Message)
{
    xerror* pError = xrtErrorCreate(XERR_STATE, "mdo.modules", 1,
        Message != NULL ? Message : "module operation failed");
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static void MdoModulesCopyError(char* Target, size_t Capacity, cstr Message)
{
    if ( Target == NULL || Capacity == 0u ) return;
    snprintf(Target, Capacity, "%s", Message != NULL ? Message :
        "module operation failed");
}

static bool MdoModulesGrow(void** ppItems, size_t* pCapacity,
    size_t Count, size_t ItemSize, size_t Limit)
{
    size_t iCapacity;
    void* pItems;

    if ( Count > Limit ) return false;
    if ( Count <= *pCapacity ) return true;
    iCapacity = *pCapacity != 0u ? *pCapacity : 4u;
    while ( iCapacity < Count ) {
        if ( iCapacity > Limit / 2u ) {
            iCapacity = Limit;
            break;
        }
        iCapacity *= 2u;
    }
    if ( iCapacity < Count || iCapacity > SIZE_MAX / ItemSize ) return false;
    pItems = xrtRealloc(*ppItems, iCapacity * ItemSize);
    if ( pItems == NULL ) return false;
    *ppItems = pItems;
    *pCapacity = iCapacity;
    return true;
}

static bool MdoModulesStringCopy(char** pTarget, cstr Source, size_t Limit,
    bool Required)
{
    size_t iLength = 0u;

    *pTarget = NULL;
    if ( Source == NULL || Source[0] == '\0' ) return !Required;
    while ( iLength <= Limit && Source[iLength] != '\0' ) iLength++;
    if ( iLength > Limit ) return false;
    *pTarget = xrtStrDupN(Source, iLength);
    return *pTarget != NULL;
}

static bool MdoModulesIdValid(cstr Id)
{
    const unsigned char* p = (const unsigned char*)Id;
    size_t iLength = 0u;

    if ( p == NULL || !(p[0] >= 'a' && p[0] <= 'z') ) return false;
    for ( ; p[iLength] != '\0'; ++iLength ) {
        unsigned char c = p[iLength];
        if ( iLength >= MDO_MODULE_ID_LIMIT ||
             !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '-') ) return false;
    }
    return iLength != 0u;
}

static bool MdoModulesOneBit(uint64 Value)
{
    return Value != 0u && (Value & (Value - 1u)) == 0u;
}

static bool MdoModulesContainsZero(const void* Data, size_t Size)
{
    const unsigned char* pBytes = (const unsigned char*)Data;
    size_t i;
    for ( i = 0u; i < Size; ++i ) {
        if ( pBytes[i] == 0u ) return true;
    }
    return false;
}

static void MdoModulesToolUnit(MdoModuleToolBinding* pTool)
{
    if ( pTool == NULL ) return;
    xrtFree(pTool->Id);
    xrtFree(pTool->Name);
    xrtFree(pTool->Description);
    xrtFree(pTool->ParametersJson);
    xrtFree(pTool->SerialGroup);
    xrtFree(pTool->PermissionResource);
    memset(pTool, 0, sizeof(*pTool));
}

static void MdoModulesStringArrayUnit(char** pItems, size_t Count)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) xrtFree(pItems[i]);
    xrtFree(pItems);
}

static void MdoModulesAgentUnit(MdoModuleAgentBinding* pAgent)
{
    if ( pAgent == NULL ) return;
    xrtFree(pAgent->Id);
    xrtFree(pAgent->Name);
    xrtFree(pAgent->Description);
    xrtFree(pAgent->Model);
    xrtFree(pAgent->ReasoningEffort);
    xrtFree(pAgent->SystemPrompt);
    xrtFree(pAgent->PermissionProfile);
    MdoModulesStringArrayUnit(pAgent->Tools, pAgent->ToolCount);
    MdoModulesStringArrayUnit(pAgent->Skills, pAgent->SkillCount);
    memset(pAgent, 0, sizeof(*pAgent));
}

static bool MdoModulesGenerationRef(MdoModuleGeneration* pGeneration)
{
    uint32 iRefs;

    if ( pGeneration == NULL ) return false;
    iRefs = xrtAtomic32Load(&pGeneration->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 iExpected = iRefs;
        if ( iRefs == 0u || iRefs == UINT32_MAX ) return false;
        if ( xrtAtomic32CompareExchange(&pGeneration->Refs, &iExpected,
                iRefs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return true;
        iRefs = iExpected;
    }
}

static void MdoModulesGenerationRelease(MdoModuleGeneration* pGeneration)
{
    size_t i;
    uint32 iPrevious;

    if ( pGeneration == NULL ) return;
    iPrevious = xrtAtomic32FetchSub(&pGeneration->Refs, 1u,
        XMEMORY_ACQ_REL);
    if ( iPrevious > 1u ) return;
    if ( iPrevious == 0u ) abort();
    if ( pGeneration->UnregisterReady && pGeneration->Unregister != NULL )
        pGeneration->Unregister(pGeneration->ModuleData);
    if ( pGeneration->Tcc != NULL ) xsDestroyTCC(pGeneration->Tcc);
    for ( i = 0u; i < pGeneration->ToolCount; ++i )
        MdoModulesToolUnit(&pGeneration->Tools[i]);
    for ( i = 0u; i < pGeneration->AgentCount; ++i )
        MdoModulesAgentUnit(&pGeneration->Agents[i]);
    MdoModulesStringArrayUnit(pGeneration->Dependencies,
        pGeneration->DependencyCount);
    xrtFree(pGeneration->Tools);
    xrtFree(pGeneration->Agents);
    xrtFree(pGeneration->Id);
    xrtFree(pGeneration->Name);
    xrtFree(pGeneration->Description);
    xrtFree(pGeneration->Version);
    xrtFree(pGeneration->SourcePath);
    memset(pGeneration, 0, sizeof(*pGeneration));
    xrtFree(pGeneration);
}

static bool MdoModulesToolOwnerRetain(void* pUserData)
{
    MdoModuleToolBinding* pTool = (MdoModuleToolBinding*)pUserData;
    return pTool != NULL && MdoModulesGenerationRef(pTool->Owner);
}

static void MdoModulesToolOwnerRelease(void* pUserData)
{
    MdoModuleToolBinding* pTool = (MdoModuleToolBinding*)pUserData;
    if ( pTool != NULL ) MdoModulesGenerationRelease(pTool->Owner);
}

static MdoModuleCatalog* MdoModulesCatalogCreate(uint64 Generation,
    size_t ModuleCount)
{
    MdoModuleCatalog* pCatalog = (MdoModuleCatalog*)xrtCalloc(1u,
        sizeof(*pCatalog));
    if ( pCatalog == NULL ) return NULL;
    if ( ModuleCount != 0u ) {
        pCatalog->Modules = (MdoModuleGeneration**)xrtCalloc(ModuleCount,
            sizeof(*pCatalog->Modules));
        if ( pCatalog->Modules == NULL ) {
            xrtFree(pCatalog);
            return NULL;
        }
    }
    xrtAtomic32Init(&pCatalog->Refs, 1u);
    pCatalog->Generation = Generation;
    pCatalog->ModuleCount = ModuleCount;
    return pCatalog;
}

MdoModuleCatalog* MdoModuleCatalogRef(MdoModuleCatalog* pCatalog)
{
    uint32 iRefs;
    if ( pCatalog == NULL ) return NULL;
    iRefs = xrtAtomic32Load(&pCatalog->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 iExpected = iRefs;
        if ( iRefs == 0u || iRefs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&pCatalog->Refs, &iExpected,
                iRefs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) )
            return pCatalog;
        iRefs = iExpected;
    }
}

void MdoModuleCatalogRelease(MdoModuleCatalog* pCatalog)
{
    size_t i;
    uint32 iPrevious;
    if ( pCatalog == NULL ) return;
    iPrevious = xrtAtomic32FetchSub(&pCatalog->Refs, 1u, XMEMORY_ACQ_REL);
    if ( iPrevious > 1u ) return;
    if ( iPrevious == 0u ) abort();
    for ( i = 0u; i < pCatalog->ModuleCount; ++i )
        MdoModulesGenerationRelease(pCatalog->Modules[i]);
    xrtFree(pCatalog->Modules);
    xrtFree(pCatalog->Tools);
    xrtFree(pCatalog->Agents);
    memset(pCatalog, 0, sizeof(*pCatalog));
    xrtFree(pCatalog);
}

static MdoModuleDiagnostics* MdoModulesDiagnosticsCreate(void)
{
    MdoModuleDiagnostics* pDiagnostics = (MdoModuleDiagnostics*)xrtCalloc(
        1u, sizeof(*pDiagnostics));
    if ( pDiagnostics != NULL ) xrtAtomic32Init(&pDiagnostics->Refs, 1u);
    return pDiagnostics;
}

MdoModuleDiagnostics* MdoModuleDiagnosticsRef(
    MdoModuleDiagnostics* pDiagnostics)
{
    uint32 iRefs;
    if ( pDiagnostics == NULL ) return NULL;
    iRefs = xrtAtomic32Load(&pDiagnostics->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 iExpected = iRefs;
        if ( iRefs == 0u || iRefs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&pDiagnostics->Refs, &iExpected,
                iRefs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) )
            return pDiagnostics;
        iRefs = iExpected;
    }
}

void MdoModuleDiagnosticsRelease(MdoModuleDiagnostics* pDiagnostics)
{
    size_t i;
    uint32 iPrevious;
    if ( pDiagnostics == NULL ) return;
    iPrevious = xrtAtomic32FetchSub(&pDiagnostics->Refs, 1u,
        XMEMORY_ACQ_REL);
    if ( iPrevious > 1u ) return;
    if ( iPrevious == 0u ) abort();
    for ( i = 0u; i < pDiagnostics->Count; ++i ) {
        xrtFree(pDiagnostics->Entries[i].SourcePath);
        xrtFree(pDiagnostics->Entries[i].SourceHash);
        xrtFree(pDiagnostics->Entries[i].Message);
    }
    xrtFree(pDiagnostics->Entries);
    xrtFree(pDiagnostics);
}

static bool MdoModulesDiagnosticAdd(MdoModuleDiagnostics* pDiagnostics,
    MdoModuleDiagnosticStage Stage, cstr SourcePath, cstr SourceHash,
    cstr Message)
{
    MdoModuleDiagnosticEntry* pEntry;
    if ( pDiagnostics == NULL ||
         !MdoModulesGrow((void**)&pDiagnostics->Entries,
            &pDiagnostics->Capacity, pDiagnostics->Count + 1u,
            sizeof(*pDiagnostics->Entries), MDO_MODULE_COUNT_LIMIT) )
        return false;
    pEntry = &pDiagnostics->Entries[pDiagnostics->Count];
    memset(pEntry, 0, sizeof(*pEntry));
    pEntry->Stage = Stage;
    if ( !MdoModulesStringCopy(&pEntry->SourcePath, SourcePath, 4096u, false) ||
         !MdoModulesStringCopy(&pEntry->SourceHash, SourceHash, 64u, false) ||
         !MdoModulesStringCopy(&pEntry->Message, Message,
            MDO_MODULE_ERROR_LIMIT - 1u, true) ) {
        xrtFree(pEntry->SourcePath);
        xrtFree(pEntry->SourceHash);
        xrtFree(pEntry->Message);
        memset(pEntry, 0, sizeof(*pEntry));
        return false;
    }
    pDiagnostics->Count++;
    return true;
}

static void MdoModulesSourceUnit(MdoModuleSource* pSource)
{
    if ( pSource == NULL ) return;
    xrtFree(pSource->VirtualPath);
    xrtFree(pSource->RelativePath);
    memset(pSource, 0, sizeof(*pSource));
}

static void MdoModulesSourcesUnit(MdoModuleSource* pSources, size_t Count)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) MdoModulesSourceUnit(&pSources[i]);
    xrtFree(pSources);
}

static int MdoModulesSourceCompare(const void* pLeft, const void* pRight)
{
    const MdoModuleSource* pA = (const MdoModuleSource*)pLeft;
    const MdoModuleSource* pB = (const MdoModuleSource*)pRight;
    return strcmp(pA->VirtualPath, pB->VirtualPath);
}

static bool MdoModulesExternalSource(cstr RelativePath, bool* pExternal)
{
    xfile File = MdoHomeOpenRead(RelativePath);
    const xerror* pError;

    *pExternal = false;
    if ( File != NULL ) {
        bool bClosed = xrtClose(File);
        if ( !bClosed ) return false;
        *pExternal = true;
        return true;
    }
    pError = xrtGetError();
    if ( pError != NULL && xrtErrorKind(pError) == XERR_NOT_FOUND ) {
        xrtClearError();
        return true;
    }
    return false;
}

static bool MdoModulesDiscoverDirectory(MdoModuleKind Kind, cstr Directory,
    cstr RelativeDirectory, MdoModuleSource** ppSources, size_t* pCount,
    size_t* pCapacity, MdoModuleDiagnostics* pDiagnostics)
{
    xvfs Vfs = MdoHomeApplicationVfs();
    xdir Dir;
    xdirentry Entry;
    xdirnext Next;

    Dir = Vfs != NULL ? xrtVfsDirOpen(Vfs, Directory, XDIR_STAT) : NULL;
    if ( Dir == NULL ) {
        const xerror* pError = xrtGetError();
        if ( pError != NULL && xrtErrorKind(pError) == XERR_NOT_FOUND ) {
            xrtClearError();
            return true;
        }
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_DISCOVERY, Directory, NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "module directory cannot be opened");
        return false;
    }
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        MdoModuleSource* pSource;
        size_t iName = Entry.Name.Size;
        size_t iVirtual = strlen(Directory);
        size_t iRelative = strlen(RelativeDirectory);

        bool Declarative = strcmp(RelativeDirectory, "subagents") == 0 || strcmp(RelativeDirectory,"agents")==0;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Info.Type != XFILE_TYPE_FILE || iName <= (Declarative ? 3u : 2u) ||
             strcmp(Entry.Name.Data + iName - (Declarative ? 3u : 2u),
                 Declarative ? ".md" : ".c") != 0 ) continue;
        if (Declarative || strcmp(RelativeDirectory, "tools") == 0) {
            char Id[65];
            bool Enabled;
            size_t Suffix = Declarative ? 3u : 2u;
            if (iName - Suffix >= sizeof(Id)) continue;
            memcpy(Id, Entry.Name.Data, iName - Suffix); Id[iName - Suffix] = '\0';
            if (!MdoExtensionIdValid(Id)) continue;
            if (!MdoExtensionEnabled(Declarative ? RelativeDirectory : "tools", Id, &Enabled)) {
                (void)xrtDirClose(Dir); return false;
            }
            if (!Enabled) continue;
        }
        if ( *pCount >= MDO_MODULE_COUNT_LIMIT ||
             iVirtual > SIZE_MAX - iName - 2u ||
             iRelative > SIZE_MAX - iName - 2u ||
             !MdoModulesGrow((void**)ppSources, pCapacity, *pCount + 1u,
                sizeof(**ppSources), MDO_MODULE_COUNT_LIMIT) ) {
            (void)xrtDirClose(Dir);
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_DISCOVERY, Directory, NULL,
                "module source count or memory limit was exceeded");
            return false;
        }
        pSource = &(*ppSources)[*pCount];
        memset(pSource, 0, sizeof(*pSource));
        pSource->Kind = Kind;
        pSource->Declarative = Declarative;
        pSource->VirtualPath = (char*)xrtMalloc(iVirtual + iName + 2u);
        pSource->RelativePath = (char*)xrtMalloc(iRelative + iName + 2u);
        if ( pSource->VirtualPath == NULL || pSource->RelativePath == NULL ) {
            MdoModulesSourceUnit(pSource);
            (void)xrtDirClose(Dir);
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_DISCOVERY, Directory, NULL,
                "failed to copy a module source path");
            return false;
        }
        snprintf(pSource->VirtualPath, iVirtual + iName + 2u, "%s/%s",
            Directory, Entry.Name.Data);
        snprintf(pSource->RelativePath, iRelative + iName + 2u, "%s/%s",
            RelativeDirectory, Entry.Name.Data);
        if ( !MdoModulesExternalSource(pSource->RelativePath,
                &pSource->External) ) {
            const xerror* pError = xrtGetError();
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_DISCOVERY, pSource->VirtualPath, NULL,
                pError != NULL ? xrtErrorMessage(pError) :
                "external module source cannot be inspected");
            MdoModulesSourceUnit(pSource);
            (void)xrtDirClose(Dir);
            return false;
        }
        (*pCount)++;
    }
    if ( Next == XDIR_NEXT_ERROR ) {
        const xerror* pError = xrtGetError();
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_DISCOVERY, Directory, NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "module directory enumeration failed");
        (void)xrtDirClose(Dir);
        return false;
    }
    if ( !xrtDirClose(Dir) ) {
        const xerror* pError = xrtGetError();
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_DISCOVERY, Directory, NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "module directory close failed");
        return false;
    }
    return true;
}

static bool MdoModulesDiscover(MdoModuleSource** ppSources, size_t* pCount,
    MdoModuleDiagnostics* pDiagnostics)
{
    size_t iCapacity = 0u;

    *ppSources = NULL;
    *pCount = 0u;
    if ( !MdoModulesDiscoverDirectory(MDO_MODULE_TOOLS,
            "/app/default-home/modules/tools", "modules/tools",
            ppSources, pCount, &iCapacity, pDiagnostics) ||
         !MdoModulesDiscoverDirectory(MDO_MODULE_TOOLS,
            "/app/default-home/tools", "tools",
            ppSources, pCount, &iCapacity, pDiagnostics) ||
         !MdoModulesDiscoverDirectory(MDO_MODULE_AGENTS,
            "/app/default-home/agents", "agents",
            ppSources, pCount, &iCapacity, pDiagnostics) ||
         !MdoModulesDiscoverDirectory(MDO_MODULE_AGENTS,
            "/app/default-home/modules/agents", "modules/agents",
            ppSources, pCount, &iCapacity, pDiagnostics) ||
         !MdoModulesDiscoverDirectory(MDO_MODULE_SUBAGENTS,
            "/app/default-home/modules/subagents", "modules/subagents",
            ppSources, pCount, &iCapacity, pDiagnostics) ||
         !MdoModulesDiscoverDirectory(MDO_MODULE_SUBAGENTS,
            "/app/default-home/subagents", "subagents",
            ppSources, pCount, &iCapacity, pDiagnostics) ) {
        MdoModulesSourcesUnit(*ppSources, *pCount);
        *ppSources = NULL;
        *pCount = 0u;
        return false;
    }
    if ( *pCount > 1u ) qsort(*ppSources, *pCount, sizeof(**ppSources),
        MdoModulesSourceCompare);
    return true;
}

static bool MdoModulesHashField(xsha256* pHash, const void* pData,
    size_t iSize)
{
    static const unsigned char Zero = 0u;
    return xrtSha256Update(pHash, pData, iSize) &&
        xrtSha256Update(pHash, &Zero, 1u);
}

static bool MdoModulesSourceHash(cstr Path, const void* pSource,
    size_t iSource, const void* pHeader, size_t iHeader, char Output[65])
{
    static const char Hex[] = "0123456789abcdef";
    unsigned char Digest[32];
    xsha256 Hash;
    size_t i;

    xrtSha256Init(&Hash);
    if ( !MdoModulesHashField(&Hash, Path, strlen(Path)) ||
         !MdoModulesHashField(&Hash, pSource, iSource) ||
         !MdoModulesHashField(&Hash, pHeader, iHeader) ||
         !MdoModulesHashField(&Hash, MDO_MODULE_COMPILE_ID,
            strlen(MDO_MODULE_COMPILE_ID)) ||
         !xrtSha256Final(&Hash, Digest) ) return false;
    for ( i = 0u; i < sizeof(Digest); ++i ) {
        Output[i * 2u] = Hex[Digest[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Digest[i] & 0x0fu];
    }
    Output[64] = '\0';
    return true;
}

static void MdoModulesTccError(void* pOpaque, const char* Message)
{
    MdoModuleGeneration* pGeneration = (MdoModuleGeneration*)pOpaque;
    size_t iLength;
    size_t iAvailable;

    if ( pGeneration == NULL || Message == NULL ) return;
    iLength = strlen(Message);
    iAvailable = sizeof(pGeneration->CompilerLog) - 1u -
        pGeneration->CompilerLogLength;
    if ( iLength > iAvailable ) iLength = iAvailable;
    if ( iLength != 0u ) {
        memcpy(pGeneration->CompilerLog + pGeneration->CompilerLogLength,
            Message, iLength);
        pGeneration->CompilerLogLength += iLength;
    }
    if ( pGeneration->CompilerLogLength <
         sizeof(pGeneration->CompilerLog) - 1u )
        pGeneration->CompilerLog[pGeneration->CompilerLogLength++] = '\n';
    pGeneration->CompilerLog[pGeneration->CompilerLogLength] = '\0';
}

static void MdoModulesHostLog(void* pContext, mdo_log_level Level,
    const char* Message)
{
    static const char* Levels[] = { "debug", "info", "warning", "error" };
    MdoModuleGeneration* pGeneration = (MdoModuleGeneration*)pContext;
    const char* sLevel = (Level >= MDO_LOG_DEBUG && Level <= MDO_LOG_ERROR)
        ? Levels[(unsigned)Level] : "unknown";
    if ( pGeneration == NULL ||
         (pGeneration->Capabilities & MDO_CAPABILITY_LOG) == 0u ) return;
    printf("[mdo] module[%s] %s: %.2048s\n",
        pGeneration->Id != NULL ? pGeneration->Id : "unregistered",
        sLevel, Message != NULL ? Message : "");
}

static uint64_t MdoModulesHostClock(void* pContext)
{
    MdoModuleGeneration* pGeneration = (MdoModuleGeneration*)pContext;
    return pGeneration != NULL &&
        (pGeneration->Capabilities & MDO_CAPABILITY_CLOCK) != 0u
        ? xrtClock() : 0u;
}

static int64_t MdoModulesHostUnix(void* pContext)
{
    MdoModuleGeneration* pGeneration = (MdoModuleGeneration*)pContext;
    return pGeneration != NULL &&
        (pGeneration->Capabilities & MDO_CAPABILITY_CLOCK) != 0u
        ? (int64_t)xrtNow() : 0;
}

static bool MdoModulesHostCancelled(void* pContext, const void* pCancel)
{
    (void)pContext;
    return pCancel != NULL && xrtCancelRequested((const xcancel*)pCancel);
}

static bool MdoModulesCopyStringArray(char*** ppTarget, size_t Count,
    const char* const* pSource, size_t CountLimit, size_t StringLimit,
    bool ValidateIds)
{
    char** pTarget;
    size_t i;

    *ppTarget = NULL;
    if ( Count == 0u ) return true;
    if ( Count > CountLimit || pSource == NULL ||
         Count > SIZE_MAX / sizeof(*pTarget) ) return false;
    pTarget = (char**)xrtCalloc(Count, sizeof(*pTarget));
    if ( pTarget == NULL ) return false;
    for ( i = 0u; i < Count; ++i ) {
        if ( (ValidateIds && !MdoModulesIdValid(pSource[i])) ||
             !MdoModulesStringCopy(&pTarget[i], pSource[i], StringLimit,
                true) ) {
            MdoModulesStringArrayUnit(pTarget, Count);
            return false;
        }
    }
    *ppTarget = pTarget;
    return true;
}

static bool MdoModulesCopyDescriptor(MdoModuleGeneration* pGeneration,
    const mdo_module_v1* pModule, char* Error, size_t ErrorCapacity)
{
    size_t i;

    if ( pModule == NULL || pModule->Size < sizeof(*pModule) ||
         pModule->AbiVersion != MDO_MODULE_ABI_VERSION ||
         !MdoModulesIdValid(pModule->Id) || pModule->Register == NULL ||
         (pModule->RequiredCapabilities & ~MDO_CAPABILITY_ALL) != 0u ||
         pModule->DependencyCount > MDO_MODULE_DEPENDENCY_LIMIT ||
         (pModule->DependencyCount != 0u && pModule->Dependencies == NULL) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "invalid module descriptor or ABI version");
        return false;
    }
    if ( !MdoModulesStringCopy(&pGeneration->Id, pModule->Id,
            MDO_MODULE_ID_LIMIT, true) ||
         !MdoModulesStringCopy(&pGeneration->Name, pModule->Name,
            MDO_MODULE_NAME_LIMIT, true) ||
         !MdoModulesStringCopy(&pGeneration->Description,
            pModule->Description, MDO_MODULE_DESCRIPTION_LIMIT, true) ||
         !MdoModulesStringCopy(&pGeneration->Version, pModule->Version,
            MDO_MODULE_NAME_LIMIT, true) ||
         !MdoModulesCopyStringArray(&pGeneration->Dependencies,
            pModule->DependencyCount, pModule->Dependencies,
            MDO_MODULE_DEPENDENCY_LIMIT, MDO_MODULE_ID_LIMIT, true) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "module descriptor exceeds a string, dependency, or memory limit");
        return false;
    }
    pGeneration->DependencyCount = pModule->DependencyCount;
    for ( i = 0u; i < pGeneration->DependencyCount; ++i ) {
        size_t j;
        if ( strcmp(pGeneration->Dependencies[i], pGeneration->Id) == 0 ) {
            MdoModulesCopyError(Error, ErrorCapacity,
                "module cannot depend on itself");
            return false;
        }
        for ( j = 0u; j < i; ++j ) {
            if ( strcmp(pGeneration->Dependencies[i],
                    pGeneration->Dependencies[j]) == 0 ) {
                MdoModulesCopyError(Error, ErrorCapacity,
                    "module dependency list contains a duplicate");
                return false;
            }
        }
    }
    pGeneration->Capabilities = pModule->RequiredCapabilities;
    pGeneration->Unregister = pModule->Unregister;
    pGeneration->HostCore.Size = sizeof(pGeneration->HostCore);
    pGeneration->HostCore.AbiVersion = MDO_MODULE_ABI_VERSION;
    pGeneration->HostCore.Context = pGeneration;
    pGeneration->HostCore.Log =
        (pGeneration->Capabilities & MDO_CAPABILITY_LOG) != 0u
        ? MdoModulesHostLog : NULL;
    pGeneration->HostCore.MonotonicMicroseconds =
        (pGeneration->Capabilities & MDO_CAPABILITY_CLOCK) != 0u
        ? MdoModulesHostClock : NULL;
    pGeneration->HostCore.UnixMicroseconds =
        (pGeneration->Capabilities & MDO_CAPABILITY_CLOCK) != 0u
        ? MdoModulesHostUnix : NULL;
    pGeneration->HostCore.CancelRequested = MdoModulesHostCancelled;
    pGeneration->HostServices.Size = sizeof(pGeneration->HostServices);
    pGeneration->HostServices.AbiVersion = MDO_MODULE_ABI_VERSION;
    pGeneration->HostServices.GrantedCapabilities =
        pGeneration->Capabilities;
    pGeneration->HostServices.Core = &pGeneration->HostCore;
    return true;
}

static MdoModuleGeneration* MdoModulesCompile(const MdoModuleSource* pSource,
    uint64 Generation, const void* pHeader, size_t iHeader,
    mdo_module_register_v1* pRegister, MdoModuleDiagnostics* pDiagnostics)
{
    MdoModuleGeneration* pGeneration = NULL;
    mdo_module_entry_v1 Entry;
    const mdo_module_v1* pModule;
    xs_tcc_config Config;
    xerror* pTccError = NULL;
    bytes pBytes = NULL;
    size_t iBytes = 0u;
    char sError[MDO_MODULE_ERROR_LIMIT];
    union {
        void* Pointer;
        mdo_module_entry_v1 Function;
    } EntryCast;

    *pRegister = NULL;
    pBytes = xrtVfsReadAllLimit(MdoHomeApplicationVfs(), pSource->VirtualPath,
        MDO_MODULE_SOURCE_LIMIT, &iBytes);
    if ( pBytes == NULL || MdoModulesContainsZero(pBytes, iBytes) ) {
        const xerror* pError = xrtGetError();
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_READ, pSource->VirtualPath, NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "module source cannot be read or contains an embedded NUL byte");
        xrtFree(pBytes);
        return NULL;
    }
    pGeneration = (MdoModuleGeneration*)xrtCalloc(1u,
        sizeof(*pGeneration));
    if ( pGeneration == NULL ) goto memory_failed;
    xrtAtomic32Init(&pGeneration->Refs, 1u);
    pGeneration->Generation = Generation;
    pGeneration->Kind = pSource->Kind;
    pGeneration->External = pSource->External;
    pGeneration->SourcePath = xrtStrDup(pSource->VirtualPath);
    if ( pGeneration->SourcePath == NULL ||
         !MdoExtensionHashBytes(pBytes,iBytes,pGeneration->SourceRevision) ||
         !MdoModulesSourceHash(pSource->VirtualPath, pBytes, iBytes,
            pHeader, iHeader, pGeneration->SourceHash) ) goto memory_failed;

    xsTccConfigInit(&Config);
    Config.ApplicationVfs = MdoHomeApplicationVfs();
    Config.ApplicationRoot = MdoHomeApplicationRoot();
    Config.Flags = XS_TCC_RESTRICT_HOST_SYMBOLS;
    pGeneration->Tcc = xsCreateTCCEx(&Config, &pTccError);
    if ( pGeneration->Tcc == NULL ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_COMPILE, pSource->VirtualPath,
            pGeneration->SourceHash,
            pTccError != NULL ? xrtErrorMessage(pTccError) :
            "restricted TCC state creation failed");
        xrtErrorFree(pTccError);
        xrtFree(pBytes);
        MdoModulesGenerationRelease(pGeneration);
        return NULL;
    }
    xrtErrorFree(pTccError);
    tcc_set_error_func(pGeneration->Tcc, pGeneration, MdoModulesTccError);
    if ( tcc_set_options(pGeneration->Tcc, MDO_MODULE_COMPILE_OPTIONS) < 0 ||
         tcc_add_quote_include_path(pGeneration->Tcc,
            "/app/generated/module-sdk") < 0 ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_COMPILE, pSource->VirtualPath,
            pGeneration->SourceHash,
            pGeneration->CompilerLog[0] != '\0' ? pGeneration->CompilerLog :
            "module compiler configuration failed");
        xrtFree(pBytes);
        MdoModulesGenerationRelease(pGeneration);
        return NULL;
    }
    tcc_define_symbol(pGeneration->Tcc, "MDO_MODULE_BUILD", "1");
    if ( tcc_compile_string(pGeneration->Tcc, (const char*)pBytes) < 0 ||
         tcc_relocate(pGeneration->Tcc) < 0 ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_COMPILE, pSource->VirtualPath,
            pGeneration->SourceHash,
            pGeneration->CompilerLog[0] != '\0' ? pGeneration->CompilerLog :
            "module compilation or relocation failed");
        xrtFree(pBytes);
        MdoModulesGenerationRelease(pGeneration);
        return NULL;
    }
    xrtFree(pBytes);
    EntryCast.Pointer = tcc_get_symbol(pGeneration->Tcc,
        MDO_MODULE_ENTRY_SYMBOL);
    Entry = EntryCast.Function;
    if ( Entry == NULL || (pModule = Entry()) == NULL ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_ENTRY, pSource->VirtualPath,
            pGeneration->SourceHash,
            "module does not export a valid mdoModuleEntry");
        MdoModulesGenerationRelease(pGeneration);
        return NULL;
    }
    memset(sError, 0, sizeof(sError));
    if ( !MdoModulesCopyDescriptor(pGeneration, pModule, sError,
            sizeof(sError)) ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_VALIDATE, pSource->VirtualPath,
            pGeneration->SourceHash, sError);
        MdoModulesGenerationRelease(pGeneration);
        return NULL;
    }
    *pRegister = pModule->Register;
    return pGeneration;

memory_failed:
    (void)MdoModulesDiagnosticAdd(pDiagnostics,
        MDO_MODULE_DIAGNOSTIC_READ, pSource->VirtualPath,
        pGeneration != NULL ? pGeneration->SourceHash : NULL,
        "failed to allocate module source state");
    xrtFree(pBytes);
    MdoModulesGenerationRelease(pGeneration);
    return NULL;
}

static bool MdoModulesSchemaValid(cstr Json)
{
    xvalue* pValue;
    bool bValid;
    size_t iLength = 0u;
    if ( Json == NULL || Json[0] == '\0' ) return false;
    while ( iLength <= MDO_MODULE_SCHEMA_LIMIT && Json[iLength] != '\0' )
        iLength++;
    if ( iLength > MDO_MODULE_SCHEMA_LIMIT ) return false;
    pValue = xrtJsonParse((xstrview){ Json, iLength });
    bValid = pValue != NULL && xrtValueType(pValue) == XVALUE_OBJECT;
    if ( pValue != NULL ) xrtValueRelease(pValue);
    if ( !bValid ) xrtClearError();
    return bValid;
}

static bool MdoModulesToolCopy(MdoModuleGeneration* pGeneration,
    MdoModuleToolBinding* pTarget, const mdo_tool_v1* pTool,
    char* Error, size_t ErrorCapacity)
{
    mdo_tool_effects uMutating;

    memset(pTarget, 0, sizeof(*pTarget));
    if ( pTool == NULL || pTool->Size < sizeof(*pTool) ||
         pTool->AbiVersion != MDO_MODULE_ABI_VERSION ||
         !MdoModulesIdValid(pTool->Id) ||
         pTool->Effects == 0u ||
         (pTool->Effects & ~MDO_TOOL_EFFECT_ALL) != 0u ||
         (pTool->Flags & ~MDO_TOOL_FLAGS_V1) != 0u ||
         pTool->Execute == NULL || !MdoModulesSchemaValid(pTool->ParametersJson) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "invalid tool descriptor, schema, effects, or ABI version");
        return false;
    }
    uMutating = pTool->Effects & ~((mdo_tool_effects)MDO_TOOL_EFFECT_READ);
    if ( uMutating != 0u && pTool->DescribePermissions == NULL &&
         (!MdoModulesOneBit(uMutating) || pTool->PermissionResource == NULL ||
          pTool->PermissionResource[0] == '\0') ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "mutating tool requires permission discovery or one constant resource");
        return false;
    }
    if ( uMutating == 0u && pTool->PermissionResource != NULL &&
         pTool->PermissionResource[0] != '\0' ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "read-only tool must not declare a mutating permission resource");
        return false;
    }
    if ( !MdoModulesStringCopy(&pTarget->Id, pTool->Id,
            MDO_MODULE_ID_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->Name, pTool->Name,
            MDO_MODULE_NAME_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->Description, pTool->Description,
            MDO_MODULE_DESCRIPTION_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->ParametersJson,
            pTool->ParametersJson, MDO_MODULE_SCHEMA_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->PermissionResource,
            pTool->PermissionResource, MDO_MODULE_DESCRIPTION_LIMIT, false) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "tool descriptor exceeds a string or memory limit");
        MdoModulesToolUnit(pTarget);
        return false;
    }
    if ( pTool->SerialGroup != NULL && pTool->SerialGroup[0] != '\0' ) {
        if ( !MdoModulesStringCopy(&pTarget->SerialGroup,
                pTool->SerialGroup, MDO_MODULE_ID_LIMIT, true) ) {
            MdoModulesCopyError(Error, ErrorCapacity,
                "tool serial group exceeds a string or memory limit");
            MdoModulesToolUnit(pTarget);
            return false;
        }
    } else if ( (pTool->Flags & MDO_TOOL_PARALLEL_SAFE) == 0u ) {
        pTarget->SerialGroup = xrtStrDup(pGeneration->Id);
        if ( pTarget->SerialGroup == NULL ) {
            MdoModulesCopyError(Error, ErrorCapacity,
                "failed to copy the default module serial group");
            MdoModulesToolUnit(pTarget);
            return false;
        }
    }
    pTarget->Owner = pGeneration;
    pTarget->Effects = pTool->Effects;
    pTarget->Flags = pTool->Flags;
    pTarget->MaxResultBytes = pTool->MaxResultBytes;
    pTarget->UserData = pTool->UserData;
    pTarget->DescribePermissions = pTool->DescribePermissions;
    pTarget->Execute = pTool->Execute;
    pTarget->Cancel = pTool->Cancel;
    return true;
}

static bool MdoModulesAgentCopy(MdoModuleGeneration* pGeneration,
    MdoModuleAgentBinding* pTarget, const mdo_agent_v1* pAgent,
    char* Error, size_t ErrorCapacity)
{
    mdo_agent_flags uRoles;

    memset(pTarget, 0, sizeof(*pTarget));
    if ( pAgent == NULL || pAgent->Size < sizeof(*pAgent) ||
         pAgent->AbiVersion != MDO_MODULE_ABI_VERSION ||
         !MdoModulesIdValid(pAgent->Id) ||
         (pAgent->Flags & ~MDO_AGENT_FLAGS_V1) != 0u ||
         (pAgent->AllowedEffects & ~MDO_TOOL_EFFECT_ALL) != 0u ||
         ((pAgent->Acquire != NULL) != (pAgent->Release != NULL)) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "invalid Agent descriptor, effects, lifecycle, or ABI version");
        return false;
    }
    uRoles = pAgent->Flags & (MDO_AGENT_MAIN | MDO_AGENT_SUBAGENT);
    if ( uRoles == 0u ||
         (pGeneration->Kind == MDO_MODULE_SUBAGENTS &&
          (uRoles != MDO_AGENT_SUBAGENT)) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "Agent role exceeds the source directory role");
        return false;
    }
    if ( !MdoModulesStringCopy(&pTarget->Id, pAgent->Id,
            MDO_MODULE_ID_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->Name, pAgent->Name,
            MDO_MODULE_NAME_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->Description, pAgent->Description,
            MDO_MODULE_DESCRIPTION_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->Model, pAgent->Model,
            MDO_MODULE_NAME_LIMIT, false) ||
         !MdoModulesStringCopy(&pTarget->ReasoningEffort,
            pAgent->ReasoningEffort, MDO_MODULE_NAME_LIMIT, false) ||
         !MdoModulesStringCopy(&pTarget->SystemPrompt, pAgent->SystemPrompt,
            MDO_MODULE_PROMPT_LIMIT, true) ||
         !MdoModulesStringCopy(&pTarget->PermissionProfile,
            pAgent->PermissionProfile, MDO_MODULE_NAME_LIMIT, false) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "Agent descriptor exceeds a string or memory limit");
        MdoModulesAgentUnit(pTarget);
        return false;
    }
    if ( !MdoModulesCopyStringArray(&pTarget->Tools, pAgent->ToolCount,
            pAgent->Tools, MDO_MODULE_AGENT_TOOL_LIMIT,
            MDO_MODULE_ID_LIMIT, true) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "Agent tool list exceeds a count, string, or memory limit");
        MdoModulesAgentUnit(pTarget);
        return false;
    }
    pTarget->ToolCount = pAgent->ToolCount;
    {
        size_t i;
        for ( i = 0u; i < pTarget->ToolCount; ++i ) {
            size_t j;
            for ( j = 0u; j < i; ++j ) {
                if ( strcmp(pTarget->Tools[i], pTarget->Tools[j]) == 0 ) {
                    MdoModulesCopyError(Error, ErrorCapacity,
                        "Agent tool list contains a duplicate ID");
                    MdoModulesAgentUnit(pTarget);
                    return false;
                }
            }
        }
    }
    if ( !MdoModulesCopyStringArray(&pTarget->Skills, pAgent->SkillCount,
            pAgent->Skills, MDO_MODULE_AGENT_SKILL_LIMIT,
            MDO_MODULE_ID_LIMIT, true) ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "Agent skill list exceeds a count, string, or memory limit");
        MdoModulesAgentUnit(pTarget);
        return false;
    }
    pTarget->SkillCount = pAgent->SkillCount;
    {
        size_t i;
        for ( i = 0u; i < pTarget->SkillCount; ++i ) {
            size_t j;
            for ( j = 0u; j < i; ++j ) {
                if ( strcmp(pTarget->Skills[i], pTarget->Skills[j]) == 0 ) {
                    MdoModulesCopyError(Error, ErrorCapacity,
                        "Agent skill list contains a duplicate ID");
                    MdoModulesAgentUnit(pTarget);
                    return false;
                }
            }
        }
    }
    pTarget->Owner = pGeneration;
    pTarget->ContextWindowTokens = pAgent->ContextWindowTokens;
    pTarget->MaxInputTokens = pAgent->MaxInputTokens;
    pTarget->MaxOutputTokens = pAgent->MaxOutputTokens;
    pTarget->MaxTurns = pAgent->MaxTurns;
    pTarget->TimeoutMilliseconds = pAgent->TimeoutMilliseconds;
    pTarget->MaxFinalBytes = pAgent->MaxFinalBytes;
    pTarget->AllowedEffects = pAgent->AllowedEffects;
    pTarget->MaxDepth = pAgent->MaxDepth;
    pTarget->Flags = pAgent->Flags;
    pTarget->UserData = pAgent->UserData;
    pTarget->Acquire = pAgent->Acquire;
    pTarget->Release = pAgent->Release;
    return true;
}

static mdo_result MdoModulesRegistrarAddTool(void* pContext,
    const mdo_tool_v1* pTool, char* Error, size_t ErrorCapacity)
{
    MdoModuleRegistrarContext* pRegistrar =
        (MdoModuleRegistrarContext*)pContext;
    MdoModuleGeneration* pGeneration;
    MdoModuleToolBinding Tool;
    size_t i;

    if ( pRegistrar == NULL || pRegistrar->Generation == NULL ||
         pRegistrar->Failed ) return MDO_RESULT_ERROR;
    pGeneration = pRegistrar->Generation;
    if ( pGeneration->Kind != MDO_MODULE_TOOLS ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "this module directory cannot register tools");
        goto failed;
    }
    if ( !MdoModulesToolCopy(pGeneration, &Tool, pTool,
            Error, ErrorCapacity) ) goto failed;
    for ( i = 0u; i < pGeneration->ToolCount; ++i ) {
        if ( strcmp(pGeneration->Tools[i].Id, Tool.Id) == 0 ) {
            MdoModulesToolUnit(&Tool);
            MdoModulesCopyError(Error, ErrorCapacity,
                "module registered a duplicate tool ID");
            goto failed;
        }
    }
    if ( !MdoModulesGrow((void**)&pGeneration->Tools,
            &pGeneration->ToolCapacity, pGeneration->ToolCount + 1u,
            sizeof(*pGeneration->Tools), MDO_MODULE_TOOL_LIMIT) ) {
        MdoModulesToolUnit(&Tool);
        MdoModulesCopyError(Error, ErrorCapacity,
            "module tool count or memory limit was exceeded");
        goto failed;
    }
    pGeneration->Tools[pGeneration->ToolCount++] = Tool;
    return MDO_RESULT_OK;

failed:
    pRegistrar->Failed = true;
    MdoModulesCopyError(pRegistrar->Message, sizeof(pRegistrar->Message),
        Error != NULL && Error[0] != '\0' ? Error :
        "module tool registration failed");
    return MDO_RESULT_ERROR;
}

static mdo_result MdoModulesRegistrarAddAgent(void* pContext,
    const mdo_agent_v1* pAgent, char* Error, size_t ErrorCapacity)
{
    MdoModuleRegistrarContext* pRegistrar =
        (MdoModuleRegistrarContext*)pContext;
    MdoModuleGeneration* pGeneration;
    MdoModuleAgentBinding Agent;
    size_t i;

    if ( pRegistrar == NULL || pRegistrar->Generation == NULL ||
         pRegistrar->Failed ) return MDO_RESULT_ERROR;
    pGeneration = pRegistrar->Generation;
    if ( pGeneration->Kind == MDO_MODULE_TOOLS ) {
        MdoModulesCopyError(Error, ErrorCapacity,
            "this module directory cannot register Agents");
        goto failed;
    }
    if ( !MdoModulesAgentCopy(pGeneration, &Agent, pAgent,
            Error, ErrorCapacity) ) goto failed;
    for ( i = 0u; i < pGeneration->AgentCount; ++i ) {
        if ( strcmp(pGeneration->Agents[i].Id, Agent.Id) == 0 ) {
            MdoModulesAgentUnit(&Agent);
            MdoModulesCopyError(Error, ErrorCapacity,
                "module registered a duplicate Agent ID");
            goto failed;
        }
    }
    if ( !MdoModulesGrow((void**)&pGeneration->Agents,
            &pGeneration->AgentCapacity, pGeneration->AgentCount + 1u,
            sizeof(*pGeneration->Agents), MDO_MODULE_AGENT_LIMIT) ) {
        MdoModulesAgentUnit(&Agent);
        MdoModulesCopyError(Error, ErrorCapacity,
            "module Agent count or memory limit was exceeded");
        goto failed;
    }
    pGeneration->Agents[pGeneration->AgentCount++] = Agent;
    return MDO_RESULT_OK;

failed:
    pRegistrar->Failed = true;
    MdoModulesCopyError(pRegistrar->Message, sizeof(pRegistrar->Message),
        Error != NULL && Error[0] != '\0' ? Error :
        "module Agent registration failed");
    return MDO_RESULT_ERROR;
}

static bool MdoModulesRegister(MdoModuleGeneration* pGeneration,
    mdo_module_register_v1 Register, MdoModuleDiagnostics* pDiagnostics)
{
    MdoModuleRegistrarContext Context;
    mdo_registrar_v1 Registrar;
    mdo_result Result;
    char sError[MDO_MODULE_ERROR_LIMIT];

    memset(&Context, 0, sizeof(Context));
    Context.Generation = pGeneration;
    memset(&Registrar, 0, sizeof(Registrar));
    Registrar.Size = sizeof(Registrar);
    Registrar.AbiVersion = MDO_MODULE_ABI_VERSION;
    Registrar.Context = &Context;
    Registrar.AddTool = MdoModulesRegistrarAddTool;
    Registrar.AddAgent = MdoModulesRegistrarAddAgent;
    memset(sError, 0, sizeof(sError));
    Result = Register(&pGeneration->HostServices, &Registrar,
        &pGeneration->ModuleData, sError, sizeof(sError));
    pGeneration->UnregisterReady = Result == MDO_RESULT_OK ||
        pGeneration->ModuleData != NULL;
    if ( Result != MDO_RESULT_OK || Context.Failed ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_REGISTER, pGeneration->SourcePath,
            pGeneration->SourceHash,
            Context.Failed && Context.Message[0] != '\0' ? Context.Message :
            (sError[0] != '\0' ? sError : "module Register callback failed"));
        return false;
    }
    if ( (pGeneration->Kind == MDO_MODULE_TOOLS &&
          pGeneration->ToolCount == 0u) ||
         (pGeneration->Kind != MDO_MODULE_TOOLS &&
          pGeneration->AgentCount == 0u) ) {
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_VALIDATE, pGeneration->SourcePath,
            pGeneration->SourceHash,
            "module registered no descriptor for its source directory");
        return false;
    }
    return true;
}

static size_t MdoModulesFindModule(const MdoModuleCatalog* pCatalog,
    cstr Id)
{
    size_t i;
    for ( i = 0u; i < pCatalog->ModuleCount; ++i ) {
        if ( pCatalog->Modules[i] != NULL &&
             strcmp(pCatalog->Modules[i]->Id, Id) == 0 ) return i;
    }
    return SIZE_MAX;
}

static bool MdoModulesRegistrationOrder(const MdoModuleCatalog* pCatalog,
    size_t* pOrder, MdoModuleDiagnostics* pDiagnostics)
{
    bool pDone[MDO_MODULE_COUNT_LIMIT];
    size_t i;
    size_t iOut = 0u;

    memset(pDone, 0, sizeof(pDone));
    for ( i = 0u; i < pCatalog->ModuleCount; ++i ) {
        size_t j;
        MdoModuleGeneration* pModule = pCatalog->Modules[i];
        for ( j = 0u; j < i; ++j ) {
            if ( strcmp(pCatalog->Modules[j]->Id, pModule->Id) == 0 ) {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_VALIDATE, pModule->SourcePath,
                    pModule->SourceHash, "duplicate module ID");
                return false;
            }
        }
        for ( j = 0u; j < pModule->DependencyCount; ++j ) {
            if ( MdoModulesFindModule(pCatalog,
                    pModule->Dependencies[j]) == SIZE_MAX ) {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_VALIDATE, pModule->SourcePath,
                    pModule->SourceHash, "module dependency does not exist");
                return false;
            }
        }
    }
    while ( iOut < pCatalog->ModuleCount ) {
        bool bProgress = false;
        for ( i = 0u; i < pCatalog->ModuleCount; ++i ) {
            size_t j;
            bool bReady = true;
            if ( pDone[i] ) continue;
            for ( j = 0u; j < pCatalog->Modules[i]->DependencyCount; ++j ) {
                size_t iDependency = MdoModulesFindModule(pCatalog,
                    pCatalog->Modules[i]->Dependencies[j]);
                if ( iDependency == SIZE_MAX || !pDone[iDependency] ) {
                    bReady = false;
                    break;
                }
            }
            if ( !bReady ) continue;
            pDone[i] = true;
            pOrder[iOut++] = i;
            bProgress = true;
        }
        if ( !bProgress ) {
            for ( i = 0u; i < pCatalog->ModuleCount; ++i ) {
                if ( !pDone[i] ) {
                    (void)MdoModulesDiagnosticAdd(pDiagnostics,
                        MDO_MODULE_DIAGNOSTIC_VALIDATE,
                        pCatalog->Modules[i]->SourcePath,
                        pCatalog->Modules[i]->SourceHash,
                        "module dependency graph contains a cycle");
                    break;
                }
            }
            return false;
        }
    }
    return true;
}

static MdoModuleToolBinding* MdoModulesFindTool(
    const MdoModuleCatalog* pCatalog, cstr Id)
{
    size_t i;
    for ( i = 0u; i < pCatalog->ToolCount; ++i ) {
        if ( strcmp(pCatalog->Tools[i]->Id, Id) == 0 )
            return pCatalog->Tools[i];
    }
    return NULL;
}

static bool MdoModulesRuntimeTool(cstr Id, xwork_tool_effects* pEffects)
{
    const MdoBuiltinTool* Builtin = MdoBuiltinToolFind(Id);
    xwork_tool_catalog* pCatalog =
        xworkRuntimeToolCatalogSnapshot(g_MdoModules.Runtime);
    xwork_tool_info Info;
    bool bFound = false;
    size_t i;

    if ( Builtin != NULL ) {
        *pEffects = Builtin->Effects;
        xworkToolCatalogRelease(pCatalog);
        return true;
    }
    if ( pCatalog == NULL ) return false;
    for ( i = 0u; i < xworkToolCatalogCount(pCatalog); ++i ) {
        memset(&Info, 0, sizeof(Info));
        if ( xworkToolCatalogToolAt(pCatalog, i, &Info) &&
             Info.sSource != NULL &&
             strcmp(Info.sSource, MDO_MODULE_SOURCE) != 0 &&
             strcmp(Info.sName, Id) == 0 ) {
            *pEffects = Info.uEffects;
            bFound = true;
            break;
        }
    }
    xworkToolCatalogRelease(pCatalog);
    return bFound;
}

static bool MdoModulesValidateCatalog(MdoModuleCatalog* pCatalog,
    MdoModuleDiagnostics* pDiagnostics)
{
    size_t i;
    size_t iTool = 0u;
    size_t iAgent = 0u;

    for ( i = 0u; i < pCatalog->ModuleCount; ++i ) {
        MdoModuleGeneration* pModule = pCatalog->Modules[i];
        if (pModule->Kind==MDO_MODULE_TOOLS && pModule->ToolCount==0u &&
            !strncmp(pModule->SourcePath,"/app/default-home/tools/",sizeof("/app/default-home/tools/")-1u)) {
            (void)MdoModulesDiagnosticAdd(pDiagnostics,MDO_MODULE_DIAGNOSTIC_VALIDATE,pModule->SourcePath,pModule->SourceHash,
                "A managed C tool file must register at least one tool");
            return false;
        }
        if ( pModule->ToolCount > MDO_MODULE_TOOL_LIMIT - iTool ||
             pModule->AgentCount > MDO_MODULE_AGENT_LIMIT - iAgent ) {
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_VALIDATE, pModule->SourcePath,
                pModule->SourceHash,
                "complete module catalog exceeds a descriptor count limit");
            return false;
        }
        iTool += pModule->ToolCount;
        iAgent += pModule->AgentCount;
    }
    if ( iTool != 0u ) {
        pCatalog->Tools = (MdoModuleToolBinding**)xrtCalloc(iTool,
            sizeof(*pCatalog->Tools));
        if ( pCatalog->Tools == NULL ) return false;
    }
    if ( iAgent != 0u ) {
        pCatalog->Agents = (MdoModuleAgentBinding**)xrtCalloc(iAgent,
            sizeof(*pCatalog->Agents));
        if ( pCatalog->Agents == NULL ) return false;
    }
    for ( i = 0u; i < pCatalog->ModuleCount; ++i ) {
        size_t j;
        MdoModuleGeneration* pModule = pCatalog->Modules[i];
        for ( j = 0u; j < pModule->ToolCount; ++j )
            pCatalog->Tools[pCatalog->ToolCount++] = &pModule->Tools[j];
        for ( j = 0u; j < pModule->AgentCount; ++j )
            pCatalog->Agents[pCatalog->AgentCount++] = &pModule->Agents[j];
    }
    for ( i = 0u; i < pCatalog->ToolCount; ++i ) {
        size_t j;
        if (strncmp(pCatalog->Tools[i]->Owner->SourcePath, "/app/default-home/tools/", sizeof("/app/default-home/tools/") - 1u) == 0 &&
            MdoBuiltinToolFind(MdoBuiltinToolCanonicalId(pCatalog->Tools[i]->Id)) != NULL) {
            (void)MdoModulesDiagnosticAdd(pDiagnostics, MDO_MODULE_DIAGNOSTIC_VALIDATE,
                pCatalog->Tools[i]->Owner->SourcePath, pCatalog->Tools[i]->Owner->SourceHash,
                "Custom tools cannot replace built-in tool IDs");
            return false;
        }
        for ( j = 0u; j < i; ++j ) {
            if ( strcmp(pCatalog->Tools[i]->Id,
                    pCatalog->Tools[j]->Id) == 0 ) {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_VALIDATE,
                    pCatalog->Tools[i]->Owner->SourcePath,
                    pCatalog->Tools[i]->Owner->SourceHash,
                    "duplicate tool ID across modules");
                return false;
            }
        }
    }
    for ( i = 0u; i < pCatalog->AgentCount; ++i ) {
        size_t j;
        MdoModuleAgentBinding* pAgent = pCatalog->Agents[i];
        for ( j = 0u; j < i; ++j ) {
            if ( strcmp(pAgent->Id, pCatalog->Agents[j]->Id) == 0 ) {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_VALIDATE,
                    pAgent->Owner->SourcePath, pAgent->Owner->SourceHash,
                    "duplicate Agent ID across modules");
                return false;
            }
        }
        for ( j = 0u; j < pAgent->ToolCount; ++j ) {
            MdoModuleToolBinding* pTool = MdoModulesFindTool(pCatalog,
                pAgent->Tools[j]);
            xwork_tool_effects uEffects = 0u;
            if ( pTool != NULL ) uEffects = (xwork_tool_effects)pTool->Effects;
            else if ( !MdoModulesRuntimeTool(pAgent->Tools[j], &uEffects) ) {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_VALIDATE,
                    pAgent->Owner->SourcePath, pAgent->Owner->SourceHash,
                    "Agent references an unknown tool ID");
                return false;
            }
            if ( (uEffects & ~(xwork_tool_effects)pAgent->AllowedEffects) != 0u ) {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_VALIDATE,
                    pAgent->Owner->SourcePath, pAgent->Owner->SourceHash,
                    "Agent tool exceeds its allowed effect ceiling");
                return false;
            }
        }
    }
    return true;
}

static void MdoModulesXworkError(xwork_error* pError,
    xwork_error_code Code, cstr Message)
{
    if ( pError == NULL ) return;
    xworkErrorInit(pError);
    pError->eCode = Code;
    snprintf(pError->sMessage, sizeof(pError->sMessage), "%s",
        Message != NULL && Message[0] != '\0' ? Message :
        "module tool callback failed");
}

static xwork_result MdoModulesResult(mdo_result Result)
{
    switch ( Result ) {
    case MDO_RESULT_OK: return XWORK_RESULT_OK;
    case MDO_RESULT_CANCELLED: return XWORK_RESULT_CANCELLED;
    case MDO_RESULT_LIMIT: return XWORK_RESULT_LIMIT;
    case MDO_RESULT_TIMEOUT: return XWORK_RESULT_TIMEOUT;
    default: return XWORK_RESULT_ERROR;
    }
}

static void MdoModulesToolContext(mdo_tool_context_v1* pTarget,
    const MdoModuleToolBinding* pTool, const xwork_tool_context* pSource)
{
    memset(pTarget, 0, sizeof(*pTarget));
    pTarget->Size = sizeof(*pTarget);
    pTarget->AbiVersion = MDO_MODULE_ABI_VERSION;
    pTarget->ToolId = pTool->Id;
    pTarget->ToolCallId = pSource->sToolCallId;
    pTarget->WorkspaceRoot = pSource->sWorkspaceRoot;
    pTarget->AgentTurn = pSource->uAgentTurn;
    pTarget->AgentId = pSource->uAgentId;
    pTarget->RunId = pSource->uRunId;
    pTarget->CatalogGeneration = pSource->uCatalogGeneration;
    pTarget->Deadline = pSource->uDeadline;
    pTarget->CancelToken = pSource->pCancel;
}

static bool MdoModulesPermissionAdd(void* pContext,
    mdo_resource_kind Kind, mdo_resource_access Access, const char* Resource)
{
    return xworkPermissionResourceWriterAdd(
        (xwork_permission_resource_writer*)pContext,
        (xwork_resource_kind)Kind, (xwork_resource_access)Access, Resource);
}

static bool MdoModulesResultWrite(void* pContext, const char* Text,
    size_t Length)
{
    return xworkToolResultWriterWrite((xwork_tool_result_writer*)pContext,
        Text, Length);
}

static bool MdoModulesResultWriteText(void* pContext, const char* Text)
{
    return xworkToolResultWriterWriteText(
        (xwork_tool_result_writer*)pContext, Text);
}

static bool MdoModulesResultSetImage(void* pContext,
    const unsigned char* Data, size_t Size, const char* Mime)
{
    return xworkToolResultWriterSetImage(
        (xwork_tool_result_writer*)pContext, Data, Size, Mime);
}

static bool MdoModulesResultSetSuccess(void* pContext, bool Success)
{
    return xworkToolResultWriterSetSuccess(
        (xwork_tool_result_writer*)pContext, Success);
}

static bool MdoModulesStaticPermission(MdoModuleToolBinding* pTool,
    xwork_permission_resource_writer* pWriter)
{
    mdo_tool_effects uEffect = pTool->Effects &
        ~((mdo_tool_effects)MDO_TOOL_EFFECT_READ);
    xwork_resource_kind Kind = XWORK_RESOURCE_NONE;
    xwork_resource_access Access = 0u;

    switch ( uEffect ) {
    case MDO_TOOL_EFFECT_WORKSPACE_WRITE:
        Kind = XWORK_RESOURCE_PATH; Access = XWORK_RESOURCE_ACCESS_WRITE; break;
    case MDO_TOOL_EFFECT_PROCESS:
        Kind = XWORK_RESOURCE_COMMAND; Access = XWORK_RESOURCE_ACCESS_EXECUTE; break;
    case MDO_TOOL_EFFECT_NETWORK:
        Kind = XWORK_RESOURCE_NETWORK; Access = XWORK_RESOURCE_ACCESS_CONNECT; break;
    case MDO_TOOL_EFFECT_EXTERNAL_SERVICE:
        Kind = XWORK_RESOURCE_EXTERNAL_SERVICE; Access = XWORK_RESOURCE_ACCESS_USE; break;
    case MDO_TOOL_EFFECT_SECRETS:
        Kind = XWORK_RESOURCE_SECRET; Access = XWORK_RESOURCE_ACCESS_USE; break;
    case MDO_TOOL_EFFECT_SCHEDULE:
        Kind = XWORK_RESOURCE_SCHEDULE; Access = XWORK_RESOURCE_ACCESS_CONTROL; break;
    case MDO_TOOL_EFFECT_AGENT_DELEGATION:
        Kind = XWORK_RESOURCE_AGENT; Access = XWORK_RESOURCE_ACCESS_CONTROL; break;
    default: return false;
    }
    return xworkPermissionResourceWriterAdd(pWriter, Kind, Access,
        pTool->PermissionResource);
}

static xwork_result MdoModulesDescribePermissions(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_permission_resource_writer* pWriter, xwork_error* pError)
{
    MdoModuleToolBinding* pTool = (MdoModuleToolBinding*)pUserData;
    mdo_tool_context_v1 Context;
    mdo_permission_writer_v1 Writer;
    mdo_result Result;
    char sError[MDO_MODULE_ERROR_LIMIT];

    if ( pTool == NULL || pContext == NULL || pWriter == NULL ) {
        MdoModulesXworkError(pError, XWORK_ERROR_INVALID_ARGUMENT,
            "module permission callback received an invalid context");
        return XWORK_RESULT_ERROR;
    }
    if ( pTool->DescribePermissions == NULL ) {
        if ( MdoModulesStaticPermission(pTool, pWriter) )
            return XWORK_RESULT_OK;
        MdoModulesXworkError(pError, XWORK_ERROR_TOOL,
            "constant module permission resource is invalid");
        return XWORK_RESULT_ERROR;
    }
    MdoModulesToolContext(&Context, pTool, pContext);
    memset(&Writer, 0, sizeof(Writer));
    Writer.Size = sizeof(Writer);
    Writer.AbiVersion = MDO_MODULE_ABI_VERSION;
    Writer.Context = pWriter;
    Writer.Add = MdoModulesPermissionAdd;
    memset(sError, 0, sizeof(sError));
    Result = pTool->DescribePermissions(pTool->UserData, &Context,
        ArgumentsJson, &Writer, sError, sizeof(sError));
    if ( Result != MDO_RESULT_OK )
        MdoModulesXworkError(pError,
            Result == MDO_RESULT_LIMIT ? XWORK_ERROR_LIMIT :
            (Result == MDO_RESULT_CANCELLED ? XWORK_ERROR_CANCELLED :
            (Result == MDO_RESULT_TIMEOUT ? XWORK_ERROR_TIMEOUT :
            XWORK_ERROR_TOOL)), sError);
    return MdoModulesResult(Result);
}

static xwork_result MdoModulesExecute(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_tool_result_writer* pWriter, xwork_error* pError)
{
    MdoModuleToolBinding* pTool = (MdoModuleToolBinding*)pUserData;
    mdo_tool_context_v1 Context;
    mdo_result_writer_v1 Writer;
    mdo_result Result;
    char sError[MDO_MODULE_ERROR_LIMIT];

    if ( pTool == NULL || pContext == NULL || pWriter == NULL ||
         pTool->Execute == NULL ) {
        MdoModulesXworkError(pError, XWORK_ERROR_INVALID_ARGUMENT,
            "module execute callback received an invalid context");
        return XWORK_RESULT_ERROR;
    }
    MdoModulesToolContext(&Context, pTool, pContext);
    memset(&Writer, 0, sizeof(Writer));
    Writer.Size = sizeof(Writer);
    Writer.AbiVersion = MDO_MODULE_ABI_VERSION;
    Writer.Context = pWriter;
    Writer.Write = MdoModulesResultWrite;
    Writer.WriteText = MdoModulesResultWriteText;
    Writer.SetImage = MdoModulesResultSetImage;
    Writer.SetSuccess = MdoModulesResultSetSuccess;
    memset(sError, 0, sizeof(sError));
    Result = pTool->Execute(pTool->UserData, &Context, ArgumentsJson,
        &Writer, sError, sizeof(sError));
    if ( Result != MDO_RESULT_OK )
        MdoModulesXworkError(pError,
            Result == MDO_RESULT_LIMIT ? XWORK_ERROR_LIMIT :
            (Result == MDO_RESULT_CANCELLED ? XWORK_ERROR_CANCELLED :
            (Result == MDO_RESULT_TIMEOUT ? XWORK_ERROR_TIMEOUT :
            XWORK_ERROR_TOOL)), sError);
    return MdoModulesResult(Result);
}

static xwork_tool_definition* MdoModulesDefinitions(
    const MdoModuleCatalog* pCatalog)
{
    xwork_tool_definition* pDefinitions;
    size_t i;

    if ( pCatalog->ToolCount == 0u ) return NULL;
    pDefinitions = (xwork_tool_definition*)xrtCalloc(pCatalog->ToolCount,
        sizeof(*pDefinitions));
    if ( pDefinitions == NULL ) return NULL;
    for ( i = 0u; i < pCatalog->ToolCount; ++i ) {
        MdoModuleToolBinding* pTool = pCatalog->Tools[i];
        pDefinitions[i].sName = pTool->Id;
        pDefinitions[i].sDescription = pTool->Description;
        pDefinitions[i].sParametersJson = pTool->ParametersJson;
        pDefinitions[i].bStrict = (pTool->Flags & MDO_TOOL_STRICT) != 0u;
        pDefinitions[i].uEffects = (xwork_tool_effects)pTool->Effects;
        pDefinitions[i].pUserData = pTool;
        pDefinitions[i].sSource = MDO_MODULE_SOURCE;
        pDefinitions[i].OnDescribePermissions =
            pTool->DescribePermissions != NULL ||
            pTool->PermissionResource != NULL
            ? MdoModulesDescribePermissions : NULL;
        pDefinitions[i].OnExecuteV2 = MdoModulesExecute;
        pDefinitions[i].iMaxResultBytes = pTool->MaxResultBytes;
        pDefinitions[i].OnOwnerRetain = MdoModulesToolOwnerRetain;
        pDefinitions[i].OnOwnerRelease = MdoModulesToolOwnerRelease;
        pDefinitions[i].bParallelSafe =
            (pTool->Flags & MDO_TOOL_PARALLEL_SAFE) != 0u;
        pDefinitions[i].sSerialGroup = pTool->SerialGroup;
    }
    return pDefinitions;
}

static MdoModuleGeneration* MdoModulesReadAgentProfile(const MdoModuleSource* Source,
    uint64 Generation, MdoModuleDiagnostics* Diagnostics)
{
    char Id[65], Error[MDO_MODULE_ERROR_LIMIT] = {0};
    const char* Base = strrchr(Source->RelativePath, '/');
    char* Text = MdoExtensionRead(Source->RelativePath, false, MDO_EXTENSION_TEXT_LIMIT, NULL);
    MdoModuleGeneration* Module = NULL;
    MdoAgentFile File;
    MdoModuleRegistrarContext Registrar;
    size_t Length = Base != NULL ? strlen(Base + 1) : 0u;
    bool Parsed = false;
    memset(&File, 0, sizeof(File));
    if (Length < 4u || Length - 3u >= sizeof(Id) || Text == NULL) goto failed;
    memcpy(Id, Base + 1, Length - 3u); Id[Length - 3u] = '\0';
    if (!MdoAgentFileParse(Id, Text,Source->Kind==MDO_MODULE_AGENTS, &File, Error, sizeof(Error))) goto failed;
    Parsed = true;
    Module = (MdoModuleGeneration*)xrtCalloc(1u, sizeof(*Module));
    if (Module == NULL) goto failed;
    xrtAtomic32Init(&Module->Refs, 1u);
    Module->Generation = Generation; Module->Kind = Source->Kind;
    Module->Profile=Source->Kind==MDO_MODULE_AGENTS; Module->UseCode=File.UseCode;
    Module->External = Source->External;
    { char ModuleId[128]; snprintf(ModuleId,sizeof(ModuleId),"%s%s",Module->Profile?"profile.":"",File.Id);
      Module->Id = xrtStrDup(ModuleId); }
    Module->Name = xrtStrDup(File.Name);
    Module->Description = xrtStrDup(File.Description);
    Module->Version = xrtStrDup("1");
    Module->SourcePath = xrtStrDup(Source->VirtualPath);
    if (Module->Id == NULL || Module->Name == NULL || Module->Description == NULL ||
        Module->Version == NULL || Module->SourcePath == NULL ||
        !MdoExtensionHash(Text, Module->SourceHash) || !MdoExtensionHash(Text,Module->SourceRevision)) goto failed;
    memset(&Registrar, 0, sizeof(Registrar)); Registrar.Generation = Module;
    if (MdoModulesRegistrarAddAgent(&Registrar, &File.Agent, Error, sizeof(Error)) != MDO_RESULT_OK)
        goto failed;
    MdoAgentFileUnit(&File); xrtFree(Text); return Module;
failed:
    if (Parsed) MdoAgentFileUnit(&File);
    xrtFree(Text); MdoModulesGenerationRelease(Module);
    (void)MdoModulesDiagnosticAdd(Diagnostics, MDO_MODULE_DIAGNOSTIC_VALIDATE,
        Source->VirtualPath, NULL, Error[0] != '\0' ? Error : "Cannot read Agent definition");
    return NULL;
}

static bool MdoModulesApplyProfiles(MdoModuleCatalog* Catalog,MdoModuleDiagnostics* Diagnostics)
{
    size_t i,j,k;
    for (i=0u;i<Catalog->ModuleCount;++i) {
        MdoModuleGeneration* Profile=Catalog->Modules[i]; MdoModuleAgentBinding* Config;
        MdoModuleAgentBinding* Native=NULL; MdoModuleGeneration* Owner=NULL;
        if (!Profile->Profile || Profile->AgentCount!=1u) continue;
        Config=&Profile->Agents[0];
        for (j=0u;j<Catalog->ModuleCount;++j) {
            if (Catalog->Modules[j]->Profile) continue;
            for (k=0u;k<Catalog->Modules[j]->AgentCount;++k)
                if (!strcmp(Catalog->Modules[j]->Agents[k].Id,Config->Id)) {
                    if (Native) goto invalid;
                    Native=&Catalog->Modules[j]->Agents[k]; Owner=Catalog->Modules[j];
                }
        }
        if (!Native) { if (Profile->UseCode) goto invalid; continue; }
        if (!(Native->Flags&MDO_AGENT_MAIN)) goto invalid;
        /* Both descriptors are owned by this unpublished candidate. Move the
         * ordinary profile onto its C owner so callbacks keep their TCC lifetime.
         * C mode owns the base prompt/lifecycle/budgets; the UI owns tool scope. */
        if (Profile->UseCode) {
            xrtFree(Config->SystemPrompt); Config->SystemPrompt=Native->SystemPrompt; Native->SystemPrompt=NULL;
            Config->UserData=Native->UserData; Config->Acquire=Native->Acquire; Config->Release=Native->Release;
            Config->ContextWindowTokens=Native->ContextWindowTokens; Config->MaxInputTokens=Native->MaxInputTokens;
            Config->MaxOutputTokens=Native->MaxOutputTokens; Config->MaxTurns=Native->MaxTurns;
            Config->TimeoutMilliseconds=Native->TimeoutMilliseconds; Config->MaxFinalBytes=Native->MaxFinalBytes;
            Config->AllowedEffects &= Native->AllowedEffects;
            if (Native->MaxDepth<Config->MaxDepth) Config->MaxDepth=Native->MaxDepth;
        }
        MdoModulesAgentUnit(Native); *Native=*Config; Native->Owner=Owner;
        memset(Config,0,sizeof(*Config)); Profile->AgentCount=0u;
        continue;
invalid:
        (void)MdoModulesDiagnosticAdd(Diagnostics,MDO_MODULE_DIAGNOSTIC_VALIDATE,Profile->SourcePath,Profile->SourceHash,
            "C mode requires exactly one C Agent with the same ID; check modules/agents and the Agent role");
        return false;
    }
    /* A disabled canonical main profile must also suppress its C counterpart;
     * otherwise disabling a restrictive profile could expose a broader Agent. */
    for (i=0u;i<Catalog->ModuleCount;++i) {
        MdoModuleGeneration* Module=Catalog->Modules[i];
        for (j=0u;j<Module->AgentCount;) {
            MdoModuleAgentBinding* Agent=&Module->Agents[j];
            cstr Id=!strcmp(Agent->Id,"mdo.default")?"default":!strncmp(Agent->Id,"agent.",6u)?Agent->Id+6u:NULL;
            bool Enabled=true;
            if (Id && (Agent->Flags&MDO_AGENT_MAIN) && !MdoExtensionEnabled("agents",Id,&Enabled)) return false;
            if (Enabled) { ++j; continue; }
            MdoModulesAgentUnit(Agent);
            --Module->AgentCount;
            memmove(Agent,Agent+1u,(Module->AgentCount-j)*sizeof(*Agent));
            memset(&Module->Agents[Module->AgentCount],0,sizeof(*Agent));
        }
    }
    return true;
}

static MdoModuleCatalog* MdoModulesBuildCandidate(uint64 Generation,
    MdoModuleDiagnostics* pDiagnostics)
{
    MdoModuleSource* pSources = NULL;
    size_t iSourceCount = 0u;
    bytes pHeader = NULL;
    size_t iHeader = 0u;
    MdoModuleCatalog* pCatalog = NULL;
    mdo_module_register_v1* pRegisters = NULL;
    size_t pOrder[MDO_MODULE_COUNT_LIMIT];
    size_t i;

    pHeader = xrtVfsReadAllLimit(MdoHomeApplicationVfs(),
        "/app/generated/module-sdk/mdo/module.h",
        MDO_MODULE_HEADER_LIMIT, &iHeader);
    if ( pHeader == NULL ) {
        const xerror* pError = xrtGetError();
        (void)MdoModulesDiagnosticAdd(pDiagnostics,
            MDO_MODULE_DIAGNOSTIC_READ,
            "/app/generated/module-sdk/mdo/module.h", NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "packed module SDK header cannot be read");
        return NULL;
    }
    if ( !MdoModulesDiscover(&pSources, &iSourceCount, pDiagnostics) ) {
        xrtFree(pHeader);
        return NULL;
    }
    pCatalog = MdoModulesCatalogCreate(Generation, iSourceCount);
    if ( pCatalog == NULL ) goto memory_failed;
    if ( iSourceCount != 0u ) {
        pRegisters = (mdo_module_register_v1*)xrtCalloc(iSourceCount,
            sizeof(*pRegisters));
        if ( pRegisters == NULL ) goto memory_failed;
    }
    for ( i = 0u; i < iSourceCount; ++i ) {
        pCatalog->Modules[i] = pSources[i].Declarative
            ? MdoModulesReadAgentProfile(&pSources[i], Generation, pDiagnostics)
            : MdoModulesCompile(&pSources[i], Generation,
                pHeader, iHeader, &pRegisters[i], pDiagnostics);
        if ( pCatalog->Modules[i] == NULL ) goto failed;
    }
    if ( !MdoModulesRegistrationOrder(pCatalog, pOrder, pDiagnostics) )
        goto failed;
    for ( i = 0u; i < iSourceCount; ++i ) {
        size_t iModule = pOrder[i];
        if ( !pSources[iModule].Declarative &&
             !MdoModulesRegister(pCatalog->Modules[iModule],
                pRegisters[iModule], pDiagnostics) ) goto failed;
    }
    if ( !MdoModulesApplyProfiles(pCatalog,pDiagnostics) || !MdoModulesValidateCatalog(pCatalog, pDiagnostics) ) {
        if ( pDiagnostics->Count == 0u )
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_VALIDATE, NULL, NULL,
                "failed to allocate or validate the complete module catalog");
        goto failed;
    }
    xrtFree(pRegisters);
    MdoModulesSourcesUnit(pSources, iSourceCount);
    xrtFree(pHeader);
    return pCatalog;

memory_failed:
    (void)MdoModulesDiagnosticAdd(pDiagnostics,
        MDO_MODULE_DIAGNOSTIC_READ, NULL, NULL,
        "failed to allocate the module catalog candidate");
failed:
    xrtFree(pRegisters);
    MdoModulesSourcesUnit(pSources, iSourceCount);
    xrtFree(pHeader);
    MdoModuleCatalogRelease(pCatalog);
    return NULL;
}

static void MdoModulesPublishDiagnosticsLocked(
    MdoModuleDiagnostics* pDiagnostics,
    MdoModuleDiagnostics** ppPrevious)
{
    *ppPrevious = g_MdoModules.Diagnostics;
    g_MdoModules.Diagnostics = pDiagnostics;
}

bool MdoModuleManagerReload(void)
{
    MdoModuleDiagnostics* pDiagnostics;
    MdoModuleDiagnostics* pOldDiagnostics = NULL;
    MdoModuleCatalog* pCandidate = NULL;
    MdoModuleCatalog* pOldCatalog = NULL;
    xwork_tool_definition* pDefinitions = NULL;
    xwork_error WorkError;
    char sFailure[MDO_MODULE_ERROR_LIMIT];
    bool bOk = false;

    pDiagnostics = MdoModulesDiagnosticsCreate();
    if ( pDiagnostics == NULL ) {
        MdoModulesSetError("failed to allocate module diagnostics");
        return false;
    }
    memset(sFailure, 0, sizeof(sFailure));
    if ( g_MdoModules.Lock == NULL ||
         !xrtMutexLock(g_MdoModules.Lock) ) {
        MdoModuleDiagnosticsRelease(pDiagnostics);
        MdoModulesSetError("module manager is unavailable");
        return false;
    }
    if ( !g_MdoModules.Initialized || g_MdoModules.Runtime == NULL ) {
        (void)xrtMutexUnlock(g_MdoModules.Lock);
        MdoModuleDiagnosticsRelease(pDiagnostics);
        MdoModulesSetError("module manager is not initialized");
        return false;
    }
    pCandidate = MdoModulesBuildCandidate(g_MdoModules.NextGeneration,
        pDiagnostics);
    if ( pCandidate != NULL ) {
        pDefinitions = MdoModulesDefinitions(pCandidate);
        if ( pCandidate->ToolCount != 0u && pDefinitions == NULL ) {
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_PUBLISH, NULL, NULL,
                "failed to allocate xwork tool definitions");
        } else {
            xworkErrorInit(&WorkError);
            if ( xworkRuntimeReplaceToolsBySource(g_MdoModules.Runtime,
                    MDO_MODULE_SOURCE, pDefinitions,
                    pCandidate->ToolCount, NULL, &WorkError) ) {
                pOldCatalog = g_MdoModules.Catalog;
                g_MdoModules.Catalog = pCandidate;
                pCandidate = NULL;
                g_MdoModules.NextGeneration++;
                MdoModulesPublishDiagnosticsLocked(pDiagnostics,
                    &pOldDiagnostics);
                pDiagnostics = NULL;
                bOk = true;
            } else {
                (void)MdoModulesDiagnosticAdd(pDiagnostics,
                    MDO_MODULE_DIAGNOSTIC_PUBLISH, NULL, NULL,
                    WorkError.sMessage[0] != '\0' ? WorkError.sMessage :
                    "xwork rejected the module tool catalog");
            }
        }
    }
    if ( !bOk ) {
        if ( pDiagnostics->Count == 0u )
            (void)MdoModulesDiagnosticAdd(pDiagnostics,
                MDO_MODULE_DIAGNOSTIC_PUBLISH, NULL, NULL,
                "module reload failed without a detailed diagnostic");
        if ( pDiagnostics->Count != 0u )
            MdoModulesCopyError(sFailure, sizeof(sFailure),
                pDiagnostics->Entries[0].Message);
        MdoModulesPublishDiagnosticsLocked(pDiagnostics, &pOldDiagnostics);
        pDiagnostics = NULL;
    }
    (void)xrtMutexUnlock(g_MdoModules.Lock);
    xrtFree(pDefinitions);
    MdoModuleCatalogRelease(pCandidate);
    MdoModuleCatalogRelease(pOldCatalog);
    MdoModuleDiagnosticsRelease(pOldDiagnostics);
    MdoModuleDiagnosticsRelease(pDiagnostics);
    if ( !bOk ) MdoModulesSetError(sFailure[0] != '\0' ? sFailure :
        "module reload failed");
    return bOk;
}

bool MdoModuleManagerInit(xwork_runtime* pRuntime)
{
    if ( pRuntime == NULL ) {
        MdoModulesSetError("module manager requires an xwork runtime");
        return false;
    }
    if ( g_MdoModules.Initialized ) {
        if ( g_MdoModules.Runtime == pRuntime ) return true;
        MdoModulesSetError("module manager is already bound to another runtime");
        return false;
    }
    memset(&g_MdoModules, 0, sizeof(g_MdoModules));
    g_MdoModules.Lock = xrtMutexCreate();
    g_MdoModules.Runtime = xworkRuntimeRef(pRuntime);
    g_MdoModules.Diagnostics = MdoModulesDiagnosticsCreate();
    g_MdoModules.NextGeneration = 1u;
    if ( g_MdoModules.Lock == NULL || g_MdoModules.Runtime == NULL ||
         g_MdoModules.Diagnostics == NULL ) {
        MdoModuleManagerUnit();
        MdoModulesSetError("failed to allocate module manager state");
        return false;
    }
    g_MdoModules.Initialized = true;
    if ( !MdoModuleManagerReload() ) {
        MdoModuleManagerUnit();
        return false;
    }
    return true;
}

void MdoModuleManagerUnit(void)
{
    xmutex* pLock = g_MdoModules.Lock;
    xwork_runtime* pRuntime = NULL;
    MdoModuleCatalog* pCatalog = NULL;
    MdoModuleDiagnostics* pDiagnostics = NULL;

    if ( pLock != NULL ) (void)xrtMutexLock(pLock);
    pRuntime = g_MdoModules.Runtime;
    pCatalog = g_MdoModules.Catalog;
    pDiagnostics = g_MdoModules.Diagnostics;
    g_MdoModules.Runtime = NULL;
    g_MdoModules.Catalog = NULL;
    g_MdoModules.Diagnostics = NULL;
    g_MdoModules.Initialized = false;
    if ( pLock != NULL ) (void)xrtMutexUnlock(pLock);
    if ( pRuntime != NULL ) {
        xwork_error Error;
        xworkErrorInit(&Error);
        (void)xworkRuntimeReplaceToolsBySource(pRuntime,
            MDO_MODULE_SOURCE, NULL, 0u, NULL, &Error);
    }
    MdoModuleCatalogRelease(pCatalog);
    MdoModuleDiagnosticsRelease(pDiagnostics);
    xworkRuntimeRelease(pRuntime);
    if ( pLock != NULL ) (void)xrtMutexDestroy(pLock);
    memset(&g_MdoModules, 0, sizeof(g_MdoModules));
}

uint64 MdoModuleManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( g_MdoModules.Lock != NULL && xrtMutexLock(g_MdoModules.Lock) ) {
        if ( g_MdoModules.Catalog != NULL )
            Generation = g_MdoModules.Catalog->Generation;
        (void)xrtMutexUnlock(g_MdoModules.Lock);
    }
    return Generation;
}

MdoModuleCatalog* MdoModuleCatalogSnapshot(void)
{
    MdoModuleCatalog* pCatalog = NULL;
    if ( g_MdoModules.Lock != NULL && xrtMutexLock(g_MdoModules.Lock) ) {
        pCatalog = MdoModuleCatalogRef(g_MdoModules.Catalog);
        (void)xrtMutexUnlock(g_MdoModules.Lock);
    }
    return pCatalog;
}

MdoModuleDiagnostics* MdoModuleDiagnosticsSnapshot(void)
{
    MdoModuleDiagnostics* pDiagnostics = NULL;
    if ( g_MdoModules.Lock != NULL && xrtMutexLock(g_MdoModules.Lock) ) {
        pDiagnostics = MdoModuleDiagnosticsRef(g_MdoModules.Diagnostics);
        (void)xrtMutexUnlock(g_MdoModules.Lock);
    }
    return pDiagnostics;
}

size_t MdoModuleCatalogModuleCount(const MdoModuleCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->ModuleCount : 0u;
}

size_t MdoModuleCatalogToolCount(const MdoModuleCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->ToolCount : 0u;
}

size_t MdoModuleCatalogAgentCount(const MdoModuleCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->AgentCount : 0u;
}

bool MdoModuleCatalogModuleAt(const MdoModuleCatalog* pCatalog,
    size_t iIndex, MdoModuleInfo* pInfo)
{
    MdoModuleGeneration* pModule;
    if ( pCatalog == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) || iIndex >= pCatalog->ModuleCount )
        return false;
    pModule = pCatalog->Modules[iIndex];
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Generation = pCatalog->Generation;
    pInfo->Kind = pModule->Kind;
    pInfo->External = pModule->External;
    pInfo->Id = pModule->Id;
    pInfo->Name = pModule->Name;
    pInfo->Description = pModule->Description;
    pInfo->Version = pModule->Version;
    pInfo->SourcePath = pModule->SourcePath;
    pInfo->SourceHash = pModule->SourceHash;
    pInfo->SourceRevision = pModule->SourceRevision;
    pInfo->Capabilities = pModule->Capabilities;
    pInfo->ToolCount = pModule->ToolCount;
    pInfo->AgentCount = pModule->AgentCount;
    return true;
}

bool MdoModuleCatalogToolAt(const MdoModuleCatalog* pCatalog,
    size_t iIndex, MdoModuleToolInfo* pInfo)
{
    MdoModuleToolBinding* pTool;
    if ( pCatalog == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) || iIndex >= pCatalog->ToolCount )
        return false;
    pTool = pCatalog->Tools[iIndex];
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Generation = pCatalog->Generation;
    pInfo->ModuleId = pTool->Owner->Id;
    pInfo->Id = pTool->Id;
    pInfo->Name = pTool->Name;
    pInfo->Description = pTool->Description;
    pInfo->ParametersJson = pTool->ParametersJson;
    pInfo->Effects = pTool->Effects;
    pInfo->Flags = pTool->Flags;
    pInfo->SerialGroup = pTool->SerialGroup;
    pInfo->PermissionResource = pTool->PermissionResource;
    pInfo->MaxResultBytes = pTool->MaxResultBytes;
    return true;
}

bool MdoModuleCatalogAgentAt(const MdoModuleCatalog* pCatalog,
    size_t iIndex, MdoModuleAgentInfo* pInfo)
{
    MdoModuleAgentBinding* pAgent;
    if ( pCatalog == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) || iIndex >= pCatalog->AgentCount )
        return false;
    pAgent = pCatalog->Agents[iIndex];
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Generation = pCatalog->Generation;
    pInfo->ModuleId = pAgent->Owner->Id;
    pInfo->Id = pAgent->Id;
    pInfo->Name = pAgent->Name;
    pInfo->Description = pAgent->Description;
    pInfo->Model = pAgent->Model;
    pInfo->ReasoningEffort = pAgent->ReasoningEffort;
    pInfo->SystemPrompt = pAgent->SystemPrompt;
    pInfo->PermissionProfile = pAgent->PermissionProfile;
    pInfo->Tools = (const char* const*)pAgent->Tools;
    pInfo->ToolCount = pAgent->ToolCount;
    pInfo->Skills = (const char* const*)pAgent->Skills;
    pInfo->SkillCount = pAgent->SkillCount;
    pInfo->AllowedEffects = pAgent->AllowedEffects;
    pInfo->Flags = pAgent->Flags;
    pInfo->ContextWindowTokens = pAgent->ContextWindowTokens;
    pInfo->MaxInputTokens = pAgent->MaxInputTokens;
    pInfo->MaxOutputTokens = pAgent->MaxOutputTokens;
    pInfo->MaxTurns = pAgent->MaxTurns;
    pInfo->TimeoutMilliseconds = pAgent->TimeoutMilliseconds;
    pInfo->MaxFinalBytes = pAgent->MaxFinalBytes;
    pInfo->MaxDepth = pAgent->MaxDepth;
    return true;
}

static MdoModuleAgentBinding* MdoModulesFindAgent(
    const MdoModuleCatalog* pCatalog, cstr AgentId)
{
    size_t i;
    if ( pCatalog == NULL || AgentId == NULL || AgentId[0] == '\0' )
        return NULL;
    for ( i = 0u; i < pCatalog->AgentCount; ++i )
        if ( strcmp(pCatalog->Agents[i]->Id, AgentId) == 0 )
            return pCatalog->Agents[i];
    return NULL;
}

bool MdoModuleCatalogAgentFind(const MdoModuleCatalog* pCatalog,
    const char* AgentId, MdoModuleAgentInfo* pInfo)
{
    MdoModuleAgentBinding* pAgent = MdoModulesFindAgent(pCatalog, AgentId);
    size_t i;
    if ( pAgent == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) ) return false;
    for ( i = 0u; i < pCatalog->AgentCount; ++i )
        if ( pCatalog->Agents[i] == pAgent )
            return MdoModuleCatalogAgentAt(pCatalog, i, pInfo);
    return false;
}

bool MdoModuleCatalogAgentAcquire(const MdoModuleCatalog* pCatalog,
    const char* AgentId, char* ErrorMessage, size_t ErrorCapacity)
{
    MdoModuleAgentBinding* pAgent = MdoModulesFindAgent(pCatalog, AgentId);
    char LocalError[MDO_MODULE_ERROR_LIMIT];
    mdo_result Result;
    if ( ErrorMessage != NULL && ErrorCapacity != 0u ) ErrorMessage[0] = '\0';
    if ( pAgent == NULL ) {
        MdoModulesCopyError(ErrorMessage, ErrorCapacity, "Agent was not found");
        MdoModulesSetError("Agent was not found");
        return false;
    }
    if ( pAgent->Acquire == NULL ) return true;
    memset(LocalError, 0, sizeof(LocalError));
    Result = pAgent->Acquire(pAgent->UserData, &pAgent->Owner->HostServices,
        LocalError, sizeof(LocalError));
    if ( Result == MDO_RESULT_OK ) return true;
    MdoModulesCopyError(ErrorMessage, ErrorCapacity,
        LocalError[0] != '\0' ? LocalError : "Agent lifecycle acquire failed");
    MdoModulesSetError(LocalError[0] != '\0' ? LocalError :
        "Agent lifecycle acquire failed");
    return false;
}

void MdoModuleCatalogAgentRelease(const MdoModuleCatalog* pCatalog,
    const char* AgentId)
{
    MdoModuleAgentBinding* pAgent = MdoModulesFindAgent(pCatalog, AgentId);
    if ( pAgent != NULL && pAgent->Release != NULL )
        pAgent->Release(pAgent->UserData);
}

size_t MdoModuleDiagnosticsCount(const MdoModuleDiagnostics* pDiagnostics)
{
    return pDiagnostics != NULL ? pDiagnostics->Count : 0u;
}

bool MdoModuleDiagnosticsAt(const MdoModuleDiagnostics* pDiagnostics,
    size_t iIndex, MdoModuleDiagnosticInfo* pInfo)
{
    const MdoModuleDiagnosticEntry* pEntry;
    if ( pDiagnostics == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) || iIndex >= pDiagnostics->Count )
        return false;
    pEntry = &pDiagnostics->Entries[iIndex];
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Stage = pEntry->Stage;
    pInfo->SourcePath = pEntry->SourcePath;
    pInfo->SourceHash = pEntry->SourceHash;
    pInfo->Message = pEntry->Message;
    return true;
}
