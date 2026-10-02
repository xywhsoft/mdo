/* Private storage transaction, included after home_import.inc.c by home.c.
 * The journal is immutable once ready. Each rename is reconstructed from
 * exactly one of its two locations, not from a mutable progress counter.
 * Retired journals can only be cleaned up, never interpreted as rollback. */
#define MDO_HOME_PURGE_DIR ".mdo-purge"
#define MDO_HOME_PURGE_GC ".mdo-purge-cleanup"
#define MDO_HOME_PURGE_MAGIC "mdo-project-purge-v1\n"
#define MDO_HOME_PURGE_MANIFEST_LIMIT (512u * 1024u)
#define MDO_HOME_PURGE_NODE_LIMIT 8192u
#define MDO_HOME_PURGE_RECEIPT_DIR "data/project-purges"
#define MDO_HOME_PURGE_RECEIPT_LIMIT 1024u
#define MDO_HOME_PURGE_RECEIPT_BYTES 2048u

typedef struct MdoHomePurgeManifest {
    char Project[65];
    xfileinfo Journal, Payload;
    MdoHomePurgeTarget* Targets;
    size_t Count;
    bool HasRequest;
    MdoHomePurgeRequest Request;
} MdoHomePurgeManifest;

static bool MdoHomePurgeRequestValid(const MdoHomePurgeRequest* Request);
static bool MdoHomePurgeJournal(cstr Base, bool* Empty);
static bool MdoHomePurgeLoad(cstr Base, MdoHomePurgeManifest* Manifest);
static bool MdoHomePurgePayload(cstr Base, const MdoHomePurgeManifest* Manifest,
    bool Retired, size_t* Nodes);

static bool MdoHomePurgeError(cstr Message)
{
    MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STORAGE, Message);
    return false;
}

static bool MdoHomePurgeId(cstr Id)
{
    size_t i;
    if ( Id == NULL || Id[0] == '\0' || Id[0] == '.' ) return false;
    for ( i = 0u; i < 65u && Id[i] != '\0'; ++i ) {
        unsigned char Ch = (unsigned char)Id[i];
        if ( !((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') ||
                (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' || Ch == '.') )
            return false;
    }
    return i < 65u;
}

/* Recovery cannot name arbitrary Home files, parent paths or overlapping
 * trees. Plan ownership is validated by the caller's exclusive inventory;
 * this layer accepts only a syntactically valid plan namespace. The two global
 * reference files require the caller's owned reference guard and conditional
 * project association check; they are not unconditional project roots. */
static bool MdoHomePurgePath(cstr Project, const MdoHomePurgeTarget* Target)
{
    static const struct { cstr Format; bool Directory; } Fixed[] = {
        { "projects/%s.json", false }, { "projects/%s.json.bak", false },
        { "data/project-drafts/%s.json", false }, { "data/project-drafts/%s.json.bak", false },
        { "memory/projects/%s.json", false }, { "memory/projects/%s.json.bak", false },
        { "sessions/%s", true }, { "migration/session-prompts/%s", true }
    };
    char Path[256], Id[65];
    const char* Start;
    size_t i, Size;
    bool History;
    if ( strcmp(Target->Path, "data/draft.json") == 0 ||
         strcmp(Target->Path, "data/workspace-state.json") == 0 )
        return Target->Info.Type == XFILE_TYPE_FILE;
    for ( i = 0u; i < sizeof(Fixed) / sizeof(Fixed[0]); ++i ) {
        snprintf(Path, sizeof(Path), Fixed[i].Format, Project);
        if ( strcmp(Target->Path, Path) == 0 ) return Target->Info.Type ==
            (Fixed[i].Directory ? XFILE_TYPE_DIRECTORY : XFILE_TYPE_FILE);
    }
    if ( Target->Info.Type != XFILE_TYPE_FILE ||
         strncmp(Target->Path, "schedules/", 10u) != 0 ) return false;
    Start = Target->Path + 10u;
    History = strncmp(Start, "history/", 8u) == 0;
    if ( History ) Start += 8u;
    Size = strlen(Start);
    if ( History && Size > 6u && strcmp(Start + Size - 6u, ".jsonl") == 0 ) Size -= 6u;
    else if ( !History && Size > 9u && strcmp(Start + Size - 9u, ".json.bak") == 0 ) Size -= 9u;
    else if ( !History && Size > 5u && strcmp(Start + Size - 5u, ".json") == 0 ) Size -= 5u;
    else return false;
    if ( Size >= sizeof(Id) ) return false;
    memcpy(Id, Start, Size); Id[Size] = '\0';
    return MdoHomePurgeId(Id);
}

static bool MdoHomePurgeSame(const xfileinfo* Actual, const xfileinfo* Expected)
{
    return (Actual->Available & XFILE_INFO_IDENTITY) != 0u &&
        Expected->Identity != 0u && Actual->Device == Expected->Device &&
        Actual->Identity == Expected->Identity && Actual->Type == Expected->Type &&
        (Actual->Type != XFILE_TYPE_FILE ||
            ((Actual->Available & XFILE_INFO_SIZE) != 0u && Actual->Size == Expected->Size));
}

static bool MdoHomePurgeTargets(const MdoHomePurgeManifest* Manifest)
{
    size_t i, j;
    bool Definition = false;
    char Required[96];
    if ( !MdoHomePurgeId(Manifest->Project) || Manifest->Targets == NULL ||
         Manifest->Count == 0u || Manifest->Count > MDO_HOME_PURGE_TARGET_LIMIT ) return false;
    snprintf(Required, sizeof(Required), "projects/%s.json", Manifest->Project);
    for ( i = 0u; i < Manifest->Count; ++i ) {
        const MdoHomePurgeTarget* Target = &Manifest->Targets[i];
        if ( memchr(Target->Path, '\0', sizeof(Target->Path)) == NULL ||
             !MdoHomePurgePath(Manifest->Project, Target) ||
             (Target->Info.Available & XFILE_INFO_IDENTITY) == 0u || Target->Info.Identity == 0u ||
             (Target->Info.Type == XFILE_TYPE_FILE &&
                (Target->Info.Available & XFILE_INFO_SIZE) == 0u) ||
             (i != 0u && strcmp(Manifest->Targets[i - 1u].Path, Target->Path) >= 0) ) return false;
        for ( j = 0u; j < i; ++j )
            if ( Target->Info.Device == Manifest->Targets[j].Info.Device &&
                 Target->Info.Identity == Manifest->Targets[j].Info.Identity ) return false;
        if ( strcmp(Target->Path, Required) == 0 ) Definition = true;
    }
    if ( Manifest->HasRequest ) {
        bool Selection = false, Draft = false;
        if ( !MdoHomePurgeRequestValid(&Manifest->Request) ||
             strcmp(Manifest->Request.ProjectId, Manifest->Project) != 0 ||
             Manifest->Request.Targets != Manifest->Count ) return false;
        for ( i = 0u; i < Manifest->Count; ++i ) {
            Selection = Selection || strcmp(Manifest->Targets[i].Path, "data/workspace-state.json") == 0;
            Draft = Draft || strcmp(Manifest->Targets[i].Path, "data/draft.json") == 0;
        }
        if ( Selection != Manifest->Request.Selection || Draft != Manifest->Request.GlobalDraft ) return false;
    }
    return Definition;
}

static bool MdoHomePurgeTree(cstr Path, unsigned Depth, size_t* Nodes, bool Remove)
{
    xfileinfo Info;
    xdir Directory;
    xdirentry Entry;
    xdirnext Next;
    bool Exists, Ok = false;
    if ( Depth > 16u || ++*Nodes > MDO_HOME_PURGE_NODE_LIMIT ||
         !MdoHomeImportStat(Path, &Exists, &Info) || !Exists ) return false;
    if ( Info.Type == XFILE_TYPE_FILE ) return !Remove || xrtRootRemove(g_MdoHome.Root, Path);
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) return false;
    Directory = xrtRootDirOpen(g_MdoHome.Root, Path, XDIR_STAT);
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Child[4096];
        size_t i;
        int Written;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || Entry.Name.Size == 0u ||
             Entry.Name.Size > 255u || !xrtUtf8Valid(Entry.Name, NULL) ||
             MdoHomeImportName(Entry.Name, ".") || MdoHomeImportName(Entry.Name, "..") ) goto done;
        for ( i = 0u; i < Entry.Name.Size; ++i ) {
            unsigned char Ch = (unsigned char)Entry.Name.Data[i];
            if ( Ch < 0x20u || Ch == 0x7fu || Ch == '/' || Ch == '\\' || Ch == ':' ) goto done;
        }
        Written = snprintf(Child, sizeof(Child), "%s/%.*s", Path,
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Child) ||
             !MdoHomePurgeTree(Child, Depth + 1u, Nodes, Remove) ) goto done;
    }
    Ok = Next == XDIR_NEXT_END;
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    if ( Ok && Remove ) Ok = xrtRootRemove(g_MdoHome.Root, Path);
    return Ok;
}

static bool MdoHomePurgeReadBounded(cstr Path, size_t Limit, char** Text, size_t* Size)
{
    xfileoptions Options;
    xfile File;
    bool Ok;
    xrtFileOptionsInit(&Options); Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    if ( File == NULL ) return false;
    Ok = MdoHomeReadFileBounded(File, Limit, Text, Size);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoHomePurgeRead(cstr Path, char** Text, size_t* Size)
{
    return MdoHomePurgeReadBounded(Path, MDO_HOME_PURGE_MANIFEST_LIMIT, Text, Size);
}

static bool MdoHomePurgeMagic(cstr Path)
{
    char* Text = NULL;
    size_t Size = 0u;
    bool Ok = MdoHomePurgeRead(Path, &Text, &Size) &&
        Size == sizeof(MDO_HOME_PURGE_MAGIC) - 1u &&
        memcmp(Text, MDO_HOME_PURGE_MAGIC, Size) == 0;
    xrtFree(Text);
    return Ok;
}

static bool MdoHomePurgeMarker(cstr Name, const void* Bytes, size_t Size)
{
    char Temporary[96], Target[96];
    char* Read = NULL;
    size_t ReadSize = 0u;
    bool Ok;
    snprintf(Temporary, sizeof(Temporary), "%s/%s.tmp", MDO_HOME_PURGE_DIR, Name);
    snprintf(Target, sizeof(Target), "%s/%s", MDO_HOME_PURGE_DIR, Name);
    if ( !MdoHomeImportWrite(Temporary, Bytes, Size) ) return false;
    if ( xrtRootRenameNoReplace(g_MdoHome.Root, Temporary, Target) ) return true;
    Ok = MdoHomePurgeRead(Target, &Read, &ReadSize) && ReadSize == Size &&
        memcmp(Read, Bytes, Size) == 0;
    xrtFree(Read);
    if ( Ok ) xrtClearError();
    return Ok;
}

static bool MdoHomePurgeTake(xvalue* Object, cstr Key, xvalue* Value)
{
    bool Ok = Value != NULL && xrtValueObjectSet(Object, xrtStrView(Key), Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoHomePurgePutIdentity(xvalue* Object, cstr Key, const xfileinfo* Info)
{
    char Identity[34];
    snprintf(Identity, sizeof(Identity), "%016llx:%016llx",
        (unsigned long long)Info->Device, (unsigned long long)Info->Identity);
    return MdoHomePurgeTake(Object, Key, xrtValueString(xrtStrView(Identity)));
}

static bool MdoHomePurgeGetIdentity(const xvalue* Object, cstr Key, xfileinfo* Info)
{
    xstrview Text;
    if ( !xrtValueGetString(xrtValueObjectGet(Object, xrtStrView(Key)), &Text) ||
         Text.Size != 33u || Text.Data[16] != ':' ||
         !MdoHomeImportHex(Text.Data, &Info->Device) ||
         !MdoHomeImportHex(Text.Data + 17u, &Info->Identity) || Info->Identity == 0u ) return false;
    Info->Available |= XFILE_INFO_IDENTITY;
    return true;
}

bool MdoHomePurgeRequestIdValid(cstr Id)
{
    size_t i;
    if ( Id == NULL ) return false;
    for ( i = 0u; i < 32u; ++i )
        if ( !((Id[i] >= '0' && Id[i] <= '9') || (Id[i] >= 'a' && Id[i] <= 'f')) ) return false;
    return Id[32] == '\0';
}

static bool MdoHomePurgeRequestValid(const MdoHomePurgeRequest* Request)
{
    return Request != NULL && memchr(Request->Id, '\0', sizeof(Request->Id)) != NULL &&
        memchr(Request->ProjectId, '\0', sizeof(Request->ProjectId)) != NULL &&
        MdoHomePurgeRequestIdValid(Request->Id) && MdoHomePurgeId(Request->ProjectId) &&
        Request->Revision != 0u && Request->Revision != UINT64_MAX &&
        Request->CreatedAt != 0u && Request->CreatedAt <= INT64_MAX &&
        Request->Targets <= MDO_HOME_PURGE_TARGET_LIMIT &&
        Request->Files <= MDO_HOME_PURGE_NODE_LIMIT && Request->Directories <= MDO_HOME_PURGE_NODE_LIMIT &&
        Request->Files + Request->Directories <= MDO_HOME_PURGE_NODE_LIMIT &&
        Request->Targets <= Request->Files + Request->Directories &&
        Request->Schedules <= Request->Targets &&
        Request->Files >= (size_t)Request->Selection + (size_t)Request->GlobalDraft &&
        (Request->Targets != 0u || (Request->Files == 0u && Request->Directories == 0u &&
            Request->Schedules == 0u && Request->Bytes == 0u &&
            !Request->Selection && !Request->GlobalDraft));
}

static bool MdoHomePurgeRequestSame(const MdoHomePurgeRequest* A, const MdoHomePurgeRequest* B)
{
    return strcmp(A->Id, B->Id) == 0 && strcmp(A->ProjectId, B->ProjectId) == 0 &&
        A->Revision == B->Revision && A->CreatedAt == B->CreatedAt &&
        A->Targets == B->Targets && A->Files == B->Files &&
        A->Directories == B->Directories && A->Schedules == B->Schedules && A->Bytes == B->Bytes &&
        A->Selection == B->Selection && A->GlobalDraft == B->GlobalDraft;
}

static xvalue* MdoHomePurgeRequestValue(const MdoHomePurgeRequest* Request)
{
    xvalue* Root = xrtValueObject();
    bool Ok = Root != NULL && MdoHomePurgeTake(Root, "version", xrtValueInt(1)) &&
        MdoHomePurgeTake(Root, "request_id", xrtValueString(xrtStrView(Request->Id))) &&
        MdoHomePurgeTake(Root, "project", xrtValueString(xrtStrView(Request->ProjectId))) &&
        MdoHomePurgeTake(Root, "revision", xrtValueUInt(Request->Revision)) &&
        MdoHomePurgeTake(Root, "created_at_us", xrtValueUInt(Request->CreatedAt)) &&
        MdoHomePurgeTake(Root, "targets", xrtValueUInt(Request->Targets)) &&
        MdoHomePurgeTake(Root, "files", xrtValueUInt(Request->Files)) &&
        MdoHomePurgeTake(Root, "directories", xrtValueUInt(Request->Directories)) &&
        MdoHomePurgeTake(Root, "schedules", xrtValueUInt(Request->Schedules)) &&
        MdoHomePurgeTake(Root, "bytes", xrtValueUInt(Request->Bytes)) &&
        MdoHomePurgeTake(Root, "selection", xrtValueBool(Request->Selection)) &&
        MdoHomePurgeTake(Root, "global_draft", xrtValueBool(Request->GlobalDraft));
    if ( !Ok ) { xrtValueRelease(Root); Root = NULL; }
    return Root;
}

static bool MdoHomePurgeRequestNumber(const xvalue* Root, cstr Key, uint64* Number)
{
    const xvalue* Value = xrtValueObjectGet(Root, xrtStrView(Key));
    int64 Signed;
    if ( xrtValueGetUInt(Value, Number) ) return true;
    if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Number = (uint64)Signed; return true;
}

static bool MdoHomePurgeRequestParse(const xvalue* Root, MdoHomePurgeRequest* Request, bool Terminal)
{
    xstrview Id, Project;
    uint64 Version, Targets, Files, Directories, Schedules;
    bool Ok = xrtValueType(Root) == XVALUE_OBJECT && xrtValueCount(Root) == (Terminal ? 13u : 12u) &&
        MdoHomePurgeRequestNumber(Root, "version", &Version) && Version == 1u &&
        xrtValueGetString(xrtValueObjectGet(Root, XRT_STR_LITERAL("request_id")), &Id) && Id.Size == 32u &&
        xrtValueGetString(xrtValueObjectGet(Root, XRT_STR_LITERAL("project")), &Project) &&
        Project.Size < sizeof(Request->ProjectId) && memchr(Project.Data, '\0', Project.Size) == NULL &&
        MdoHomePurgeRequestNumber(Root, "revision", &Request->Revision) &&
        MdoHomePurgeRequestNumber(Root, "created_at_us", &Request->CreatedAt) &&
        MdoHomePurgeRequestNumber(Root, "targets", &Targets) && Targets <= MDO_HOME_PURGE_TARGET_LIMIT &&
        MdoHomePurgeRequestNumber(Root, "files", &Files) && Files <= MDO_HOME_PURGE_NODE_LIMIT &&
        MdoHomePurgeRequestNumber(Root, "directories", &Directories) && Directories <= MDO_HOME_PURGE_NODE_LIMIT &&
        MdoHomePurgeRequestNumber(Root, "schedules", &Schedules) && Schedules <= MDO_HOME_PURGE_TARGET_LIMIT &&
        MdoHomePurgeRequestNumber(Root, "bytes", &Request->Bytes) &&
        xrtValueGetBool(xrtValueObjectGet(Root, XRT_STR_LITERAL("selection")), &Request->Selection) &&
        xrtValueGetBool(xrtValueObjectGet(Root, XRT_STR_LITERAL("global_draft")), &Request->GlobalDraft);
    if ( !Ok ) return false;
    memcpy(Request->Id, Id.Data, Id.Size); Request->Id[Id.Size] = '\0';
    memcpy(Request->ProjectId, Project.Data, Project.Size); Request->ProjectId[Project.Size] = '\0';
    Request->Targets = (size_t)Targets; Request->Files = (size_t)Files;
    Request->Directories = (size_t)Directories; Request->Schedules = (size_t)Schedules;
    return MdoHomePurgeRequestValid(Request);
}

static bool MdoHomePurgeRecordRead(cstr Path, bool Terminal, MdoHomePurgeReceipt* Receipt, bool* Exists)
{
    char* Text = NULL;
    size_t Size = 0u;
    xfileinfo Before, After;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    xstrview Outcome;
    bool StillExists, Ok;
    memset(Receipt, 0, sizeof(*Receipt)); *Exists = false;
    if ( !MdoHomeImportStat(Path, Exists, &Before) ) return false;
    if ( !*Exists ) return true;
    Ok = Before.Type == XFILE_TYPE_FILE && (Before.Available & XFILE_INFO_SIZE) != 0u &&
        Before.Size <= MDO_HOME_PURGE_RECEIPT_BYTES &&
        MdoHomePurgeReadBounded(Path, MDO_HOME_PURGE_RECEIPT_BYTES, &Text, &Size) &&
        MdoHomeImportStat(Path, &StillExists, &After) && StillExists && MdoHomePurgeSame(&After, &Before);
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = MDO_HOME_PURGE_RECEIPT_BYTES;
    Config.MaxDepth = 2u; Config.MaxValues = 40u; Config.MaxContainerItems = 13u;
    if ( Ok ) Root = xrtJsonRead(xrtStrViewN(Text, Size), &Config);
    Ok = Ok && Root != NULL && MdoHomePurgeRequestParse(Root, &Receipt->Request, Terminal);
    if ( Ok && Terminal ) {
        Ok = xrtValueGetString(xrtValueObjectGet(Root, XRT_STR_LITERAL("outcome")), &Outcome);
        if ( Ok && MdoHomeImportName(Outcome, "committed") ) Receipt->Outcome = MDO_HOME_PURGE_COMMITTED;
        else if ( Ok && MdoHomeImportName(Outcome, "aborted") ) Receipt->Outcome = MDO_HOME_PURGE_ABORTED;
        else Ok = false;
        if ( Ok ) {
            Receipt->Committed = Receipt->Outcome == MDO_HOME_PURGE_COMMITTED;
            /* A cancellation has no ready manifest and cannot commit. */
            Ok = !Receipt->Committed || Receipt->Request.Targets != 0u;
        }
    }
    xrtValueRelease(Root); xrtFree(Text);
    return Ok ? true : MdoHomePurgeError("invalid or changed project purge request/result record");
}

static void MdoHomePurgeReceiptPath(char Path[96], cstr Id)
{
    snprintf(Path, 96u, "%s/%s.json", MDO_HOME_PURGE_RECEIPT_DIR, Id);
}

static bool MdoHomePurgeReceiptGetLocked(cstr Id, MdoHomePurgeReceipt* Receipt, bool* Found)
{
    char Path[96];
    size_t i;
    bool Exists;
    MdoHomePurgeReceipt Pending;
    memset(Receipt, 0, sizeof(*Receipt)); *Found = false;
    if ( g_MdoHome.Root == NULL ) return true;
    MdoHomePurgeReceiptPath(Path, Id);
    if ( !MdoHomePurgeRecordRead(Path, true, Receipt, Found) ) return false;
    if ( *Found && strcmp(Receipt->Request.Id, Id) != 0 )
        return MdoHomePurgeError("project purge result filename does not match its request ID");
    for ( i = 0u; i < 2u; ++i ) {
        cstr Base = i == 0u ? MDO_HOME_PURGE_DIR : MDO_HOME_PURGE_GC;
        bool Empty;
        snprintf(Path, sizeof(Path), "%s/request", Base);
        if ( !MdoHomePurgeRecordRead(Path, false, &Pending, &Exists) ) return false;
        if ( Exists && strcmp(Pending.Request.Id, Id) == 0 ) {
            xfileinfo Info;
            bool CommitMarker;
            if ( !MdoHomePurgeJournal(Base, &Empty) || Empty ) return false;
            if ( *Found && !MdoHomePurgeRequestSame(&Receipt->Request, &Pending.Request) )
                return MdoHomePurgeError("terminal and pending project purge records disagree");
            snprintf(Path, sizeof(Path), "%s/committed", Base);
            if ( !MdoHomeImportStat(Path, &CommitMarker, &Info) ||
                 (CommitMarker && (Info.Type != XFILE_TYPE_FILE || !MdoHomePurgeMagic(Path))) ) return false;
            if ( *Found && ((CommitMarker && Receipt->Outcome != MDO_HOME_PURGE_COMMITTED) ||
                 (i == 0u && !CommitMarker && Receipt->Outcome == MDO_HOME_PURGE_COMMITTED)) )
                return MdoHomePurgeError("project purge result contradicts its commit marker");
            if ( *Found && Receipt->Outcome == MDO_HOME_PURGE_ABORTED ) {
                size_t Nodes = 0u;
                if ( !MdoHomePurgePayload(Base, NULL, true, &Nodes) )
                    return MdoHomePurgeError("project purge abort result still has moved payload");
            }
            if ( CommitMarker ) {
                MdoHomePurgeManifest Manifest;
                bool Ok;
                memset(&Manifest, 0, sizeof(Manifest));
                Ok = MdoHomePurgeLoad(Base, &Manifest) && Manifest.HasRequest &&
                    MdoHomePurgeRequestSame(&Manifest.Request, &Pending.Request);
                xrtFree(Manifest.Targets);
                if ( !Ok ) return MdoHomePurgeError("pending purge commit has no matching ready manifest");
                Pending.Committed = true;
            }
            if ( !*Found ) { *Receipt = Pending; *Found = true; }
        }
    }
    return true;
}

bool MdoHomePurgeReceiptGet(cstr Id, MdoHomePurgeReceipt* Receipt, bool* Found)
{
    bool Ok;
    if ( Receipt != NULL ) memset(Receipt, 0, sizeof(*Receipt));
    if ( Found != NULL ) *Found = false;
    if ( !g_MdoHome.Initialized || Receipt == NULL || Found == NULL || !MdoHomePurgeRequestIdValid(Id) )
        return MdoHomePurgeError("invalid project purge receipt query");
    xrtMutexLock(g_MdoHome.Lock);
    Ok = MdoHomePurgeReceiptGetLocked(Id, Receipt, Found);
    xrtMutexUnlock(g_MdoHome.Lock);
    if ( !Ok ) { memset(Receipt, 0, sizeof(*Receipt)); *Found = false; }
    return Ok;
}

/* Reserve capacity before accepting a request. Only generated immutable JSON
 * names are admitted; no automatic expiry can turn an old ID into a new purge. */
static bool MdoHomePurgeReceiptCapacity(void)
{
    xfileinfo Before, After;
    xdir Directory;
    xdirentry Entry;
    xdirnext Next;
    bool Exists, Ok = false;
    size_t Count = 0u;
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_RECEIPT_DIR, &Exists, &Before) ) return false;
    if ( !Exists ) return true;
    if ( Before.Type != XFILE_TYPE_DIRECTORY ) return false;
    Directory = xrtRootDirOpen(g_MdoHome.Root, MDO_HOME_PURGE_RECEIPT_DIR, XDIR_STAT);
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Id[33];
        if ( ++Count >= MDO_HOME_PURGE_RECEIPT_LIMIT || Entry.Name.Size != 37u ||
             (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || Entry.Info.Type != XFILE_TYPE_FILE ||
             memcmp(Entry.Name.Data + 32u, ".json", 5u) != 0 ) goto done;
        memcpy(Id, Entry.Name.Data, 32u); Id[32] = '\0';
        if ( !MdoHomePurgeRequestIdValid(Id) ) goto done;
    }
    Ok = Next == XDIR_NEXT_END && MdoHomeImportStat(MDO_HOME_PURGE_RECEIPT_DIR, &Exists, &After) &&
        Exists && MdoHomePurgeSame(&After, &Before);
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok ? true : MdoHomePurgeError("project purge result directory is invalid or at capacity");
}

static bool MdoHomePurgeRequestSave(const MdoHomePurgeRequest* Request)
{
    xvalue* Root = MdoHomePurgeRequestValue(Request);
    char* Text;
    size_t Size = 0u;
    bool Ok;
    Text = Root != NULL ? xrtJsonStringify(Root, false, &Size) : NULL;
    Ok = Text != NULL && Size <= MDO_HOME_PURGE_RECEIPT_BYTES && MdoHomePurgeMarker("request", Text, Size);
    xrtFree(Text); xrtValueRelease(Root);
    return Ok;
}

/* A terminal receipt is published only after complete rollback, or after the
 * commit marker plus source-position validation. A retired v2 journal may not
 * delete payload unless its immutable terminal receipt is still verified. */
static bool MdoHomePurgeReceiptPublish(const MdoHomePurgeRequest* Request, MdoHomePurgeOutcome Outcome)
{
    char Path[96], *Text = NULL, *Partial = NULL;
    size_t Size = 0u, PartialSize = 0u;
    MdoHomePurgeReceipt Existing;
    xfileinfo Info;
    xvalue* Root;
    bool Exists, Ok = false;
    if ( !MdoHomePurgeRequestValid(Request) ||
         (Outcome != MDO_HOME_PURGE_COMMITTED && Outcome != MDO_HOME_PURGE_ABORTED) ||
         (Outcome == MDO_HOME_PURGE_COMMITTED && Request->Targets == 0u) )
        return MdoHomePurgeError("invalid project purge terminal publication");
    MdoHomePurgeReceiptPath(Path, Request->Id);
    if ( !MdoHomePurgeRecordRead(Path, true, &Existing, &Exists) ) return false;
    if ( Exists ) return MdoHomePurgeRequestSame(&Existing.Request, Request) && Existing.Outcome == Outcome ?
        true : MdoHomePurgeError("conflicting immutable project purge result");
    if ( !MdoHomeEnsureParents(g_MdoHome.Root, Path) ) return false;
    Root = MdoHomePurgeRequestValue(Request);
    if ( Root == NULL || !MdoHomePurgeTake(Root, "outcome", xrtValueString(xrtStrView(
            Outcome == MDO_HOME_PURGE_COMMITTED ? "committed" : "aborted"))) ) goto done;
    Text = xrtJsonStringify(Root, false, &Size);
    if ( Text == NULL || Size > MDO_HOME_PURGE_RECEIPT_BYTES ) goto done;
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_DIR "/result.tmp", &Exists, &Info) ) goto done;
    if ( Exists ) {
        if ( Info.Type != XFILE_TYPE_FILE || Info.Size > MDO_HOME_PURGE_RECEIPT_BYTES ||
             !MdoHomePurgeReadBounded(MDO_HOME_PURGE_DIR "/result.tmp",
                MDO_HOME_PURGE_RECEIPT_BYTES, &Partial, &PartialSize) ) goto done;
        /* Only a prefix of our canonical write can be an interrupted scratch
         * file. Foreign or changed bytes are evidence, not disposable data. */
        if ( PartialSize > Size || memcmp(Partial, Text, PartialSize) != 0 ) goto done;
        if ( PartialSize != Size && !xrtRootRemove(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/result.tmp") ) goto done;
    }
    if ( (!Exists || PartialSize != Size) &&
         !MdoHomeImportWrite(MDO_HOME_PURGE_DIR "/result.tmp", Text, Size) ) goto done;
    if ( xrtRootRenameNoReplace(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/result.tmp", Path) ) Ok = true;
    else if ( MdoHomePurgeRecordRead(Path, true, &Existing, &Exists) && Exists &&
              MdoHomePurgeRequestSame(&Existing.Request, Request) && Existing.Outcome == Outcome ) {
        xrtClearError(); Ok = true;
    }
done:
    xrtFree(Text); xrtFree(Partial); xrtValueRelease(Root);
    return Ok;
}

static bool MdoHomePurgeSave(const MdoHomePurgeManifest* Manifest)
{
    xvalue* Root = xrtValueObject();
    xvalue* Array = xrtValueArray();
    char* Text = NULL;
    size_t Size = 0u, i;
    bool Ok = Root != NULL && Array != NULL &&
        MdoHomePurgeTake(Root, "version", xrtValueInt(Manifest->HasRequest ? 2 : 1)) &&
        MdoHomePurgeTake(Root, "project", xrtValueString(xrtStrView(Manifest->Project))) &&
        MdoHomePurgePutIdentity(Root, "journal", &Manifest->Journal) &&
        MdoHomePurgePutIdentity(Root, "payload", &Manifest->Payload);
    if ( Ok && Manifest->HasRequest ) Ok = MdoHomePurgeTake(Root, "request",
        MdoHomePurgeRequestValue(&Manifest->Request));
    for ( i = 0u; Ok && i < Manifest->Count; ++i ) {
        const MdoHomePurgeTarget* Target = &Manifest->Targets[i];
        char Bytes[17];
        xvalue* Item = xrtValueObject();
        snprintf(Bytes, sizeof(Bytes), "%016llx", (unsigned long long)
            (Target->Info.Type == XFILE_TYPE_FILE ? Target->Info.Size : 0u));
        Ok = Item != NULL && MdoHomePurgeTake(Item, "path",
            xrtValueString(xrtStrView(Target->Path))) &&
            MdoHomePurgeTake(Item, "type", xrtValueString(xrtStrView(
                Target->Info.Type == XFILE_TYPE_FILE ? "file" : "directory"))) &&
            MdoHomePurgePutIdentity(Item, "identity", &Target->Info) &&
            MdoHomePurgeTake(Item, "bytes", xrtValueString(xrtStrView(Bytes))) &&
            xrtValueArrayAppend(Array, Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = xrtValueObjectSet(Root, XRT_STR_LITERAL("targets"), Array);
    if ( Ok ) Text = xrtJsonStringify(Root, false, &Size);
    Ok = Text != NULL && Size <= MDO_HOME_PURGE_MANIFEST_LIMIT &&
        MdoHomePurgeMarker("ready", Text, Size);
    xrtFree(Text); xrtValueRelease(Array); xrtValueRelease(Root);
    return Ok;
}

static bool MdoHomePurgeLoad(cstr Base, MdoHomePurgeManifest* Manifest)
{
    char Path[96], *Text = NULL;
    size_t Size = 0u, i;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* Array;
    xstrview Project;
    int64 Version = 0;
    bool Ok;
    snprintf(Path, sizeof(Path), "%s/ready", Base);
    Ok = MdoHomePurgeRead(Path, &Text, &Size);
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_HOME_PURGE_MANIFEST_LIMIT; Config.MaxDepth = 4u;
    Config.MaxValues = 12u * MDO_HOME_PURGE_TARGET_LIMIT + 32u;
    Config.MaxContainerItems = MDO_HOME_PURGE_TARGET_LIMIT;
    if ( Ok ) Root = xrtJsonRead(xrtStrViewN(Text, Size), &Config);
    Ok = Root != NULL && xrtValueType(Root) == XVALUE_OBJECT &&
        xrtValueGetInt(xrtValueObjectGet(Root, XRT_STR_LITERAL("version")), &Version) &&
        (Version == 1 || Version == 2) && xrtValueCount(Root) == (Version == 1 ? 5u : 6u) &&
        xrtValueGetString(xrtValueObjectGet(Root,
            XRT_STR_LITERAL("project")), &Project) && Project.Size < sizeof(Manifest->Project) &&
        memchr(Project.Data, '\0', Project.Size) == NULL &&
        MdoHomePurgeGetIdentity(Root, "journal", &Manifest->Journal) &&
        MdoHomePurgeGetIdentity(Root, "payload", &Manifest->Payload);
    if ( Ok ) { memcpy(Manifest->Project, Project.Data, Project.Size); Manifest->Project[Project.Size] = '\0'; }
    Manifest->HasRequest = Version == 2;
    if ( Ok && Manifest->HasRequest ) Ok = MdoHomePurgeRequestParse(
        xrtValueObjectGet(Root, XRT_STR_LITERAL("request")), &Manifest->Request, false);
    Array = Root != NULL ? xrtValueObjectGet(Root, XRT_STR_LITERAL("targets")) : NULL;
    Ok = Ok && Array != NULL && xrtValueType(Array) == XVALUE_ARRAY &&
        xrtValueCount(Array) != 0u && xrtValueCount(Array) <= MDO_HOME_PURGE_TARGET_LIMIT;
    if ( Ok ) {
        Manifest->Count = xrtValueCount(Array);
        Manifest->Targets = (MdoHomePurgeTarget*)xrtCalloc(Manifest->Count, sizeof(*Manifest->Targets));
        Ok = Manifest->Targets != NULL;
    }
    Manifest->Journal.Type = Manifest->Payload.Type = XFILE_TYPE_DIRECTORY;
    for ( i = 0u; Ok && i < Manifest->Count; ++i ) {
        const xvalue* Item = xrtValueArrayGet(Array, i);
        MdoHomePurgeTarget* Target = &Manifest->Targets[i];
        xstrview PathText, Type, Bytes;
        Ok = xrtValueType(Item) == XVALUE_OBJECT && xrtValueCount(Item) == 4u &&
            xrtValueGetString(xrtValueObjectGet(Item, XRT_STR_LITERAL("path")), &PathText) &&
            PathText.Size < sizeof(Target->Path) && memchr(PathText.Data, '\0', PathText.Size) == NULL &&
            xrtValueGetString(xrtValueObjectGet(Item, XRT_STR_LITERAL("type")), &Type) &&
            MdoHomePurgeGetIdentity(Item, "identity", &Target->Info) &&
            xrtValueGetString(xrtValueObjectGet(Item, XRT_STR_LITERAL("bytes")), &Bytes) &&
            Bytes.Size == 16u && MdoHomeImportHex(Bytes.Data, &Target->Info.Size);
        if ( !Ok ) break;
        memcpy(Target->Path, PathText.Data, PathText.Size); Target->Path[PathText.Size] = '\0';
        Target->Info.Available |= XFILE_INFO_SIZE;
        if ( MdoHomeImportName(Type, "file") ) Target->Info.Type = XFILE_TYPE_FILE;
        else if ( MdoHomeImportName(Type, "directory") && Target->Info.Size == 0u )
            Target->Info.Type = XFILE_TYPE_DIRECTORY;
        else Ok = false;
    }
    Ok = Ok && MdoHomePurgeTargets(Manifest);
    xrtValueRelease(Root); xrtFree(Text);
    return Ok;
}

static void MdoHomePurgeSlot(char Path[96], cstr Base, size_t Index)
{
    snprintf(Path, 96u, "%s/payload/%04u", Base, (unsigned)Index);
}

static bool MdoHomePurgeJournal(cstr Base, bool* Empty)
{
    static const char* const Names[] = { "owner", "ready", "ready.tmp",
        "committed", "committed.tmp", "payload", "request", "request.tmp", "result.tmp" };
    xdir Directory = xrtRootDirOpen(g_MdoHome.Root, Base, XDIR_STAT);
    xdirentry Entry;
    xdirnext Next;
    bool Ok = false, Owner = false;
    *Empty = true;
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        size_t i;
        *Empty = false;
        for ( i = 0u; i < sizeof(Names) / sizeof(Names[0]); ++i )
            if ( MdoHomeImportName(Entry.Name, Names[i]) ) break;
        if ( i == sizeof(Names) / sizeof(Names[0]) || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Info.Type != (i == 5u ? XFILE_TYPE_DIRECTORY : XFILE_TYPE_FILE) ) goto done;
        if ( i == 0u ) Owner = true;
    }
    Ok = Next == XDIR_NEXT_END;
    if ( Ok && !*Empty ) {
        char Path[96];
        snprintf(Path, sizeof(Path), "%s/owner", Base);
        Ok = Owner && MdoHomePurgeMagic(Path);
    }
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok;
}

/* Every payload name is a generated slot from the immutable manifest. This
 * rejects extra files before rollback or any committed cleanup deletion. */
static bool MdoHomePurgePayload(cstr Base, const MdoHomePurgeManifest* Manifest,
    bool Retired, size_t* Nodes)
{
    char Path[96];
    xfileinfo Info;
    xdir Directory;
    xdirentry Entry;
    xdirnext Next;
    bool Exists, Ok = false;
    snprintf(Path, sizeof(Path), "%s/payload", Base);
    if ( !MdoHomeImportStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return Retired;
    if ( Info.Type != XFILE_TYPE_DIRECTORY || (Manifest != NULL &&
            !MdoHomePurgeSame(&Info, &Manifest->Payload)) ) return false;
    Directory = xrtRootDirOpen(g_MdoHome.Root, Path, XDIR_STAT);
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Slot[96], Name[5];
        size_t Index, i;
        if ( Manifest == NULL || Entry.Name.Size != 4u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) goto done;
        Index = 0u;
        for ( i = 0u; i < 4u; ++i ) {
            unsigned char Ch = (unsigned char)Entry.Name.Data[i];
            if ( Ch < '0' || Ch > '9' ) goto done;
            Index = Index * 10u + Ch - '0';
        }
        snprintf(Name, sizeof(Name), "%04u", (unsigned)Index);
        if ( Index >= Manifest->Count || !MdoHomeImportName(Entry.Name, Name) ) goto done;
        MdoHomePurgeSlot(Slot, Base, Index);
        if ( !MdoHomeImportStat(Slot, &Exists, &Info) || !Exists ||
             !MdoHomePurgeSame(&Info, &Manifest->Targets[Index].Info) ||
             !MdoHomePurgeTree(Slot, 0u, Nodes, false) ) goto done;
    }
    Ok = Next == XDIR_NEXT_END;
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok;
}

static bool MdoHomePurgeMove(cstr Source, cstr Target, const xfileinfo* Expected)
{
    xfileinfo Info;
    bool Exists, SourceExists;
    if ( !MdoHomeImportStat(Source, &Exists, &Info) || !Exists ||
         !MdoHomePurgeSame(&Info, Expected) ||
         !MdoHomeImportStat(Target, &Exists, &Info) || Exists ) return false;
    (void)xrtRootRenameNoReplace(g_MdoHome.Root, Source, Target);
    if ( !MdoHomeImportStat(Source, &SourceExists, &Info) || SourceExists ||
         !MdoHomeImportStat(Target, &Exists, &Info) || !Exists ||
         !MdoHomePurgeSame(&Info, Expected) ) return false;
    xrtClearError();
    return true;
}

static bool MdoHomePurgeJournalRequest(cstr Base, const MdoHomePurgeManifest* Manifest,
    MdoHomePurgeRequest* Request, bool* Found)
{
    char Path[96];
    MdoHomePurgeReceipt Record;
    bool Exists;
    xfileinfo Info;
    snprintf(Path, sizeof(Path), "%s/request", Base);
    if ( !MdoHomePurgeRecordRead(Path, false, &Record, Found) ) return false;
    if ( Manifest != NULL && (Manifest->HasRequest != *Found || (*Found &&
         !MdoHomePurgeRequestSame(&Manifest->Request, &Record.Request))) )
        return MdoHomePurgeError("purge manifest and accepted request disagree");
    if ( *Found ) *Request = Record.Request;
    else {
        snprintf(Path, sizeof(Path), "%s/result.tmp", Base);
        if ( !MdoHomeImportStat(Path, &Exists, &Info) || Exists )
            return MdoHomePurgeError("purge result scratch has no accepted request");
    }
    return true;
}

/* Validate existing terminal evidence before any compensating move. An abort
 * result/scratch can only exist after all slots have been restored. */
static bool MdoHomePurgeRecoveryResult(const MdoHomePurgeRequest* Request, bool Committed, size_t* Nodes)
{
    char Path[96];
    MdoHomePurgeReceipt Receipt;
    xfileinfo Info;
    bool Exists, Scratch;
    MdoHomePurgeReceiptPath(Path, Request->Id);
    if ( !MdoHomePurgeRecordRead(Path, true, &Receipt, &Exists) ||
         (Exists && (!MdoHomePurgeRequestSame(&Receipt.Request, Request) ||
            Receipt.Outcome != (Committed ? MDO_HOME_PURGE_COMMITTED : MDO_HOME_PURGE_ABORTED))) ||
         !MdoHomeImportStat(MDO_HOME_PURGE_DIR "/result.tmp", &Scratch, &Info) ||
         (!Committed && (Exists || Scratch) && !MdoHomePurgePayload(MDO_HOME_PURGE_DIR, NULL, true, Nodes)) )
        return MdoHomePurgeError("project purge terminal evidence contradicts recovery");
    return true;
}

static bool MdoHomePurgeGc(void)
{
    static const char* const Files[] = { "ready.tmp", "committed.tmp", "committed", "ready", "owner" };
    MdoHomePurgeManifest Manifest;
    MdoHomePurgeRequest Request;
    MdoHomePurgeReceipt Receipt;
    xfileinfo Info;
    bool Exists, Empty, Ready, HasRequest, Ok = false;
    size_t Nodes = 0u, i;
    memset(&Manifest, 0, sizeof(Manifest));
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_GC, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ||
         !MdoHomePurgeJournal(MDO_HOME_PURGE_GC, &Empty) ) goto done;
    if ( Empty ) { Ok = xrtRootRemove(g_MdoHome.Root, MDO_HOME_PURGE_GC); goto done; }
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_GC "/ready", &Ready, &Info) ) goto done;
    if ( Ready && (!MdoHomePurgeLoad(MDO_HOME_PURGE_GC, &Manifest) ||
            !MdoHomeImportStat(MDO_HOME_PURGE_GC, &Exists, &Info) ||
            !MdoHomePurgeSame(&Info, &Manifest.Journal)) ) goto done;
    if ( !MdoHomePurgePayload(MDO_HOME_PURGE_GC, Ready ? &Manifest : NULL, true, &Nodes) ) goto done;
    if ( !MdoHomePurgeJournalRequest(MDO_HOME_PURGE_GC, Ready ? &Manifest : NULL, &Request, &HasRequest) ) goto done;
    if ( HasRequest ) {
        char Path[96];
        bool Committed;
        MdoHomePurgeReceiptPath(Path, Request.Id);
        if ( !MdoHomePurgeRecordRead(Path, true, &Receipt, &Exists) || !Exists ||
             !MdoHomePurgeRequestSame(&Receipt.Request, &Request) ) goto done;
        if ( !MdoHomeImportStat(MDO_HOME_PURGE_GC "/committed", &Committed, &Info) ||
             (Committed && (Receipt.Outcome != MDO_HOME_PURGE_COMMITTED ||
                !MdoHomePurgeMagic(MDO_HOME_PURGE_GC "/committed"))) ||
             (Receipt.Outcome == MDO_HOME_PURGE_ABORTED &&
                !MdoHomePurgePayload(MDO_HOME_PURGE_GC, NULL, true, &Nodes)) ) goto done;
    }
    Nodes = 0u;
    for ( i = 0u; Ready && i < Manifest.Count; ++i ) {
        char Slot[96];
        MdoHomePurgeSlot(Slot, MDO_HOME_PURGE_GC, i);
        if ( !MdoHomeImportStat(Slot, &Exists, &Info) || (Exists &&
                (!MdoHomePurgeSame(&Info, &Manifest.Targets[i].Info) ||
                 !MdoHomePurgeTree(Slot, 0u, &Nodes, true))) ) goto done;
    }
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_GC "/payload", &Exists, &Info) ||
         (Exists && !xrtRootRemove(g_MdoHome.Root, MDO_HOME_PURGE_GC "/payload")) ) goto done;
    for ( i = 0u; i < 2u; ++i ) {
        cstr Path = i == 0u ? MDO_HOME_PURGE_GC "/request.tmp" : MDO_HOME_PURGE_GC "/result.tmp";
        if ( !MdoHomeImportStat(Path, &Exists, &Info) || (Exists && !xrtRootRemove(g_MdoHome.Root, Path)) ) goto done;
    }
    for ( i = 0u; i < 5u; ++i ) {
        char Path[96];
        /* Keep the accepted request through removal of ready. After it is
         * removed, only the already-validated ownership tail can remain. */
        if ( i == 4u && HasRequest && !xrtRootRemove(g_MdoHome.Root, MDO_HOME_PURGE_GC "/request") ) goto done;
        snprintf(Path, sizeof(Path), "%s/%s", MDO_HOME_PURGE_GC, Files[i]);
        if ( !MdoHomeImportStat(Path, &Exists, &Info) ||
             (Exists && !xrtRootRemove(g_MdoHome.Root, Path)) ) goto done;
    }
    Ok = xrtRootRemove(g_MdoHome.Root, MDO_HOME_PURGE_GC);
done:
    xrtFree(Manifest.Targets);
    return Ok;
}

static bool MdoHomePurgeRecoverLocked(void)
{
    MdoHomePurgeManifest Manifest;
    MdoHomePurgeRequest Request;
    xfileinfo Info;
    bool Exists, Gc, Empty, Ready, Committed, Other, HasRequest, Ok = false;
    size_t i, Nodes = 0u;
    memset(&Manifest, 0, sizeof(Manifest));
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_DIR, &Exists, &Info) ||
         !MdoHomeImportStat(MDO_HOME_PURGE_GC, &Gc, &Info) ) goto done;
    if ( Exists || Gc ) {
        if ( !MdoHomeImportStat(MDO_HOME_IMPORT_DIR, &Other, &Info) || Other ||
             !MdoHomeImportStat(MDO_HOME_IMPORT_GC, &Other, &Info) || Other || (Exists && Gc) ) goto done;
    }
    if ( !MdoHomePurgeGc() ) goto done;
    if ( !Exists ) { Ok = true; goto done; }
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_DIR, &Exists, &Info) ||
         Info.Type != XFILE_TYPE_DIRECTORY || !MdoHomePurgeJournal(MDO_HOME_PURGE_DIR, &Empty) ) goto done;
    Manifest.Journal = Info;
    if ( Empty ) { Ok = xrtRootRemove(g_MdoHome.Root, MDO_HOME_PURGE_DIR); goto done; }
    if ( !MdoHomeImportStat(MDO_HOME_PURGE_DIR "/ready", &Ready, &Info) ||
         !MdoHomeImportStat(MDO_HOME_PURGE_DIR "/committed", &Committed, &Info) ) goto done;
    if ( Committed && (!Ready || !MdoHomePurgeMagic(MDO_HOME_PURGE_DIR "/committed")) ) goto done;
    if ( Ready ) {
        if ( !MdoHomePurgeLoad(MDO_HOME_PURGE_DIR, &Manifest) ||
             !MdoHomeImportStat(MDO_HOME_PURGE_DIR, &Exists, &Info) ||
             !MdoHomePurgeSame(&Info, &Manifest.Journal) ||
             !MdoHomePurgePayload(MDO_HOME_PURGE_DIR, &Manifest, false, &Nodes) ) goto done;
        if ( !MdoHomePurgeJournalRequest(MDO_HOME_PURGE_DIR, &Manifest, &Request, &HasRequest) ) goto done;
        if ( HasRequest && !MdoHomePurgeRecoveryResult(&Request, Committed, &Nodes) ) goto done;
        /* Validate every position before the first compensating move. Both
         * and neither are ambiguous; even an empty replacement is refused. */
        for ( i = 0u; i < Manifest.Count; ++i ) {
            char Slot[96];
            bool Source, Target;
            MdoHomePurgeSlot(Slot, MDO_HOME_PURGE_DIR, i);
            if ( !MdoHomeImportStat(Manifest.Targets[i].Path, &Source, &Info) ||
                 (Source && (!MdoHomePurgeSame(&Info, &Manifest.Targets[i].Info) ||
                    !MdoHomePurgeTree(Manifest.Targets[i].Path, 0u, &Nodes, false))) ||
                 !MdoHomeImportStat(Slot, &Target, &Info) || Source == Target ||
                 (Committed && Source) ) goto done;
        }
        if ( !Committed ) for ( i = Manifest.Count; i-- > 0u; ) {
            char Slot[96];
            MdoHomePurgeSlot(Slot, MDO_HOME_PURGE_DIR, i);
            if ( !MdoHomeImportStat(Slot, &Exists, &Info) || (Exists &&
                    !MdoHomePurgeMove(Slot, Manifest.Targets[i].Path, &Manifest.Targets[i].Info)) ) goto done;
        }
    } else if ( !MdoHomePurgePayload(MDO_HOME_PURGE_DIR, NULL, true, &Nodes) ) goto done;
    if ( !Ready && !MdoHomePurgeJournalRequest(MDO_HOME_PURGE_DIR, NULL, &Request, &HasRequest) ) goto done;
    if ( !Ready && HasRequest && !MdoHomePurgeRecoveryResult(&Request, false, &Nodes) ) goto done;
    if ( HasRequest && !MdoHomePurgeReceiptPublish(&Request,
            Committed ? MDO_HOME_PURGE_COMMITTED : MDO_HOME_PURGE_ABORTED) ) goto done;
    if ( !MdoHomePurgeMove(MDO_HOME_PURGE_DIR, MDO_HOME_PURGE_GC, &Manifest.Journal) ) goto done;
    Ok = MdoHomePurgeGc();
done:
    xrtFree(Manifest.Targets);
    if ( !Ok ) (void)MdoHomePurgeError("project purge recovery is ambiguous or incomplete; preserve its journal");
    return Ok;
}

/* Both execution and cancellation must see an unused journal slot while
 * holding Home's lock. Never start a second transaction over pending evidence. */
static bool MdoHomePurgeAvailableLocked(void)
{
    size_t i;
    bool Exists;
    xfileinfo Info;
    if ( !MdoHomeWritableLocked() ) return false;
    if ( g_MdoHome.Restore != NULL ) return MdoHomePurgeError("session restore is in progress");
    if ( g_MdoHome.Root == NULL || g_MdoHome.LeaseFile == NULL )
        return MdoHomePurgeError("project purge requires an already-existing leased Home");
    for ( i = 0u; i < 2u; ++i ) {
        cstr Journal = i == 0u ? MDO_HOME_PURGE_DIR : MDO_HOME_PURGE_GC;
        if ( !MdoHomeImportStat(Journal, &Exists, &Info) ) return false;
        if ( Exists ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "pending project purge journal requires restart");
            return false;
        }
    }
    return true;
}

bool MdoHomePurgeRequestCancel(cstr Id, cstr Project, uint64 Revision, int64 CreatedAt,
    MdoHomePurgeReceipt* Receipt, bool* Replayed)
{
    MdoHomePurgeRequest Request;
    bool Found, Created = false, Ok = false;
    if ( Receipt != NULL ) memset(Receipt, 0, sizeof(*Receipt));
    if ( Replayed != NULL ) *Replayed = false;
    if ( !g_MdoHome.Initialized || Receipt == NULL || Replayed == NULL ||
         !MdoHomePurgeRequestIdValid(Id) || !MdoHomePurgeId(Project) || CreatedAt <= 0 )
        return MdoHomePurgeError("invalid project purge cancellation");
    memset(&Request, 0, sizeof(Request));
    snprintf(Request.Id, sizeof(Request.Id), "%s", Id);
    snprintf(Request.ProjectId, sizeof(Request.ProjectId), "%s", Project);
    Request.Revision = Revision; Request.CreatedAt = (uint64)CreatedAt;
    if ( !MdoHomePurgeRequestValid(&Request) )
        return MdoHomePurgeError("invalid project purge cancellation binding");
    xrtMutexLock(g_MdoHome.Lock);
    if ( !MdoHomePurgeReceiptGetLocked(Id, Receipt, &Found) ) goto done;
    if ( Found ) {
        if ( strcmp(Receipt->Request.ProjectId, Project) != 0 ||
             Receipt->Request.Revision != Revision || Receipt->Request.CreatedAt != (uint64)CreatedAt ) {
            MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
                "project purge request ID belongs to another project version");
            goto done;
        }
        if ( Receipt->Outcome == MDO_HOME_PURGE_PENDING ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "accepted project purge requires recovery before cancellation");
            (void)MdoHomePurgeError(g_MdoHome.Message);
            goto done;
        }
        *Replayed = true; Ok = true; xrtClearError(); goto done;
    }
    if ( !MdoHomePurgeAvailableLocked() || !MdoHomePurgeReceiptCapacity() ) goto done;
    if ( !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR, 0700u) ) goto done;
    Created = true;
    /* No ready marker or payload: the same empty preparation recovery path
     * publishes ABORTED, even if this process exits just after acceptance. */
    if ( !MdoHomeImportWrite(MDO_HOME_PURGE_DIR "/owner", MDO_HOME_PURGE_MAGIC,
            sizeof(MDO_HOME_PURGE_MAGIC) - 1u) || !MdoHomePurgeRequestSave(&Request) ) goto done;
    Ok = MdoHomePurgeRecoverLocked();
done:
    if ( !Ok && Created ) {
        xerror* Saved = xrtTakeError();
        if ( !MdoHomePurgeRecoverLocked() ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "project purge cancellation recovery requires restart; preserve its journal");
        }
        xrtClearError();
        if ( Saved != NULL ) xrtSetErrorTake(Saved);
        else (void)MdoHomePurgeError("project purge cancellation publication failed");
    }
    /* Publication/close errors can still leave a completed terminal record.
     * Verify acceptance and cleanup rather than claiming cancellation failed. */
    if ( Created && !g_MdoHome.RestartRequired ) {
        Ok = MdoHomePurgeReceiptGetLocked(Id, Receipt, &Found) && Found &&
            MdoHomePurgeRequestSame(&Request, &Receipt->Request) && Receipt->Outcome == MDO_HOME_PURGE_ABORTED;
        if ( Ok ) xrtClearError();
    }
    if ( !Ok ) { memset(Receipt, 0, sizeof(*Receipt)); *Replayed = false; }
    xrtMutexUnlock(g_MdoHome.Lock);
    return Ok;
}

static bool MdoHomePurgeFilesImpl(cstr ProjectId, const MdoHomePurgeTarget* Targets,
    size_t Count, const MdoHomePurgeRequest* Request, bool* Committed)
{
    MdoHomePurgeManifest Manifest;
    xfileinfo Info;
    bool Exists, Created = false, Ok = false;
    size_t i, Nodes = 0u;
    if ( Committed != NULL ) *Committed = false;
    if ( !g_MdoHome.Initialized || Committed == NULL || !MdoHomePurgeId(ProjectId) )
        return MdoHomePurgeError("invalid project purge storage request");
    memset(&Manifest, 0, sizeof(Manifest));
    snprintf(Manifest.Project, sizeof(Manifest.Project), "%s", ProjectId);
    Manifest.Targets = (MdoHomePurgeTarget*)Targets; Manifest.Count = Count;
    if ( Request != NULL ) { Manifest.HasRequest = true; Manifest.Request = *Request; }
    if ( !MdoHomePurgeTargets(&Manifest) ) return MdoHomePurgeError("invalid project purge targets");
    xrtMutexLock(g_MdoHome.Lock);
    if ( !MdoHomePurgeAvailableLocked() ) goto done;
    if ( Request != NULL ) {
        char Path[96];
        MdoHomePurgeReceipt Receipt;
        MdoHomePurgeReceiptPath(Path, Request->Id);
        if ( !MdoHomePurgeRecordRead(Path, true, &Receipt, &Exists) ) goto done;
        if ( Exists ) {
            MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
                "project purge request ID is already recorded");
            goto done;
        }
        if ( !MdoHomePurgeReceiptCapacity() ) goto done;
    }
    for ( i = 0u; i < Count; ++i )
        if ( !MdoHomeImportStat(Targets[i].Path, &Exists, &Info) || !Exists ||
             !MdoHomePurgeSame(&Info, &Targets[i].Info) ||
             !MdoHomePurgeTree(Targets[i].Path, 0u, &Nodes, false) ) goto done;
    if ( !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR, 0700u) ) goto done;
    Created = true;
    if ( !MdoHomeImportWrite(MDO_HOME_PURGE_DIR "/owner", MDO_HOME_PURGE_MAGIC,
            sizeof(MDO_HOME_PURGE_MAGIC) - 1u) ||
         (Request != NULL && !MdoHomePurgeRequestSave(Request)) ||
         !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/payload", 0700u) ||
         !MdoHomeImportStat(MDO_HOME_PURGE_DIR, &Exists, &Manifest.Journal) || !Exists ||
         !MdoHomeImportStat(MDO_HOME_PURGE_DIR "/payload", &Exists, &Manifest.Payload) || !Exists ||
         !MdoHomePurgeSave(&Manifest) ) goto done;
    for ( i = 0u; i < Count; ++i ) {
        char Slot[96];
        MdoHomePurgeSlot(Slot, MDO_HOME_PURGE_DIR, i);
        if ( !MdoHomePurgeMove(Targets[i].Path, Slot, &Targets[i].Info) ) goto done;
    }
    if ( !MdoHomePurgeMarker("committed", MDO_HOME_PURGE_MAGIC,
            sizeof(MDO_HOME_PURGE_MAGIC) - 1u) ) goto done;
    *Committed = true;
    Ok = MdoHomePurgeRecoverLocked();
done:
    if ( !Ok ) {
        xerror* Saved = xrtTakeError();
        if ( Created && !MdoHomePurgeRecoverLocked() ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "project purge recovery requires restart; preserve its journal");
        }
        xrtClearError();
        if ( Saved != NULL ) xrtSetErrorTake(Saved);
        else (void)MdoHomePurgeError("project purge storage transaction failed");
    }
    xrtMutexUnlock(g_MdoHome.Lock);
    return Ok;
}

bool MdoHomePurgeFiles(cstr ProjectId, const MdoHomePurgeTarget* Targets, size_t Count, bool* Committed)
{
    return MdoHomePurgeFilesImpl(ProjectId, Targets, Count, NULL, Committed);
}

bool MdoHomePurgeFilesRequested(const MdoHomePurgeRequest* Request,
    const MdoHomePurgeTarget* Targets, size_t Count, bool* Committed)
{
    return MdoHomePurgeFilesImpl(Request != NULL ? Request->ProjectId : NULL,
        Targets, Count, Request, Committed);
}
