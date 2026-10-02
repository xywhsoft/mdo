#include <stdio.h>
#include <string.h>
#include "backup_internal.h"
#include "internal.h"

typedef struct MdoRestoreBuffer { char* Data; size_t Bytes, Capacity, Limit; } MdoRestoreBuffer;

static bool MdoRestoreSet(xvalue* Root, const char* Key, xvalue* Value)
{
    bool Ok = Root != NULL && Value != NULL && xrtValueObjectSetTake(Root, xrtStrView(Key), &Value);
    xrtValueRelease(Value); return Ok;
}

static bool MdoRestoreText(char* Output, size_t Capacity, const char* Input)
{
    size_t Size = 0u;
    if ( Input == NULL ) return false;
    while ( Size < Capacity && Input[Size] != '\0' ) ++Size;
    if ( Size == 0u || Size == Capacity || !xrtUtf8Valid(xrtStrViewN(Input, Size), NULL) ) return false;
    memcpy(Output, Input, Size + 1u); return true;
}

static bool MdoRestoreTarget(const MdoSessionBackup* Source, const MdoSessionBackupRestoreTarget* Target,
    MdoSessionInfo* Info, xwork_error* Error)
{
    if ( Target == NULL || Target->Size != sizeof(*Target) || Target->RestoredAt <= 0 ) goto invalid;
    *Info = Source->Info;
    if ( !MdoRestoreText(Info->ProjectId, sizeof(Info->ProjectId), Target->ProjectId) ||
         !MdoRestoreText(Info->Id, sizeof(Info->Id), Target->SessionId) ||
         strlen(Info->Id) != 32u || !MdoBackupDigits(Info->Id, 32u, true) || strcmp(Info->Id, Source->Info.Id) == 0 ||
         !MdoRestoreText(Info->WorkspaceRoot, sizeof(Info->WorkspaceRoot), Target->WorkspaceRoot) ||
         !MdoBackupWorkspaceAbsolute(xrtStrView(Info->WorkspaceRoot)) ) goto invalid;
    Info->Revision = 1u; Info->CreatedAt = Info->UpdatedAt = Target->RestoredAt;
    Info->Status = Info->PreviousStatus = MDO_SESSION_ACTIVE;
    Info->Pinned = Info->RuntimeOpen = false;
    memset(Info->ParentSessionId, 0, sizeof(Info->ParentSessionId)); Info->ForkedThroughSequence = 0u;
    Info->ConfigRevision = Info->ModelGeneration = Info->ModuleGeneration = Info->SkillGeneration = 0u;
    return true;
invalid:
    return MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "invalid explicit restore target", NULL);
}

/* Build a complete replacement with the final file/total byte budget. No
 * append can grow an allocation beyond the resulting UI file limit. */
static bool MdoRestoreLine(MdoRestoreBuffer* Output, const xvalue* Root, xwork_error* Error)
{
    size_t Bytes = 0u, Needed, Next;
    str Json = xrtJsonStringify(Root, false, &Bytes);
    char* Buffer;
    if ( Json == NULL ) return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot encode rebound UI event", NULL);
    if ( Output->Bytes >= Output->Limit || Bytes >= Output->Limit - Output->Bytes ) {
        xrtFree(Json); return MdoBackupError(Error, XWORK_ERROR_LIMIT, "rebound UI exceeds byte budget", "ui-events.jsonl");
    }
    Needed = Output->Bytes + Bytes + 2u;
    if ( Needed > Output->Capacity ) {
        Next = Output->Capacity != 0u ? Output->Capacity : 4096u;
        while ( Next < Needed ) Next *= 2u;
        if ( Next > Output->Limit + 1u ) Next = Output->Limit + 1u;
        Buffer = (char*)xrtRealloc(Output->Data, Next);
        if ( Buffer == NULL ) { xrtFree(Json); return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot own rebound UI bytes", NULL); }
        Output->Data = Buffer; Output->Capacity = Next;
    }
    memcpy(Output->Data + Output->Bytes, Json, Bytes); Output->Bytes += Bytes;
    Output->Data[Output->Bytes++] = '\n'; Output->Data[Output->Bytes] = '\0';
    xrtFree(Json); return true;
}

static bool MdoRestoreArtifact(const MdoSessionBackup* Source, xvalue* Root, xvalue* Paths,
    MdoSessionBackupRestoreInfo* Facts, xwork_error* Error)
{
    xstrview Path;
    char Relative[MDO_SESSION_BACKUP_PATH_CAPACITY];
    uint64 Id;
    xvalue* Row;
    bool Ok;
    if ( !MdoBackupView(Root, "artifact_path", &Path) ) goto invalid;
    if ( Path.Size == 0u ) return true;
    if ( !MdoBackupArtifactRelative(Path, Relative) || MdoBackupFind(Source, Relative) == NULL ||
         !MdoBackupUInt(Root, "event_id", &Id) ) goto invalid;
    if ( strlen("sessions///") + strlen(Facts->Target.ProjectId) + strlen(Facts->Target.Id) + strlen(Relative) >= MDO_SESSION_PATH_CAPACITY )
        return MdoBackupError(Error, XWORK_ERROR_LIMIT, "target artifact path exceeds live reader limits", NULL);
    if ( xrtValueCount(Paths) >= MDO_BACKUP_ORIGIN_MAX_REFS )
        return MdoBackupError(Error, XWORK_ERROR_LIMIT, "too many artifact origin references", NULL);
    Row = xrtValueObject();
    Ok = Row != NULL && MdoRestoreSet(Row, "event_id", xrtValueUInt(Id)) &&
        MdoRestoreSet(Row, "source_path", xrtValueString(Path)) &&
        MdoRestoreSet(Row, "target_path", xrtValueString(xrtStrView(Relative))) &&
        xrtValueArrayAppendTake(Paths, &Row);
    xrtValueRelease(Row);
    if ( !Ok || !MdoRestoreSet(Root, "artifact_path", xrtValueString(xrtStrView(Relative))) )
        return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot own artifact origin mapping", NULL);
    ++Facts->ArtifactReferences; return true;
invalid:
    return MdoBackupError(Error, XWORK_ERROR_IO, "invalid source artifact reference", "ui-events.jsonl");
}

static bool MdoRestoreUi(const MdoSessionBackup* Source, MdoSessionBackup* Copy, xvalue* Paths,
    MdoSessionBackupRestoreInfo* Facts, const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const MdoBackupOwnedFile* Ui = MdoBackupFind(Source, "ui-events.jsonl");
    MdoRestoreBuffer Output = {0};
    size_t Offset = 0u;
    bool Ok = false;
    if ( Ui == NULL || Ui->Bytes == 0u ) return true;
    Output.Limit = MdoBackupPathLimit("ui-events.jsonl", false);
    if ( Limits->FileBytes < Output.Limit ) Output.Limit = Limits->FileBytes;
    if ( Limits->TotalBytes - (Copy->Bytes - Ui->Bytes) < Output.Limit ) Output.Limit = Limits->TotalBytes - (Copy->Bytes - Ui->Bytes);
    while ( Offset < Ui->Bytes ) {
        const char* End = (const char*)memchr(Ui->Data + Offset, '\n', Ui->Bytes - Offset);
        xvalue* Root;
        size_t Bytes;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) goto done;
        if ( End == NULL ) goto invalid;
        Bytes = (size_t)(End - (Ui->Data + Offset));
        Root = MdoBackupJson(Ui->Data + Offset, Bytes);
        Ok = Root != NULL && MdoRestoreSet(Root, "project_id", xrtValueString(xrtStrView(Copy->Info.ProjectId))) &&
            MdoRestoreSet(Root, "session_id", xrtValueString(xrtStrView(Copy->Info.Id))) &&
            MdoRestoreArtifact(Source, Root, Paths, Facts, Error) && MdoRestoreLine(&Output, Root, Error);
        xrtValueRelease(Root);
        if ( !Ok ) goto done;
        Ok = false; ++Facts->UiRecords; Offset += Bytes + 1u;
    }
    Ok = MdoBackupReplaceOwned(Copy, "ui-events.jsonl", &Output.Data, Output.Bytes, Limits, Cancel, Error); goto done;
invalid:
    (void)MdoBackupError(Error, XWORK_ERROR_IO, "partial UI journal cannot be rebound", "ui-events.jsonl");
done:
    xrtFree(Output.Data); return Ok;
}

MdoSessionBackup* MdoSessionBackupPrepareRestore(const MdoSessionBackup* Backup,
    const MdoSessionBackupRestoreTarget* Target, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, MdoSessionBackupRestoreInfo* Facts, xwork_error* Error)
{
    MdoSessionBackupRestoreInfo* Result = NULL;
    MdoSessionBackup *Reconciled = NULL, *Copy = NULL;
    MdoSessionBackupLimits Budget;
    MdoSessionInfo Info, Parsed;
    xwork_error Local;
    xvalue* Paths = NULL;
    str Meta = NULL;
    size_t Bytes = 0u;
    bool Ok = false;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Facts == NULL || Facts->Size != sizeof(*Facts) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "restore facts have the wrong size", NULL); return NULL;
    }
    memset(Facts, 0, sizeof(*Facts)); Facts->Size = sizeof(*Facts);
    if ( Backup == NULL || !Backup->Decoded || Backup->Schema != MDO_SESSION_BACKUP_SCHEMA ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "restore preparation requires a decoded v2 backup", NULL); return NULL;
    }
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) || !MdoBackupCheck(&Budget, Cancel, Error) ||
         !MdoRestoreTarget(Backup, Target, &Info, Error) ) return NULL;
    Meta = MdoSessionsInternalMetaJson(&Info, &Bytes);
    if ( Meta == NULL ) { (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot encode target metadata", NULL); goto done; }
    if ( !MdoBackupMetaRead(xrtStrViewN(Meta, Bytes), &Parsed) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "target metadata fails live schema", NULL); goto done;
    }
    Result = (MdoSessionBackupRestoreInfo*)xrtCalloc(1u, sizeof(*Result)); Paths = xrtValueArray();
    if ( Result == NULL || Paths == NULL ) { (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot own restore preparation", NULL); goto done; }
    Result->Size = sizeof(*Result); Result->Source = Backup->Info; Result->Target = Parsed;
    Result->Projections.Size = sizeof(Result->Projections); Result->Inputs.Size = sizeof(Result->Inputs);
    Reconciled = MdoSessionBackupReconcileHistory(Backup, &Budget, Cancel, &Result->Projections, Error);
    if ( Reconciled == NULL ) goto done;
    Copy = MdoSessionBackupReviewInputs(Reconciled, &Budget, Cancel, &Result->Inputs, Error);
    MdoSessionBackupRelease(Reconciled); Reconciled = NULL;
    if ( Copy == NULL ) goto done;
    Copy->Info = Parsed;
    if ( !MdoBackupReplaceOwned(Copy, "meta.json", &Meta, Bytes, &Budget, Cancel, Error) ||
         !MdoRestoreUi(Backup, Copy, Paths, Result, &Budget, Cancel, Error) ||
         !MdoBackupOriginAppend(Backup, Copy, &Paths, Result, &Budget, Cancel, Error) ||
         !MdoBackupValidate(Copy, &Budget, Cancel, &Copy->History, Error) ||
         !MdoBackupRelationsValidate(Copy, &Budget, Cancel, &Copy->Relations, Error) ||
         !MdoBackupCheck(&Budget, Cancel, Error) ) goto done;
    *Facts = *Result; Ok = true;
done:
    xrtFree(Meta); xrtFree(Result); xrtValueRelease(Paths); MdoSessionBackupRelease(Reconciled);
    if ( !Ok ) {
        MdoSessionBackupRelease(Copy); Copy = NULL;
        if ( Error->eCode == XWORK_ERROR_NONE ) (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot prepare restore copy", NULL);
    }
    return Copy;
}
