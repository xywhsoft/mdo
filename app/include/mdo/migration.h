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

/* Resolves and inspects one known legacy source without creating Home or
 * changing the source. A missing or ineligible source is a successful preview
 * with Found/Importable false and an explanatory Message. */
bool MdoLegacyMigrationPreview(const char* SourceId,
    MdoMigrationPreview* Preview, xwork_error* Error);

/* Returns both well-known source slots in stable priority order. Duplicate
 * physical paths are represented once; Count is always set on success. */
bool MdoLegacyMigrationDiscover(MdoMigrationPreview* Previews,
    size_t Capacity, size_t* Count, xwork_error* Error);

#endif
