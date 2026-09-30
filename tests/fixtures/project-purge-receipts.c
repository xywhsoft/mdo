/* Copied HTTP fixture only. Faults and checkpoints never enter product code. */
#define MDO_RECEIPT_PROBE_PREFIX "/__fixture/project-purge-request/"
static unsigned g_MdoReceiptFault;

static void MdoReceiptFixtureInitState(void)
{
    MdoBootstrapSnapshot Snapshot;
    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    if ( !MdoBootstrapGetSnapshot(&Snapshot) || !Snapshot.Ready ) {
        printf("receipt_fixture_init_failed=1\n"); fflush(stdout);
    }
}
static bool g_MdoReceiptCollision;
static xatomic32 g_MdoReceiptCollisionReady;
static unsigned g_MdoCancelOrder;
static xatomic32 g_MdoCancelStage, g_MdoCancelStarted, g_MdoCancelDone;

static bool MdoReceiptWait(const xatomic32* Value)
{
    unsigned i;
    for ( i = 0u; i < 300u; ++i ) {
        if ( xrtAtomic32Load(Value, XMEMORY_ACQUIRE) != 0u ) return true;
        xrtSleep(10u);
    }
    return false;
}

void MdoReceiptFixtureBeforeReferences(void)
{
    unsigned i;
    if ( g_MdoCancelOrder == 1u ) {
        xrtAtomic32Store(&g_MdoCancelStage, 1u, XMEMORY_RELEASE);
        (void)MdoReceiptWait(&g_MdoCancelDone);
    }
    if ( !g_MdoReceiptCollision ) return;
    (void)xrtAtomic32FetchAdd(&g_MdoReceiptCollisionReady, 1u, XMEMORY_RELEASE);
    for ( i = 0u; i < 300u && xrtAtomic32Load(&g_MdoReceiptCollisionReady, XMEMORY_ACQUIRE) < 2u; ++i )
        xrtSleep(10u);
}

typedef struct MdoReceiptCollision {
    MdoProjectInfo Project;
    MdoProjectPurgeResult Result;
    MdoProjectPurgeStatus Status;
} MdoReceiptCollision;

typedef struct MdoReceiptCancelCall {
    MdoReceiptCollision* Execution;
    MdoHomePurgeReceipt Receipt;
    bool Ok, Replayed;
} MdoReceiptCancelCall;

static int32 MdoReceiptCancelRun(ptr Data)
{
    MdoReceiptCancelCall* Call = (MdoReceiptCancelCall*)Data;
    const MdoProjectInfo* Project = &Call->Execution->Project;
    if ( MdoReceiptWait(&g_MdoCancelStage) ) {
        xrtAtomic32Store(&g_MdoCancelStarted, 1u, XMEMORY_RELEASE);
        Call->Ok = MdoHomePurgeRequestCancel("88888888888888888888888888888888",
            Project->Id, Project->Revision, Project->CreatedAt, &Call->Receipt, &Call->Replayed);
    }
    xrtAtomic32Store(&g_MdoCancelDone, 1u, XMEMORY_RELEASE);
    return 0;
}

static int32 MdoReceiptCollisionRun(ptr Data)
{
    MdoReceiptCollision* Call = (MdoReceiptCollision*)Data;
    Call->Status = MdoProjectPurgeExecuteRequested("88888888888888888888888888888888",
        Call->Project.Id, Call->Project.Revision, Call->Project.CreatedAt, &Call->Result, NULL);
    return 0;
}

static void MdoReceiptCheckpoint(const char* Name)
{
    printf("purge_receipt_checkpoint=%s\n", Name); fflush(stdout); xrtSleep(8000u);
}

bool MdoReceiptFixturePublish(const MdoHomePurgeRequest* Request, MdoHomePurgeOutcome Outcome)
{
    if ( g_MdoReceiptFault == 1u ) return MdoHomePurgeError("bounded result publication failure");
    return MdoHomePurgeReceiptPublish(Request, Outcome);
}

bool MdoReceiptFixtureWrite(cstr Path, const void* Text, size_t Size)
{
    if ( g_MdoReceiptFault == 2u ) {
        bool Ok = MdoHomeImportWrite(Path, Text, Size / 2u);
        if ( Ok ) MdoReceiptCheckpoint("partial");
        return false;
    }
    return MdoHomeImportWrite(Path, Text, Size);
}

bool MdoReceiptFixtureRename(xroot Root, cstr Source, cstr Target)
{
    bool Ok;
    if ( g_MdoReceiptFault == 3u ) MdoReceiptCheckpoint("temporary");
    Ok = xrtRootRenameNoReplace(Root, Source, Target);
    if ( Ok && g_MdoReceiptFault == 4u ) MdoReceiptCheckpoint("published");
    if ( Ok && g_MdoReceiptFault == 5u ) {
        (void)MdoHomePurgeError("bounded rename close error after publication"); return false;
    }
    return Ok;
}

void MdoReceiptFixtureAccepted(void)
{
    if ( g_MdoCancelOrder == 2u ) {
        xrtAtomic32Store(&g_MdoCancelStage, 1u, XMEMORY_RELEASE);
        (void)MdoReceiptWait(&g_MdoCancelStarted);
    }
    if ( g_MdoReceiptFault == 6u ) MdoReceiptCheckpoint("accepted");
}

static bool MdoReceiptFixtureText(const xvalue* Root, cstr Key, char* Text, size_t Capacity)
{
    xstrview View;
    if ( !xrtValueGetString(xrtValueObjectGet(Root, xrtStrView(Key)), &View) ||
         View.Size >= Capacity || memchr(View.Data, '\0', View.Size) != NULL ) return false;
    memcpy(Text, View.Data, View.Size); Text[View.Size] = '\0'; return true;
}

static bool MdoPurgeReceiptFixtureControl(XS_HttpReq* Http)
{
    static const char Prefix[] = MDO_RECEIPT_PROBE_PREFIX;
    MdoApiContext Context;
    MdoApiJsonBody Body;
    MdoProjectPurgeResult Result;
    MdoHomePurgeReceipt Receipt;
    MdoProjectPurgeStatus Status;
    xwork_error Error;
    xvalue* Data;
    char Id[65] = { 0 }, Project[65] = { 0 };
    xstrview Target;
    int64 Revision = 1, CreatedAt = 0, Fault = 0;
    bool Ok = true, Found;
    if ( Http == NULL || Http->head == NULL ) return false;
    Target = Http->head->Target;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    memset(&Context, 0, sizeof(Context)); Context.Request = Http;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "fixture-purge-receipt");
    if ( MdoApiJsonBodyRead(&Context, &Body) != MDO_API_BODY_OK ) {
        (void)MdoApiReplyError(&Context, 400u, "fixture_body", "Purge receipt fixture needs JSON", NULL); return true;
    }
    Data = xrtValueObject();
    if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "fault") ) {
        Ok = xrtValueGetInt(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("value")), &Fault) && Fault >= 0 && Fault <= 6;
        if ( Ok ) g_MdoReceiptFault = (unsigned)Fault;
    } else if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "cancel-race") ) {
        MdoReceiptCollision Execution;
        MdoReceiptCancelCall Cancel;
        xthread* Threads[2] = { NULL, NULL };
        size_t i;
        memset(&Execution, 0, sizeof(Execution)); memset(&Cancel, 0, sizeof(Cancel));
        Execution.Project.Size = sizeof(Execution.Project); Cancel.Execution = &Execution;
        Ok = xrtValueGetInt(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("order")), &Fault) &&
            (Fault == 1 || Fault == 2) && MdoProjectGet("purge-probe", &Execution.Project, &Found, &Error) && Found;
        if ( Ok ) {
            g_MdoCancelOrder = (unsigned)Fault;
            xrtAtomic32Init(&g_MdoCancelStage, 0u); xrtAtomic32Init(&g_MdoCancelStarted, 0u);
            xrtAtomic32Init(&g_MdoCancelDone, 0u);
            Threads[0] = xrtThreadCreate(MdoReceiptCollisionRun, &Execution, 0u);
            Threads[1] = xrtThreadCreate(MdoReceiptCancelRun, &Cancel, 0u);
            for ( i = 0u; i < 2u; ++i ) if ( Threads[i] != NULL ) {
                (void)xrtThreadWait(Threads[i]); xrtThreadDestroy(Threads[i]);
            }
            g_MdoCancelOrder = 0u;
            Ok = Threads[0] != NULL && Threads[1] != NULL && Cancel.Ok &&
                MdoApiValueSetUInt(Data, "execution_status", Execution.Status) &&
                MdoApiValueSetUInt(Data, "execution_targets", Execution.Result.Targets) &&
                MdoApiValueSetBool(Data, "execution_replayed", Execution.Result.Replayed) &&
                MdoApiValueSetBool(Data, "execution_committed", Execution.Result.Committed) &&
                MdoApiValueSetUInt(Data, "cancel_outcome", Cancel.Receipt.Outcome) &&
                MdoApiValueSetBool(Data, "cancel_committed", Cancel.Receipt.Committed) &&
                MdoApiValueSetBool(Data, "cancel_replayed", Cancel.Replayed);
        }
    } else if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "cancel-invalid") ) {
        bool Replayed = true;
        bool Committed = true;
        MdoHomePurgeRequest Request;
        MdoHomePurgeTarget Target;
        memset(&Request, 0, sizeof(Request)); memset(&Target, 0, sizeof(Target));
        snprintf(Request.Id, sizeof(Request.Id), "dddddddddddddddddddddddddddddddd");
        snprintf(Request.ProjectId, sizeof(Request.ProjectId), "purge-probe");
        Request.Revision = 1u; Request.CreatedAt = 1u;
        memset(&Receipt, 0xff, sizeof(Receipt));
        Ok = !MdoHomePurgeRequestCancel("dddddddddddddddddddddddddddddddd", "purge-probe", 0u,
                1, &Receipt, &Replayed) && !Replayed && Receipt.Request.Id[0] == '\0' && !Receipt.Committed &&
            !MdoHomePurgeRequestCancel("dddddddddddddddddddddddddddddddd", "purge-probe", UINT64_MAX,
                1, &Receipt, &Replayed) &&
            !MdoHomePurgeRequestCancel("dddddddddddddddddddddddddddddddd", "purge-probe", 1u,
                0, &Receipt, &Replayed) &&
            !MdoHomePurgeRequestCancel(NULL, "purge-probe", 1u, 1, &Receipt, &Replayed) &&
            !MdoHomePurgeRequestCancel("dddddddddddddddddddddddddddddddd", "../outside", 1u,
                1, &Receipt, &Replayed) &&
            !MdoHomePurgeRequestCancel("dddddddddddddddddddddddddddddddd", "purge-probe", 1u,
                1, NULL, &Replayed) &&
            !MdoHomePurgeFilesRequested(&Request, &Target, 1u, &Committed) && !Committed &&
            !MdoHomePurgeFilesRequested(&Request, NULL, 0u, &Committed) && !Committed;
    } else if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "collision") ) {
        MdoReceiptCollision Calls[2];
        xthread* Threads[2] = { NULL, NULL };
        size_t i;
        memset(Calls, 0, sizeof(Calls));
        for ( i = 0u; i < 2u; ++i ) {
            Calls[i].Project.Size = sizeof(Calls[i].Project);
            Ok = Ok && MdoProjectGet(i == 0u ? "purge-probe" : "purge-other",
                &Calls[i].Project, &Found, &Error) && Found;
        }
        if ( Ok ) {
            xrtAtomic32Init(&g_MdoReceiptCollisionReady, 0u); g_MdoReceiptCollision = true;
            for ( i = 0u; i < 2u; ++i ) Threads[i] = xrtThreadCreate(MdoReceiptCollisionRun, &Calls[i], 0u);
            for ( i = 0u; i < 2u; ++i ) if ( Threads[i] != NULL ) {
                (void)xrtThreadWait(Threads[i]); xrtThreadDestroy(Threads[i]);
            }
            g_MdoReceiptCollision = false;
            Ok = Threads[0] != NULL && Threads[1] != NULL &&
                xrtAtomic32Load(&g_MdoReceiptCollisionReady, XMEMORY_ACQUIRE) == 2u &&
                MdoApiValueSetUInt(Data, "first", Calls[0].Status) &&
                MdoApiValueSetUInt(Data, "second", Calls[1].Status);
        }
    } else if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "reserved") ) {
        static const char* const Paths[] = { "data/project-purges", "data/project-purges/test.json",
            "DATA./PROJECT-PURGES ./test.json", "data /Project-Purges./test.json" };
        size_t i;
        for ( i = 0u; i < sizeof(Paths) / sizeof(Paths[0]); ++i ) {
            char* Native = MdoHomeExternalPath(Paths[i]);
            xfile File = MdoHomeOpenWrite(Paths[i], XFILE_CREATE | XFILE_WRITE);
            Ok = Ok && Native == NULL && File == NULL && !MdoHomeAtomicWrite(Paths[i], "x", 1u, false);
            xrtFree(Native); if ( File != NULL ) (void)xrtClose(File);
        }
        Ok = Ok && !MdoHomeRenameNoReplace("data", "data-saved") &&
            !MdoHomeRenameNoReplace("DATA. ", "data-saved") &&
            !MdoHomeRenameNoReplace("data-saved", "DATA.");
    } else if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "get") ) {
        Ok = MdoReceiptFixtureText(Body.Value, "id", Id, sizeof(Id));
        if ( Ok ) Ok = MdoHomePurgeReceiptGet(Id, &Receipt, &Found);
        if ( Ok ) {
            Ok = MdoApiValueSetBool(Data, "found", Found);
            if ( Found ) Ok = Ok && MdoApiValueSetUInt(Data, "outcome", Receipt.Outcome) &&
                MdoApiValueSetBool(Data, "committed", Receipt.Committed) &&
                MdoApiValueSetString(Data, "id", Receipt.Request.Id) &&
                MdoApiValueSetString(Data, "project", Receipt.Request.ProjectId) &&
                MdoApiValueSetUInt(Data, "revision", Receipt.Request.Revision) &&
                MdoApiValueSetUInt(Data, "created_at", Receipt.Request.CreatedAt);
        }
    } else if ( MdoApiViewEqualText(Target, MDO_RECEIPT_PROBE_PREFIX "execute") ) {
        Ok = MdoReceiptFixtureText(Body.Value, "id", Id, sizeof(Id)) &&
            MdoReceiptFixtureText(Body.Value, "project", Project, sizeof(Project)) &&
            xrtValueGetInt(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("revision")), &Revision) &&
            xrtValueGetInt(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("created_at")), &CreatedAt);
        if ( Ok ) {
            Status = MdoProjectPurgeExecuteRequested(Id, Project, (uint64)Revision, CreatedAt, &Result, &Error);
            Ok = MdoApiValueSetUInt(Data, "status", Status) &&
                MdoApiValueSetBool(Data, "committed", Result.Committed) &&
                MdoApiValueSetBool(Data, "replayed", Result.Replayed) &&
                MdoApiValueSetBool(Data, "restart_required", Result.RestartRequired) &&
                MdoApiValueSetUInt(Data, "targets", Result.Targets) &&
                MdoApiValueSetUInt(Data, "files", Result.Files) &&
                MdoApiValueSetUInt(Data, "directories", Result.Directories) &&
                MdoApiValueSetUInt(Data, "bytes", Result.Bytes) &&
                MdoApiValueSetUInt(Data, "schedules", Result.Schedules) &&
                MdoApiValueSetBool(Data, "selection_removed", Result.SelectionRemoved) &&
                MdoApiValueSetBool(Data, "global_draft_removed", Result.GlobalDraftRemoved) &&
                MdoApiValueSetString(Data, "request_id", Result.RequestId) &&
                MdoApiValueSetString(Data, "error", Error.sMessage);
        }
    } else Ok = false;
    MdoApiJsonBodyUnit(&Body);
    if ( !Ok ) {
        xrtValueRelease(Data);
        (void)MdoApiReplyError(&Context, 503u, "fixture_receipt", "Receipt fixture refused", NULL);
    } else (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
