#ifndef MDO_SKILLS_H
#define MDO_SKILLS_H

#include <xsbase.h>

#define MDO_SKILL_FORMAT_VERSION 1u

typedef struct MdoSkillCatalog MdoSkillCatalog;
typedef struct MdoSkillDiagnostics MdoSkillDiagnostics;

typedef enum MdoSkillTrust {
    MDO_SKILL_TRUST_BUILTIN = 0,
    MDO_SKILL_TRUST_EXTERNAL_REFERENCE
} MdoSkillTrust;

typedef enum MdoSkillResourceKind {
    MDO_SKILL_RESOURCE_SCRIPT = 1,
    MDO_SKILL_RESOURCE_TEMPLATE,
    MDO_SKILL_RESOURCE_ASSET
} MdoSkillResourceKind;

typedef enum MdoSkillDiagnosticStage {
    MDO_SKILL_DIAGNOSTIC_DISCOVERY = 1,
    MDO_SKILL_DIAGNOSTIC_OPEN,
    MDO_SKILL_DIAGNOSTIC_FRONTMATTER,
    MDO_SKILL_DIAGNOSTIC_RESOURCE,
    MDO_SKILL_DIAGNOSTIC_PUBLISH
} MdoSkillDiagnosticStage;

typedef struct MdoSkillInfo {
    uint32 Size;
    uint64 Generation;
    bool External;
    MdoSkillTrust Trust;
    const char* Id;
    const char* Name;
    const char* Description;
    const char* Version;
    const char* License;
    const char* Compatibility;
    const char* SourcePath;
    const char* MetadataHash;
    size_t BodyBytes;
    size_t EstimatedTokens;
    const char* const* RequiredTools;
    size_t RequiredToolCount;
    const char* const* RequiredMcpServers;
    size_t RequiredMcpServerCount;
    const char* const* RequiredPermissions;
    size_t RequiredPermissionCount;
    size_t ResourceCount;
} MdoSkillInfo;

typedef struct MdoSkillResourceInfo {
    uint32 Size;
    uint64 Generation;
    const char* SkillId;
    MdoSkillResourceKind Kind;
    const char* Path;
    size_t Bytes;
} MdoSkillResourceInfo;

typedef struct MdoSkillContent {
    uint32 Size;
    uint64 Generation;
    bool External;
    MdoSkillTrust Trust;
    char* Text;
    size_t Bytes;
    size_t EstimatedTokens;
} MdoSkillContent;

typedef struct MdoSkillResourceContent {
    uint32 Size;
    uint64 Generation;
    bool External;
    MdoSkillTrust Trust;
    MdoSkillResourceKind Kind;
    bytes Data;
    size_t Bytes;
} MdoSkillResourceContent;

typedef struct MdoSkillDiagnosticInfo {
    uint32 Size;
    MdoSkillDiagnosticStage Stage;
    const char* SkillId;
    const char* SourcePath;
    const char* Message;
} MdoSkillDiagnosticInfo;

bool MdoSkillManagerInit(void);
void MdoSkillManagerUnit(void);
bool MdoSkillManagerReload(void);
uint64 MdoSkillManagerGeneration(void);

MdoSkillCatalog* MdoSkillCatalogSnapshot(void);
MdoSkillCatalog* MdoSkillCatalogRef(MdoSkillCatalog* pCatalog);
void MdoSkillCatalogRelease(MdoSkillCatalog* pCatalog);
size_t MdoSkillCatalogCount(const MdoSkillCatalog* pCatalog);
bool MdoSkillCatalogAt(const MdoSkillCatalog* pCatalog, size_t iIndex,
    MdoSkillInfo* pInfo);
bool MdoSkillCatalogFind(const MdoSkillCatalog* pCatalog, const char* Id,
    MdoSkillInfo* pInfo);
size_t MdoSkillCatalogResourceCount(const MdoSkillCatalog* pCatalog,
    const char* SkillId);
bool MdoSkillCatalogResourceAt(const MdoSkillCatalog* pCatalog,
    const char* SkillId, size_t iIndex, MdoSkillResourceInfo* pInfo);

/* Body and resource bytes are read only after explicit selection. The caller
 * owns returned buffers and releases them with the matching Unit function. */
bool MdoSkillCatalogLoadBody(const MdoSkillCatalog* pCatalog,
    const char* SkillId, MdoSkillContent* pContent);
void MdoSkillContentUnit(MdoSkillContent* pContent);
bool MdoSkillCatalogLoadResource(const MdoSkillCatalog* pCatalog,
    const char* SkillId, const char* Path, size_t Limit,
    MdoSkillResourceContent* pContent);
void MdoSkillResourceContentUnit(MdoSkillResourceContent* pContent);

MdoSkillDiagnostics* MdoSkillDiagnosticsSnapshot(void);
MdoSkillDiagnostics* MdoSkillDiagnosticsRef(
    MdoSkillDiagnostics* pDiagnostics);
void MdoSkillDiagnosticsRelease(MdoSkillDiagnostics* pDiagnostics);
size_t MdoSkillDiagnosticsCount(const MdoSkillDiagnostics* pDiagnostics);
bool MdoSkillDiagnosticsAt(const MdoSkillDiagnostics* pDiagnostics,
    size_t iIndex, MdoSkillDiagnosticInfo* pInfo);

#endif
