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
    MdoMcpTransport Transport;
    const char* Id;
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

MdoMcpDiagnostics* MdoMcpDiagnosticsSnapshot(void);
MdoMcpDiagnostics* MdoMcpDiagnosticsRef(MdoMcpDiagnostics* pDiagnostics);
void MdoMcpDiagnosticsRelease(MdoMcpDiagnostics* pDiagnostics);
size_t MdoMcpDiagnosticsCount(const MdoMcpDiagnostics* pDiagnostics);
bool MdoMcpDiagnosticsAt(const MdoMcpDiagnostics* pDiagnostics,
    size_t iIndex, MdoMcpDiagnosticInfo* pInfo);

#endif
