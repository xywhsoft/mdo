#ifndef MDO_PROJECTS_H
#define MDO_PROJECTS_H

#include <xsbase.h>
#include <xwork.h>

#ifndef MDO_PROJECT_ID_CAPACITY
#define MDO_PROJECT_ID_CAPACITY 65u
#endif
#define MDO_PROJECT_NAME_CAPACITY 257u
#define MDO_PROJECT_WORKSPACE_CAPACITY 2049u
#define MDO_PROJECT_MODEL_CAPACITY 129u
#define MDO_PROJECT_LIST_LIMIT 100u

typedef struct MdoProjectInfo {
    uint32 Size;
    char Id[MDO_PROJECT_ID_CAPACITY];
    char Name[MDO_PROJECT_NAME_CAPACITY];
    char WorkspaceRoot[MDO_PROJECT_WORKSPACE_CAPACITY];
    char DefaultModelId[MDO_PROJECT_MODEL_CAPACITY];
    uint64 Revision;
    int64 CreatedAt;
    int64 UpdatedAt;
} MdoProjectInfo;

typedef struct MdoProjectCreateOptions {
    uint32 Size;
    const char* Id;
    const char* Name;
    const char* WorkspaceRoot;
    const char* DefaultModelId;
} MdoProjectCreateOptions;

void MdoProjectCreateOptionsInit(MdoProjectCreateOptions* Options);
/* A missing definition is a successful read with Found=false. All reads are
 * external-only and leave a single-file Home untouched. */
bool MdoProjectGet(const char* Id, MdoProjectInfo* Info, bool* Found,
    xwork_error* Error);
bool MdoProjectList(MdoProjectInfo* Items, size_t Capacity, size_t* Count,
    size_t* InvalidCount, bool* Truncated, xwork_error* Error);
bool MdoProjectCreate(const MdoProjectCreateOptions* Options,
    MdoProjectInfo* Info, xwork_error* Error);

#endif
