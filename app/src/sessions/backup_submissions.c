#include <stdio.h>
#include <string.h>

#include "backup_internal.h"
#include "sidecars/draft.h"
#include "sidecars/queue.h"

/* Restore preparation never uses the live queue adapters: their read paths
 * reconcile receipts and may recover a run. Only the owning pure codecs and
 * retained-history evidence are used here. The original source remains the
 * durable input for the eventual provenance/publication transaction. */
typedef struct MdoBackupInputPlan {
    MdoQueue Queue;
    MdoDraft Draft;
    char Ids[MDO_SESSION_BACKUP_MAX_INPUTS][33];
    MdoBackupAdmission Admission[MDO_SESSION_BACKUP_MAX_INPUTS];
    size_t Count;
    MdoSessionBackupInputs Facts;
} MdoBackupInputPlan;

static bool MdoInputSet(xvalue* Root, const char* Key, xvalue* Value)
{
    bool Ok = Root != NULL && Value != NULL && xrtValueObjectSetTake(Root, xrtStrView(Key), &Value);
    xrtValueRelease(Value); return Ok;
}

static bool MdoInputString(xvalue* Root, const char* Key, const char* Text, size_t Bytes)
{
    return MdoInputSet(Root, Key, xrtValueString(xrtStrViewN(Text, Bytes)));
}

static bool MdoInputImages(xvalue* Root, const char Images[4][33], size_t Count)
{
    xvalue* Array = xrtValueArray();
    size_t i;
    bool Ok = Array != NULL;
    for ( i = 0u; Ok && i < Count; ++i ) {
        xvalue* Id = xrtValueString(xrtStrView(Images[i]));
        Ok = Id != NULL && xrtValueArrayAppendTake(Array, &Id); xrtValueRelease(Id);
    }
    if ( Ok ) Ok = MdoInputSet(Root, "attachments", Array); else xrtValueRelease(Array);
    return Ok;
}

static bool MdoInputProfile(xvalue* Root, const char* Key, const MdoComposerProfile* Profile)
{
    xvalue* Value;
    if ( !Profile->Present ) return true;
    Value = xrtValueObject();
    if ( Value == NULL || !MdoInputString(Value, "model_id", Profile->ModelId, strlen(Profile->ModelId)) ||
         !MdoInputString(Value, "reasoning_effort", Profile->ReasoningEffort, strlen(Profile->ReasoningEffort)) ||
         !MdoInputString(Value, "permission_profile", Profile->PermissionProfile, strlen(Profile->PermissionProfile)) ) {
        xrtValueRelease(Value); return false;
    }
    return MdoInputSet(Root, Key, Value);
}

/* Match the actual browser's String.trim(), including Unicode whitespace.
 * Submission bytes themselves are never normalized or changed. */
static xstrview MdoInputTrim(xstrview Text)
{
    size_t Offset = 0u, First = SIZE_MAX, Last = 0u;
    while ( Offset < Text.Size ) {
        uint32 Scalar;
        size_t Read = 0u;
        bool Space;
        if ( xrtUtf8Decode(xrtStrViewN(Text.Data + Offset, Text.Size - Offset), &Scalar, &Read) != XUTF_OK ) return Text;
        Space = (Scalar >= 9u && Scalar <= 13u) || Scalar == 0x20u || Scalar == 0xa0u ||
            Scalar == 0x1680u || (Scalar >= 0x2000u && Scalar <= 0x200au) ||
            Scalar == 0x2028u || Scalar == 0x2029u || Scalar == 0x202fu || Scalar == 0x205fu ||
            Scalar == 0x3000u || Scalar == 0xfeffu;
        if ( !Space ) { if ( First == SIZE_MAX ) First = Offset; Last = Offset + Read; }
        Offset += Read;
    }
    return First == SIZE_MAX ? xrtStrViewN(Text.Data, 0u) : xrtStrViewN(Text.Data + First, Last - First);
}

static bool MdoInputSame(const MdoQueueItem* Queue, const MdoDraftSubmission* Draft)
{
    xstrview Text = MdoInputTrim(xrtStrViewN(Draft->Text, Draft->TextSize));
    size_t i;
    if ( Queue->TextSize != Text.Size || memcmp(Queue->Text, Text.Data, Text.Size) != 0 ||
         Queue->Priority != Draft->Interrupt || Queue->AttachmentCount != Draft->AttachmentCount ||
         !MdoComposerProfileEqual(&Queue->Profile, &Draft->Profile) ) return false;
    for ( i = 0u; i < Queue->AttachmentCount; ++i )
        if ( strcmp(Queue->Attachments[i], Draft->Attachments[i]) != 0 ) return false;
    return true;
}

static size_t MdoInputFind(const MdoBackupInputPlan* Plan, const char* Id)
{
    size_t i;
    for ( i = 0u; i < Plan->Count; ++i ) if ( strcmp(Plan->Ids[i], Id) == 0 ) return i;
    return SIZE_MAX;
}

static bool MdoInputNewId(const MdoSessionBackup* Backup, MdoBackupInputPlan* Plan, char Id[33],
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    size_t Attempt, i;
    for ( Attempt = 0u; Attempt < 16u; ++Attempt ) {
        char Path[64];
        bool Collision;
        str Candidate;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        Candidate = xrtSecureStringFrom(XRT_STR_LITERAL("0123456789abcdef"), 32u);
        if ( Candidate == NULL ) return MdoBackupError(Error, XWORK_ERROR_IO, "cannot generate restore input ID", NULL);
        memcpy(Id, Candidate, 33u); xrtFree(Candidate);
        Collision = MdoInputFind(Plan, Id) != SIZE_MAX;
        snprintf(Path, sizeof(Path), "queue-receipts/%s.json", Id);
        Collision = Collision || MdoBackupFind(Backup, Path) != NULL;
        for ( i = 0u; i < Plan->Facts.Count; ++i )
            if ( Plan->Facts.Items[i].ReviewId != Id )
                Collision = Collision || strcmp(Plan->Facts.Items[i].ReviewId, Id) == 0;
        if ( !Collision ) return true;
    }
    return MdoBackupError(Error, XWORK_ERROR_LIMIT, "cannot reserve distinct restore input IDs", NULL);
}

static bool MdoInputPlanRead(const MdoSessionBackup* Backup, MdoBackupInputPlan* Plan,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const MdoBackupOwnedFile* File = MdoBackupFind(Backup, "queue.json");
    size_t i;
    if ( File != NULL && !MdoQueueParse(xrtStrViewN(File->Data, File->Bytes), &Plan->Queue) ) goto invalid;
    File = MdoBackupFind(Backup, "draft.json");
    if ( File != NULL && !MdoDraftParse(xrtStrViewN(File->Data, File->Bytes), MDO_DRAFT_SESSION, &Plan->Draft) ) goto invalid;
    for ( i = 0u; i < Plan->Queue.Count; ++i ) memcpy(Plan->Ids[Plan->Count++], Plan->Queue.Items[i].Id, 33u);
    for ( i = 0u; i < Plan->Draft.SubmissionCount; ++i ) {
        const MdoDraftSubmission* Item = Plan->Draft.Submissions[i];
        size_t Match = MdoQueueFind(&Plan->Queue, Item->Id);
        if ( Match != SIZE_MAX ) {
            if ( !MdoInputSame(&Plan->Queue.Items[Match], Item) )
                return MdoBackupError(Error, XWORK_ERROR_IO, "same input ID has conflicting queue/draft payloads", "draft.json");
        } else memcpy(Plan->Ids[Plan->Count++], Item->Id, 33u);
    }
    return MdoBackupClassifyInputs(Backup, Plan->Ids, Plan->Count, Plan->Admission, Limits, Cancel, Error);
invalid:
    return MdoBackupError(Error, XWORK_ERROR_IO, "invalid source queue/draft schema", NULL);
}

static bool MdoInputDecide(const MdoSessionBackup* Backup, MdoBackupInputPlan* Plan,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    size_t i;
    Plan->Facts.Size = sizeof(Plan->Facts); Plan->Facts.Count = Plan->Count;
    Plan->Facts.ClearedDiscardImages = Plan->Queue.DiscardCount;
    Plan->Facts.DirectRunAdmissionUncertain = Plan->Draft.RunAdmissionUncertain;
    for ( i = 0u; i < Plan->Count; ++i ) {
        MdoSessionBackupInputReview* Item = &Plan->Facts.Items[i];
        size_t Queue = MdoQueueFind(&Plan->Queue, Plan->Ids[i]), j;
        memcpy(Item->SourceId, Plan->Ids[i], 33u);
        Item->AdmissionUncertain = Plan->Admission[i] == MDO_BACKUP_ADMISSION_UNCERTAIN ||
            (Queue != SIZE_MAX && Plan->Queue.Items[Queue].State == MDO_QUEUE_SENDING);
        for ( j = 0u; j < Plan->Draft.SubmissionCount; ++j )
            if ( strcmp(Plan->Draft.Submissions[j]->Id, Item->SourceId) == 0 &&
                 Plan->Draft.Submissions[j]->State == MDO_DRAFT_POSTING ) Item->AdmissionUncertain = true;
        if ( Plan->Admission[i] == MDO_BACKUP_ACCEPTED ) {
            Item->Disposition = MDO_SESSION_BACKUP_INPUT_ACCEPTED; Item->AdmissionUncertain = false;
            if ( Queue != SIZE_MAX ) ++Plan->Facts.AcceptedQueue;
        } else {
            Item->Disposition = Queue != SIZE_MAX ? MDO_SESSION_BACKUP_INPUT_QUEUE_REVIEW : MDO_SESSION_BACKUP_INPUT_DRAFT_REVIEW;
            if ( !MdoInputNewId(Backup, Plan, Item->ReviewId, Limits, Cancel, Error) ) return false;
            if ( Queue != SIZE_MAX ) ++Plan->Facts.QueueReview; else ++Plan->Facts.DraftReview;
        }
    }
    for ( i = 0u; i < Plan->Draft.SubmissionCount; ++i ) {
        const char* Id = Plan->Draft.Submissions[i]->Id;
        const MdoSessionBackupInputReview* Item = &Plan->Facts.Items[MdoInputFind(Plan, Id)];
        if ( Item->Disposition == MDO_SESSION_BACKUP_INPUT_ACCEPTED ) ++Plan->Facts.AcceptedDraft;
        else if ( MdoQueueFind(&Plan->Queue, Id) != SIZE_MAX ) ++Plan->Facts.DuplicateDraft;
    }
    return MdoBackupCheck(Limits, Cancel, Error);
}

static xvalue* MdoInputRow(const MdoSessionBackupInputReview* Fact, const char* Text, size_t Bytes,
    const char Images[4][33], size_t Count, bool Priority, const MdoComposerProfile* Profile, bool Queue)
{
    xvalue* Row = xrtValueObject();
    if ( Row != NULL && MdoInputString(Row, "id", Fact->ReviewId, 32u) &&
         MdoInputString(Row, "text", Text, Bytes) && MdoInputImages(Row, Images, Count) &&
         MdoInputSet(Row, Queue ? "priority" : "interrupt", xrtValueBool(Priority)) &&
         MdoInputString(Row, "state", Queue ? "staged" : "rejected", Queue ? 6u : 8u) &&
         MdoInputProfile(Row, "profile", Profile) ) return Row;
    xrtValueRelease(Row); return NULL;
}

static bool MdoInputWrite(const MdoSessionBackup* Backup, MdoSessionBackup* Copy, MdoBackupInputPlan* Plan,
    bool Queue, const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const char* Path = Queue ? "queue.json" : "draft.json";
    xvalue *Root = NULL, *Rows = NULL;
    size_t i, Count = Queue ? Plan->Queue.Count : Plan->Draft.SubmissionCount;
    bool Ok = false;
    if ( MdoBackupFind(Backup, Path) == NULL ) return true;
    Root = xrtValueObject(); Rows = xrtValueArray();
    if ( Root == NULL || Rows == NULL ) goto done;
    for ( i = 0u; i < Count; ++i ) {
        xvalue* Row;
        const MdoQueueItem* Q = Queue ? &Plan->Queue.Items[i] : NULL;
        const MdoDraftSubmission* D = Queue ? NULL : Plan->Draft.Submissions[i];
        const MdoSessionBackupInputReview* Fact = &Plan->Facts.Items[MdoInputFind(Plan, Queue ? Q->Id : D->Id)];
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) goto done;
        if ( Fact->Disposition != (Queue ? MDO_SESSION_BACKUP_INPUT_QUEUE_REVIEW : MDO_SESSION_BACKUP_INPUT_DRAFT_REVIEW) ) continue;
        Row = Queue ? MdoInputRow(Fact, Q->Text, Q->TextSize, Q->Attachments, Q->AttachmentCount, Q->Priority, &Q->Profile, true) :
            MdoInputRow(Fact, D->Text, D->TextSize, D->Attachments, D->AttachmentCount, D->Interrupt, &D->Profile, false);
        if ( Row == NULL ) goto done;
        Ok = xrtValueArrayAppendTake(Rows, &Row); xrtValueRelease(Row);
        if ( !Ok ) goto done;
        Ok = false;
    }
    if ( !MdoInputSet(Root, "schema_version", xrtValueUInt(7u)) ||
         !xrtValueObjectSetTake(Root, xrtStrView(Queue ? "items" : "submissions"), &Rows) ) goto done;
    if ( Queue ) {
        if ( !MdoInputSet(Root, "discard_images", xrtValueArray()) ) goto done;
    } else if ( !MdoInputSet(Root, "revision", xrtValueUInt(1u)) ||
                !MdoInputString(Root, "text", Plan->Draft.Text, Plan->Draft.TextSize) ||
                !MdoInputImages(Root, Plan->Draft.Attachments, Plan->Draft.AttachmentCount) ||
                !MdoInputSet(Root, "run_admission_uncertain", xrtValueBool(false)) ||
                !MdoInputProfile(Root, "composer_profile", &Plan->Draft.ComposerProfile) ) goto done;
    Ok = MdoBackupReplaceJson(Copy, Path, Root, Limits, Cancel, Error);
done:
    xrtValueRelease(Root); xrtValueRelease(Rows);
    if ( !Ok && Error->eCode == XWORK_ERROR_NONE )
        return MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot encode review inputs", Path);
    return Ok;
}

static bool MdoInputVerify(MdoSessionBackup* Copy, MdoBackupInputPlan* Plan, xwork_error* Error)
{
    const MdoBackupOwnedFile* File = MdoBackupFind(Copy, "queue.json");
    MdoQueueRelease(&Plan->Queue); MdoDraftUnit(&Plan->Draft);
    if ( File != NULL && !MdoQueueParse(xrtStrViewN(File->Data, File->Bytes), &Plan->Queue) ) goto invalid;
    File = MdoBackupFind(Copy, "draft.json");
    if ( File != NULL && !MdoDraftParse(xrtStrViewN(File->Data, File->Bytes), MDO_DRAFT_SESSION, &Plan->Draft) ) goto invalid;
    return true;
invalid:
    return MdoBackupError(Error, XWORK_ERROR_IO, "review inputs exceed live codec limits", "queue.json/draft.json");
}

MdoSessionBackup* MdoSessionBackupReviewInputs(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, MdoSessionBackupInputs* Facts, xwork_error* Error)
{
    MdoBackupInputPlan* Plan = NULL;
    MdoSessionBackup* Copy = NULL;
    MdoSessionBackupLimits Budget;
    xwork_error Local;
    size_t i;
    bool Ok = false;
    if ( Error == NULL ) Error = &Local;
    xworkErrorInit(Error);
    if ( Facts == NULL || Facts->Size != sizeof(*Facts) ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "input review facts have the wrong size", NULL); return NULL;
    }
    memset(Facts, 0, sizeof(*Facts)); Facts->Size = sizeof(*Facts);
    if ( Backup == NULL || !Backup->Decoded || Backup->Schema != MDO_SESSION_BACKUP_SCHEMA ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "input review requires a decoded v2 backup", NULL); return NULL;
    }
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) || !MdoBackupCheck(&Budget, Cancel, Error) ) return NULL;
    if ( Backup->Count > Budget.Files || Backup->Bytes > Budget.TotalBytes ) goto limit;
    for ( i = 0u; i < Backup->Count; ++i ) if ( Backup->Files[i].Bytes > Budget.FileBytes ) goto limit;
    Plan = (MdoBackupInputPlan*)xrtCalloc(1u, sizeof(*Plan));
    if ( Plan == NULL ) { (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot own restore input plan", NULL); goto done; }
    if ( !MdoInputPlanRead(Backup, Plan, &Budget, Cancel, Error) || !MdoInputDecide(Backup, Plan, &Budget, Cancel, Error) ) goto done;
    Copy = MdoBackupClone(Backup, true, &Budget, Cancel, Error);
    if ( Copy == NULL || !MdoInputWrite(Backup, Copy, Plan, true, &Budget, Cancel, Error) ||
         !MdoInputWrite(Backup, Copy, Plan, false, &Budget, Cancel, Error) ||
         !MdoInputVerify(Copy, Plan, Error) ||
         !MdoBackupValidate(Copy, &Budget, Cancel, &Copy->History, Error) ||
         !MdoBackupRelationsValidate(Copy, &Budget, Cancel, &Copy->Relations, Error) ||
         !MdoBackupCheck(&Budget, Cancel, Error) ) goto done;
    *Facts = Plan->Facts; Ok = true;
done:
    if ( Plan != NULL ) { MdoQueueRelease(&Plan->Queue); MdoDraftUnit(&Plan->Draft); xrtFree(Plan); }
    if ( !Ok ) { MdoSessionBackupRelease(Copy); Copy = NULL; }
    return Copy;
limit:
    (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "backup exceeds input review budgets", NULL); return NULL;
}
