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

typedef struct MdoMemoryTransferSummary {
    uint32 Size;
    uint64 Generation;  /* target generation for preview/import consistency. */
    size_t StoreCount;
    size_t ProjectCount;
    size_t EntryCount;
    uint64 TotalBytes;
} MdoMemoryTransferSummary;

typedef struct MdoMemoryImportOptions {
    uint32 Size;
    const char* Directory;
    uint64 ExpectedGeneration; /* UINT64_MAX skips the preview generation. */
    const char* Actor;
    const char* Reason;
} MdoMemoryImportOptions;

/* Project writes, imports, and Agent bindings require an initialized project
 * lifecycle service. They take leases before memory locks or durable writes. */
bool MdoMemoryManagerInit(xwork_runtime* Runtime);
void MdoMemoryManagerUnit(void);
uint64 MdoMemoryManagerGeneration(void);

void MdoMemoryWriteOptionsInit(MdoMemoryWriteOptions* Options);
void MdoMemoryRemoveOptionsInit(MdoMemoryRemoveOptions* Options);
void MdoMemoryImportOptionsInit(MdoMemoryImportOptions* Options);

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

/* Directory exports use a readable manifest/global/projects layout and require
 * a destination that does not exist. Preview fully validates the source without
 * writing. Import refuses to overwrite any existing global or project store.
 * It reserves every imported project before its first audit/store write and
 * retains the reservations through any rollback. */
bool MdoMemoryExportDirectory(const char* Directory,
    MdoMemoryTransferSummary* Summary, xwork_error* Error);
bool MdoMemoryPreviewImportDirectory(const char* Directory,
    MdoMemoryTransferSummary* Summary, xwork_error* Error);
bool MdoMemoryImportDirectory(const MdoMemoryImportOptions* Options,
    MdoMemoryTransferSummary* Summary, xwork_error* Error);

/* Builds a bounded owned prompt fragment from one atomic global/project view.
 * The caller releases it with xrtFree. Empty memory returns an empty string. */
str MdoMemoryBuildPrompt(const char* ProjectId, size_t* Bytes,
    uint64* Generation, xwork_error* Error);

/* Agent bindings provide project isolation and audit correlation to runtime
 * memory tools. Bind owns copies and a project lease; tool-catalog references
 * keep it alive until their final release, including after Unbind. Unbind is
 * an idempotent lifecycle action. */
bool MdoMemoryAgentBind(xwork_agent* Agent, const char* ProjectId,
    const char* SessionId, xwork_error* Error);
void MdoMemoryAgentUnbind(xwork_agent* Agent);

#endif
