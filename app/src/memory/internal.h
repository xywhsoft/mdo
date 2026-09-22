#ifndef MDO_MEMORY_INTERNAL_H
#define MDO_MEMORY_INTERNAL_H

#include "../../include/mdo/memory.h"

typedef struct MdoMemoryImportCandidate {
    MdoMemoryScope Scope;
    const char* ProjectId;
    const char* Path;
    const MdoMemorySnapshot* Snapshot;
} MdoMemoryImportCandidate;

/* Private bridge between the store and directory-transfer translation units. */
bool MdoMemoryInternalId(const char* Text, size_t Capacity);
MdoMemorySnapshot* MdoMemoryInternalParse(MdoMemoryScope Scope,
    const char* ProjectId, xstrview Json);
char* MdoMemoryInternalJson(const MdoMemorySnapshot* Snapshot, size_t* Size);

/* Export holds this lock only while taking owned snapshots of every store. */
bool MdoMemoryInternalTransferBegin(uint64* Generation, xwork_error* Error);
void MdoMemoryInternalTransferEnd(void);

/* Publishes a fully validated import. Existing live stores make it fail. */
bool MdoMemoryInternalImportEmpty(
    const MdoMemoryImportCandidate* Stores, size_t StoreCount,
    uint64 ExpectedGeneration, const char* Actor, const char* Reason,
    uint64* Generation, xwork_error* Error);

#endif
