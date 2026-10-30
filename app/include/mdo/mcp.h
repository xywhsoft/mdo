#ifndef MDO_MCP_H
#define MDO_MCP_H

#include <xsbase.h>
#include <xwork.h>

#define MDO_MCP_CONFIG_SCHEMA_VERSION 1u

typedef struct MdoMcpCatalog MdoMcpCatalog;
typedef struct MdoMcpDiagnostics MdoMcpDiagnostics;

typedef enum MdoMcpTransport {
    MDO_MCP_TRANSPORT_STDIO = 1,
    MDO_MCP_TRANSPORT_STREAMABLE_HTTP
} MdoMcpTransport;

typedef enum MdoMcpDiagnosticStage {
    MDO_MCP_DIAGNOSTIC_DISCOVERY = 1,
    MDO_MCP_DIAGNOSTIC_READ,
    MDO_MCP_DIAGNOSTIC_PARSE,
    MDO_MCP_DIAGNOSTIC_SECRET,
    MDO_MCP_DIAGNOSTIC_RUNTIME,
    MDO_MCP_DIAGNOSTIC_PUBLISH
} MdoMcpDiagnosticStage;

typedef struct MdoMcpServerInfo {
    uint32 Size;
    uint64 Generation;
    bool External;
    bool Enabled;
    bool AutoReconnect;
    bool TrustReadOnlyAnnotations;
    bool OAuth;
    MdoMcpTransport Transport;
    const char* Id;
    const char* RuntimeId;
    const char* Name;
    const char* Description;
    const char* ProtocolVersion;
    const char* Program;
    const char* Endpoint;
    const char* WorkingDirectory;
    const char* PermissionProfile;
    const char* SourcePath;
    const char* SourceHash;
    size_t ArgumentCount;
    size_t EnvironmentCount;
    size_t HttpHeaderCount;
    size_t AllowedToolCount;
    size_t DeniedToolCount;
    uint32 StartupTimeoutMilliseconds;
    uint32 RequestTimeoutMilliseconds;
    size_t MaxMessageBytes;
    size_t MaxTools;
    xwork_tool_effects DefaultEffects;
    const char *DirectoryId,*ProjectId;
} MdoMcpServerInfo;

typedef struct MdoMcpServerStatus {
    uint32 Size;
    uint64 CatalogGeneration;
    xwork_mcp_server_state State;
    uint64 SchemaGeneration;
    uint64 SchemaExpiresMicroseconds;
    size_t DiscoveredToolCount;
    uint64 RequestsCompleted;
    bool Enabled;
    bool Connected;
    bool ToolsDiscovered;
    bool SupportsToolListChanges;
} MdoMcpServerStatus;

typedef struct MdoMcpDiagnosticInfo {
    uint32 Size;
    MdoMcpDiagnosticStage Stage;
    const char* ServerId;
    const char* SourcePath;
    const char* SourceHash;
    const char* Message;
} MdoMcpDiagnosticInfo;

/* Init/Unit belong to the serialized process lifecycle. All snapshot objects
 * remain valid until released, including across a concurrent Reload. */
bool MdoMcpManagerInit(xwork_runtime* pRuntime);
void MdoMcpManagerUnit(void);
bool MdoMcpManagerReload(void);
/* Resolve a template dependency to one approved private in-scope connection.
 * Owned string. No connection is started or credential selected implicitly. */
char* MdoMcpDependencyConnection(cstr Ref,cstr Workspace,cstr Preferred,bool RequireReady,char Error[512]);
bool MdoMcpRemoveDirectoryConnections(cstr DirectoryId,char Error[512]);
xvalue* MdoMcpConnections(cstr Id,char Error[512]);
/* Private device bindings only; archive manifests never contain these values.
 * Preview is cold. Save requires the reviewed whole-directory approval digest. */
bool MdoMcpConnectionManage(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
uint64 MdoMcpManagerGeneration(void);

MdoMcpCatalog* MdoMcpCatalogSnapshot(void);
MdoMcpCatalog* MdoMcpCatalogRef(MdoMcpCatalog* pCatalog);
void MdoMcpCatalogRelease(MdoMcpCatalog* pCatalog);
size_t MdoMcpCatalogCount(const MdoMcpCatalog* pCatalog);
bool MdoMcpCatalogAt(const MdoMcpCatalog* pCatalog, size_t iIndex,
    MdoMcpServerInfo* pInfo);
bool MdoMcpCatalogFind(const MdoMcpCatalog* pCatalog, const char* ServerId,
    MdoMcpServerInfo* pInfo);

bool MdoMcpManagerGetStatus(const char* ServerId,
    MdoMcpServerStatus* pStatus);
bool MdoMcpManagerSetEnabled(const char* ServerId, bool Enabled,
    xwork_error* pError);
bool MdoMcpManagerDisconnect(const char* ServerId, xwork_error* pError);
bool MdoMcpManagerRefresh(const char* ServerId, xcancel* pCancel,
    uint64 Deadline, xwork_error* pError);
bool MdoMcpValidateText(const char* Id, const char* Text, char* Error, size_t Capacity);
/* Reads project definitions without executing them. Scope restrictions are
 * inherited by subagents and apply to search, schema load and invocation. */
bool MdoMcpConfigureAgent(xwork_agent* Agent, const char* Workspace, xwork_error* Error);
bool MdoMcpConfigureRole(xwork_agent* Agent,const char* Workspace,const char* const* Ids,size_t Count,xwork_error* Error);
bool MdoMcpConfigureRoleScoped(xwork_agent* Agent,const char* Workspace,const char* Scope,const char* const* Ids,size_t Count,xwork_error* Error);
str MdoMcpRolePrompt(const char* Workspace,const char* const* Ids,size_t Count,bool Restrict,xwork_error* Error);
bool MdoMcpRead(const char* Server,const char* Method,const char* Params,xcancel* Cancel,uint64 Deadline,char** Result,xwork_error* Error);
bool MdoMcpCheckTool(xwork_agent* Agent,cstr Tool,cstr Arguments,cstr Workspace,cstr Scope,char Error[512]);
xvalue* MdoMcpDescribe(const char* Server);
xvalue* MdoMcpAuthorizationStatus(const char* ServerId);
bool MdoMcpAuthorizationStart(const char* ServerId,const char* LocalOrigin);
bool MdoMcpAuthorizationOpen(const char* ServerId);
bool MdoMcpAuthorizationCallback(const char* State,const char* Code,const char* Issuer,const char* Error);
bool MdoMcpAuthorizationForget(const char* ServerId);

MdoMcpDiagnostics* MdoMcpDiagnosticsSnapshot(void);
MdoMcpDiagnostics* MdoMcpDiagnosticsRef(MdoMcpDiagnostics* pDiagnostics);
void MdoMcpDiagnosticsRelease(MdoMcpDiagnostics* pDiagnostics);
size_t MdoMcpDiagnosticsCount(const MdoMcpDiagnostics* pDiagnostics);
bool MdoMcpDiagnosticsAt(const MdoMcpDiagnostics* pDiagnostics,
    size_t iIndex, MdoMcpDiagnosticInfo* pInfo);

#endif
