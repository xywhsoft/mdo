#ifndef MDO_MEMORY_H
#define MDO_MEMORY_H

#include <xsbase.h>
#include <xwork.h>

#define MDO_MEMORY_ID_CAPACITY 65u
#define MDO_MEMORY_PROJECT_CAPACITY 65u
#define MDO_MEMORY_TITLE_CAPACITY 257u
#define MDO_MEMORY_TAG_CAPACITY 65u
#define MDO_MEMORY_PATH_CAPACITY 256u

typedef enum MdoMemoryScope {
    MDO_MEMORY_GLOBAL = 1,
    MDO_MEMORY_PROJECT
} MdoMemoryScope;

typedef struct MdoMemorySnapshot MdoMemorySnapshot;

/* Strings and tag arrays are borrowed from the retained snapshot. */
typedef struct MdoMemoryEntryInfo {
    uint32 Size;
    uint64 Revision;
    int64 CreatedAt;
    int64 UpdatedAt;
    bool Pinned;
    const char* Id;
    const char* Title;
    const char* Content;
    const char* const* Tags;
    size_t TagCount;
} MdoMemoryEntryInfo;

typedef struct MdoMemoryWriteOptions {
    uint32 Size;
    MdoMemoryScope Scope;
    const char* ProjectId;       /* required only for project memory. */
    const char* Id;
    const char* Title;
    const char* Content;
    const char* const* Tags;
    size_t TagCount;
    bool Pinned;
    uint64 ExpectedRevision;     /* UINT64_MAX accepts the current revision. */
    const char* Actor;           /* bounded audit label; NULL selects "host". */
    const char* SessionId;       /* optional audit correlation. */
    const char* Reason;          /* optional bounded audit reason. */
} MdoMemoryWriteOptions;

typedef struct MdoMemoryRemoveOptions {
    uint32 Size;
    MdoMemoryScope Scope;
    const char* ProjectId;
    const char* Id;
    uint64 ExpectedRevision;
    const char* Actor;
    const char* SessionId;
    const char* Reason;
} MdoMemoryRemoveOptions;

bool MdoMemoryManagerInit(xwork_runtime* Runtime);
void MdoMemoryManagerUnit(void);
uint64 MdoMemoryManagerGeneration(void);

void MdoMemoryWriteOptionsInit(MdoMemoryWriteOptions* Options);
void MdoMemoryRemoveOptionsInit(MdoMemoryRemoveOptions* Options);

MdoMemorySnapshot* MdoMemorySnapshotCreate(MdoMemoryScope Scope,
    const char* ProjectId, xwork_error* Error);
MdoMemorySnapshot* MdoMemorySnapshotRef(MdoMemorySnapshot* Snapshot);
void MdoMemorySnapshotRelease(MdoMemorySnapshot* Snapshot);
MdoMemoryScope MdoMemorySnapshotScope(const MdoMemorySnapshot* Snapshot);
const char* MdoMemorySnapshotProjectId(const MdoMemorySnapshot* Snapshot);
uint64 MdoMemorySnapshotRevision(const MdoMemorySnapshot* Snapshot);
uint64 MdoMemorySnapshotGeneration(const MdoMemorySnapshot* Snapshot);
size_t MdoMemorySnapshotCount(const MdoMemorySnapshot* Snapshot);
bool MdoMemorySnapshotAt(const MdoMemorySnapshot* Snapshot, size_t Index,
    MdoMemoryEntryInfo* Info);

bool MdoMemoryUpsert(const MdoMemoryWriteOptions* Options,
    xwork_error* Error);
bool MdoMemoryRemove(const MdoMemoryRemoveOptions* Options,
    xwork_error* Error);

#endif
