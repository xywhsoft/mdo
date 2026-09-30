#include <stdio.h>
#include <string.h>

#include "purge_intent.h"
#include "../../include/mdo/project_lifecycle.h"
#include "../../include/mdo/projects.h"

#define MDO_PURGE_INTENT_PATH "data/project-purge-intent.json"
#define MDO_PURGE_INTENT_BYTES 2048u

typedef struct MdoPurgeIntent {
    MdoApiPurgeBinding Binding;
    char Name[MDO_PROJECT_NAME_CAPACITY];
    bool Present;
} MdoPurgeIntent;

static xmutex* g_MdoPurgeIntentLock;

bool MdoApiPurgeIntentInit(void)
{
    if ( g_MdoPurgeIntentLock == NULL ) g_MdoPurgeIntentLock = xrtMutexCreate();
    return g_MdoPurgeIntentLock != NULL;
}

void MdoApiPurgeIntentUnit(void)
{
    if ( g_MdoPurgeIntentLock != NULL ) xrtMutexDestroy(g_MdoPurgeIntentLock);
    g_MdoPurgeIntentLock = NULL;
}

static bool MdoPurgeIntentSame(const MdoApiPurgeBinding* A, const MdoApiPurgeBinding* B)
{
    return strcmp(A->RequestId, B->RequestId) == 0 && strcmp(A->ProjectId, B->ProjectId) == 0 &&
        A->Revision == B->Revision && A->CreatedAt == B->CreatedAt;
}

static bool MdoPurgeIntentReceiptSame(const MdoApiPurgeBinding* A, const MdoHomePurgeReceipt* B)
{
    return strcmp(A->RequestId, B->Request.Id) == 0 && strcmp(A->ProjectId, B->Request.ProjectId) == 0 &&
        A->Revision == B->Request.Revision && (uint64)A->CreatedAt == B->Request.CreatedAt;
}

static bool MdoPurgeIntentText(const xvalue* Object, cstr Key, char* Output, size_t Capacity)
{
    xstrview Text;
    if ( !xrtValueGetString(xrtValueObjectGet(Object, xrtStrView(Key)), &Text) ||
         Text.Size == 0u || Text.Size >= Capacity || memchr(Text.Data, '\0', Text.Size) != NULL ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    memcpy(Output, Text.Data, Text.Size); Output[Text.Size] = '\0';
    return true;
}

static bool MdoPurgeIntentUInt(const xvalue* Object, cstr Key, uint64* Output)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    int64 Signed;
    if ( xrtValueGetUInt(Value, Output) ) return true;
    if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Output = (uint64)Signed; return true;
}

static bool MdoPurgeIntentIdentity(const xfileinfo* A, const xfileinfo* B)
{
    const uint32 Required = XFILE_INFO_IDENTITY | XFILE_INFO_SIZE;
    return A->Type == XFILE_TYPE_FILE && B->Type == XFILE_TYPE_FILE &&
        (A->Available & Required) == Required && (B->Available & Required) == Required &&
        A->Identity != 0u && A->Device == B->Device && A->Identity == B->Identity && A->Size == B->Size;
}

/* No built-in fallback, repair, backup or implicit empty-on-error. File-handle
 * and path identity/size must agree before and after the bounded read. */
static bool MdoPurgeIntentRead(MdoPurgeIntent* Intent)
{
    char Bytes[MDO_PURGE_INTENT_BYTES + 1u];
    xfileinfo Before, Opened, After;
    xfile File = NULL;
    xvalue* Root = NULL;
    const xvalue* Value;
    xjsonreadconfig Config;
    uint64 Version, CreatedAt;
    bool Exists, Ok = false;
    memset(Intent, 0, sizeof(*Intent));
    if ( !MdoHomeExternalStat(MDO_PURGE_INTENT_PATH, &Exists, &Before) ) return false;
    if ( !Exists ) return true;
    if ( Before.Type != XFILE_TYPE_FILE || (Before.Available & XFILE_INFO_SIZE) == 0u ||
         Before.Size > MDO_PURGE_INTENT_BYTES ) return false;
    File = MdoHomeOpenRead(MDO_PURGE_INTENT_PATH);
    if ( File == NULL || !xrtFileStat(File, &Opened) || !MdoPurgeIntentIdentity(&Before, &Opened) ||
         !xrtReadFull(File, Bytes, (size_t)Before.Size, NULL) ||
         !xrtFileStat(File, &After) || !MdoPurgeIntentIdentity(&Before, &After) ||
         !MdoHomeExternalStat(MDO_PURGE_INTENT_PATH, &Exists, &After) || !Exists ||
         !MdoPurgeIntentIdentity(&Before, &After) ) goto done;
    Bytes[Before.Size] = '\0';
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = MDO_PURGE_INTENT_BYTES;
    Config.MaxDepth = 3u; Config.MaxValues = 20u; Config.MaxContainerItems = 5u;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Before.Size), &Config);
    if ( Root == NULL || xrtValueType(Root) != XVALUE_OBJECT || xrtValueCount(Root) != 2u ||
         !MdoPurgeIntentUInt(Root, "schema_version", &Version) || Version != 1u ) goto done;
    Value = xrtValueObjectGet(Root, XRT_STR_LITERAL("intent"));
    /* Empty is represented by absence of the file, never a null/empty record. */
    Ok = xrtValueType(Value) == XVALUE_OBJECT && xrtValueCount(Value) == 5u &&
        MdoPurgeIntentText(Value, "purge_request_id", Intent->Binding.RequestId, sizeof(Intent->Binding.RequestId)) &&
        MdoPurgeIntentText(Value, "project_id", Intent->Binding.ProjectId, sizeof(Intent->Binding.ProjectId)) &&
        MdoPurgeIntentText(Value, "name", Intent->Name, sizeof(Intent->Name)) &&
        MdoPurgeIntentUInt(Value, "revision", &Intent->Binding.Revision) &&
        MdoPurgeIntentUInt(Value, "created_at", &CreatedAt) && CreatedAt > 0u && CreatedAt <= INT64_MAX;
    if ( Ok ) {
        Intent->Binding.CreatedAt = (int64)CreatedAt;
        Ok = MdoApiPurgeBindingValid(&Intent->Binding);
    }
done:
    xrtValueRelease(Root);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    Intent->Present = Ok;
    return Ok;
}

static xvalue* MdoPurgeIntentValue(const MdoPurgeIntent* Intent)
{
    xvalue* Value;
    bool Ok;
    if ( !Intent->Present ) return xrtValueNull();
    Value = xrtValueObject();
    Ok = Value != NULL && MdoApiValueSetString(Value, "purge_request_id", Intent->Binding.RequestId) &&
        MdoApiValueSetString(Value, "project_id", Intent->Binding.ProjectId) &&
        MdoApiValueSetUInt(Value, "revision", Intent->Binding.Revision) &&
        MdoApiValueSetInt(Value, "created_at", Intent->Binding.CreatedAt) &&
        MdoApiValueSetString(Value, "name", Intent->Name);
    if ( !Ok ) { xrtValueRelease(Value); Value = NULL; }
    return Value;
}

static bool MdoPurgeIntentWrite(const MdoPurgeIntent* Intent)
{
    xvalue* Root = xrtValueObject();
    char* Text = NULL;
    size_t Size = 0u;
    bool Ok = Root != NULL && MdoApiValueSetUInt(Root, "schema_version", 1u) &&
        xrtValueObjectSetNew(Root, XRT_STR_LITERAL("intent"), MdoPurgeIntentValue(Intent));
    if ( Ok ) Text = xrtJsonStringify(Root, false, &Size);
    Ok = Text != NULL && Size <= MDO_PURGE_INTENT_BYTES &&
        MdoHomeAtomicWrite(MDO_PURGE_INTENT_PATH, Text, Size, false);
    xrtFree(Text); xrtValueRelease(Root);
    return Ok;
}

static xvalue* MdoPurgeIntentReplyValue(const MdoPurgeIntent* Intent, bool Replayed)
{
    xvalue* Data = xrtValueObject();
    if ( Data == NULL || !xrtValueObjectSetNew(Data, XRT_STR_LITERAL("intent"), MdoPurgeIntentValue(Intent)) ||
         !MdoApiValueSetBool(Data, "replayed", Replayed) ) {
        xrtValueRelease(Data); return NULL;
    }
    return Data;
}

static bool MdoPurgeIntentReply(MdoApiContext* Context, const MdoPurgeIntent* Intent, bool Replayed)
{
    char ETag[64];
    xvalue* Data = MdoPurgeIntentReplyValue(Intent, Replayed);
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u, "purge_intent_unavailable",
        "The purge intent could not be serialized", NULL);
    snprintf(ETag, sizeof(ETag), "\"mdo-purge-intent-%s\"", Intent->Present ? Intent->Binding.RequestId : "empty");
    return MdoApiReplySuccessTakeEntityTag(Context, 200u, Data, ETag);
}

bool MdoApiPurgeIntentActionBegin(MdoApiContext* Context, const MdoApiPurgeBinding* Binding)
{
    MdoPurgeIntent Intent;
    bool Conflict = false, Read = false;
    if ( g_MdoPurgeIntentLock != NULL ) {
        xrtMutexLock(g_MdoPurgeIntentLock);
        Read = MdoPurgeIntentRead(&Intent);
        Conflict = Read && Intent.Present && !MdoPurgeIntentSame(&Intent.Binding, Binding);
        if ( Read && !Conflict ) return true;
        xrtMutexUnlock(g_MdoPurgeIntentLock);
    }
    (void)MdoApiReplyError(Context, Conflict ? 409u : 503u,
        Conflict ? "purge_intent_conflict" : "purge_intent_unavailable",
        Conflict ? "Settle the existing purge intent before starting another ID" :
            "The saved purge intent could not be verified; preserve its request ID", NULL);
    return false;
}

void MdoApiPurgeIntentActionEnd(void)
{
    xrtMutexUnlock(g_MdoPurgeIntentLock);
}

bool MdoApiProjectPurgeIntentPrepareRoute(MdoApiContext* Context)
{
    MdoApiPurgeBinding Binding;
    MdoPurgeIntent Intent, Verified;
    MdoProjectInfo Project;
    MdoProjectLease* Lease = NULL;
    MdoHomePurgeReceipt Receipt;
    MdoHomeSnapshot Home;
    xwork_error Error;
    bool Found, Replayed = false;
    uint16 Status = 503u;
    cstr Code = "purge_intent_unavailable", Message = "The purge intent could not be saved; verify the same request ID";
    if ( !MdoApiPurgeBindingRead(Context, &Binding) ) return true;
    if ( g_MdoPurgeIntentLock == NULL ) return MdoApiReplyError(Context, Status, Code, Message, NULL);
    xrtMutexLock(g_MdoPurgeIntentLock);
    if ( !MdoPurgeIntentRead(&Intent) ) goto done;
    if ( Intent.Present ) {
        if ( !MdoPurgeIntentSame(&Intent.Binding, &Binding) ) {
            Status = 409u; Code = "purge_intent_conflict"; Message = "Settle the existing purge intent before starting another ID";
        } else { Status = 200u; Replayed = true; }
        goto done;
    }
    if ( !MdoHomePurgeReceiptGet(Binding.RequestId, &Receipt, &Found) ) goto done;
    if ( Found ) {
        Status = 409u; Code = "purge_request_conflict"; Message = "This purge request ID is already accepted; query its result";
        goto done;
    }
    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);
    if ( !MdoHomeGetSnapshot(&Home) || Home.RestartRequired ) {
        Code = "purge_restart_required"; Message = "Restart before preparing another purge intent"; goto done;
    }
    Lease = MdoProjectLeaseAcquire(Binding.ProjectId, MDO_PROJECT_LEASE_SHARED, &Error);
    if ( Lease == NULL ) {
        if ( Error.eCode == XWORK_ERROR_CONTEXT ) {
            Status = 409u; Code = "project_busy"; Message = "Project data is being changed; try again later";
        }
        goto done;
    }
    memset(&Project, 0, sizeof(Project)); Project.Size = sizeof(Project);
    if ( !MdoProjectGet(Binding.ProjectId, &Project, &Found, &Error) ) goto done;
    if ( !Found ) { Status = 404u; Code = "project_not_found"; Message = "The reviewed project definition no longer exists"; goto done; }
    if ( Project.Revision != Binding.Revision || Project.CreatedAt != Binding.CreatedAt ) {
        Status = 412u; Code = "revision_conflict"; Message = "The project changed after review; refresh its preview"; goto done;
    }
    Intent.Binding = Binding; Intent.Present = true;
    snprintf(Intent.Name, sizeof(Intent.Name), "%s", Project.Name);
    (void)MdoPurgeIntentWrite(&Intent);
    /* A publication/close error may still have saved it. Read back the exact
     * binding/name before confirming; a lost reply must never require a new ID. */
    if ( MdoPurgeIntentRead(&Verified) && Verified.Present &&
         MdoPurgeIntentSame(&Intent.Binding, &Verified.Binding) && strcmp(Intent.Name, Verified.Name) == 0 ) {
        Intent = Verified; Status = 200u; xrtClearError();
    }
done:
    MdoProjectLeaseRelease(Lease);
    xrtMutexUnlock(g_MdoPurgeIntentLock);
    if ( Status == 200u ) return MdoPurgeIntentReply(Context, &Intent, Replayed);
    return MdoApiReplyError(Context, Status, Code, Message, NULL);
}

static int MdoPurgeIntentAckId(const MdoApiContext* Context, char Id[33])
{
    static const char Prefix[] = "\"mdo-purge-intent-";
    const xhttpfield* Field = NULL;
    xhttpnext Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("If-Match"), &Field);
    xstrview Value;
    if ( Next == XHTTP_NEXT_END ) return 0;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ) return -1;
    Value = xrtStrTrim(Field->Value);
    if ( Value.Size != sizeof(Prefix) - 1u + 33u ||
         memcmp(Value.Data, Prefix, sizeof(Prefix) - 1u) != 0 || Value.Data[Value.Size - 1u] != '"' ) return -1;
    memcpy(Id, Value.Data + sizeof(Prefix) - 1u, 32u); Id[32] = '\0';
    return MdoHomePurgeRequestIdValid(Id) ? 1 : -1;
}

bool MdoApiProjectPurgeIntentRoute(MdoApiContext* Context)
{
    MdoPurgeIntent Intent, Verified;
    MdoHomePurgeReceipt Receipt;
    MdoHomeSnapshot Home;
    char Id[33];
    bool Found, Ack = Context->Request->head->MethodCode == XHTTP_METHOD_DELETE, Replayed = false;
    int Precondition;
    uint16 Status = 503u;
    cstr Code = "purge_intent_unavailable", Message = "The saved purge intent could not be verified";
    if ( Ack ) {
        Precondition = MdoPurgeIntentAckId(Context, Id);
        if ( Precondition <= 0 ) return MdoApiReplyError(Context, Precondition == 0 ? 428u : 400u,
            Precondition == 0 ? "precondition_required" : "invalid_precondition",
            "If-Match must contain the saved purge intent ETag", NULL);
    }
    if ( g_MdoPurgeIntentLock == NULL ) return MdoApiReplyError(Context, Status, Code, Message, NULL);
    xrtMutexLock(g_MdoPurgeIntentLock);
    if ( !MdoPurgeIntentRead(&Intent) ) goto done;
    if ( !Ack ) { Status = 200u; goto done; }
    if ( Intent.Present && strcmp(Intent.Binding.RequestId, Id) != 0 ) {
        Status = 412u; Code = "revision_conflict"; Message = "The saved purge intent belongs to another request ID"; goto done;
    }
    if ( !MdoHomePurgeReceiptGet(Id, &Receipt, &Found) ) {
        Code = "purge_result_unavailable"; Message = "The purge result could not be verified"; goto done;
    }
    if ( !Found || Receipt.Outcome == MDO_HOME_PURGE_PENDING ) {
        Status = 409u; Code = "purge_intent_unsettled";
        Message = "Verify a terminal result or durably cancel the same ID before discarding its intent"; goto done;
    }
    if ( Intent.Present && !MdoPurgeIntentReceiptSame(&Intent.Binding, &Receipt) ) {
        Status = 409u; Code = "purge_request_conflict"; Message = "The saved intent and purge receipt disagree"; goto done;
    }
    if ( !Intent.Present ) { Status = 200u; Replayed = true; goto done; }
    (void)MdoHomeRemove(MDO_PURGE_INTENT_PATH, false);
    if ( MdoPurgeIntentRead(&Verified) && !Verified.Present ) {
        Intent = Verified; Status = 200u; xrtClearError(); goto done;
    }
    memset(&Home, 0, sizeof(Home)); Home.Size = sizeof(Home);
    if ( !MdoHomeGetSnapshot(&Home) || Home.RestartRequired ) {
        Code = "purge_restart_required"; Message = "The result is recorded; restart before acknowledging its intent";
    }
done:
    xrtMutexUnlock(g_MdoPurgeIntentLock);
    if ( Status == 200u ) return MdoPurgeIntentReply(Context, &Intent, Replayed);
    return MdoApiReplyError(Context, Status, Code, Message, NULL);
}
