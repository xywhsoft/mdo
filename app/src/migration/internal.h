#ifndef MDO_MIGRATION_INTERNAL_H
#define MDO_MIGRATION_INTERNAL_H

#include "../../include/mdo/migration.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/project_lifecycle.h"

#define MDO_MIGRATION_MAX_FILES 4096u
#define MDO_MIGRATION_MAX_ENTRIES 8192u
#define MDO_MIGRATION_MAX_DEPTH 8u
#define MDO_MIGRATION_MAX_PATH 1024u
#define MDO_MIGRATION_MAX_TOTAL (UINT64_C(256) * 1024u * 1024u)
#define MDO_MIGRATION_MAX_FILE (UINT64_C(64) * 1024u * 1024u)
#define MDO_MIGRATION_MAX_JSON (1024u * 1024u)

typedef enum MdoMigrationFileKind {
    MDO_MIGRATION_FILE_OTHER = 0,
    MDO_MIGRATION_FILE_CONFIG,
    MDO_MIGRATION_FILE_PROJECT,
    MDO_MIGRATION_FILE_SESSION_META,
    MDO_MIGRATION_FILE_SESSION_JOURNAL,
    MDO_MIGRATION_FILE_SESSION_SNAPSHOT,
    MDO_MIGRATION_FILE_SESSION_UI,
    MDO_MIGRATION_FILE_MEMORY,
    MDO_MIGRATION_FILE_SCHEDULE,
    MDO_MIGRATION_FILE_AUDIT
} MdoMigrationFileKind;

typedef struct MdoMigrationFile {
    char* Path;
    uint64 Size;
    MdoMigrationFileKind Kind;
} MdoMigrationFile;

typedef struct MdoMigrationScan {
    MdoMigrationFile* Files;
    size_t Count;
    size_t EntryCount;
    uint64 TotalBytes;
} MdoMigrationScan;

typedef struct MdoMigrationModelMap {
    char* OldId;
    char* NewId;
    MdoModelProtocol Protocol;
    uint64 ContextTokens;
    uint32 MaxOutputTokens;
    char ReasoningEffort[33];
} MdoMigrationModelMap;

typedef struct MdoMigrationProjectMap {
    char* OldId;
    char* NewId;
    char* WorkspaceRoot;
    MdoProjectLease* Lease; /* protects publication and failed-stage cleanup */
} MdoMigrationProjectMap;

typedef struct MdoMigrationContext {
    xroot SourceRoot;
    xroot StageRoot;
    MdoMigrationScan Scan;
    MdoMigrationPreview Preview;
    MdoMigrationApplyResult* Result;
    char* StagePath;
    MdoMigrationModelMap* Models;
    size_t ModelCount;
    MdoMigrationProjectMap* Projects;
    size_t ProjectCount;
    bool LegacySystemPrompt;
    bool LegacyNetworkSettings;
    bool LegacyWindowSettings;
    bool LegacyAudit;
    size_t UnsupportedSchedules;
    size_t UnsupportedMemory;
    size_t UnsupportedFiles;
} MdoMigrationContext;

void MdoMigrationError(xwork_error* Error, xwork_error_code Code,
    const char* Message);
void MdoMigrationXrtError(xwork_error* Error, const char* Fallback);
bool MdoMigrationCopy(char* Output, size_t Capacity, const char* Text);
bool MdoMigrationEndsWith(const char* Text, const char* Suffix);
void MdoMigrationScanUnit(MdoMigrationScan* Scan);
bool MdoMigrationScanDirectory(xroot Root, const char* Path, size_t Depth,
    MdoMigrationScan* Scan, xwork_error* Error);
int MdoMigrationFileCompare(const void* LeftValue, const void* RightValue);
MdoMigrationFile* MdoMigrationFind(MdoMigrationScan* Scan, const char* Path);
bool MdoMigrationRead(xroot Root, const MdoMigrationFile* Item, size_t Limit,
    char** Data, size_t* Size, xwork_error* Error);
xvalue* MdoMigrationParseJson(xroot Root, const MdoMigrationFile* Item,
    xwork_error* Error);

bool MdoMigrationIdentifier(const char* Text, const char* Prefix,
    char* Output, size_t Capacity);
bool MdoMigrationStageDirectory(MdoMigrationContext* Context,
    const char* Path, xwork_error* Error);
bool MdoMigrationStageWrite(MdoMigrationContext* Context, const char* Path,
    const void* Data, size_t Size, uint32 Mode, xwork_error* Error);
bool MdoMigrationStageCopy(MdoMigrationContext* Context,
    const MdoMigrationFile* Source, const char* Target, uint32 Mode,
    xwork_error* Error);
bool MdoMigrationMeasureStage(MdoMigrationContext* Context,
    xwork_error* Error);
MdoMigrationModelMap* MdoMigrationModelFind(MdoMigrationContext* Context,
    const char* OldId);
MdoMigrationProjectMap* MdoMigrationProjectFind(
    MdoMigrationContext* Context, const char* OldId);
void MdoMigrationContextUnit(MdoMigrationContext* Context);

bool MdoMigrationConvertConfig(MdoMigrationContext* Context,
    xwork_error* Error);
/* Build the complete destination identity map without creating directories or
 * files. It is immutable once apply acquires the corresponding project leases. */
bool MdoMigrationPlanProjects(MdoMigrationContext* Context,
    xwork_error* Error);
bool MdoMigrationConvertSessions(MdoMigrationContext* Context,
    xwork_error* Error);
bool MdoMigrationConvertMemory(MdoMigrationContext* Context,
    xwork_error* Error);
bool MdoMigrationConvertSchedules(MdoMigrationContext* Context,
    xwork_error* Error);
bool MdoMigrationWriteReport(MdoMigrationContext* Context,
    xwork_error* Error);

#endif
