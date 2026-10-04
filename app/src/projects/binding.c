#include <stdio.h>
#include <string.h>
#include "../../include/mdo/home.h"
#include "../../include/mdo/project_binding.h"

static bool MdoProjectBindingError(xwork_error* Error, xwork_error_code Code, cstr Message)
{
    if ( Error != NULL ) {
        xworkErrorInit(Error); Error->eCode = Code;
        snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
    }
    return false;
}

static bool MdoProjectBindingText(cstr Text, size_t Capacity)
{
    size_t i;
    for ( i = 0u; i < Capacity && Text[i] != '\0'; ++i )
        if ( (unsigned char)Text[i] < 0x20u || Text[i] == 0x7f ) return false;
    return i != 0u && i < Capacity && xrtUtf8Valid(xrtStrViewN(Text, i), NULL);
}

bool MdoProjectBindingMatches(const MdoProjectBinding* A, const MdoProjectBinding* B)
{
    return A != NULL && B != NULL && A->Size == sizeof(*A) && B->Size == sizeof(*B) &&
        MdoProjectBindingText(A->ProjectId, sizeof(A->ProjectId)) &&
        MdoProjectBindingText(B->ProjectId, sizeof(B->ProjectId)) &&
        MdoProjectBindingText(A->WorkspaceRoot, sizeof(A->WorkspaceRoot)) &&
        MdoProjectBindingText(B->WorkspaceRoot, sizeof(B->WorkspaceRoot)) &&
        strcmp(A->ProjectId, B->ProjectId) == 0 && A->Revision == B->Revision && A->CreatedAt == B->CreatedAt &&
        strcmp(A->WorkspaceRoot, B->WorkspaceRoot) == 0 &&
        (A->WorkspaceIdentity.Available & B->WorkspaceIdentity.Available & XFILE_INFO_IDENTITY) != 0u &&
        A->WorkspaceIdentity.Identity != 0u && A->WorkspaceIdentity.Type == XFILE_TYPE_DIRECTORY &&
        B->WorkspaceIdentity.Type == XFILE_TYPE_DIRECTORY &&
        A->WorkspaceIdentity.Identity == B->WorkspaceIdentity.Identity &&
        A->WorkspaceIdentity.Device == B->WorkspaceIdentity.Device;
}

bool MdoProjectBindingGet(cstr ProjectId, MdoProjectBinding* Binding, xwork_error* Error)
{
    MdoProjectInfo Info = {0};
    MdoProjectBinding Candidate = {0};
    str Relative = NULL, Real = NULL;
    xroot Root = NULL;
    bool Found, Ok = false;
    xworkErrorInit(Error);
    if ( Binding == NULL || Binding->Size != sizeof(*Binding) )
        return MdoProjectBindingError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid project binding output");
    memset(Binding, 0, sizeof(*Binding)); Binding->Size = sizeof(*Binding);
    Info.Size = sizeof(Info); Candidate.Size = sizeof(Candidate);
    if ( !MdoProjectGet(ProjectId, &Info, &Found, Error) ) goto done;
    if ( !Found && strcmp(ProjectId, "default") != 0 ) {
        (void)MdoProjectBindingError(Error, XWORK_ERROR_CONTEXT, "target project does not exist"); goto done;
    }
    if ( !Found ) {
        xfileinfo Stat;
        bool Exists;
        if ( !MdoHomeExternalStat(MDO_DEFAULT_WORKSPACE_PATH, &Exists, &Stat) ||
             !Exists || Stat.Type != XFILE_TYPE_DIRECTORY ) {
            (void)MdoProjectBindingError(Error, XWORK_ERROR_IO,
                "default project workspace is not an existing directory");
            goto done;
        }
        Relative = MdoHomeDefaultWorkspacePath(false);
        if ( Relative != NULL ) Real = xrtPathReal(Relative);
    }
    else if ( xrtPathIsAbs(Info.WorkspaceRoot) ) Real = xrtPathReal(Info.WorkspaceRoot);
    else {
        Relative = xrtPathJoin(xsAppPath(), Info.WorkspaceRoot);
        if ( Relative != NULL ) Real = xrtPathReal(Relative);
    }
    if ( Real == NULL || !MdoProjectBindingText(Real, sizeof(Candidate.WorkspaceRoot)) ||
         (Root = xrtRootOpen(Real)) == NULL || !xrtRootStat(Root, ".", false, &Candidate.WorkspaceIdentity) ||
         Candidate.WorkspaceIdentity.Type != XFILE_TYPE_DIRECTORY ||
         (Candidate.WorkspaceIdentity.Available & XFILE_INFO_IDENTITY) == 0u || Candidate.WorkspaceIdentity.Identity == 0u ) {
        (void)MdoProjectBindingError(Error, XWORK_ERROR_IO, "target workspace is not an existing stable physical directory"); goto done;
    }
    snprintf(Candidate.ProjectId, sizeof(Candidate.ProjectId), "%s", ProjectId);
    snprintf(Candidate.WorkspaceRoot, sizeof(Candidate.WorkspaceRoot), "%s", Real);
    if ( Found ) { Candidate.Revision = Info.Revision; Candidate.CreatedAt = Info.CreatedAt; }
    Ok = true;
done:
    if ( Root != NULL && !xrtRootClose(Root) ) {
        Ok = MdoProjectBindingError(Error, XWORK_ERROR_IO, "cannot close target workspace inspection");
    }
    xrtFree(Relative); xrtFree(Real);
    if ( Ok ) *Binding = Candidate;
    return Ok;
}

bool MdoProjectBindingPrepare(cstr ProjectId, MdoProjectBinding* Binding,
    xwork_error* Error)
{
    MdoProjectInfo Info = {0};
    bool Found;
    if ( Binding == NULL || Binding->Size != sizeof(*Binding) )
        return MdoProjectBindingGet(ProjectId, Binding, Error);
    Info.Size = sizeof(Info);
    if ( !MdoProjectGet(ProjectId, &Info, &Found, Error) ) return false;
    if ( !Found && strcmp(ProjectId, "default") == 0 ) {
        str Workspace = MdoHomeDefaultWorkspacePath(true);
        if ( Workspace == NULL ) return MdoProjectBindingError(Error,
            XWORK_ERROR_IO, "cannot prepare the default project workspace");
        xrtFree(Workspace);
    }
    return MdoProjectBindingGet(ProjectId, Binding, Error);
}

bool MdoProjectWithBinding(const MdoProjectBinding* Expected, MdoProjectLease* Owner,
    MdoProjectBindingCallback Callback, void* Data, xwork_error* Error)
{
    MdoProjectBinding Current = {0};
    MdoProjectDefinitionLease* Writer = NULL;
    xfile File = NULL;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( Expected == NULL || Expected->Size != sizeof(*Expected) || Callback == NULL ||
         !MdoProjectBindingText(Expected->ProjectId, sizeof(Expected->ProjectId)) ||
         !MdoProjectBindingText(Expected->WorkspaceRoot, sizeof(Expected->WorkspaceRoot)) ||
         !MdoProjectLeaseProtects(Owner, Expected->ProjectId, MDO_PROJECT_LEASE_SHARED) )
        return MdoProjectBindingError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid project publication binding or owner");
    Writer = MdoProjectDefinitionAcquire(Owner, Error);
    if ( Writer == NULL ) return false;
    File = MdoHomeOpenWrite("projects/.writer.lock", XFILE_READ | XFILE_CREATE | XFILE_SYNC);
    if ( File == NULL || !xrtFileLock(File, XFILE_LOCK_EXCLUSIVE, false) ) {
        (void)MdoProjectBindingError(Error, XWORK_ERROR_CONTEXT, "project store is unavailable for publication"); goto done;
    }
    Current.Size = sizeof(Current);
    if ( !MdoProjectBindingGet(Expected->ProjectId, &Current, Error) ) goto done;
    if ( !MdoProjectBindingMatches(Expected, &Current) ) {
        (void)MdoProjectBindingError(Error, XWORK_ERROR_CONTEXT, "target project or workspace changed after review"); goto done;
    }
    Ok = Callback(&Current, Data, Error);
done:
    if ( File != NULL && !xrtClose(File) ) {
        /* Callback may already have committed. Do not revoke its commit fact. */
        Ok = MdoProjectBindingError(Error, XWORK_ERROR_IO, "cannot close project publication writer");
    }
    MdoProjectDefinitionRelease(Writer);
    return Ok;
}
