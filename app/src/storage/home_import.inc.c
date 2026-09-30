/* Private Home transaction implementation, included by home.c.
 * The cache is never traversed or moved. Only a flushed, validated manifest
 * authorizes data-root moves. Recovery reconstructs each move from its two
 * locations, so a rename that succeeded but reported a close error is safe.
 * Retiring the whole journal before deleting it prevents interrupted cleanup
 * from turning a committed import into an apparent precommit transaction. */

#define MDO_HOME_IMPORT_DIR ".mdo-import"
#define MDO_HOME_IMPORT_GC ".mdo-import-cleanup"
#define MDO_HOME_IMPORT_MAGIC "mdo-home-import-v1\n"
#define MDO_HOME_IMPORT_LIMIT 8192u

static const char* const g_MdoHomeImportRoots[] = {
    "config", "secrets", "projects", "sessions", "memory", "schedules", "migration"
};

struct MdoHomeImport { bool Active; };
typedef struct MdoHomeImportIdentity { uint64 Device, File; } MdoHomeImportIdentity;

static bool MdoHomeImportError(cstr Message)
{
    MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STORAGE, Message);
    return false;
}

static bool MdoHomeImportName(xstrview Name, cstr Text)
{
    return Name.Size == strlen(Text) && memcmp(Name.Data, Text, Name.Size) == 0;
}

static bool MdoHomeImportStat(cstr Path, bool* Exists, xfileinfo* Info)
{
    *Exists = false;
    if ( xrtRootStat(g_MdoHome.Root, Path, false, Info) ) {
        *Exists = true;
        return true;
    }
    if ( xrtGetError() != NULL && xrtErrorKind(xrtGetError()) == XERR_NOT_FOUND ) {
        xrtClearError();
        return true;
    }
    return false;
}

/* Check just the cache's three parent levels. Browser-owned contents can
 * change continuously and are outside this transaction's data scope. */
static bool MdoHomeImportCacheOnly(cstr Path, unsigned Level, uint32 Roots,
    bool Journal, bool* Eligible)
{
    xdir Directory = xrtRootDirOpen(g_MdoHome.Root, Path, XDIR_STAT);
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    size_t Count = 0u;
    bool Ok = false;
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        size_t i;
        const char* Child = NULL;
        if ( ++Count > 12u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) {
            *Eligible = false;
            break;
        }
        if ( Level == 0u && MdoHomeImportName(Entry.Name, MDO_HOME_LEASE_PATH) &&
             Entry.Info.Type == XFILE_TYPE_FILE ) continue;
        if ( Level == 0u && Journal &&
             MdoHomeImportName(Entry.Name, MDO_HOME_IMPORT_DIR) &&
             Entry.Info.Type == XFILE_TYPE_DIRECTORY ) continue;
        if ( Level == 0u && Roots != 0u ) {
            for ( i = 0u; i < 7u; ++i )
                if ( (Roots & (1u << i)) != 0u &&
                     MdoHomeImportName(Entry.Name, g_MdoHomeImportRoots[i]) &&
                     Entry.Info.Type == XFILE_TYPE_DIRECTORY ) break;
            if ( i < 7u ) continue;
        }
        if ( Level == 0u && MdoHomeImportName(Entry.Name, "data") ) Child = "data";
        if ( Level == 1u && MdoHomeImportName(Entry.Name, "cache") ) Child = "data/cache";
        if ( Level == 2u && MdoHomeImportName(Entry.Name, "webview2") &&
             Entry.Info.Type == XFILE_TYPE_DIRECTORY ) continue;
        if ( Child == NULL || Entry.Info.Type != XFILE_TYPE_DIRECTORY ) {
            *Eligible = false;
            break;
        }
        if ( !MdoHomeImportCacheOnly(Child, Level + 1u, 0u, false, Eligible) ) goto done;
        if ( !*Eligible ) break;
    }
    Ok = Next != XDIR_NEXT_ERROR;
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok;
}

bool MdoHomeImportInspect(bool* Available, bool* PreserveCache)
{
    bool Ok = true;
    xfileinfo Info;
    if ( Available == NULL || PreserveCache == NULL || !g_MdoHome.Initialized )
        return MdoHomeImportError("invalid Home import inspection");
    *Available = false; *PreserveCache = false;
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Import != NULL ) {
        /* Source revalidation by the owner must hash the same target mode;
         * eligibility stays false so no second importer can begin. */
        *PreserveCache = true;
        goto done;
    }
    if ( g_MdoHome.RestartRequired ||
         g_MdoHome.Persistence == MDO_PERSISTENCE_EPHEMERAL ) goto done;
    if ( g_MdoHome.Root != NULL ) {
        *Available = true;
        Ok = MdoHomeImportCacheOnly(".", 0u, 0u, false, Available);
        *PreserveCache = Ok && *Available;
    } else if ( !xrtPathStat(g_MdoHome.Path, false, &Info) ) {
        if ( xrtGetError() != NULL && xrtErrorKind(xrtGetError()) == XERR_NOT_FOUND ) {
            xrtClearError();
            *Available = true;
        } else Ok = false;
    }
done:
    xrtMutexUnlock(g_MdoHome.Lock);
    return Ok;
}

static bool MdoHomeImportRead(cstr Path, char** Text, size_t* Size)
{
    xfileoptions Options;
    xfile File;
    bool Ok;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    if ( File == NULL ) return false;
    Ok = MdoHomeReadFileBounded(File, 2048u, Text, Size);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoHomeImportMagic(cstr Path)
{
    char* Text = NULL;
    size_t Size = 0u;
    bool Ok = MdoHomeImportRead(Path, &Text, &Size) &&
        Size == sizeof(MDO_HOME_IMPORT_MAGIC) - 1u &&
        memcmp(Text, MDO_HOME_IMPORT_MAGIC, Size) == 0;
    xrtFree(Text);
    return Ok ? true : MdoHomeImportError("invalid Home import ownership/commit marker");
}

static bool MdoHomeImportWrite(cstr Path, const void* Bytes, size_t Size)
{
    xfileoptions Options;
    xfile File;
    bool Ok;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_NOFOLLOW | XFILE_SYNC;
    Options.Mode = 0600u;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    if ( File == NULL ) return false;
    Ok = xrtWriteFull(File, Bytes, Size, NULL) && xrtFlush(File);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoHomeImportMarker(cstr Name, const char* Text)
{
    char Temporary[96], Target[96];
    char* Read = NULL;
    size_t Size = 0u;
    bool Ok;
    snprintf(Temporary, sizeof(Temporary), "%s/%s.tmp", MDO_HOME_IMPORT_DIR, Name);
    snprintf(Target, sizeof(Target), "%s/%s", MDO_HOME_IMPORT_DIR, Name);
    if ( !MdoHomeImportWrite(Temporary, Text, strlen(Text)) ) return false;
    if ( xrtRootRenameNoReplace(g_MdoHome.Root, Temporary, Target) ) return true;
    /* Root rename may report a handle-close error after the actual move. */
    Ok = MdoHomeImportRead(Target, &Read, &Size) && Size == strlen(Text) &&
        memcmp(Read, Text, Size) == 0;
    xrtFree(Read);
    if ( Ok ) xrtClearError();
    return Ok;
}

static bool MdoHomeImportHex(const char* Text, uint64* Value)
{
    size_t i;
    *Value = 0u;
    for ( i = 0u; i < 16u; ++i ) {
        unsigned char Ch = (unsigned char)Text[i];
        if ( Ch >= '0' && Ch <= '9' ) Ch -= '0';
        else if ( Ch >= 'a' && Ch <= 'f' ) Ch = (unsigned char)(Ch - 'a' + 10u);
        else return false;
        *Value = (*Value << 4u) | Ch;
    }
    return true;
}

static bool MdoHomeImportManifest(uint32* Roots, MdoHomeImportIdentity Identities[7])
{
    char* Text = NULL;
    size_t Size = 0u;
    xvalue* Value = NULL;
    const xvalue* Ids;
    xjsonreadconfig Config;
    size_t i;
    int64 Version = 0, Mask = 0;
    bool Ok = MdoHomeImportRead(MDO_HOME_IMPORT_DIR "/ready", &Text, &Size);
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = 2048u; Config.MaxDepth = 4u;
    Config.MaxValues = 32u; Config.MaxContainerItems = 8u;
    if ( Ok ) Value = xrtJsonRead(xrtStrViewN(Text, Size), &Config);
    Ok = Value != NULL && xrtValueType(Value) == XVALUE_OBJECT &&
        xrtValueCount(Value) == 3u && xrtValueGetInt(xrtValueObjectGet(Value,
            XRT_STR_LITERAL("version")), &Version) && Version == 1u &&
        xrtValueGetInt(xrtValueObjectGet(Value, XRT_STR_LITERAL("roots")), &Mask) &&
        Mask > 0 && Mask <= 127;
    Ids = Value != NULL ? xrtValueObjectGet(Value, XRT_STR_LITERAL("identities")) : NULL;
    Ok = Ok && Ids != NULL && xrtValueType(Ids) == XVALUE_ARRAY && xrtValueCount(Ids) == 7u;
    for ( i = 0u; Ok && i < 7u; ++i ) {
        xstrview Id;
        Ok = xrtValueGetString(xrtValueArrayGet(Ids, i), &Id) && Id.Size == 33u &&
            Id.Data[16] == ':' && MdoHomeImportHex(Id.Data, &Identities[i].Device) &&
            MdoHomeImportHex(Id.Data + 17u, &Identities[i].File);
        if ( Ok && (Mask & (1u << i)) == 0u )
            Ok = Identities[i].Device == 0u && Identities[i].File == 0u;
        if ( Ok && (Mask & (1u << i)) != 0u ) Ok = Identities[i].File != 0u;
    }
    if ( Ok ) *Roots = (uint32)Mask;
    xrtValueRelease(Value);
    xrtFree(Text);
    return Ok ? true : MdoHomeImportError("invalid Home import manifest");
}

/* The manifest can name only these seven roots; no path ever comes from it.
 * Deep payload checks reject links and bound cleanup work before any move. */
static bool MdoHomeImportTree(cstr Path, unsigned Depth, size_t* Count, bool Remove)
{
    xdir Directory;
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Ok = false;
    if ( Depth > 16u ) return MdoHomeImportError("Home import tree is too deep");
    Directory = xrtRootDirOpen(g_MdoHome.Root, Path, XDIR_STAT);
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Child[4096];
        int Written;
        if ( ++*Count > MDO_HOME_IMPORT_LIMIT || Entry.Name.Size == 0u ||
             Entry.Name.Size > 255u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             memchr(Entry.Name.Data, '/', Entry.Name.Size) != NULL ||
             memchr(Entry.Name.Data, '\\', Entry.Name.Size) != NULL ||
             memchr(Entry.Name.Data, ':', Entry.Name.Size) != NULL ||
             MdoHomeImportName(Entry.Name, ".") || MdoHomeImportName(Entry.Name, "..") )
            goto invalid;
        Written = snprintf(Child, sizeof(Child), "%s/%s", Path, Entry.Name.Data);
        if ( Written <= 0 || (size_t)Written >= sizeof(Child) ) goto invalid;
        if ( Entry.Info.Type == XFILE_TYPE_DIRECTORY ) {
            if ( !MdoHomeImportTree(Child, Depth + 1u, Count, Remove) ) goto done;
        } else if ( Entry.Info.Type == XFILE_TYPE_FILE ) {
            if ( Remove && !xrtRootRemove(g_MdoHome.Root, Child) ) goto done;
        } else goto invalid;
    }
    Ok = Next == XDIR_NEXT_END;
    goto done;
invalid:
    (void)MdoHomeImportError("Home import contains a link or unsupported entry");
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    if ( Ok && Remove ) Ok = xrtRootRemove(g_MdoHome.Root, Path);
    return Ok;
}

static bool MdoHomeImportPayload(uint32* Roots)
{
    xdir Directory = xrtRootDirOpen(g_MdoHome.Root,
        MDO_HOME_IMPORT_DIR "/payload", XDIR_STAT);
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Ok = false;
    size_t Count = 0u;
    *Roots = 0u;
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        size_t i;
        for ( i = 0u; i < 7u; ++i )
            if ( MdoHomeImportName(Entry.Name, g_MdoHomeImportRoots[i]) ) break;
        if ( i == 7u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Info.Type != XFILE_TYPE_DIRECTORY || (*Roots & (1u << i)) != 0u )
            goto invalid;
        *Roots |= 1u << i;
    }
    Ok = Next == XDIR_NEXT_END;
    goto done;
invalid:
    (void)MdoHomeImportError("Home import contains an unknown data root");
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok && MdoHomeImportTree(MDO_HOME_IMPORT_DIR "/payload", 0u, &Count, false);
}

static bool MdoHomeImportJournal(cstr Base, bool* Empty)
{
    static const char* const Names[] = {
        "owner", "ready", "ready.tmp", "committed", "committed.tmp", "payload"
    };
    xdir Directory = xrtRootDirOpen(g_MdoHome.Root, Base, XDIR_STAT);
    xdirentry Entry;
    xdirnext Next = XDIR_NEXT_ERROR;
    bool Ok = false, Owner = false;
    *Empty = true;
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        size_t i;
        *Empty = false;
        for ( i = 0u; i < 6u; ++i )
            if ( MdoHomeImportName(Entry.Name, Names[i]) ) break;
        if ( i == 6u || (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ||
             Entry.Info.Type != (i == 5u ? XFILE_TYPE_DIRECTORY : XFILE_TYPE_FILE) )
            goto invalid;
        if ( i == 0u ) Owner = true;
    }
    Ok = Next == XDIR_NEXT_END;
    if ( Ok && !*Empty ) {
        char Path[80];
        snprintf(Path, sizeof(Path), "%s/owner", Base);
        Ok = Owner ? MdoHomeImportMagic(Path) :
            MdoHomeImportError("Home import journal lacks an ownership marker");
    }
    goto done;
invalid:
    (void)MdoHomeImportError("Home import journal contains an unknown entry");
done:
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok;
}

static bool MdoHomeImportGc(void)
{
    static const char* const Files[] = {
        "ready.tmp", "committed.tmp", "ready", "committed", "owner"
    };
    xfileinfo Info;
    bool Exists, Empty;
    size_t Count = 0u, i;
    if ( !MdoHomeImportStat(MDO_HOME_IMPORT_GC, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ||
         !MdoHomeImportJournal(MDO_HOME_IMPORT_GC, &Empty) ) return false;
    if ( !Empty ) {
        if ( !MdoHomeImportStat(MDO_HOME_IMPORT_GC "/payload", &Exists, &Info) ) return false;
        if ( Exists && (Info.Type != XFILE_TYPE_DIRECTORY ||
                !MdoHomeImportTree(MDO_HOME_IMPORT_GC "/payload", 0u, &Count, false)) ) return false;
        Count = 0u;
        if ( Exists && !MdoHomeImportTree(MDO_HOME_IMPORT_GC "/payload", 0u, &Count, true) ) return false;
        for ( i = 0u; i < 5u; ++i ) {
            char Path[96];
            snprintf(Path, sizeof(Path), "%s/%s", MDO_HOME_IMPORT_GC, Files[i]);
            if ( !MdoHomeImportStat(Path, &Exists, &Info) ) return false;
            if ( Exists && (Info.Type != XFILE_TYPE_FILE ||
                    !xrtRootRemove(g_MdoHome.Root, Path)) ) return false;
        }
    }
    return xrtRootRemove(g_MdoHome.Root, MDO_HOME_IMPORT_GC);
}

static bool MdoHomeImportRetire(void)
{
    if ( !xrtRootRenameNoReplace(g_MdoHome.Root, MDO_HOME_IMPORT_DIR, MDO_HOME_IMPORT_GC) ) {
        xfileinfo Info;
        bool Source, Target;
        if ( !MdoHomeImportStat(MDO_HOME_IMPORT_DIR, &Source, &Info) || Source ||
             !MdoHomeImportStat(MDO_HOME_IMPORT_GC, &Target, &Info) || !Target ||
             Info.Type != XFILE_TYPE_DIRECTORY ) return false;
        xrtClearError();
    }
    return MdoHomeImportGc();
}

/* Validate the complete inventory before rollback. For every manifest bit,
 * exactly one anchored location must hold a real directory. Both/neither is
 * ambiguous and freezes bootstrap instead of guessing or overwriting data. */
static bool MdoHomeImportPositions(uint32 Roots, const MdoHomeImportIdentity Identities[7],
    bool Committed, uint32* Installed)
{
    bool Eligible = true;
    uint32 Payload;
    size_t i;
    *Installed = 0u;
    if ( !MdoHomeImportCacheOnly(".", 0u, Roots, true, &Eligible) || !Eligible ||
         !MdoHomeImportPayload(&Payload) || (Payload & ~Roots) != 0u ) return false;
    for ( i = 0u; i < 7u; ++i ) {
        xfileinfo Info;
        bool Target;
        if ( !MdoHomeImportStat(g_MdoHomeImportRoots[i], &Target, &Info) ) return false;
        if ( Target && (Info.Type != XFILE_TYPE_DIRECTORY || (Roots & (1u << i)) == 0u) )
            return MdoHomeImportError("Home import target changed during recovery");
        if ( (Roots & (1u << i)) == 0u ) continue;
        if ( Target == ((Payload & (1u << i)) != 0u) || (Committed && !Target) )
            return MdoHomeImportError("Home import has ambiguous data-root locations");
        if ( !Target ) {
            char Source[96];
            bool Exists;
            snprintf(Source, sizeof(Source), "%s/payload/%s", MDO_HOME_IMPORT_DIR,
                g_MdoHomeImportRoots[i]);
            if ( !MdoHomeImportStat(Source, &Exists, &Info) || !Exists ) return false;
        }
        if ( (Info.Available & XFILE_INFO_IDENTITY) == 0u ||
             Info.Device != Identities[i].Device || Info.Identity != Identities[i].File )
            return MdoHomeImportError("Home import data-root identity changed");
        if ( Target ) *Installed |= 1u << i;
    }
    return true;
}

static bool MdoHomeImportRecoverLocked(bool* Published)
{
    xfileinfo Info;
    bool Exists, Empty, Ready, Committed, Eligible = true;
    uint32 Roots = 0u, Installed = 0u;
    MdoHomeImportIdentity Identities[7];
    size_t i;
    if ( Published != NULL ) *Published = false;
    if ( !MdoHomeImportGc() ||
         !MdoHomeImportStat(MDO_HOME_IMPORT_DIR, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ||
         !MdoHomeImportJournal(MDO_HOME_IMPORT_DIR, &Empty) ) return false;
    if ( Empty ) return xrtRootRemove(g_MdoHome.Root, MDO_HOME_IMPORT_DIR);
    if ( !MdoHomeImportStat(MDO_HOME_IMPORT_DIR "/ready", &Ready, &Info) ||
         !MdoHomeImportStat(MDO_HOME_IMPORT_DIR "/committed", &Committed, &Info) ) return false;
    if ( Committed && (!Ready || !MdoHomeImportMagic(MDO_HOME_IMPORT_DIR "/committed")) ) return false;
    if ( Ready ) {
        if ( !MdoHomeImportManifest(&Roots, Identities) ||
             !MdoHomeImportPositions(Roots, Identities, Committed, &Installed) ) return false;
        if ( Published != NULL ) *Published = Committed;
        if ( !Committed ) {
            for ( i = 7u; i-- > 0u; ) {
                char Destination[96];
                if ( (Installed & (1u << i)) == 0u ) continue;
                snprintf(Destination, sizeof(Destination), "%s/payload/%s",
                    MDO_HOME_IMPORT_DIR, g_MdoHomeImportRoots[i]);
                if ( !xrtRootRenameNoReplace(g_MdoHome.Root,
                        g_MdoHomeImportRoots[i], Destination) ) return false;
            }
        }
    } else if ( !MdoHomeImportCacheOnly(".", 0u, 0u, true, &Eligible) || !Eligible )
        return MdoHomeImportError("unprepared Home import has unexpected user data");
    return MdoHomeImportRetire();
}

MdoHomeImport* MdoHomeImportBegin(xroot* Stage, str* Path)
{
    MdoHomeImport* Import = NULL;
    bool Eligible = true;
    if ( Stage == NULL || Path == NULL || !g_MdoHome.Initialized ) {
        (void)MdoHomeImportError("invalid Home import begin request");
        return NULL;
    }
    *Stage = NULL; *Path = NULL;
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Root == NULL || g_MdoHome.LeaseFile == NULL ||
         !MdoHomeWritableLocked() ||
         !MdoHomeImportCacheOnly(".", 0u, 0u, false, &Eligible) || !Eligible ) {
        (void)MdoHomeImportError("Home import requires an unused cache-only Home");
        goto done;
    }
    Import = (MdoHomeImport*)xrtMalloc(sizeof(*Import));
    if ( Import == NULL ) goto done;
    if ( !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_IMPORT_DIR, 0700u) ) {
        xrtFree(Import); Import = NULL; goto done;
    }
    Import->Active = true;
    g_MdoHome.Import = Import;
    if ( !MdoHomeImportWrite(MDO_HOME_IMPORT_DIR "/owner", MDO_HOME_IMPORT_MAGIC,
            sizeof(MDO_HOME_IMPORT_MAGIC) - 1u) ||
         !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_IMPORT_DIR "/payload", 0700u) ) goto fail;
    *Path = xrtPathJoin(g_MdoHome.Path, MDO_HOME_IMPORT_DIR "/payload");
    *Stage = xrtRootOpenIn(g_MdoHome.Root, MDO_HOME_IMPORT_DIR "/payload");
    if ( *Path == NULL || *Stage == NULL ) goto fail;
    goto done;
fail:
    {
        xerror* Saved = xrtTakeError();
        if ( *Stage != NULL ) (void)xrtRootClose(*Stage);
        xrtFree(*Path); *Path = NULL; *Stage = NULL;
        if ( !MdoHomeImportRecoverLocked(NULL) ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "Home import cleanup failed; startup recovery is required");
        }
        g_MdoHome.Import = NULL;
        xrtFree(Import); Import = NULL;
        xrtClearError();
        if ( Saved != NULL ) xrtSetErrorTake(Saved);
    }
done:
    xrtMutexUnlock(g_MdoHome.Lock);
    return Import;
}

static bool MdoHomeImportPublishLocked(void)
{
    uint32 Roots;
    bool Eligible = true;
    char Manifest[1024];
    size_t Length;
    size_t i;
    if ( !MdoHomeImportCacheOnly(".", 0u, 0u, true, &Eligible) || !Eligible ||
         !MdoHomeImportPayload(&Roots) || Roots == 0u )
        return MdoHomeImportError("Home import payload or target is unavailable");
    Length = (size_t)snprintf(Manifest, sizeof(Manifest),
        "{\"version\":1,\"roots\":%u,\"identities\":[", Roots);
    for ( i = 0u; i < 7u; ++i ) {
        xfileinfo Info;
        bool Exists;
        char Source[96];
        memset(&Info, 0, sizeof(Info));
        if ( (Roots & (1u << i)) != 0u ) {
            snprintf(Source, sizeof(Source), "%s/payload/%s", MDO_HOME_IMPORT_DIR,
                g_MdoHomeImportRoots[i]);
            if ( !MdoHomeImportStat(Source, &Exists, &Info) || !Exists ||
                 (Info.Available & XFILE_INFO_IDENTITY) == 0u || Info.Identity == 0u )
                return MdoHomeImportError("Home import requires stable directory identities");
        }
        Length += (size_t)snprintf(Manifest + Length, sizeof(Manifest) - Length,
            "%s\"%016llx:%016llx\"", i != 0u ? "," : "",
            (unsigned long long)Info.Device, (unsigned long long)Info.Identity);
    }
    (void)snprintf(Manifest + Length, sizeof(Manifest) - Length, "]}\n");
    if ( !MdoHomeImportMarker("ready", Manifest) ) return false;
    for ( i = 0u; i < 7u; ++i ) {
        char Source[96];
        if ( (Roots & (1u << i)) == 0u ) continue;
        snprintf(Source, sizeof(Source), "%s/payload/%s", MDO_HOME_IMPORT_DIR,
            g_MdoHomeImportRoots[i]);
        if ( !xrtRootRenameNoReplace(g_MdoHome.Root, Source, g_MdoHomeImportRoots[i]) ) return false;
    }
    return MdoHomeImportMarker("committed", MDO_HOME_IMPORT_MAGIC);
}

bool MdoHomeImportEnd(MdoHomeImport* Import, bool Publish)
{
    bool Ok = false;
    if ( Import == NULL || !g_MdoHome.Initialized )
        return MdoHomeImportError("invalid Home import end request");
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Import != Import || !Import->Active ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        return MdoHomeImportError("Home import is not the current transaction");
    }
    if ( Publish ) Ok = MdoHomeImportPublishLocked();
    if ( Ok ) {
        g_MdoHome.RestartRequired = true;
        snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
            "Home import completed; restart mdo to use imported data");
    } else {
        xerror* Saved = xrtTakeError();
        bool WasCommitted = false;
        bool Recovered = MdoHomeImportRecoverLocked(&WasCommitted);
        if ( !Recovered ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "Home import recovery failed; restart is required before writing");
        }
        Ok = Recovered && (!Publish || WasCommitted);
        if ( WasCommitted ) {
            g_MdoHome.RestartRequired = true;
            snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message),
                "Home import completed; restart mdo to use imported data");
        }
        if ( Recovered ) {
            xrtClearError();
            if ( Saved != NULL ) xrtSetErrorTake(Saved);
        } else xrtErrorFree(Saved);
    }
    Import->Active = false;
    g_MdoHome.Import = NULL;
    xrtMutexUnlock(g_MdoHome.Lock);
    xrtFree(Import);
    return Ok;
}
