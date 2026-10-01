#ifndef MDO_MIGRATION_H
#define MDO_MIGRATION_H

#include <xsbase.h>
#include <xwork.h>

#define MDO_MIGRATION_SOURCE_ID_CAPACITY 33u
#define MDO_MIGRATION_PATH_CAPACITY 2049u
#define MDO_MIGRATION_TOKEN_CAPACITY 65u

#define MDO_MIGRATION_SOURCE_PORTABLE_DATA "portable-data"
#define MDO_MIGRATION_SOURCE_USER_HOME "user-home"

typedef struct MdoMigrationPreview {
    uint32 Size;
    bool Found;
    bool Valid;
    bool Importable;
    bool TargetAvailable;
    bool PreserveBrowserCache;
    size_t FileCount;
    size_t ProjectCount;
    size_t SessionCount;
    size_t ModelCount;
    size_t ScheduleCount;
    size_t MemoryFileCount;
    size_t UnsupportedCount;
    size_t ConflictCount;
    size_t EstimatedWriteCount;
    uint64 TotalBytes;
    char SourceId[MDO_MIGRATION_SOURCE_ID_CAPACITY];
    char SourcePath[MDO_MIGRATION_PATH_CAPACITY];
    char TargetPath[MDO_MIGRATION_PATH_CAPACITY];
    char PreviewToken[MDO_MIGRATION_TOKEN_CAPACITY];
    char Message[256];
} MdoMigrationPreview;

typedef struct MdoMigrationApplyOptions {
    uint32 Size;
    const char* SourceId;
    const char* PreviewToken;
} MdoMigrationApplyOptions;

typedef enum MdoMigrationApplyFailureKind {
    MDO_MIGRATION_APPLY_FAILURE_NONE = 0,
    MDO_MIGRATION_APPLY_FAILURE_STORAGE_UNSUPPORTED
} MdoMigrationApplyFailureKind;

typedef struct MdoMigrationApplyResult {
    uint32 Size;
    bool RestartRequired;
    size_t ImportedModels;
    size_t ImportedProjects;
    size_t ImportedSessions;
    size_t ImportedSchedules;
    size_t ImportedMemoryEntries;
    size_t SkippedItems;
    size_t WrittenFiles;
    uint64 WrittenBytes;
    char TargetPath[MDO_MIGRATION_PATH_CAPACITY];
    char ReportPath[MDO_MIGRATION_PATH_CAPACITY];
    /* Typed failure survives cleanup; callers must not classify error text.
     * NONE leaves the general xwork_error as the authoritative failure. */
    MdoMigrationApplyFailureKind Failure;
} MdoMigrationApplyResult;

/* Resolves and inspects one known legacy source without creating Home or
 * changing the source. A missing or ineligible source is a successful preview
 * with Found/Importable false and an explanatory Message. */
bool MdoLegacyMigrationPreview(const char* SourceId,
    MdoMigrationPreview* Preview, xwork_error* Error);

/* Returns both well-known source slots in stable priority order. Duplicate
 * physical paths are represented once; Count is always set on success. */
bool MdoLegacyMigrationDiscover(MdoMigrationPreview* Previews,
    size_t Capacity, size_t* Count, xwork_error* Error);

void MdoMigrationApplyOptionsInit(MdoMigrationApplyOptions* Options);

/* Re-runs the complete preview and requires an exact content-bound token.
 * A missing target is published once from sibling staging. A mounted Home
 * containing only browser cache uses the recoverable Home import transaction;
 * its cache is retained and writes stay frozen until restart. The source is
 * never modified; source/target containment is refused.
 * Requires the project lifecycle service. All mapped projects are reserved
 * before the first staging write, through publication or failed-stage cleanup;
 * a busy destination project returns XWORK_ERROR_CONTEXT without writing. */
bool MdoLegacyMigrationApply(const MdoMigrationApplyOptions* Options,
    MdoMigrationApplyResult* Result, xwork_error* Error);

#endif
