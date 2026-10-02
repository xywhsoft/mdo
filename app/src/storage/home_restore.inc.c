/* Included after Home's import/purge leaves. These existing helpers provide
 * anchored stat, bounded reads, flushed exclusive writes and identity JSON.
 * Publication has one commit point: payload -> live directory. Recovery does
 * not roll a live session back or inspect/delete its evolving contents. */
#define MDO_HOME_RESTORE_DIR ".mdo-session-restore"
#define MDO_HOME_RESTORE_GC ".mdo-session-restore-cleanup"
#define MDO_HOME_RESTORE_RECORD_BYTES 2048u
#define MDO_HOME_RESTORE_NODES 4096u
#define MDO_HOME_RESTORE_BYTES (64u * 1024u * 1024u)

typedef struct MdoHomeRestoreRecord {
    char Project[65], Session[33], Name[41];
    xfileinfo Journal, Parent, Directory;
    bool Ready;
} MdoHomeRestoreRecord;

struct MdoHomeSessionRestore { MdoHomeRestoreRecord Record; };
typedef struct MdoHomeRestoreBudget { size_t Nodes, Files; uint64 Bytes; } MdoHomeRestoreBudget;

static bool MdoHomeRestoreError(cstr Message)
{
    MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STORAGE, Message);
    return false;
}

static bool MdoHomeRestoreName(cstr Name)
{
    return Name != NULL && strlen(Name) == 40u &&
        strncmp(Name, "restore-", 8u) == 0 &&
        MdoSessionFileDigits(Name + 8u, 32u, true);
}

static void MdoHomeRestoreTarget(char Path[128], const MdoHomeRestoreRecord* Record)
{
    snprintf(Path, 128u, "sessions/%s/%s", Record->Project, Record->Session);
}

static bool MdoHomeRestoreStat(cstr Base, cstr Name, bool* Exists, xfileinfo* Info)
{
    char Path[512];
    int Length = snprintf(Path, sizeof(Path), "%s%s%s", Base, Name[0] ? "/" : "", Name);
    return Length > 0 && (size_t)Length < sizeof(Path) && MdoHomeImportStat(Path, Exists, Info);
}

static bool MdoHomeRestoreIdentity(const xfileinfo* Info)
{
    return Info->Type == XFILE_TYPE_DIRECTORY &&
        (Info->Available & XFILE_INFO_IDENTITY) != 0u && Info->Identity != 0u;
}

static bool MdoHomeRestoreCopy(const xvalue* Object, cstr Key, char* Text, size_t Capacity)
{
    xstrview Value;
    if ( !xrtValueGetString(xrtValueObjectGet(Object, xrtStrView(Key)), &Value) ||
         Value.Size == 0u || Value.Size >= Capacity || memchr(Value.Data, 0, Value.Size) != NULL ) return false;
    memcpy(Text, Value.Data, Value.Size); Text[Value.Size] = '\0';
    return true;
}

static xvalue* MdoHomeRestoreRead(cstr Base, cstr Name)
{
    char Path[128], *Text = NULL;
    size_t Bytes = 0u;
    xvalue* Value = NULL;
    xjsonreadconfig Config;
    snprintf(Path, sizeof(Path), "%s/%s", Base, Name);
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = MDO_HOME_RESTORE_RECORD_BYTES;
    Config.MaxDepth = 2u; Config.MaxValues = 16u; Config.MaxContainerItems = 8u;
    if ( MdoHomePurgeReadBounded(Path, MDO_HOME_RESTORE_RECORD_BYTES, &Text, &Bytes) )
        Value = xrtJsonRead(xrtStrViewN(Text, Bytes), &Config);
    xrtFree(Text);
    return Value;
}

static bool MdoHomeRestoreSave(cstr Name, const MdoHomeRestoreRecord* Record, bool Ready)
{
    xvalue* Object = xrtValueObject();
    str Text = NULL;
    size_t TextBytes = 0u;
    char Temporary[128], Target[128];
    bool Ok = Object != NULL &&
        MdoHomePurgeTake(Object, "version", xrtValueInt(1));
    if ( Ready ) Ok = Ok && MdoHomePurgeTake(Object, "directory",
        xrtValueString(xrtStrView(Record->Name))) &&
        MdoHomePurgePutIdentity(Object, "identity", &Record->Directory);
    else Ok = Ok && MdoHomePurgeTake(Object, "project", xrtValueString(xrtStrView(Record->Project))) &&
        MdoHomePurgeTake(Object, "session", xrtValueString(xrtStrView(Record->Session))) &&
        MdoHomePurgePutIdentity(Object, "journal", &Record->Journal) &&
        MdoHomePurgePutIdentity(Object, "parent", &Record->Parent);
    if ( Ok ) Text = xrtJsonStringify(Object, false, &TextBytes);
    Ok = Text != NULL && TextBytes <= MDO_HOME_RESTORE_RECORD_BYTES;
    snprintf(Temporary, sizeof(Temporary), "%s/%s.tmp", MDO_HOME_RESTORE_DIR, Name);
    snprintf(Target, sizeof(Target), "%s/%s", MDO_HOME_RESTORE_DIR, Name);
    if ( Ok ) Ok = MdoHomeImportWrite(Temporary, Text, TextBytes);
    if ( Ok && !xrtRootRenameNoReplace(g_MdoHome.Root, Temporary, Target) ) {
        char* Read = NULL; size_t Bytes = 0u;
        Ok = MdoHomePurgeReadBounded(Target, MDO_HOME_RESTORE_RECORD_BYTES, &Read, &Bytes) &&
            Bytes == TextBytes && memcmp(Read, Text, Bytes) == 0;
        xrtFree(Read);
    }
    xrtFree(Text); xrtValueRelease(Object);
    if ( Ok ) xrtClearError();
    return Ok;
}

/* Inventory the journal itself before interpreting any record. Scratch files
 * are bounded regular files, never payload authority. An incomplete owner
 * can authorize cleanup only while its staging parent is empty. */
static bool MdoHomeRestoreJournal(cstr Base, bool* Owner, bool* Ready)
{
    static const char* const Names[] = { "owner", "owner.tmp", "ready", "ready.tmp", "payload" };
    xdir Dir = xrtRootDirOpen(g_MdoHome.Root, Base, XDIR_STAT);
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Ok = false;
    size_t Count = 0u;
    *Owner = false; *Ready = false;
    if ( Dir == NULL ) return false;
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        size_t i;
        if ( ++Count > 5u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) goto done;
        for ( i = 0u; i < 5u; ++i ) if ( MdoHomeImportName(Entry.Name, Names[i]) ) break;
        if ( i == 5u || Entry.Info.Type != (i == 4u ? XFILE_TYPE_DIRECTORY : XFILE_TYPE_FILE) ||
             (i != 4u && ((Entry.Info.Available & XFILE_INFO_SIZE) == 0u ||
                Entry.Info.Size > MDO_HOME_RESTORE_RECORD_BYTES)) ) goto done;
        if ( i == 0u ) *Owner = true;
        if ( i == 2u ) *Ready = true;
    }
    Ok = Next == XDIR_NEXT_END && (!*Ready || *Owner);
done:
    if ( !xrtDirClose(Dir) ) Ok = false;
    return Ok;
}

static bool MdoHomeRestoreLoad(cstr Base, MdoHomeRestoreRecord* Record, bool Retired)
{
    xvalue* Owner = MdoHomeRestoreRead(Base, "owner");
    xvalue* Ready = NULL;
    xfileinfo Actual;
    int64 Version = 0;
    bool Exists, Ok;
    memset(Record, 0, sizeof(*Record));
    Record->Journal.Type = Record->Parent.Type = Record->Directory.Type = XFILE_TYPE_DIRECTORY;
    Ok = Owner != NULL && xrtValueType(Owner) == XVALUE_OBJECT && xrtValueCount(Owner) == 5u &&
        xrtValueGetInt(xrtValueObjectGet(Owner, XRT_STR_LITERAL("version")), &Version) && Version == 1 &&
        MdoHomeRestoreCopy(Owner, "project", Record->Project, sizeof(Record->Project)) &&
        MdoHomePurgeId(Record->Project) &&
        MdoHomeRestoreCopy(Owner, "session", Record->Session, sizeof(Record->Session)) &&
        MdoHomePurgeRequestIdValid(Record->Session) &&
        MdoHomePurgeGetIdentity(Owner, "journal", &Record->Journal) &&
        MdoHomePurgeGetIdentity(Owner, "parent", &Record->Parent) &&
        MdoHomeRestoreStat(Base, "", &Exists, &Actual) && Exists &&
        MdoHomePurgeSame(&Actual, &Record->Journal) &&
        MdoHomeRestoreStat(Base, "payload", &Exists, &Actual) &&
        (Exists ? MdoHomePurgeSame(&Actual, &Record->Parent) : Retired);
    if ( Ok ) Ok = MdoHomeRestoreStat(Base, "ready", &Record->Ready, &Actual);
    if ( Ok && Record->Ready ) {
        Ready = MdoHomeRestoreRead(Base, "ready");
        Ok = Ready != NULL && xrtValueType(Ready) == XVALUE_OBJECT && xrtValueCount(Ready) == 3u &&
            xrtValueGetInt(xrtValueObjectGet(Ready, XRT_STR_LITERAL("version")), &Version) && Version == 1 &&
            MdoHomeRestoreCopy(Ready, "directory", Record->Name, sizeof(Record->Name)) &&
            MdoHomeRestoreName(Record->Name) && MdoHomePurgeGetIdentity(Ready, "identity", &Record->Directory);
    }
    xrtValueRelease(Ready); xrtValueRelease(Owner);
    return Ok;
}

/* Validate the complete bounded tree before deleting anything. Recheck each
 * enumerated identity immediately before its mutation. Only whitelist paths
 * can be removed: no links, arbitrary depth, .runtime.lock or unknown files.
 * Private journal ownership covers partial writes before ready, unlike the
 * separate Stage API which tracks individual file identities in memory. */
static bool MdoHomeRestoreTree(cstr Base, cstr Relative, unsigned Depth,
    MdoHomeRestoreBudget* Budget, bool Remove)
{
    char Path[512];
    xfileinfo Info, Current;
    xdir Dir;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Exists, Ok = false;
    size_t Limit;
    int Length = snprintf(Path, sizeof(Path), "%s%s%s", Base, Relative[0] ? "/" : "", Relative);
    if ( Length <= 0 || (size_t)Length >= sizeof(Path) || Depth > 3u ||
         ++Budget->Nodes > MDO_HOME_RESTORE_NODES ||
         !MdoHomeImportStat(Path, &Exists, &Info) || !Exists ||
         (Info.Available & XFILE_INFO_IDENTITY) == 0u || Info.Identity == 0u ) return false;
    Limit = MdoSessionFileLimit(Relative, Info.Type == XFILE_TYPE_DIRECTORY);
    if ( Limit == 0u ) return false;
    if ( Info.Type == XFILE_TYPE_FILE ) {
        if ( (Info.Available & XFILE_INFO_SIZE) == 0u || Info.Size > Limit ||
             Info.Size > MDO_HOME_RESTORE_BYTES - Budget->Bytes ) return false;
        if ( ++Budget->Files > 1024u ) return false;
        Budget->Bytes += Info.Size;
        return !Remove || (xrtRootStat(g_MdoHome.Root, Path, false, &Current) &&
            MdoHomePurgeSame(&Current, &Info) && xrtRootRemove(g_MdoHome.Root, Path));
    }
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) return false;
    Dir = xrtRootDirOpen(g_MdoHome.Root, Path, XDIR_STAT);
    if ( Dir == NULL ) return false;
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        char Child[256];
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || Entry.Name.Size == 0u ||
             Entry.Name.Size >= sizeof(Child) || memchr(Entry.Name.Data, 0, Entry.Name.Size) != NULL ) goto done;
        Length = snprintf(Child, sizeof(Child), "%s%s%.*s", Relative, Relative[0] ? "/" : "",
            (int)Entry.Name.Size, Entry.Name.Data);
        if ( Length <= 0 || (size_t)Length >= sizeof(Child) ||
             !MdoHomeRestoreTree(Base, Child, Depth + 1u, Budget, Remove) ) goto done;
    }
    Ok = Next == XDIR_NEXT_END;
done:
    if ( !xrtDirClose(Dir) ) Ok = false;
    return Ok && (!Remove || (xrtRootStat(g_MdoHome.Root, Path, false, &Current) &&
        MdoHomePurgeSame(&Current, &Info) && xrtRootRemove(g_MdoHome.Root, Path)));
}

/* Return at most one real staging directory, checking parent/root identities.
 * Pre-owner recovery requires an empty parent. Ready fixes both name and ID. */
static bool MdoHomeRestorePayload(cstr Base, const MdoHomeRestoreRecord* Record,
    bool Retired, char Name[41], xfileinfo* Identity)
{
    char Path[128];
    xfileinfo Parent;
    xdir Dir;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Exists, Ok = false;
    size_t Count = 0u;
    MdoHomeRestoreBudget Budget = {0};
    Name[0] = '\0';
    if ( !MdoHomeRestoreStat(Base, "payload", &Exists, &Parent) ) return false;
    if ( !Exists ) return Retired || Record == NULL;
    if ( !MdoHomeRestoreIdentity(&Parent) || (Record != NULL && !MdoHomePurgeSame(&Parent, &Record->Parent)) ) return false;
    snprintf(Path, sizeof(Path), "%s/payload", Base);
    Dir = xrtRootDirOpen(g_MdoHome.Root, Path, XDIR_STAT);
    if ( Dir == NULL ) return false;
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        char Directory[160];
        if ( ++Count > 1u || Record == NULL || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Name.Size != 40u || !MdoHomeRestoreIdentity(&Entry.Info) ) goto done;
        memcpy(Name, Entry.Name.Data, 40u); Name[40] = '\0';
        if ( !MdoHomeRestoreName(Name) || (Record->Ready &&
                (strcmp(Name, Record->Name) != 0 || !MdoHomePurgeSame(&Entry.Info, &Record->Directory))) ) goto done;
        snprintf(Directory, sizeof(Directory), "%s/%s", Path, Name);
        if ( !MdoHomeRestoreTree(Directory, "", 0u, &Budget, false) ) goto done;
        *Identity = Entry.Info;
    }
    Ok = Next == XDIR_NEXT_END;
done:
    if ( !xrtDirClose(Dir) ) Ok = false;
    return Ok;
}

/* GC has no live-target semantics. Removing ready/owner cannot turn a
 * committed transaction into rollback, including interruption after retire. */
static bool MdoHomeRestoreGc(void)
{
    static const char* const Names[] = { "ready.tmp", "owner.tmp", "ready", "owner" };
    MdoHomeRestoreRecord Record;
    xfileinfo Info, Directory;
    char Name[41], Path[160];
    bool Exists, Owner, Ready;
    size_t i;
    MdoHomeRestoreBudget Budget = {0};
    if ( !MdoHomeImportStat(MDO_HOME_RESTORE_GC, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( !MdoHomeRestoreIdentity(&Info) || !MdoHomeRestoreJournal(MDO_HOME_RESTORE_GC, &Owner, &Ready) ||
         (Owner && !MdoHomeRestoreLoad(MDO_HOME_RESTORE_GC, &Record, true)) ||
         !MdoHomeRestorePayload(MDO_HOME_RESTORE_GC, Owner ? &Record : NULL, true, Name, &Directory) ) return false;
    if ( Name[0] != '\0' ) {
        snprintf(Path, sizeof(Path), "%s/payload/%s", MDO_HOME_RESTORE_GC, Name);
        if ( !MdoHomeRestoreStat(MDO_HOME_RESTORE_GC, "payload", &Exists, &Info) || !Exists ||
             !MdoHomePurgeSame(&Info, &Record.Parent) ||
             !xrtRootStat(g_MdoHome.Root, Path, false, &Info) || !MdoHomePurgeSame(&Info, &Directory) ||
             !MdoHomeRestoreTree(Path, "", 0u, &Budget, true) ) return false;
    }
    snprintf(Path, sizeof(Path), "%s/payload", MDO_HOME_RESTORE_GC);
    if ( !MdoHomeImportStat(Path, &Exists, &Info) ||
         (Exists && !xrtRootRemove(g_MdoHome.Root, Path)) ) return false;
    for ( i = 0u; i < 4u; ++i ) {
        snprintf(Path, sizeof(Path), "%s/%s", MDO_HOME_RESTORE_GC, Names[i]);
        if ( !MdoHomeImportStat(Path, &Exists, &Info) ||
             (Exists && !xrtRootRemove(g_MdoHome.Root, Path)) ) return false;
    }
    return xrtRootRemove(g_MdoHome.Root, MDO_HOME_RESTORE_GC);
}

static bool MdoHomeRestoreRetire(const xfileinfo* Expected)
{
    return MdoHomePurgeMove(MDO_HOME_RESTORE_DIR, MDO_HOME_RESTORE_GC, Expected) && MdoHomeRestoreGc();
}

static bool MdoHomeSessionRestoreRecoverLocked(bool* Committed)
{
    static const char* const Others[] = { MDO_HOME_IMPORT_DIR, MDO_HOME_IMPORT_GC,
        MDO_HOME_PURGE_DIR, MDO_HOME_PURGE_GC };
    MdoHomeRestoreRecord Record;
    xfileinfo Info, Journal, Directory;
    bool Exists, Gc, Owner, Ready, Other;
    char Name[41], Target[128];
    size_t i;
    if ( Committed != NULL ) *Committed = false;
    if ( !MdoHomeImportStat(MDO_HOME_RESTORE_DIR, &Exists, &Journal) ||
         !MdoHomeImportStat(MDO_HOME_RESTORE_GC, &Gc, &Info) || (Exists && Gc) ) return false;
    if ( !Exists && !Gc ) return true;
    for ( i = 0u; i < 4u; ++i )
        if ( !MdoHomeImportStat(Others[i], &Other, &Info) || Other ) return false;
    if ( Gc ) return MdoHomeRestoreGc();
    if ( !MdoHomeRestoreIdentity(&Journal) || !MdoHomeRestoreJournal(MDO_HOME_RESTORE_DIR, &Owner, &Ready) ||
         (Owner && !MdoHomeRestoreLoad(MDO_HOME_RESTORE_DIR, &Record, false)) ||
         !MdoHomeRestorePayload(MDO_HOME_RESTORE_DIR, Owner ? &Record : NULL, false, Name, &Directory) ) return false;
    if ( Ready ) {
        MdoHomeRestoreTarget(Target, &Record);
        if ( !MdoHomeImportStat(Target, &Exists, &Info) ) return false;
        if ( Name[0] == '\0' ) {
            if ( !Exists || !MdoHomePurgeSame(&Info, &Record.Directory) ) return false;
            if ( Committed != NULL ) *Committed = true;
        } else if ( Exists && MdoHomePurgeSame(&Info, &Record.Directory) ) return false;
        /* Source retained, absent or foreign target: rename did not commit.
         * The foreign target is preserved; only our private source is retired. */
    }
    return MdoHomeRestoreRetire(&Journal);
}

static void MdoHomeRestoreFreeze(void)
{
    g_MdoHome.RestartRequired = true;
    snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
        "session restore requires startup recovery; preserve its journal and target ID");
}

MdoHomeSessionRestore* MdoHomeSessionRestoreBegin(cstr Project, cstr Session, xroot* Parent)
{
    static const char* const Journals[] = { MDO_HOME_RESTORE_DIR, MDO_HOME_RESTORE_GC,
        MDO_HOME_IMPORT_DIR, MDO_HOME_IMPORT_GC, MDO_HOME_PURGE_DIR, MDO_HOME_PURGE_GC };
    MdoHomeSessionRestore* Restore = NULL;
    xfileinfo Info;
    char Target[128];
    bool Exists, Created = false;
    size_t i;
    if ( Parent == NULL || *Parent != NULL || !g_MdoHome.Initialized ||
         !MdoHomePurgeId(Project) || !MdoHomePurgeRequestIdValid(Session) ) {
        (void)MdoHomeRestoreError("invalid session restore begin request"); return NULL;
    }
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Restore != NULL ) { (void)MdoHomeRestoreError("session restore is already in progress"); goto done; }
    if ( !MdoHomeEnsureLocked() ) goto done;
    for ( i = 0u; i < 6u; ++i ) {
        if ( !MdoHomeImportStat(Journals[i], &Exists, &Info) ) goto done;
        if ( Exists ) { MdoHomeRestoreFreeze(); (void)MdoHomeRestoreError("pending Home transaction requires startup recovery"); goto done; }
    }
    Restore = (MdoHomeSessionRestore*)xrtCalloc(1u, sizeof(*Restore));
    if ( Restore == NULL ) goto done;
    snprintf(Restore->Record.Project, sizeof(Restore->Record.Project), "%s", Project);
    snprintf(Restore->Record.Session, sizeof(Restore->Record.Session), "%s", Session);
    MdoHomeRestoreTarget(Target, &Restore->Record);
    if ( !MdoHomeImportStat(Target, &Exists, &Info) ) goto fail;
    if ( Exists ) { (void)MdoHomeRestoreError("restore target session ID already exists"); goto fail; }
    if ( !MdoHomeEnsureParents(g_MdoHome.Root, Target) ||
         !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_RESTORE_DIR, 0700u) ) goto fail;
    Created = true;
    /* Capability preflight moves only the empty owned directory. */
    if ( !xrtRootStat(g_MdoHome.Root, MDO_HOME_RESTORE_DIR, false, &Restore->Record.Journal) ||
         !MdoHomeRestoreIdentity(&Restore->Record.Journal) ||
         !MdoHomePurgeMove(MDO_HOME_RESTORE_DIR, MDO_HOME_RESTORE_GC, &Restore->Record.Journal) ||
         !MdoHomePurgeMove(MDO_HOME_RESTORE_GC, MDO_HOME_RESTORE_DIR, &Restore->Record.Journal) ||
         !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/payload", 0700u) ||
         !xrtRootStat(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/payload", false, &Restore->Record.Parent) ||
         !MdoHomeRestoreIdentity(&Restore->Record.Parent) || !MdoHomeRestoreSave("owner", &Restore->Record, false) ) goto fail;
    *Parent = xrtRootOpenIn(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/payload");
    if ( *Parent == NULL ) goto fail;
    g_MdoHome.Restore = Restore;
    goto done;
fail:
    {
        xerror* Saved = xrtTakeError();
        if ( Created && !MdoHomeSessionRestoreRecoverLocked(NULL) ) MdoHomeRestoreFreeze();
        xrtFree(Restore); Restore = NULL; xrtClearError();
        if ( Saved != NULL ) xrtSetErrorTake(Saved);
        else (void)MdoHomeRestoreError("cannot reserve session restore storage");
    }
done:
    xrtMutexUnlock(g_MdoHome.Lock);
    return Restore;
}

bool MdoHomeSessionRestoreEnd(MdoHomeSessionRestore* Restore, cstr Name,
    const xfileinfo* Expected, bool Publish, bool* Committed)
{
    MdoHomeRestoreRecord Record;
    char Directory[41], Source[160], Target[128];
    xfileinfo Identity, Info;
    bool Owner, Ready, Exists, Ok = false, Recovered, Trusted = false;
    if ( Committed != NULL ) *Committed = false;
    if ( Restore == NULL || Committed == NULL || !g_MdoHome.Initialized )
        return MdoHomeRestoreError("invalid session restore end request");
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Restore != Restore ) {
        xrtMutexUnlock(g_MdoHome.Lock); return MdoHomeRestoreError("session restore is not the current owner");
    }
    if ( !MdoHomeRestoreJournal(MDO_HOME_RESTORE_DIR, &Owner, &Ready) || !Owner || Ready ||
         !MdoHomeRestoreLoad(MDO_HOME_RESTORE_DIR, &Record, false) ||
         strcmp(Record.Project, Restore->Record.Project) != 0 || strcmp(Record.Session, Restore->Record.Session) != 0 ||
         !MdoHomePurgeSame(&Record.Journal, &Restore->Record.Journal) ||
         !MdoHomePurgeSame(&Record.Parent, &Restore->Record.Parent) ||
         !MdoHomeRestorePayload(MDO_HOME_RESTORE_DIR, &Record, false, Directory, &Identity) ) goto done;
    Trusted = true;
    if ( !Publish ) { Ok = true; goto done; }
    if ( g_MdoHome.RestartRequired || !MdoHomeRestoreName(Name) || strcmp(Name, Directory) != 0 ||
         Expected == NULL || !MdoHomeRestoreIdentity(Expected) || !MdoHomePurgeSame(&Identity, Expected) ) {
        (void)MdoHomeRestoreError("restore publication requires the verified private directory"); goto done;
    }
    snprintf(Record.Name, sizeof(Record.Name), "%s", Name); Record.Directory = Identity;
    MdoHomeRestoreTarget(Target, &Record);
    if ( !MdoHomeImportStat(Target, &Exists, &Info) || Exists || !MdoHomeRestoreSave("ready", &Record, true) ) goto done;
    snprintf(Source, sizeof(Source), "%s/payload/%s", MDO_HOME_RESTORE_DIR, Record.Name);
    /* Recover actual positions after every rename result, including native
     * handle-close errors reported after the move already committed. */
    (void)xrtRootRenameNoReplace(g_MdoHome.Root, Source, Target);
done:
    {
        xerror* Saved = xrtTakeError();
        Recovered = Trusted && MdoHomeSessionRestoreRecoverLocked(Committed);
        if ( !Recovered ) MdoHomeRestoreFreeze();
        Ok = Recovered && (*Committed || (!Publish && Ok));
        g_MdoHome.Restore = NULL; xrtFree(Restore);
        xrtClearError();
        if ( !Ok ) {
            if ( Saved != NULL ) xrtSetErrorTake(Saved);
            else (void)MdoHomeRestoreError("session restore publication/cleanup failed; inspect its commit state");
        } else xrtErrorFree(Saved);
    }
    xrtMutexUnlock(g_MdoHome.Lock);
    return Ok;
}
