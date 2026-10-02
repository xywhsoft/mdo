#ifndef MDO_PROJECT_BINDING_H
#define MDO_PROJECT_BINDING_H

#include "projects.h"
#include "project_lifecycle.h"

/* An owning read snapshot for publication into a reviewed project. Revision
 * plus CreatedAt distinguish replacement from a new project incarnation.
 * WorkspaceRoot is the existing physical directory, never a source backup's
 * path. Revision/CreatedAt zero identify ONLY the absent virtual default
 * project's current working directory, matching session/workspace APIs. */
typedef struct MdoProjectBinding {
    uint32 Size;
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char WorkspaceRoot[MDO_PROJECT_WORKSPACE_CAPACITY];
    uint64 Revision;
    int64 CreatedAt;
    xfileinfo WorkspaceIdentity;
} MdoProjectBinding;

/* External-only project read; never creates Home. Relative configured paths
 * resolve against xsAppPath, virtual default against cwd. Missing nondefault
 * projects, nonexistent/non-directory workspaces and unstable identities fail.
 * Wrong Size leaves output untouched; other failure clears it except Size. */
bool MdoProjectBindingGet(cstr ProjectId, MdoProjectBinding* Binding,
    xwork_error* Error);
/* Pure bounded snapshot comparison; false for invalid sizes/text/identities.
 * Match is advisory until WithBinding repeats the read under writer exclusion. */
bool MdoProjectBindingMatches(const MdoProjectBinding* A, const MdoProjectBinding* B);

typedef bool (*MdoProjectBindingCallback)(const MdoProjectBinding* Current,
    void* Data, xwork_error* Error);
/* A short synchronous publication boundary, not a staging worker. Pins a
 * current shared Owner, takes the process definition gate and native writer
 * file lock, then re-reads project revision/incarnation and physical workspace
 * identity. Calls Callback only on an exact match. All locks close on return,
 * including callback failure. Do not run model/pixel/Stage preparation, network
 * waits, project mutation or lifecycle acquire inside Callback. Acquire session
 * manager/storage locks only after this boundary's definition lock. Drain
 * callers before lifecycle/Home Unit. A close error after Callback cannot
 * undo its commit: the caller's explicit commit fact remains authoritative. */
bool MdoProjectWithBinding(const MdoProjectBinding* Expected,
    MdoProjectLease* Owner, MdoProjectBindingCallback Callback, void* Data,
    xwork_error* Error);

#endif
