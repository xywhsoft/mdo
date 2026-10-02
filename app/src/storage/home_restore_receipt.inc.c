/* Included by the restore journal leaf. Immutable request/result records share
 * Home's lock, anchored I/O and no-replace rename helpers. No live session is
 * opened to answer a terminal receipt, even after its project was deleted. */
#define MDO_HOME_RESTORE_RECEIPT_DIR "data/session-restores"
#define MDO_HOME_RESTORE_RECEIPT_LIMIT 1024u

static bool MdoHomeRestoreRequestValid(const MdoHomeSessionRestoreRequest* Request)
{
    return Request != NULL && Request->Size == sizeof(*Request) &&
        memchr(Request->ProjectId, 0, sizeof(Request->ProjectId)) != NULL &&
        memchr(Request->SessionId, 0, sizeof(Request->SessionId)) == Request->SessionId + 32u &&
        memchr(Request->SourceSessionId, 0, sizeof(Request->SourceSessionId)) != NULL &&
        memchr(Request->SourceSha256, 0, sizeof(Request->SourceSha256)) == Request->SourceSha256 + 64u &&
        MdoHomePurgeId(Request->ProjectId) && MdoHomePurgeRequestIdValid(Request->SessionId) &&
        MdoHomePurgeId(Request->SourceSessionId) && MdoSessionFileDigits(Request->SourceSha256, 64u, true) &&
        Request->RestoredAt > 0 && Request->ProjectCreatedAt >= 0 && Request->ProjectRevision != UINT64_MAX &&
        ((Request->ProjectRevision == 0u && Request->ProjectCreatedAt == 0 && strcmp(Request->ProjectId, "default") == 0) ||
         (Request->ProjectRevision != 0u && Request->ProjectCreatedAt > 0));
}

static bool MdoHomeRestoreRequestSame(const MdoHomeSessionRestoreRequest* A,
    const MdoHomeSessionRestoreRequest* B)
{
    return strcmp(A->ProjectId, B->ProjectId) == 0 && strcmp(A->SessionId, B->SessionId) == 0 &&
        strcmp(A->SourceSessionId, B->SourceSessionId) == 0 && strcmp(A->SourceSha256, B->SourceSha256) == 0 &&
        A->ProjectRevision == B->ProjectRevision && A->ProjectCreatedAt == B->ProjectCreatedAt && A->RestoredAt == B->RestoredAt;
}

static xvalue* MdoHomeRestoreRequestValue(const MdoHomeSessionRestoreRequest* Request)
{
    xvalue* Value = xrtValueObject();
    bool Ok = Value != NULL && MdoHomeRestoreRequestValid(Request) &&
        MdoHomePurgeTake(Value, "version", xrtValueInt(1)) &&
        MdoHomePurgeTake(Value, "project", xrtValueString(xrtStrView(Request->ProjectId))) &&
        MdoHomePurgeTake(Value, "session", xrtValueString(xrtStrView(Request->SessionId))) &&
        MdoHomePurgeTake(Value, "source_session", xrtValueString(xrtStrView(Request->SourceSessionId))) &&
        MdoHomePurgeTake(Value, "source_sha256", xrtValueString(xrtStrView(Request->SourceSha256))) &&
        MdoHomePurgeTake(Value, "project_revision", xrtValueUInt(Request->ProjectRevision)) &&
        MdoHomePurgeTake(Value, "project_created_at", xrtValueInt(Request->ProjectCreatedAt)) &&
        MdoHomePurgeTake(Value, "restored_at", xrtValueInt(Request->RestoredAt));
    if ( !Ok ) { xrtValueRelease(Value); Value = NULL; }
    return Value;
}

static bool MdoHomeRestoreRequestParse(const xvalue* Value, MdoHomeSessionRestoreRequest* Request)
{
    int64 Version;
    memset(Request, 0, sizeof(*Request)); Request->Size = sizeof(*Request);
    return Value != NULL && xrtValueType(Value) == XVALUE_OBJECT && xrtValueCount(Value) == 8u &&
        xrtValueGetInt(xrtValueObjectGet(Value, XRT_STR_LITERAL("version")), &Version) && Version == 1 &&
        MdoHomeRestoreCopy(Value, "project", Request->ProjectId, sizeof(Request->ProjectId)) &&
        MdoHomeRestoreCopy(Value, "session", Request->SessionId, sizeof(Request->SessionId)) &&
        MdoHomeRestoreCopy(Value, "source_session", Request->SourceSessionId, sizeof(Request->SourceSessionId)) &&
        MdoHomeRestoreCopy(Value, "source_sha256", Request->SourceSha256, sizeof(Request->SourceSha256)) &&
        MdoHomePurgeRequestNumber(Value, "project_revision", &Request->ProjectRevision) &&
        xrtValueGetInt(xrtValueObjectGet(Value, XRT_STR_LITERAL("project_created_at")), &Request->ProjectCreatedAt) &&
        xrtValueGetInt(xrtValueObjectGet(Value, XRT_STR_LITERAL("restored_at")), &Request->RestoredAt) &&
        MdoHomeRestoreRequestValid(Request);
}

static void MdoHomeRestoreReceiptInit(MdoHomeSessionRestoreReceipt* Receipt)
{
    memset(Receipt, 0, sizeof(*Receipt)); Receipt->Size = sizeof(*Receipt);
    Receipt->Request.Size = sizeof(Receipt->Request);
}

static void MdoHomeRestoreReceiptPath(char Path[96], cstr Id)
{
    snprintf(Path, 96u, "%s/%s.json", MDO_HOME_RESTORE_RECEIPT_DIR, Id);
}

static bool MdoHomeRestoreReceiptRead(cstr Id, MdoHomeSessionRestoreReceipt* Receipt, bool* Exists)
{
    char Path[96], *Text = NULL;
    size_t Bytes = 0u;
    xfileinfo Before, After;
    xvalue* Value = NULL;
    xjsonreadconfig Config;
    xstrview Outcome;
    int64 Version;
    bool StillExists, Ok;
    MdoHomeRestoreReceiptInit(Receipt); *Exists = false;
    MdoHomeRestoreReceiptPath(Path, Id);
    if ( !MdoHomeImportStat(Path, Exists, &Before) ) return false;
    if ( !*Exists ) return true;
    Ok = Before.Type == XFILE_TYPE_FILE && (Before.Available & XFILE_INFO_SIZE) != 0u &&
        Before.Size <= MDO_HOME_RESTORE_RECORD_BYTES &&
        MdoHomePurgeReadBounded(Path, MDO_HOME_RESTORE_RECORD_BYTES, &Text, &Bytes) &&
        MdoHomeImportStat(Path, &StillExists, &After) && StillExists && MdoHomePurgeSame(&Before, &After);
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = MDO_HOME_RESTORE_RECORD_BYTES;
    Config.MaxDepth = 3u; Config.MaxValues = 48u; Config.MaxContainerItems = 8u;
    if ( Ok ) Value = xrtJsonRead(xrtStrViewN(Text, Bytes), &Config);
    Ok = Value != NULL && xrtValueType(Value) == XVALUE_OBJECT &&
        xrtValueGetInt(xrtValueObjectGet(Value, XRT_STR_LITERAL("version")), &Version) && Version == 1 &&
        MdoHomeRestoreRequestParse(xrtValueObjectGet(Value, XRT_STR_LITERAL("request")), &Receipt->Request) &&
        strcmp(Id, Receipt->Request.SessionId) == 0 &&
        xrtValueGetString(xrtValueObjectGet(Value, XRT_STR_LITERAL("outcome")), &Outcome);
    if ( Ok && MdoHomeImportName(Outcome, "committed") ) {
        Receipt->Outcome = MDO_HOME_SESSION_RESTORE_COMMITTED; Receipt->Committed = true;
        Receipt->DirectoryIdentity.Type = XFILE_TYPE_DIRECTORY;
        Ok = xrtValueCount(Value) == 4u && MdoHomePurgeGetIdentity(Value, "directory", &Receipt->DirectoryIdentity);
    } else if ( Ok && MdoHomeImportName(Outcome, "aborted") ) {
        Receipt->Outcome = MDO_HOME_SESSION_RESTORE_ABORTED; Ok = xrtValueCount(Value) == 3u;
    } else Ok = false;
    xrtValueRelease(Value); xrtFree(Text);
    return Ok || MdoHomeRestoreError("invalid or changed immutable session restore result");
}

static bool MdoHomeRestoreReceiptMatches(const MdoHomeSessionRestoreReceipt* Receipt,
    const MdoHomeRestoreRecord* Record, bool Committed)
{
    return MdoHomeRestoreRequestSame(&Receipt->Request, &Record->Request) &&
        Receipt->Committed == Committed &&
        (!Committed || (Record->Ready && MdoHomePurgeSame(&Receipt->DirectoryIdentity, &Record->Directory)));
}

static bool MdoHomeRestoreReceiptRetiredMatches(const MdoHomeSessionRestoreReceipt* Receipt,
    const MdoHomeRestoreRecord* Record)
{
    /* GC may already have removed ready. Its permanent terminal receipt keeps
     * the commit fact; deleting that marker cannot become a fresh rollback. */
    return MdoHomeRestoreRequestSame(&Receipt->Request, &Record->Request) &&
        (!Receipt->Committed || !Record->Ready || MdoHomePurgeSame(&Receipt->DirectoryIdentity, &Record->Directory));
}

/* Position checks are small anchored stats, never a payload scan on an HTTP
 * query. Missing private source can prove commit only at the exact live ID. */
static bool MdoHomeRestorePosition(cstr Base, const MdoHomeRestoreRecord* Record, bool* Committed)
{
    char Source[160], Target[128];
    xfileinfo Private, Live;
    bool SourceExists, TargetExists;
    *Committed = false;
    if ( !Record->Ready ) return true;
    snprintf(Source, sizeof(Source), "%s/payload/%s", Base, Record->Name);
    MdoHomeRestoreTarget(Target, Record);
    if ( !MdoHomeImportStat(Source, &SourceExists, &Private) || !MdoHomeImportStat(Target, &TargetExists, &Live) ) return false;
    if ( SourceExists ) return MdoHomePurgeSame(&Private, &Record->Directory) &&
        !(TargetExists && MdoHomePurgeSame(&Live, &Record->Directory));
    *Committed = TargetExists && MdoHomePurgeSame(&Live, &Record->Directory);
    return *Committed;
}

static bool MdoHomeRestoreReceiptGetLocked(cstr Id, MdoHomeSessionRestoreReceipt* Receipt, bool* Found)
{
    size_t i;
    MdoHomeRestoreReceiptInit(Receipt); *Found = false;
    if ( g_MdoHome.Root == NULL ) return true;
    if ( !MdoHomeRestoreReceiptRead(Id, Receipt, Found) ) return false;
    for ( i = 0u; i < 2u; ++i ) {
        cstr Base = i == 0u ? MDO_HOME_RESTORE_DIR : MDO_HOME_RESTORE_GC;
        MdoHomeRestoreRecord Record;
        xfileinfo Info;
        bool Owner, Ready, Exists, Commit;
        if ( !MdoHomeRestoreStat(Base, "owner", &Exists, &Info) ) return false;
        if ( !Exists ) continue;
        if ( !MdoHomeRestoreJournal(Base, &Owner, &Ready) || !Owner || !MdoHomeRestoreLoad(Base, &Record, i != 0u) ) return false;
        if ( !Record.HasRequest || strcmp(Id, Record.Request.SessionId) != 0 ) continue;
        if ( i != 0u ) {
            /* Retirement is only allowed after a verified terminal result.
             * No live-target lookup: it may have evolved since that commit. */
            if ( !*Found || !MdoHomeRestoreReceiptRetiredMatches(Receipt, &Record) ) return false;
        } else {
            if ( !MdoHomeRestorePosition(Base, &Record, &Commit) ) return false;
            if ( *Found ) {
                if ( !MdoHomeRestoreReceiptMatches(Receipt, &Record, Commit) ) return false;
            } else {
                Receipt->Request = Record.Request; Receipt->Committed = Commit;
                if ( Commit ) Receipt->DirectoryIdentity = Record.Directory;
                *Found = true;
            }
        }
    }
    return true;
}

bool MdoHomeSessionRestoreReceiptGet(cstr Id, MdoHomeSessionRestoreReceipt* Receipt, bool* Found)
{
    bool Ok;
    if ( Found != NULL ) *Found = false;
    if ( Receipt == NULL || Receipt->Size != sizeof(*Receipt) )
        return MdoHomeRestoreError("invalid session restore result output");
    MdoHomeRestoreReceiptInit(Receipt);
    if ( !g_MdoHome.Initialized || Found == NULL || !MdoHomePurgeRequestIdValid(Id) )
        return MdoHomeRestoreError("invalid session restore receipt query");
    xrtMutexLock(g_MdoHome.Lock); Ok = MdoHomeRestoreReceiptGetLocked(Id, Receipt, Found);
    xrtMutexUnlock(g_MdoHome.Lock);
    if ( !Ok ) { MdoHomeRestoreReceiptInit(Receipt); *Found = false; }
    return Ok || MdoHomeRestoreError("session restore result evidence is damaged or conflicting");
}

static bool MdoHomeRestoreReceiptCapacity(void)
{
    xfileinfo Before, After;
    xdir Dir;
    xdirentry Entry;
    xdirnext Next;
    bool Exists, Ok = false;
    size_t Count = 0u;
    if ( !MdoHomeImportStat(MDO_HOME_RESTORE_RECEIPT_DIR, &Exists, &Before) ) return false;
    if ( !Exists ) return true;
    if ( Before.Type != XFILE_TYPE_DIRECTORY ) return false;
    Dir = xrtRootDirOpen(g_MdoHome.Root, MDO_HOME_RESTORE_RECEIPT_DIR, XDIR_STAT);
    if ( Dir == NULL ) return false;
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        char Id[33];
        if ( ++Count >= MDO_HOME_RESTORE_RECEIPT_LIMIT || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Name.Size != 37u || memcmp(Entry.Name.Data + 32u, ".json", 5u) != 0 ||
             Entry.Info.Type != XFILE_TYPE_FILE || (Entry.Info.Available & XFILE_INFO_SIZE) == 0u ||
             Entry.Info.Size > MDO_HOME_RESTORE_RECORD_BYTES ) goto done;
        memcpy(Id, Entry.Name.Data, 32u); Id[32] = '\0';
        if ( !MdoHomePurgeRequestIdValid(Id) ) goto done;
    }
    Ok = Next == XDIR_NEXT_END && MdoHomeImportStat(MDO_HOME_RESTORE_RECEIPT_DIR, &Exists, &After) &&
        Exists && MdoHomePurgeSame(&Before, &After);
done:
    if ( !xrtDirClose(Dir) ) Ok = false;
    return Ok || MdoHomeRestoreError("session restore result namespace is invalid or at capacity");
}

/* Flush canonical bytes to private scratch, then no-replace publish. A torn
 * scratch is disposable only if it is an exact prefix of our proven result. */
static bool MdoHomeRestoreReceiptPublish(const MdoHomeRestoreRecord* Record, bool Committed)
{
    char Path[96], *Text = NULL, *Partial = NULL;
    size_t Bytes = 0u, PartialBytes = 0u;
    xfileinfo Before, After;
    MdoHomeSessionRestoreReceipt Existing;
    xvalue* Value = NULL;
    bool Exists, StillExists, Terminal, Ok = false;
    if ( !Record->HasRequest || !MdoHomeRestoreRequestValid(&Record->Request) || (Committed && !Record->Ready) ) return false;
    if ( !MdoHomeRestoreReceiptRead(Record->Request.SessionId, &Existing, &Terminal) ) return false;
    if ( Terminal && !MdoHomeRestoreReceiptMatches(&Existing, Record, Committed) ) return false;
    MdoHomeRestoreReceiptPath(Path, Record->Request.SessionId);
    if ( !MdoHomeEnsureParents(g_MdoHome.Root, Path) ) return false;
    Value = xrtValueObject();
    if ( Value == NULL || !MdoHomePurgeTake(Value, "version", xrtValueInt(1)) ||
         !MdoHomePurgeTake(Value, "request", MdoHomeRestoreRequestValue(&Record->Request)) ||
         !MdoHomePurgeTake(Value, "outcome", xrtValueString(xrtStrView(Committed ? "committed" : "aborted"))) ||
         (Committed && !MdoHomePurgePutIdentity(Value, "directory", &Record->Directory)) ) goto done;
    Text = xrtJsonStringify(Value, false, &Bytes);
    if ( Text == NULL || Bytes > MDO_HOME_RESTORE_RECORD_BYTES ||
         !MdoHomeImportStat(MDO_HOME_RESTORE_DIR "/result.tmp", &Exists, &Before) ) goto done;
    if ( Exists ) {
        if ( Before.Type != XFILE_TYPE_FILE || (Before.Available & XFILE_INFO_SIZE) == 0u || Before.Size > Bytes ||
             !MdoHomePurgeReadBounded(MDO_HOME_RESTORE_DIR "/result.tmp", MDO_HOME_RESTORE_RECORD_BYTES, &Partial, &PartialBytes) ||
             PartialBytes > Bytes || (PartialBytes != 0u && memcmp(Partial, Text, PartialBytes) != 0) ||
             !MdoHomeImportStat(MDO_HOME_RESTORE_DIR "/result.tmp", &StillExists, &After) ||
             !StillExists || !MdoHomePurgeSame(&Before, &After) ) goto done;
    }
    if ( Terminal ) { Ok = true; goto done; }
    if ( Exists && PartialBytes != Bytes && !xrtRootRemove(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/result.tmp") ) goto done;
    if ( (!Exists || PartialBytes != Bytes) && !MdoHomeImportWrite(MDO_HOME_RESTORE_DIR "/result.tmp", Text, Bytes) ) goto done;
    if ( xrtRootRenameNoReplace(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/result.tmp", Path) ) Ok = true;
    else if ( MdoHomeRestoreReceiptRead(Record->Request.SessionId, &Existing, &Exists) && Exists &&
              MdoHomeRestoreReceiptMatches(&Existing, Record, Committed) ) { xrtClearError(); Ok = true; }
done:
    xrtFree(Text); xrtFree(Partial); xrtValueRelease(Value); return Ok;
}

static bool MdoHomeRestoreResultScratchValid(cstr Base, const MdoHomeRestoreRecord* Record)
{
    char Scratch[128], Target[96], *Partial = NULL, *Text = NULL;
    size_t PartialBytes = 0u, Bytes = 0u;
    xfileinfo Before, After;
    bool Exists, StillExists, Ok;
    snprintf(Scratch, sizeof(Scratch), "%s/result.tmp", Base);
    if ( !MdoHomeImportStat(Scratch, &Exists, &Before) ) return false;
    if ( !Exists ) return true;
    if ( !Record->HasRequest ) return false;
    MdoHomeRestoreReceiptPath(Target, Record->Request.SessionId);
    Ok = Before.Type == XFILE_TYPE_FILE && (Before.Available & XFILE_INFO_SIZE) != 0u &&
        Before.Size <= MDO_HOME_RESTORE_RECORD_BYTES &&
        MdoHomePurgeReadBounded(Target, MDO_HOME_RESTORE_RECORD_BYTES, &Text, &Bytes) &&
        MdoHomePurgeReadBounded(Scratch, MDO_HOME_RESTORE_RECORD_BYTES, &Partial, &PartialBytes) &&
        PartialBytes <= Bytes && (PartialBytes == 0u || memcmp(Partial, Text, PartialBytes) == 0) &&
        MdoHomeImportStat(Scratch, &StillExists, &After) && StillExists && MdoHomePurgeSame(&Before, &After);
    xrtFree(Text); xrtFree(Partial); return Ok;
}
