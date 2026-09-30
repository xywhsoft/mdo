/* Private storage transaction, included after home_import.inc.c by home.c.
 * The journal is immutable once ready. Each rename is reconstructed from
 * exactly one of its two locations, not from a mutable progress counter.
 * Retired journals can only be cleaned up, never interpreted as rollback. */
#define MDO_HOME_PURGE_DIR ".mdo-purge"
#define MDO_HOME_PURGE_GC ".mdo-purge-cleanup"
#define MDO_HOME_PURGE_MAGIC "mdo-project-purge-v1\n"
#define MDO_HOME_PURGE_MANIFEST_LIMIT (512u * 1024u)
#define MDO_HOME_PURGE_NODE_LIMIT 8192u

typedef struct MdoHomePurgeManifest {
    char Project[65];
    xfileinfo Journal, Payload;
    MdoHomePurgeTarget* Targets;
    size_t Count;
} MdoHomePurgeManifest;

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
 * this layer accepts only a syntactically valid plan namespace. */
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

static bool MdoHomePurgeRead(cstr Path, char** Text, size_t* Size)
{
    xfileoptions Options;
    xfile File;
    bool Ok;
    xrtFileOptionsInit(&Options); Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    if ( File == NULL ) return false;
    Ok = MdoHomeReadFileBounded(File, MDO_HOME_PURGE_MANIFEST_LIMIT, Text, Size);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
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

static bool MdoHomePurgeSave(const MdoHomePurgeManifest* Manifest)
{
    xvalue* Root = xrtValueObject();
    xvalue* Array = xrtValueArray();
    char* Text = NULL;
    size_t Size = 0u, i;
    bool Ok = Root != NULL && Array != NULL &&
        MdoHomePurgeTake(Root, "version", xrtValueInt(1)) &&
        MdoHomePurgeTake(Root, "project", xrtValueString(xrtStrView(Manifest->Project))) &&
        MdoHomePurgePutIdentity(Root, "journal", &Manifest->Journal) &&
        MdoHomePurgePutIdentity(Root, "payload", &Manifest->Payload);
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
    Ok = Root != NULL && xrtValueType(Root) == XVALUE_OBJECT && xrtValueCount(Root) == 5u &&
        xrtValueGetInt(xrtValueObjectGet(Root, XRT_STR_LITERAL("version")), &Version) &&
        Version == 1 && xrtValueGetString(xrtValueObjectGet(Root,
            XRT_STR_LITERAL("project")), &Project) && Project.Size < sizeof(Manifest->Project) &&
        memchr(Project.Data, '\0', Project.Size) == NULL &&
        MdoHomePurgeGetIdentity(Root, "journal", &Manifest->Journal) &&
        MdoHomePurgeGetIdentity(Root, "payload", &Manifest->Payload);
    if ( Ok ) { memcpy(Manifest->Project, Project.Data, Project.Size); Manifest->Project[Project.Size] = '\0'; }
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
        "committed", "committed.tmp", "payload" };
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
        for ( i = 0u; i < 6u; ++i ) if ( MdoHomeImportName(Entry.Name, Names[i]) ) break;
        if ( i == 6u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
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

static bool MdoHomePurgeGc(void)
{
    static const char* const Files[] = { "ready.tmp", "committed.tmp", "committed", "ready", "owner" };
    MdoHomePurgeManifest Manifest;
    xfileinfo Info;
    bool Exists, Empty, Ready, Ok = false;
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
    for ( i = 0u; i < 5u; ++i ) {
        char Path[96];
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
    xfileinfo Info;
    bool Exists, Gc, Empty, Ready, Committed, Other, Ok = false;
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
    if ( !MdoHomePurgeMove(MDO_HOME_PURGE_DIR, MDO_HOME_PURGE_GC, &Manifest.Journal) ) goto done;
    Ok = MdoHomePurgeGc();
done:
    xrtFree(Manifest.Targets);
    if ( !Ok ) (void)MdoHomePurgeError("project purge recovery is ambiguous or incomplete; preserve its journal");
    return Ok;
}

bool MdoHomePurgeFiles(cstr ProjectId, const MdoHomePurgeTarget* Targets,
    size_t Count, bool* Committed)
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
    if ( !MdoHomePurgeTargets(&Manifest) ) return MdoHomePurgeError("invalid project purge targets");
    xrtMutexLock(g_MdoHome.Lock);
    if ( !MdoHomeWritableLocked() || g_MdoHome.Root == NULL || g_MdoHome.LeaseFile == NULL ) goto done;
    for ( i = 0u; i < 2u; ++i ) {
        cstr Journal = i == 0u ? MDO_HOME_PURGE_DIR : MDO_HOME_PURGE_GC;
        if ( !MdoHomeImportStat(Journal, &Exists, &Info) ) goto done;
        if ( Exists ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "pending project purge journal requires restart");
            goto done;
        }
    }
    for ( i = 0u; i < Count; ++i )
        if ( !MdoHomeImportStat(Targets[i].Path, &Exists, &Info) || !Exists ||
             !MdoHomePurgeSame(&Info, &Targets[i].Info) ||
             !MdoHomePurgeTree(Targets[i].Path, 0u, &Nodes, false) ) goto done;
    if ( !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR, 0700u) ) goto done;
    Created = true;
    if ( !MdoHomeImportWrite(MDO_HOME_PURGE_DIR "/owner", MDO_HOME_PURGE_MAGIC,
            sizeof(MDO_HOME_PURGE_MAGIC) - 1u) ||
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
