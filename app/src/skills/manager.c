#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/prompt_file.h"

#define MDO_SKILL_COUNT_LIMIT 256u
#define MDO_SKILL_DIAGNOSTIC_LIMIT 256u
#define MDO_SKILL_DOCUMENT_LIMIT (512u * 1024u)
#define MDO_SKILL_FRONTMATTER_LIMIT (64u * 1024u)
#define MDO_SKILL_RESOURCE_LIMIT (8u * 1024u * 1024u)
#define MDO_SKILL_DEPENDENCY_LIMIT 128u
#define MDO_SKILL_RESOURCE_COUNT_LIMIT 128u
#define MDO_SKILL_ID_LIMIT 96u
#define MDO_SKILL_NAME_LIMIT 256u
#define MDO_SKILL_DESCRIPTION_LIMIT 4096u
#define MDO_SKILL_SCALAR_LIMIT 1024u
#define MDO_SKILL_PATH_LIMIT 1024u
#define MDO_SKILL_ERROR_LIMIT 1024u

typedef struct MdoSkillStringList {
    char** Items;
    size_t Count;
    size_t Capacity;
} MdoSkillStringList;

typedef struct MdoSkillResource {
    MdoSkillResourceKind Kind;
    char* Path;
    size_t Bytes;
    xfileinfo Info;
    bytes CachedData;
} MdoSkillResource;

typedef struct MdoSkillEntry {
    bool External;
    bool AutoResources;
    char* Id;
    char* Name;
    char* Description;
    char* Version;
    char* License;
    char* Compatibility;
    char* SourcePath;
    char* VirtualDirectory;
    char* RelativeDirectory;
    char MetadataHash[65];
    xfileinfo DocumentInfo;
    size_t BodyOffset;
    size_t BodyBytes;
    size_t EstimatedTokens;
    char* CachedBody;
    MdoSkillStringList Tools;
    MdoSkillStringList Mcp;
    MdoSkillStringList Permissions;
    MdoSkillResource* Resources;
    size_t ResourceCount;
    size_t ResourceCapacity;
} MdoSkillEntry;

struct MdoSkillCatalog {
    xatomic32 Refs;
    xmutex* CacheLock;
    uint64 Generation;
    MdoSkillEntry* Entries;
    size_t Count;
    size_t Capacity;
};

typedef struct MdoSkillDiagnosticEntry {
    MdoSkillDiagnosticStage Stage;
    char* SkillId;
    char* SourcePath;
    char* Message;
} MdoSkillDiagnosticEntry;

struct MdoSkillDiagnostics {
    xatomic32 Refs;
    MdoSkillDiagnosticEntry* Entries;
    size_t Count;
    size_t Capacity;
};

typedef struct MdoSkillSource {
    bool External;
    char* Id;
    char* VirtualDirectory;
    char* RelativeDirectory;
} MdoSkillSource;

typedef struct MdoSkillState {
    xmutex* Lock;
    MdoSkillCatalog* Catalog;
    MdoSkillDiagnostics* Diagnostics;
    uint64 NextGeneration;
    bool Initialized;
} MdoSkillState;

typedef enum MdoSkillLoadStatus {
    MDO_SKILL_LOAD_OK = 0,
    MDO_SKILL_LOAD_INVALID,
    MDO_SKILL_LOAD_FATAL
} MdoSkillLoadStatus;

typedef enum MdoSkillField {
    MDO_SKILL_FIELD_NONE = 0,
    MDO_SKILL_FIELD_NAME,
    MDO_SKILL_FIELD_DESCRIPTION,
    MDO_SKILL_FIELD_VERSION,
    MDO_SKILL_FIELD_LICENSE,
    MDO_SKILL_FIELD_COMPATIBILITY,
    MDO_SKILL_FIELD_TOOLS,
    MDO_SKILL_FIELD_MCP,
    MDO_SKILL_FIELD_PERMISSIONS,
    MDO_SKILL_FIELD_SCRIPTS,
    MDO_SKILL_FIELD_TEMPLATES,
    MDO_SKILL_FIELD_ASSETS,
    MDO_SKILL_FIELD_COUNT
} MdoSkillField;

static MdoSkillState g_MdoSkills;

static void MdoSkillsSetError(cstr Message)
{
    xerror* pError = xrtErrorCreate(XERR_STATE, "mdo.skills", 1,
        Message != NULL ? Message : "Skill operation failed");
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static void MdoSkillsCopyError(char* Target, size_t Capacity, cstr Message)
{
    if ( Target == NULL || Capacity == 0u ) return;
    snprintf(Target, Capacity, "%s", Message != NULL ? Message :
        "Skill operation failed");
}

static bool MdoSkillsMemoryError(void)
{
    const xerror* pError = xrtGetError();
    return pError != NULL && xrtErrorKind(pError) == XERR_MEMORY;
}

static bool MdoSkillsGrow(void** ppItems, size_t* pCapacity,
    size_t Count, size_t ItemSize, size_t Limit)
{
    size_t Capacity;
    void* pItems;

    if ( Count > Limit || ItemSize == 0u ) return false;
    if ( Count <= *pCapacity ) return true;
    Capacity = *pCapacity != 0u ? *pCapacity : 4u;
    while ( Capacity < Count ) {
        if ( Capacity > Limit / 2u ) {
            Capacity = Limit;
            break;
        }
        Capacity *= 2u;
    }
    if ( Capacity < Count || Capacity > SIZE_MAX / ItemSize ) return false;
    pItems = xrtRealloc(*ppItems, Capacity * ItemSize);
    if ( pItems == NULL ) return false;
    *ppItems = pItems;
    *pCapacity = Capacity;
    return true;
}

static bool MdoSkillsIdValid(cstr Id)
{
    size_t i = 0u;
    if ( Id == NULL || Id[0] < 'a' || Id[0] > 'z' ) return false;
    while ( Id[i] != '\0' ) {
        unsigned char c = (unsigned char)Id[i];
        if ( i >= MDO_SKILL_ID_LIMIT ||
             !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '-') ) return false;
        i++;
    }
    return i != 0u;
}

static bool MdoSkillsViewEqual(xstrview View, cstr Text)
{
    size_t Length = strlen(Text);
    return View.Size == Length && memcmp(View.Data, Text, Length) == 0;
}

static bool MdoSkillsContainsByte(const void* Data, size_t Size,
    unsigned char Value)
{
    const unsigned char* pBytes = (const unsigned char*)Data;
    size_t i;
    for ( i = 0u; i < Size; ++i ) {
        if ( pBytes[i] == Value ) return true;
    }
    return false;
}

static xstrview MdoSkillsTrim(xstrview View)
{
    while ( View.Size != 0u &&
            (View.Data[0] == ' ' || View.Data[0] == '\r') ) {
        View.Data++;
        View.Size--;
    }
    while ( View.Size != 0u &&
            (View.Data[View.Size - 1u] == ' ' ||
             View.Data[View.Size - 1u] == '\r') ) View.Size--;
    return View;
}

static inline xstrview MdoSkillsTrimRight(xstrview View)
{
    while ( View.Size != 0u &&
            (View.Data[View.Size - 1u] == ' ' ||
             View.Data[View.Size - 1u] == '\r') ) View.Size--;
    return View;
}

static bool MdoSkillsCopyView(char** pTarget, xstrview View, size_t Limit,
    bool Required)
{
    *pTarget = NULL;
    if ( View.Size == 0u ) return !Required;
    if ( View.Size > Limit || !xrtUtf8Valid(View, NULL) ) return false;
    *pTarget = xrtStrDupN(View.Data, View.Size);
    return *pTarget != NULL;
}

static void MdoSkillsStringListUnit(MdoSkillStringList* pList)
{
    size_t i;
    if ( pList == NULL ) return;
    for ( i = 0u; i < pList->Count; ++i ) xrtFree(pList->Items[i]);
    xrtFree(pList->Items);
    memset(pList, 0, sizeof(*pList));
}

static bool MdoSkillsStringListAdd(MdoSkillStringList* pList, char* Value)
{
    size_t i;
    for ( i = 0u; i < pList->Count; ++i ) {
        if ( strcmp(pList->Items[i], Value) == 0 ) return false;
    }
    if ( !MdoSkillsGrow((void**)&pList->Items, &pList->Capacity,
            pList->Count + 1u, sizeof(*pList->Items),
            MDO_SKILL_DEPENDENCY_LIMIT) ) return false;
    pList->Items[pList->Count++] = Value;
    return true;
}

static void MdoSkillsResourceUnit(MdoSkillResource* pResource)
{
    if ( pResource == NULL ) return;
    xrtFree(pResource->Path);
    xrtFree(pResource->CachedData);
    memset(pResource, 0, sizeof(*pResource));
}

static void MdoSkillsEntryUnit(MdoSkillEntry* pEntry)
{
    size_t i;
    if ( pEntry == NULL ) return;
    for ( i = 0u; i < pEntry->ResourceCount; ++i )
        MdoSkillsResourceUnit(&pEntry->Resources[i]);
    xrtFree(pEntry->Resources);
    MdoSkillsStringListUnit(&pEntry->Tools);
    MdoSkillsStringListUnit(&pEntry->Mcp);
    MdoSkillsStringListUnit(&pEntry->Permissions);
    xrtFree(pEntry->Id);
    xrtFree(pEntry->Name);
    xrtFree(pEntry->Description);
    xrtFree(pEntry->Version);
    xrtFree(pEntry->License);
    xrtFree(pEntry->Compatibility);
    xrtFree(pEntry->SourcePath);
    xrtFree(pEntry->VirtualDirectory);
    xrtFree(pEntry->RelativeDirectory);
    xrtFree(pEntry->CachedBody);
    memset(pEntry, 0, sizeof(*pEntry));
}

static MdoSkillCatalog* MdoSkillsCatalogCreate(uint64 Generation)
{
    MdoSkillCatalog* pCatalog = (MdoSkillCatalog*)xrtCalloc(1u,
        sizeof(*pCatalog));
    if ( pCatalog == NULL ) return NULL;
    pCatalog->CacheLock = xrtMutexCreate();
    if ( pCatalog->CacheLock == NULL ) {
        xrtFree(pCatalog);
        return NULL;
    }
    xrtAtomic32Init(&pCatalog->Refs, 1u);
    pCatalog->Generation = Generation;
    return pCatalog;
}

MdoSkillCatalog* MdoSkillCatalogRef(MdoSkillCatalog* pCatalog)
{
    uint32 Refs;
    if ( pCatalog == NULL ) return NULL;
    Refs = xrtAtomic32Load(&pCatalog->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&pCatalog->Refs, &Expected,
                Refs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) )
            return pCatalog;
        Refs = Expected;
    }
}

void MdoSkillCatalogRelease(MdoSkillCatalog* pCatalog)
{
    uint32 Previous;
    size_t i;
    if ( pCatalog == NULL ) return;
    Previous = xrtAtomic32FetchSub(&pCatalog->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < pCatalog->Count; ++i )
        MdoSkillsEntryUnit(&pCatalog->Entries[i]);
    xrtFree(pCatalog->Entries);
    (void)xrtMutexDestroy(pCatalog->CacheLock);
    xrtFree(pCatalog);
}

static MdoSkillDiagnostics* MdoSkillsDiagnosticsCreate(void)
{
    MdoSkillDiagnostics* pDiagnostics = (MdoSkillDiagnostics*)xrtCalloc(
        1u, sizeof(*pDiagnostics));
    if ( pDiagnostics != NULL ) xrtAtomic32Init(&pDiagnostics->Refs, 1u);
    return pDiagnostics;
}

MdoSkillDiagnostics* MdoSkillDiagnosticsRef(
    MdoSkillDiagnostics* pDiagnostics)
{
    uint32 Refs;
    if ( pDiagnostics == NULL ) return NULL;
    Refs = xrtAtomic32Load(&pDiagnostics->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&pDiagnostics->Refs, &Expected,
                Refs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) )
            return pDiagnostics;
        Refs = Expected;
    }
}

void MdoSkillDiagnosticsRelease(MdoSkillDiagnostics* pDiagnostics)
{
    uint32 Previous;
    size_t i;
    if ( pDiagnostics == NULL ) return;
    Previous = xrtAtomic32FetchSub(&pDiagnostics->Refs, 1u,
        XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < pDiagnostics->Count; ++i ) {
        xrtFree(pDiagnostics->Entries[i].SkillId);
        xrtFree(pDiagnostics->Entries[i].SourcePath);
        xrtFree(pDiagnostics->Entries[i].Message);
    }
    xrtFree(pDiagnostics->Entries);
    xrtFree(pDiagnostics);
}

static bool MdoSkillsDiagnosticAdd(MdoSkillDiagnostics* pDiagnostics,
    MdoSkillDiagnosticStage Stage, cstr SkillId, cstr SourcePath, cstr Message)
{
    MdoSkillDiagnosticEntry* pEntry;
    if ( pDiagnostics == NULL ||
         !MdoSkillsGrow((void**)&pDiagnostics->Entries,
            &pDiagnostics->Capacity, pDiagnostics->Count + 1u,
            sizeof(*pDiagnostics->Entries), MDO_SKILL_DIAGNOSTIC_LIMIT) )
        return false;
    pEntry = &pDiagnostics->Entries[pDiagnostics->Count];
    memset(pEntry, 0, sizeof(*pEntry));
    if ( SkillId != NULL ) pEntry->SkillId = xrtStrDup(SkillId);
    if ( SourcePath != NULL ) pEntry->SourcePath = xrtStrDup(SourcePath);
    pEntry->Message = xrtStrDup(Message != NULL ? Message :
        "Skill validation failed");
    if ( (SkillId != NULL && pEntry->SkillId == NULL) ||
         (SourcePath != NULL && pEntry->SourcePath == NULL) ||
         pEntry->Message == NULL ) {
        xrtFree(pEntry->SkillId);
        xrtFree(pEntry->SourcePath);
        xrtFree(pEntry->Message);
        memset(pEntry, 0, sizeof(*pEntry));
        return false;
    }
    pEntry->Stage = Stage;
    pDiagnostics->Count++;
    return true;
}

static void MdoSkillsSourceUnit(MdoSkillSource* pSource)
{
    if ( pSource == NULL ) return;
    xrtFree(pSource->Id);
    xrtFree(pSource->VirtualDirectory);
    xrtFree(pSource->RelativeDirectory);
    memset(pSource, 0, sizeof(*pSource));
}

static void MdoSkillsSourcesUnit(MdoSkillSource* pSources, size_t Count)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) MdoSkillsSourceUnit(&pSources[i]);
    xrtFree(pSources);
}

static int MdoSkillsSourceCompare(const void* Left, const void* Right)
{
    return strcmp(((const MdoSkillSource*)Left)->Id,
        ((const MdoSkillSource*)Right)->Id);
}

static bool MdoSkillsJoin(char** pOutput, cstr Left, cstr Right)
{
    size_t A = strlen(Left);
    size_t B = strlen(Right);
    if ( A > SIZE_MAX - B - 2u ) return false;
    *pOutput = (char*)xrtMalloc(A + B + 2u);
    if ( *pOutput == NULL ) return false;
    snprintf(*pOutput, A + B + 2u, "%s/%s", Left, Right);
    return true;
}

static bool MdoSkillsDiscover(MdoSkillSource** ppSources, size_t* pCount,
    MdoSkillDiagnostics* pDiagnostics)
{
    static const char* VirtualRoot = "/app/default-home/skills";
    static const char* RelativeRoot = "skills";
    MdoSkillSource* pSources = NULL;
    size_t Count = 0u;
    size_t Capacity = 0u;
    xdir Dir = xrtVfsDirOpen(xsApplicationVfs(), VirtualRoot, XDIR_STAT);
    xdirentry Entry;
    xdirnext Next;
    char* Relative = NULL;
    char* Virtual = NULL;
    char* Id = NULL;

    *ppSources = NULL;
    *pCount = 0u;
    if ( Dir == NULL ) {
        const xerror* pError = xrtGetError();
        if ( pError != NULL && xrtErrorKind(pError) == XERR_NOT_FOUND ) {
            xrtClearError();
            return true;
        }
        (void)MdoSkillsDiagnosticAdd(pDiagnostics,
            MDO_SKILL_DIAGNOSTIC_DISCOVERY, NULL, VirtualRoot,
            pError != NULL ? xrtErrorMessage(pError) :
            "Skill directory cannot be opened");
        return false;
    }
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Dir, &Entry)) == XDIR_NEXT_ITEM ) {
        MdoSkillSource Source;
        xfileinfo ExternalInfo;
        bool External = false;
        bool Exists = false;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) continue;
        Relative = NULL;
        Virtual = NULL;
        Id = NULL;
        Id = xrtStrDupN(Entry.Name.Data, Entry.Name.Size);
        if ( Id == NULL ) goto fatal;
        if ( !MdoSkillsJoin(&Relative, RelativeRoot, Id) ||
             !MdoSkillsJoin(&Virtual, VirtualRoot, Id) ) goto fatal;
        memset(&ExternalInfo, 0, sizeof(ExternalInfo));
        if ( !MdoHomeExternalStat(Relative, &Exists, &ExternalInfo) )
            goto fatal;
        External = Exists;
        if ( Entry.Info.Type != XFILE_TYPE_DIRECTORY ) {
            if ( External && MdoSkillsIdValid(Id) &&
                 !MdoSkillsDiagnosticAdd(pDiagnostics,
                    MDO_SKILL_DIAGNOSTIC_DISCOVERY, Id, Virtual,
                    "external Skill path is not a directory") ) goto fatal;
            xrtFree(Id); xrtFree(Relative); xrtFree(Virtual);
            Id = NULL; Relative = NULL; Virtual = NULL;
            continue;
        }
        if ( !MdoSkillsIdValid(Id) ) {
            if ( !MdoSkillsDiagnosticAdd(pDiagnostics,
                    MDO_SKILL_DIAGNOSTIC_DISCOVERY, Id, Virtual,
                    "Skill directory has an invalid ID") ) goto fatal;
            xrtFree(Id); xrtFree(Relative); xrtFree(Virtual);
            Id = NULL; Relative = NULL; Virtual = NULL;
            continue;
        }
        if ( External && ExternalInfo.Type != XFILE_TYPE_DIRECTORY ) {
            if ( !MdoSkillsDiagnosticAdd(pDiagnostics,
                    MDO_SKILL_DIAGNOSTIC_DISCOVERY, Id, Virtual,
                    "external Skill shadow is not a directory") ) goto fatal;
            xrtFree(Id); xrtFree(Relative); xrtFree(Virtual);
            Id = NULL; Relative = NULL; Virtual = NULL;
            continue;
        }
        if ( !MdoSkillsGrow((void**)&pSources, &Capacity, Count + 1u,
                sizeof(*pSources), MDO_SKILL_COUNT_LIMIT) ) goto fatal;
        memset(&Source, 0, sizeof(Source));
        Source.External = External;
        Source.Id = Id;
        Source.VirtualDirectory = Virtual;
        Source.RelativeDirectory = Relative;
        pSources[Count++] = Source;
        Id = NULL; Relative = NULL; Virtual = NULL;
    }
    if ( Next == XDIR_NEXT_ERROR ) {
        (void)xrtDirClose(Dir);
        goto fatal_closed;
    }
    if ( !xrtDirClose(Dir) ) goto fatal_closed;
    if ( Count > 1u ) qsort(pSources, Count, sizeof(*pSources),
        MdoSkillsSourceCompare);
    *ppSources = pSources;
    *pCount = Count;
    return true;

fatal:
    xrtFree(Id);
    xrtFree(Relative);
    xrtFree(Virtual);
    (void)xrtDirClose(Dir);
fatal_closed:
    (void)MdoSkillsDiagnosticAdd(pDiagnostics,
        MDO_SKILL_DIAGNOSTIC_DISCOVERY, NULL, VirtualRoot,
        "Skill discovery failed due to I/O, capacity, or memory limits");
    MdoSkillsSourcesUnit(pSources, Count);
    return false;
}

static xfile MdoSkillsOpen(const MdoSkillSource* pSource, cstr Path,
    char** pVirtualPath)
{
    char* Virtual = NULL;
    char* Relative = NULL;
    xfile File = NULL;
    xfileoptions Options;

    *pVirtualPath = NULL;
    if ( !MdoSkillsJoin(&Virtual, pSource->VirtualDirectory, Path) ||
         !MdoSkillsJoin(&Relative, pSource->RelativeDirectory, Path) )
        goto done;
    if ( pSource->External ) {
        File = MdoHomeOpenRead(Relative);
    } else {
        xrtFileOptionsInit(&Options);
        Options.Flags = XFILE_READ;
        File = xrtVfsOpen(xsApplicationVfs(), Virtual, &Options);
    }
    *pVirtualPath = Virtual;
    Virtual = NULL;
done:
    xrtFree(Virtual);
    xrtFree(Relative);
    return File;
}

static xfile MdoSkillsOpenEntry(const MdoSkillEntry* pEntry, cstr Path,
    char** pVirtualPath)
{
    MdoSkillSource Source;
    memset(&Source, 0, sizeof(Source));
    Source.External = pEntry->External;
    Source.Id = pEntry->Id;
    Source.VirtualDirectory = pEntry->VirtualDirectory;
    Source.RelativeDirectory = pEntry->RelativeDirectory;
    return MdoSkillsOpen(&Source, Path, pVirtualPath);
}

static bool MdoSkillsSameFile(const xfileinfo* pExpected,
    const xfileinfo* pActual)
{
    uint32 Common;
    if ( pExpected->Type != XFILE_TYPE_FILE ||
         pActual->Type != XFILE_TYPE_FILE ||
         (pExpected->Available & XFILE_INFO_SIZE) == 0u ||
         (pActual->Available & XFILE_INFO_SIZE) == 0u ||
         pExpected->Size != pActual->Size ) return false;
    Common = pExpected->Available & pActual->Available;
    if ( (Common & XFILE_INFO_IDENTITY) != 0u &&
         (pExpected->Device != pActual->Device ||
          pExpected->Identity != pActual->Identity) ) return false;
    if ( (Common & XFILE_INFO_CHANGE_TIME) != 0u &&
         pExpected->Changed != pActual->Changed ) return false;
    if ( (Common & XFILE_INFO_MODIFY_TIME) != 0u &&
         pExpected->Modified != pActual->Modified ) return false;
    return true;
}

static bool MdoSkillsLineMarker(const unsigned char* pBytes,
    size_t Start, size_t End)
{
    if ( End > Start && pBytes[End - 1u] == '\r' ) End--;
    return End - Start == 3u &&
        pBytes[Start] == '-' && pBytes[Start + 1u] == '-' &&
        pBytes[Start + 2u] == '-';
}

static bool MdoSkillsReadFrontmatter(xfile File, size_t FileBytes,
    bytes* ppHeader, size_t* pHeaderBytes, size_t* pFrontStart,
    size_t* pFrontEnd, size_t* pBodyOffset, char* Error,
    size_t ErrorCapacity)
{
    bytes pHeader = NULL;
    size_t Capacity = 0u;
    size_t Count = 0u;
    size_t LineStart = 0u;
    size_t LineNumber = 0u;
    size_t Position;
    bool Closed = false;

    *ppHeader = NULL;
    for ( Position = 0u; Position < FileBytes &&
            Position < MDO_SKILL_FRONTMATTER_LIMIT; ++Position ) {
        unsigned char Byte;
        size_t Read = 0u;
        if ( !MdoSkillsGrow((void**)&pHeader, &Capacity, Count + 1u,
                sizeof(*pHeader), MDO_SKILL_FRONTMATTER_LIMIT) ||
             !xrtReadAt(File, Position, &Byte, 1u, &Read) || Read != 1u ) {
            MdoSkillsCopyError(Error, ErrorCapacity,
                "Skill front matter cannot be read");
            xrtFree(pHeader);
            return false;
        }
        pHeader[Count++] = Byte;
        if ( Byte != '\n' ) continue;
        if ( LineNumber == 0u ) {
            if ( !MdoSkillsLineMarker(pHeader, LineStart, Count - 1u) ) {
                MdoSkillsCopyError(Error, ErrorCapacity,
                    "SKILL.md must start with a --- line");
                xrtFree(pHeader);
                return false;
            }
            *pFrontStart = Count;
        } else if ( MdoSkillsLineMarker(pHeader, LineStart, Count - 1u) ) {
            *pFrontEnd = LineStart;
            *pBodyOffset = Count;
            Closed = true;
            break;
        }
        LineStart = Count;
        LineNumber++;
    }
    if ( !Closed && Position == FileBytes && Count > LineStart ) {
        if ( LineNumber != 0u &&
             MdoSkillsLineMarker(pHeader, LineStart, Count) ) {
            *pFrontEnd = LineStart;
            *pBodyOffset = Count;
            Closed = true;
        }
    }
    if ( !Closed ) {
        MdoSkillsCopyError(Error, ErrorCapacity,
            "Skill front matter is missing a closing --- line or exceeds 64 KiB");
        xrtFree(pHeader);
        return false;
    }
    if ( !xrtUtf8Valid((xstrview){ (const char*)pHeader, Count }, NULL) ) {
        MdoSkillsCopyError(Error, ErrorCapacity,
            "Skill front matter is not valid UTF-8");
        xrtFree(pHeader);
        return false;
    }
    *ppHeader = pHeader;
    *pHeaderBytes = Count;
    return true;
}

static bool MdoSkillsSingleQuoted(xstrview Input, char** pOutput)
{
    char* Output;
    size_t i;
    size_t Out = 0u;
    if ( Input.Size < 2u || Input.Data[0] != '\'' ||
         Input.Data[Input.Size - 1u] != '\'' ) return false;
    Output = (char*)xrtMalloc(Input.Size);
    if ( Output == NULL ) return false;
    for ( i = 1u; i + 1u < Input.Size; ++i ) {
        if ( Input.Data[i] == '\'' ) {
            if ( i + 2u >= Input.Size || Input.Data[i + 1u] != '\'' ) {
                xrtFree(Output);
                return false;
            }
            i++;
        }
        Output[Out++] = Input.Data[i];
    }
    Output[Out] = '\0';
    *pOutput = Output;
    return true;
}

static bool MdoSkillsScalar(xstrview Input, size_t Limit, char** pOutput)
{
    xvalue* pValue = NULL;
    xstrview String;
    bool Ok = false;

    *pOutput = NULL;
    Input = MdoSkillsTrim(Input);
    if ( Input.Size == 0u ) return false;
    if ( Input.Data[0] == '"' ) {
        pValue = xrtJsonParse(Input);
        if ( pValue != NULL && xrtValueType(pValue) == XVALUE_STRING &&
             xrtValueGetString(pValue, &String) )
            Ok = MdoSkillsCopyView(pOutput, String, Limit, true);
        if ( pValue != NULL ) xrtValueRelease(pValue);
        return Ok;
    }
    if ( Input.Data[0] == '\'' ) {
        if ( !MdoSkillsSingleQuoted(Input, pOutput) ) return false;
        if ( strlen(*pOutput) > Limit ||
             !xrtUtf8Valid((xstrview){ *pOutput, strlen(*pOutput) }, NULL) ) {
            xrtFree(*pOutput);
            *pOutput = NULL;
            return false;
        }
        return (*pOutput)[0] != '\0';
    }
    if ( Input.Data[0] == '[' || Input.Data[0] == '{' ||
         Input.Data[0] == '&' || Input.Data[0] == '*' ||
         Input.Data[0] == '!' || Input.Data[0] == '|' ||
         Input.Data[0] == '>' || Input.Data[0] == '@' ||
         Input.Data[0] == '`' ) return false;
    return MdoSkillsCopyView(pOutput, Input, Limit, true);
}

static MdoSkillField MdoSkillsField(xstrview Key)
{
    if ( MdoSkillsViewEqual(Key, "name") ) return MDO_SKILL_FIELD_NAME;
    if ( MdoSkillsViewEqual(Key, "description") )
        return MDO_SKILL_FIELD_DESCRIPTION;
    if ( MdoSkillsViewEqual(Key, "version") ) return MDO_SKILL_FIELD_VERSION;
    if ( MdoSkillsViewEqual(Key, "license") ) return MDO_SKILL_FIELD_LICENSE;
    if ( MdoSkillsViewEqual(Key, "compatibility") )
        return MDO_SKILL_FIELD_COMPATIBILITY;
    if ( MdoSkillsViewEqual(Key, "tools") ) return MDO_SKILL_FIELD_TOOLS;
    if ( MdoSkillsViewEqual(Key, "mcp") ) return MDO_SKILL_FIELD_MCP;
    if ( MdoSkillsViewEqual(Key, "permissions") )
        return MDO_SKILL_FIELD_PERMISSIONS;
    if ( MdoSkillsViewEqual(Key, "scripts") ) return MDO_SKILL_FIELD_SCRIPTS;
    if ( MdoSkillsViewEqual(Key, "templates") )
        return MDO_SKILL_FIELD_TEMPLATES;
    if ( MdoSkillsViewEqual(Key, "assets") ) return MDO_SKILL_FIELD_ASSETS;
    return MDO_SKILL_FIELD_NONE;
}

static bool MdoSkillsPathValid(cstr Path, MdoSkillResourceKind Kind)
{
    cstr Prefix = Kind == MDO_SKILL_RESOURCE_REFERENCE ? "" : Kind == MDO_SKILL_RESOURCE_SCRIPT ? "scripts/" :
        (Kind == MDO_SKILL_RESOURCE_TEMPLATE ? "templates/" : "assets/");
    size_t PrefixLength = strlen(Prefix);
    size_t Length;
    size_t Start;
    size_t i;

    if ( Path == NULL || strncmp(Path, Prefix, PrefixLength) != 0 )
        return false;
    Length = strlen(Path);
    if ( Length <= PrefixLength || Length > MDO_SKILL_PATH_LIMIT ||
         Path[Length - 1u] == '/' ||
         !xrtUtf8Valid((xstrview){ Path, Length }, NULL) ) return false;
    Start = 0u;
    for ( i = 0u; i <= Length; ++i ) {
        unsigned char c = (unsigned char)Path[i];
        if ( c == '\\' || c == ':' || (i < Length && c < 0x20u) )
            return false;
        if ( c != '/' && c != '\0' ) continue;
        if ( i == Start || (i - Start == 1u && Path[Start] == '.') ||
             (i - Start == 2u && Path[Start] == '.' &&
              Path[Start + 1u] == '.') ) return false;
        Start = i + 1u;
    }
    return true;
}

static bool MdoSkillsResourceAdd(MdoSkillEntry* pEntry,
    MdoSkillResourceKind Kind, char* Path)
{
    size_t i;
    for ( i = 0u; i < pEntry->ResourceCount; ++i ) {
        if ( strcmp(pEntry->Resources[i].Path, Path) == 0 ) return false;
    }
    if ( !MdoSkillsGrow((void**)&pEntry->Resources,
            &pEntry->ResourceCapacity, pEntry->ResourceCount + 1u,
            sizeof(*pEntry->Resources), MDO_SKILL_RESOURCE_COUNT_LIMIT) )
        return false;
    memset(&pEntry->Resources[pEntry->ResourceCount], 0,
        sizeof(*pEntry->Resources));
    pEntry->Resources[pEntry->ResourceCount].Kind = Kind;
    pEntry->Resources[pEntry->ResourceCount].Path = Path;
    pEntry->ResourceCount++;
    return true;
}

static bool MdoSkillsListValue(MdoSkillEntry* pEntry,
    MdoSkillField Field, xstrview Input, char* Error, size_t ErrorCapacity)
{
    MdoSkillStringList* pList = NULL;
    MdoSkillResourceKind Kind = 0;
    char* Value = NULL;
    size_t Limit = MDO_SKILL_ID_LIMIT;

    if ( Field == MDO_SKILL_FIELD_TOOLS ) pList = &pEntry->Tools;
    else if ( Field == MDO_SKILL_FIELD_MCP ) pList = &pEntry->Mcp;
    else if ( Field == MDO_SKILL_FIELD_PERMISSIONS )
        pList = &pEntry->Permissions;
    else if ( Field == MDO_SKILL_FIELD_SCRIPTS ) {
        Kind = MDO_SKILL_RESOURCE_SCRIPT; Limit = MDO_SKILL_PATH_LIMIT;
    } else if ( Field == MDO_SKILL_FIELD_TEMPLATES ) {
        Kind = MDO_SKILL_RESOURCE_TEMPLATE; Limit = MDO_SKILL_PATH_LIMIT;
    } else if ( Field == MDO_SKILL_FIELD_ASSETS ) {
        Kind = MDO_SKILL_RESOURCE_ASSET; Limit = MDO_SKILL_PATH_LIMIT;
    } else return false;
    if ( !MdoSkillsScalar(Input, Limit, &Value) ) {
        MdoSkillsCopyError(Error, ErrorCapacity,
            "Skill list contains an invalid scalar value");
        return false;
    }
    if ( pList != NULL ) {
        if ( !MdoSkillsIdValid(Value) ||
             !MdoSkillsStringListAdd(pList, Value) ) {
            xrtFree(Value);
            MdoSkillsCopyError(Error, ErrorCapacity,
                "Skill dependency list contains an invalid or duplicate ID");
            return false;
        }
    } else {
        if ( !MdoSkillsPathValid(Value, Kind) ||
             !MdoSkillsResourceAdd(pEntry, Kind, Value) ) {
            xrtFree(Value);
            MdoSkillsCopyError(Error, ErrorCapacity,
                "Skill resource list contains an unsafe or duplicate path");
            return false;
        }
    }
    return true;
}

static bool MdoSkillsInlineList(MdoSkillEntry* pEntry,
    MdoSkillField Field, xstrview Input, char* Error, size_t ErrorCapacity)
{
    size_t Start;
    size_t i;
    char Quote = '\0';
    bool Escape = false;
    bool SawValue = false;

    Input = MdoSkillsTrim(Input);
    if ( Input.Size < 2u || Input.Data[0] != '[' ||
         Input.Data[Input.Size - 1u] != ']' ) return false;
    Input.Data++;
    Input.Size -= 2u;
    if ( MdoSkillsTrim(Input).Size == 0u ) return true;
    Start = 0u;
    for ( i = 0u; i <= Input.Size; ++i ) {
        char c = i < Input.Size ? Input.Data[i] : ',';
        if ( Quote != '\0' ) {
            if ( Quote == '"' && Escape ) { Escape = false; continue; }
            if ( Quote == '"' && c == '\\' ) { Escape = true; continue; }
            if ( c == Quote ) Quote = '\0';
            continue;
        }
        if ( c == '"' || c == '\'' ) { Quote = c; continue; }
        if ( c != ',' ) continue;
        {
            xstrview Item = { Input.Data + Start, i - Start };
            Item = MdoSkillsTrim(Item);
            if ( Item.Size == 0u ||
                 !MdoSkillsListValue(pEntry, Field, Item,
                    Error, ErrorCapacity) ) return false;
            SawValue = true;
        }
        Start = i + 1u;
    }
    if ( Quote != '\0' || Escape || !SawValue ) {
        MdoSkillsCopyError(Error, ErrorCapacity,
            "Skill inline list has invalid quoting");
        return false;
    }
    return true;
}

static bool MdoSkillsScalarField(MdoSkillEntry* pEntry,
    MdoSkillField Field, xstrview Value)
{
    char** pTarget;
    size_t Limit;
    if ( Field == MDO_SKILL_FIELD_NAME ) {
        pTarget = &pEntry->Name; Limit = MDO_SKILL_NAME_LIMIT;
    } else if ( Field == MDO_SKILL_FIELD_DESCRIPTION ) {
        pTarget = &pEntry->Description; Limit = MDO_SKILL_DESCRIPTION_LIMIT;
    } else if ( Field == MDO_SKILL_FIELD_VERSION ) {
        pTarget = &pEntry->Version; Limit = MDO_SKILL_SCALAR_LIMIT;
    } else if ( Field == MDO_SKILL_FIELD_LICENSE ) {
        pTarget = &pEntry->License; Limit = MDO_SKILL_SCALAR_LIMIT;
    } else if ( Field == MDO_SKILL_FIELD_COMPATIBILITY ) {
        pTarget = &pEntry->Compatibility; Limit = MDO_SKILL_SCALAR_LIMIT;
    } else return false;
    return MdoSkillsScalar(Value, Limit, pTarget);
}

static bool MdoSkillsParseMetadata(MdoSkillEntry* Entry, const xvalue* Document,
    char* Error, size_t Capacity)
{
    static const char* const Keys[] = {
        "name", "description", "version", "license", "compatibility",
        "tools", "mcp", "permissions", "scripts", "templates", "assets"
    };
    size_t i;
    Entry->AutoResources = xrtValueObjectGet(Document, XRT_STR_LITERAL("scripts")) == NULL &&
        xrtValueObjectGet(Document, XRT_STR_LITERAL("templates")) == NULL &&
        xrtValueObjectGet(Document, XRT_STR_LITERAL("assets")) == NULL;
    for (i = 0u; i < sizeof(Keys) / sizeof(Keys[0]); ++i) {
        const xvalue* Value = xrtValueObjectGet(Document, xrtStrView(Keys[i]));
        MdoSkillField Field = MdoSkillsField(xrtStrView(Keys[i]));
        char* Json;
        bool Ok;
        if (Value == NULL) continue;
        Json = xrtJsonStringify(Value, false, NULL);
        if (Json == NULL) return false;
        if (Field <= MDO_SKILL_FIELD_COMPATIBILITY) {
            Ok = xrtValueType(Value) == XVALUE_STRING &&
                MdoSkillsScalarField(Entry, Field, xrtStrView(Json));
        } else {
            xstrview String;
            Ok = xrtValueGetString(Value, &String)
                ? MdoSkillsInlineList(Entry, Field, String, Error, Capacity)
                : MdoSkillsInlineList(Entry, Field, xrtStrView(Json), Error, Capacity);
        }
        xrtFree(Json);
        if (!Ok) {
            MdoSkillsCopyError(Error, Capacity, "Invalid Skill metadata field or resource path");
            return false;
        }
    }
    if (Entry->Name == NULL || Entry->Description == NULL) {
        MdoSkillsCopyError(Error, Capacity, "Skill requires name and description");
        return false;
    }
    return true;
}

static bool MdoSkillsParseFrontmatter(MdoSkillEntry* Entry,
    const unsigned char* Bytes, size_t Start, size_t End, char* Error, size_t Capacity)
{
    size_t Length = End - Start;
    char* Text = (char*)xrtMalloc(Length + 10u);
    xvalue* Document;
    bool Ok;
    if (Text == NULL) return false;
    memcpy(Text, "---\n", 4u); memcpy(Text + 4u, Bytes + Start, Length);
    memcpy(Text + Length + 4u, "---\n", 5u);
    Document = MdoPromptParse(Text, true, Error, Capacity);
    xrtFree(Text);
    Ok = Document != NULL && MdoSkillsParseMetadata(Entry, Document, Error, Capacity);
    xrtValueRelease(Document);
    return Ok;
}

bool MdoSkillValidateText(cstr Text, char* Error, size_t Capacity)
{
    xvalue* Document = MdoPromptParse(Text, true, Error, Capacity);
    MdoSkillEntry Entry;
    bool Ok;
    memset(&Entry, 0, sizeof(Entry));
    Ok = Document != NULL && MdoSkillsParseMetadata(&Entry, Document, Error, Capacity);
    xrtValueRelease(Document); MdoSkillsEntryUnit(&Entry);
    return Ok;
}

static bool MdoSkillsHash(const void* Data, size_t Size, char Output[65])
{
    static const char Hex[] = "0123456789abcdef";
    unsigned char Digest[32];
    size_t i;
    if ( !xrtSha256(Data, Size, Digest) ) return false;
    for ( i = 0u; i < sizeof(Digest); ++i ) {
        Output[i * 2u] = Hex[Digest[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Digest[i] & 0x0fu];
    }
    Output[64] = '\0';
    return true;
}

/* Standard Skills need no resource manifest. Discover names and stat only;
 * contents remain lazy and links are never followed. Legacy manifests stay exact. */
static bool MdoSkillsScanResources(const MdoSkillSource* Source,
    MdoSkillEntry* Entry, cstr Prefix, unsigned Depth)
{
    char Directory[MDO_SKILL_PATH_LIMIT + 128u];
    xdir Dir;
    xdirentry Item;
    xdirnext Next;
    bool Ok = true;
    if (Depth > 8u) return false;
    snprintf(Directory, sizeof(Directory), "%s%s%s", Source->External ?
        Source->RelativeDirectory : Source->VirtualDirectory,
        Prefix[0] ? "/" : "", Prefix);
    Dir = Source->External ? MdoHomeOpenDirectory(Directory, XDIR_STAT) :
        xrtVfsDirOpen(xsApplicationVfs(), Directory, XDIR_STAT);
    if (Dir == NULL) return false;
    while ((Next = xrtDirNext(Dir, &Item)) == XDIR_NEXT_ITEM) {
        char Path[MDO_SKILL_PATH_LIMIT + 1u];
        int Length;
        MdoSkillResourceKind Kind;
        char* Owned;
        if (!(Item.Flags & XDIR_ENTRY_UTF8) || Item.Name.Size == 0u ||
            Item.Name.Data[0] == '.') continue;
        Length = snprintf(Path, sizeof(Path), "%s%s%.*s", Prefix,
            Prefix[0] ? "/" : "", (int)Item.Name.Size, Item.Name.Data);
        if (Length < 0 || (size_t)Length >= sizeof(Path)) { Ok = false; break; }
        if (Item.Info.Type == XFILE_TYPE_DIRECTORY) {
            if (!MdoSkillsScanResources(Source, Entry, Path, Depth + 1u)) { Ok = false; break; }
        } else if (Item.Info.Type == XFILE_TYPE_FILE && strcmp(Path, "SKILL.md") != 0) {
            Kind = strncmp(Path, "scripts/", 8u) == 0 ? MDO_SKILL_RESOURCE_SCRIPT :
                strncmp(Path, "templates/", 10u) == 0 ? MDO_SKILL_RESOURCE_TEMPLATE :
                strncmp(Path, "assets/", 7u) == 0 ? MDO_SKILL_RESOURCE_ASSET : MDO_SKILL_RESOURCE_REFERENCE;
            if (!MdoSkillsPathValid(Path, Kind)) { Ok = false; break; }
            Owned = xrtStrDup(Path);
            if (Owned == NULL || !MdoSkillsResourceAdd(Entry, Kind, Owned)) {
                xrtFree(Owned); Ok = false; break;
            }
        }
    }
    if (Next == XDIR_NEXT_ERROR) Ok = false;
    if (!xrtDirClose(Dir)) Ok = false;
    return Ok;
}

static MdoSkillLoadStatus MdoSkillsOpenResources(
    const MdoSkillSource* pSource, MdoSkillEntry* pEntry,
    MdoSkillDiagnostics* pDiagnostics)
{
    size_t i;
    for ( i = 0u; i < pEntry->ResourceCount; ++i ) {
        MdoSkillResource* pResource = &pEntry->Resources[i];
        char* Virtual = NULL;
        xfile File = MdoSkillsOpen(pSource, pResource->Path, &Virtual);
        bool Valid = File != NULL && xrtFileStat(File, &pResource->Info) &&
            pResource->Info.Type == XFILE_TYPE_FILE &&
            (pResource->Info.Available & XFILE_INFO_SIZE) != 0u &&
            pResource->Info.Size <= MDO_SKILL_RESOURCE_LIMIT &&
            pResource->Info.Size <= SIZE_MAX;
        if ( Valid && !xrtClose(File) ) Valid = false;
        else if ( !Valid && File != NULL ) (void)xrtClose(File);
        if ( !Valid ) {
            const xerror* pError = xrtGetError();
            cstr Message = pError != NULL ? xrtErrorMessage(pError) :
                "declared Skill resource is missing or exceeds 8 MiB";
            bool Fatal = MdoSkillsMemoryError();
            if ( !MdoSkillsDiagnosticAdd(pDiagnostics,
                    MDO_SKILL_DIAGNOSTIC_RESOURCE, pEntry->Id,
                    Virtual != NULL ? Virtual : pEntry->SourcePath,
                    Message) ) Fatal = true;
            xrtFree(Virtual);
            return Fatal ? MDO_SKILL_LOAD_FATAL : MDO_SKILL_LOAD_INVALID;
        }
        pResource->Bytes = (size_t)pResource->Info.Size;
        xrtFree(Virtual);
    }
    return MDO_SKILL_LOAD_OK;
}

static MdoSkillLoadStatus MdoSkillsLoad(const MdoSkillSource* pSource,
    MdoSkillEntry* pEntry, MdoSkillDiagnostics* pDiagnostics)
{
    bytes pHeader = NULL;
    size_t HeaderBytes = 0u;
    size_t FrontStart = 0u;
    size_t FrontEnd = 0u;
    xfile Document = NULL;
    size_t FileSize = 0u;
    char Error[MDO_SKILL_ERROR_LIMIT];
    MdoSkillLoadStatus Status;

    memset(pEntry, 0, sizeof(*pEntry));
    memset(Error, 0, sizeof(Error));
    pEntry->External = pSource->External;
    pEntry->Id = xrtStrDup(pSource->Id);
    pEntry->VirtualDirectory = xrtStrDup(pSource->VirtualDirectory);
    pEntry->RelativeDirectory = xrtStrDup(pSource->RelativeDirectory);
    Document = MdoSkillsOpen(pSource, "SKILL.md", &pEntry->SourcePath);
    if ( pEntry->Id == NULL || pEntry->VirtualDirectory == NULL ||
         pEntry->RelativeDirectory == NULL ) {
        if ( Document != NULL ) (void)xrtClose(Document);
        return MDO_SKILL_LOAD_FATAL;
    }
    if ( Document == NULL || !xrtFileStat(Document, &pEntry->DocumentInfo) ||
         pEntry->DocumentInfo.Type != XFILE_TYPE_FILE ||
         (pEntry->DocumentInfo.Available & XFILE_INFO_SIZE) == 0u ||
         pEntry->DocumentInfo.Size > MDO_SKILL_DOCUMENT_LIMIT ||
         pEntry->DocumentInfo.Size > SIZE_MAX ) {
        const xerror* pError = xrtGetError();
        bool Fatal = MdoSkillsMemoryError();
        if ( !MdoSkillsDiagnosticAdd(pDiagnostics,
                MDO_SKILL_DIAGNOSTIC_OPEN, pEntry->Id,
                pEntry->SourcePath != NULL ? pEntry->SourcePath :
                pSource->VirtualDirectory,
                pError != NULL ? xrtErrorMessage(pError) :
                "SKILL.md is missing or exceeds 512 KiB") ) Fatal = true;
        if ( Document != NULL ) (void)xrtClose(Document);
        return Fatal ? MDO_SKILL_LOAD_FATAL : MDO_SKILL_LOAD_INVALID;
    }
    FileSize = (size_t)pEntry->DocumentInfo.Size;
    if ( !MdoSkillsReadFrontmatter(Document, FileSize,
            &pHeader, &HeaderBytes, &FrontStart, &FrontEnd,
            &pEntry->BodyOffset, Error, sizeof(Error)) ||
         !MdoSkillsParseFrontmatter(pEntry, pHeader, FrontStart, FrontEnd,
            Error, sizeof(Error)) ||
         !MdoSkillsHash(pHeader, HeaderBytes, pEntry->MetadataHash) ) {
        bool Fatal = MdoSkillsMemoryError();
        if ( !MdoSkillsDiagnosticAdd(pDiagnostics,
                MDO_SKILL_DIAGNOSTIC_FRONTMATTER, pEntry->Id,
                pEntry->SourcePath, Error[0] != '\0' ? Error :
                "Skill front matter could not be parsed") ) Fatal = true;
        xrtFree(pHeader);
        (void)xrtClose(Document);
        return Fatal ? MDO_SKILL_LOAD_FATAL : MDO_SKILL_LOAD_INVALID;
    }
    xrtFree(pHeader);
    if ( !xrtClose(Document) ) {
        bool Fatal = MdoSkillsMemoryError();
        if ( !MdoSkillsDiagnosticAdd(pDiagnostics,
                MDO_SKILL_DIAGNOSTIC_OPEN, pEntry->Id,
                pEntry->SourcePath, "SKILL.md could not be closed") )
            Fatal = true;
        return Fatal ? MDO_SKILL_LOAD_FATAL : MDO_SKILL_LOAD_INVALID;
    }
    pEntry->BodyBytes = FileSize - pEntry->BodyOffset;
    pEntry->EstimatedTokens = (pEntry->BodyBytes + 3u) / 4u;
    if (pEntry->AutoResources && !MdoSkillsScanResources(pSource, pEntry, "", 0u)) {
        (void)MdoSkillsDiagnosticAdd(pDiagnostics, MDO_SKILL_DIAGNOSTIC_RESOURCE,
            pEntry->Id, pEntry->SourcePath, "Skill resources exceed safe path, depth or count limits");
        return MDO_SKILL_LOAD_INVALID;
    }
    Status = MdoSkillsOpenResources(pSource, pEntry, pDiagnostics);
    return Status;
}

static MdoSkillCatalog* MdoSkillsBuildCandidate(uint64 Generation,
    MdoSkillDiagnostics* pDiagnostics)
{
    MdoSkillSource* pSources = NULL;
    size_t SourceCount = 0u;
    size_t i;
    MdoSkillCatalog* pCatalog = NULL;

    if ( !MdoSkillsDiscover(&pSources, &SourceCount, pDiagnostics) )
        return NULL;
    pCatalog = MdoSkillsCatalogCreate(Generation);
    if ( pCatalog == NULL ) goto fatal;
    for ( i = 0u; i < SourceCount; ++i ) {
        MdoSkillEntry Entry;
        bool Enabled;
        if (!MdoExtensionEnabled("skills", pSources[i].Id, &Enabled)) goto fatal;
        if (!Enabled) continue;
        MdoSkillLoadStatus Status = MdoSkillsLoad(&pSources[i], &Entry,
            pDiagnostics);
        if ( Status == MDO_SKILL_LOAD_FATAL ) {
            MdoSkillsEntryUnit(&Entry);
            goto fatal;
        }
        if ( Status == MDO_SKILL_LOAD_INVALID ) {
            MdoSkillsEntryUnit(&Entry);
            xrtClearError();
            continue;
        }
        if ( !MdoSkillsGrow((void**)&pCatalog->Entries,
                &pCatalog->Capacity, pCatalog->Count + 1u,
                sizeof(*pCatalog->Entries), MDO_SKILL_COUNT_LIMIT) ) {
            MdoSkillsEntryUnit(&Entry);
            goto fatal;
        }
        pCatalog->Entries[pCatalog->Count++] = Entry;
    }
    MdoSkillsSourcesUnit(pSources, SourceCount);
    return pCatalog;

fatal:
    (void)MdoSkillsDiagnosticAdd(pDiagnostics,
        MDO_SKILL_DIAGNOSTIC_PUBLISH, NULL, NULL,
        "Skill catalog candidate exceeded a memory or capacity limit");
    MdoSkillsSourcesUnit(pSources, SourceCount);
    MdoSkillCatalogRelease(pCatalog);
    return NULL;
}

bool MdoSkillManagerReload(void)
{
    MdoSkillDiagnostics* pDiagnostics = MdoSkillsDiagnosticsCreate();
    MdoSkillDiagnostics* pOldDiagnostics = NULL;
    MdoSkillCatalog* pCandidate = NULL;
    MdoSkillCatalog* pOldCatalog = NULL;
    char Error[MDO_SKILL_ERROR_LIMIT];
    bool Ok = false;

    memset(Error, 0, sizeof(Error));
    if ( pDiagnostics == NULL ) {
        MdoSkillsSetError("failed to allocate Skill diagnostics");
        return false;
    }
    if ( g_MdoSkills.Lock == NULL || !xrtMutexLock(g_MdoSkills.Lock) ) {
        MdoSkillDiagnosticsRelease(pDiagnostics);
        MdoSkillsSetError("Skill manager is unavailable");
        return false;
    }
    if ( !g_MdoSkills.Initialized ) {
        (void)xrtMutexUnlock(g_MdoSkills.Lock);
        MdoSkillDiagnosticsRelease(pDiagnostics);
        MdoSkillsSetError("Skill manager is not initialized");
        return false;
    }
    pCandidate = MdoSkillsBuildCandidate(g_MdoSkills.NextGeneration,
        pDiagnostics);
    if ( pCandidate != NULL ) {
        pOldCatalog = g_MdoSkills.Catalog;
        pOldDiagnostics = g_MdoSkills.Diagnostics;
        g_MdoSkills.Catalog = pCandidate;
        g_MdoSkills.Diagnostics = pDiagnostics;
        g_MdoSkills.NextGeneration++;
        pCandidate = NULL;
        pDiagnostics = NULL;
        Ok = true;
    } else {
        if ( pDiagnostics->Count == 0u )
            (void)MdoSkillsDiagnosticAdd(pDiagnostics,
                MDO_SKILL_DIAGNOSTIC_PUBLISH, NULL, NULL,
                "Skill reload failed without a detailed diagnostic");
        if ( pDiagnostics->Count != 0u )
            MdoSkillsCopyError(Error, sizeof(Error),
                pDiagnostics->Entries[0].Message);
        pOldDiagnostics = g_MdoSkills.Diagnostics;
        g_MdoSkills.Diagnostics = pDiagnostics;
        pDiagnostics = NULL;
    }
    (void)xrtMutexUnlock(g_MdoSkills.Lock);
    MdoSkillCatalogRelease(pCandidate);
    MdoSkillCatalogRelease(pOldCatalog);
    MdoSkillDiagnosticsRelease(pOldDiagnostics);
    MdoSkillDiagnosticsRelease(pDiagnostics);
    if ( !Ok ) MdoSkillsSetError(Error[0] != '\0' ? Error :
        "Skill reload failed");
    return Ok;
}

bool MdoSkillManagerInit(void)
{
    if ( g_MdoSkills.Initialized ) return true;
    memset(&g_MdoSkills, 0, sizeof(g_MdoSkills));
    g_MdoSkills.Lock = xrtMutexCreate();
    g_MdoSkills.Diagnostics = MdoSkillsDiagnosticsCreate();
    g_MdoSkills.NextGeneration = 1u;
    if ( g_MdoSkills.Lock == NULL || g_MdoSkills.Diagnostics == NULL ) {
        MdoSkillManagerUnit();
        MdoSkillsSetError("failed to allocate Skill manager state");
        return false;
    }
    g_MdoSkills.Initialized = true;
    if ( !MdoSkillManagerReload() ) {
        MdoSkillManagerUnit();
        return false;
    }
    return true;
}

void MdoSkillManagerUnit(void)
{
    xmutex* pLock = g_MdoSkills.Lock;
    MdoSkillCatalog* pCatalog;
    MdoSkillDiagnostics* pDiagnostics;
    if ( pLock != NULL ) (void)xrtMutexLock(pLock);
    pCatalog = g_MdoSkills.Catalog;
    pDiagnostics = g_MdoSkills.Diagnostics;
    g_MdoSkills.Catalog = NULL;
    g_MdoSkills.Diagnostics = NULL;
    g_MdoSkills.Initialized = false;
    if ( pLock != NULL ) (void)xrtMutexUnlock(pLock);
    MdoSkillCatalogRelease(pCatalog);
    MdoSkillDiagnosticsRelease(pDiagnostics);
    if ( pLock != NULL ) (void)xrtMutexDestroy(pLock);
    memset(&g_MdoSkills, 0, sizeof(g_MdoSkills));
}

uint64 MdoSkillManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( g_MdoSkills.Lock != NULL && xrtMutexLock(g_MdoSkills.Lock) ) {
        if ( g_MdoSkills.Catalog != NULL )
            Generation = g_MdoSkills.Catalog->Generation;
        (void)xrtMutexUnlock(g_MdoSkills.Lock);
    }
    return Generation;
}

MdoSkillCatalog* MdoSkillCatalogSnapshot(void)
{
    MdoSkillCatalog* pCatalog = NULL;
    if ( g_MdoSkills.Lock != NULL && xrtMutexLock(g_MdoSkills.Lock) ) {
        pCatalog = MdoSkillCatalogRef(g_MdoSkills.Catalog);
        (void)xrtMutexUnlock(g_MdoSkills.Lock);
    }
    return pCatalog;
}

MdoSkillDiagnostics* MdoSkillDiagnosticsSnapshot(void)
{
    MdoSkillDiagnostics* pDiagnostics = NULL;
    if ( g_MdoSkills.Lock != NULL && xrtMutexLock(g_MdoSkills.Lock) ) {
        pDiagnostics = MdoSkillDiagnosticsRef(g_MdoSkills.Diagnostics);
        (void)xrtMutexUnlock(g_MdoSkills.Lock);
    }
    return pDiagnostics;
}

static const MdoSkillEntry* MdoSkillsFind(const MdoSkillCatalog* pCatalog,
    cstr Id)
{
    size_t Low = 0u;
    size_t High = pCatalog != NULL ? pCatalog->Count : 0u;
    if ( Id == NULL ) return NULL;
    while ( Low < High ) {
        size_t Middle = Low + (High - Low) / 2u;
        int Compare = strcmp(pCatalog->Entries[Middle].Id, Id);
        if ( Compare == 0 ) return &pCatalog->Entries[Middle];
        if ( Compare < 0 ) Low = Middle + 1u;
        else High = Middle;
    }
    return NULL;
}

static void MdoSkillsInfo(const MdoSkillCatalog* pCatalog,
    const MdoSkillEntry* pEntry, MdoSkillInfo* pInfo)
{
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Generation = pCatalog->Generation;
    pInfo->External = pEntry->External;
    pInfo->Trust = pEntry->External ?
        MDO_SKILL_TRUST_EXTERNAL_REFERENCE : MDO_SKILL_TRUST_BUILTIN;
    pInfo->Id = pEntry->Id;
    pInfo->Name = pEntry->Name;
    pInfo->Description = pEntry->Description;
    pInfo->Version = pEntry->Version;
    pInfo->License = pEntry->License;
    pInfo->Compatibility = pEntry->Compatibility;
    pInfo->SourcePath = pEntry->SourcePath;
    pInfo->MetadataHash = pEntry->MetadataHash;
    pInfo->BodyBytes = pEntry->BodyBytes;
    pInfo->EstimatedTokens = pEntry->EstimatedTokens;
    pInfo->RequiredTools = (const char* const*)pEntry->Tools.Items;
    pInfo->RequiredToolCount = pEntry->Tools.Count;
    pInfo->RequiredMcpServers = (const char* const*)pEntry->Mcp.Items;
    pInfo->RequiredMcpServerCount = pEntry->Mcp.Count;
    pInfo->RequiredPermissions =
        (const char* const*)pEntry->Permissions.Items;
    pInfo->RequiredPermissionCount = pEntry->Permissions.Count;
    pInfo->ResourceCount = pEntry->ResourceCount;
}

size_t MdoSkillCatalogCount(const MdoSkillCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->Count : 0u;
}

uint64 MdoSkillCatalogGeneration(const MdoSkillCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->Generation : 0u;
}

bool MdoSkillCatalogAt(const MdoSkillCatalog* pCatalog, size_t iIndex,
    MdoSkillInfo* pInfo)
{
    if ( pCatalog == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) || iIndex >= pCatalog->Count )
        return false;
    MdoSkillsInfo(pCatalog, &pCatalog->Entries[iIndex], pInfo);
    return true;
}

bool MdoSkillCatalogFind(const MdoSkillCatalog* pCatalog, cstr Id,
    MdoSkillInfo* pInfo)
{
    const MdoSkillEntry* pEntry;
    if ( pCatalog == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) ) return false;
    pEntry = MdoSkillsFind(pCatalog, Id);
    if ( pEntry == NULL ) return false;
    MdoSkillsInfo(pCatalog, pEntry, pInfo);
    return true;
}

size_t MdoSkillCatalogResourceCount(const MdoSkillCatalog* pCatalog,
    cstr SkillId)
{
    const MdoSkillEntry* pEntry = MdoSkillsFind(pCatalog, SkillId);
    return pEntry != NULL ? pEntry->ResourceCount : 0u;
}

bool MdoSkillCatalogResourceAt(const MdoSkillCatalog* pCatalog,
    cstr SkillId, size_t iIndex, MdoSkillResourceInfo* pInfo)
{
    const MdoSkillEntry* pEntry;
    const MdoSkillResource* pResource;
    if ( pCatalog == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) ) return false;
    pEntry = MdoSkillsFind(pCatalog, SkillId);
    if ( pEntry == NULL || iIndex >= pEntry->ResourceCount ) return false;
    pResource = &pEntry->Resources[iIndex];
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Generation = pCatalog->Generation;
    pInfo->SkillId = pEntry->Id;
    pInfo->Kind = pResource->Kind;
    pInfo->Path = pResource->Path;
    pInfo->Bytes = pResource->Bytes;
    return true;
}

static bool MdoSkillsCacheBody(MdoSkillEntry* pEntry)
{
    xfile File = NULL;
    char* Virtual = NULL;
    xfileinfo Info;
    bytes pHeader = NULL;
    size_t HeaderBytes = 0u;
    size_t FrontStart = 0u;
    size_t FrontEnd = 0u;
    size_t BodyOffset = 0u;
    size_t Read = 0u;
    char Hash[65];
    char Error[MDO_SKILL_ERROR_LIMIT];
    char* Body = NULL;
    bool Ok = false;

    if ( pEntry->CachedBody != NULL ) return true;
    memset(&Info, 0, sizeof(Info));
    memset(Error, 0, sizeof(Error));
    File = MdoSkillsOpenEntry(pEntry, "SKILL.md", &Virtual);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         !MdoSkillsSameFile(&pEntry->DocumentInfo, &Info) ) {
        MdoSkillsSetError("Skill source changed; reload the Skill catalog");
        goto done;
    }
    if ( !MdoSkillsReadFrontmatter(File, (size_t)Info.Size, &pHeader,
            &HeaderBytes, &FrontStart, &FrontEnd, &BodyOffset,
            Error, sizeof(Error)) ||
         !MdoSkillsHash(pHeader, HeaderBytes, Hash) ||
         strcmp(Hash, pEntry->MetadataHash) != 0 ||
         BodyOffset != pEntry->BodyOffset ||
         (size_t)Info.Size - BodyOffset != pEntry->BodyBytes ) {
        MdoSkillsSetError("Skill metadata changed; reload the Skill catalog");
        goto done;
    }
    Body = (char*)xrtMalloc(pEntry->BodyBytes + 1u);
    if ( Body == NULL ) goto done;
    if ( pEntry->BodyBytes != 0u &&
         (!xrtReadAtFull(File, pEntry->BodyOffset, Body,
            pEntry->BodyBytes, &Read) || Read != pEntry->BodyBytes) ) goto done;
    Body[pEntry->BodyBytes] = '\0';
    if ( MdoSkillsContainsByte(Body, pEntry->BodyBytes, '\0') ||
         !xrtUtf8Valid((xstrview){ Body, pEntry->BodyBytes }, NULL) ) {
        MdoSkillsSetError("Skill body is not valid UTF-8 text");
        goto done;
    }
    pEntry->CachedBody = Body;
    Body = NULL;
    Ok = true;
done:
    xrtFree(Body);
    xrtFree(pHeader);
    xrtFree(Virtual);
    if ( File != NULL ) (void)xrtClose(File);
    return Ok;
}

static bool MdoSkillsCacheResource(MdoSkillEntry* pEntry,
    MdoSkillResource* pResource)
{
    xfile File = NULL;
    char* Virtual = NULL;
    xfileinfo Info;
    bytes Data = NULL;
    size_t Read = 0u;
    bool Ok = false;

    if ( pResource->CachedData != NULL ) return true;
    memset(&Info, 0, sizeof(Info));
    File = MdoSkillsOpenEntry(pEntry, pResource->Path, &Virtual);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         !MdoSkillsSameFile(&pResource->Info, &Info) ) {
        MdoSkillsSetError("Skill resource changed; reload the Skill catalog");
        goto done;
    }
    Data = (bytes)xrtMalloc(pResource->Bytes + 1u);
    if ( Data == NULL ) goto done;
    if ( pResource->Bytes != 0u &&
         (!xrtReadAtFull(File, 0u, Data, pResource->Bytes, &Read) ||
          Read != pResource->Bytes) ) goto done;
    Data[pResource->Bytes] = 0u;
    if ( pResource->Kind != MDO_SKILL_RESOURCE_ASSET &&
         (MdoSkillsContainsByte(Data, pResource->Bytes, '\0') ||
          !xrtUtf8Valid((xstrview){ (const char*)Data,
            pResource->Bytes }, NULL)) ) {
        MdoSkillsSetError("Skill script or template is not valid UTF-8 text");
        goto done;
    }
    pResource->CachedData = Data;
    Data = NULL;
    Ok = true;
done:
    xrtFree(Data);
    xrtFree(Virtual);
    if ( File != NULL ) (void)xrtClose(File);
    return Ok;
}

bool MdoSkillCatalogLoadBody(const MdoSkillCatalog* pCatalog,
    cstr SkillId, MdoSkillContent* pContent)
{
    MdoSkillEntry* pEntry;
    char* Text;
    if ( pCatalog == NULL || pContent == NULL ||
         pContent->Size < sizeof(*pContent) ) return false;
    pEntry = (MdoSkillEntry*)MdoSkillsFind(pCatalog, SkillId);
    if ( pEntry == NULL ) return false;
    if ( !xrtMutexLock(pCatalog->CacheLock) ) return false;
    if ( !MdoSkillsCacheBody(pEntry) ) {
        (void)xrtMutexUnlock(pCatalog->CacheLock);
        return false;
    }
    Text = xrtStrDupN(pEntry->CachedBody, pEntry->BodyBytes);
    (void)xrtMutexUnlock(pCatalog->CacheLock);
    if ( Text == NULL )
        return false;
    memset(pContent, 0, sizeof(*pContent));
    pContent->Size = sizeof(*pContent);
    pContent->Generation = pCatalog->Generation;
    pContent->External = pEntry->External;
    pContent->Trust = pEntry->External ?
        MDO_SKILL_TRUST_EXTERNAL_REFERENCE : MDO_SKILL_TRUST_BUILTIN;
    pContent->Text = Text;
    pContent->Bytes = pEntry->BodyBytes;
    pContent->EstimatedTokens = pEntry->EstimatedTokens;
    return true;
}

void MdoSkillContentUnit(MdoSkillContent* pContent)
{
    if ( pContent == NULL ) return;
    xrtFree(pContent->Text);
    memset(pContent, 0, sizeof(*pContent));
}

bool MdoSkillCatalogLoadResource(const MdoSkillCatalog* pCatalog,
    cstr SkillId, cstr Path, size_t Limit,
    MdoSkillResourceContent* pContent)
{
    MdoSkillEntry* pEntry;
    MdoSkillResource* pResource = NULL;
    bytes Data;
    size_t i;
    if ( pCatalog == NULL || pContent == NULL ||
         pContent->Size < sizeof(*pContent) || Path == NULL || Limit == 0u )
        return false;
    pEntry = (MdoSkillEntry*)MdoSkillsFind(pCatalog, SkillId);
    if ( pEntry == NULL ) return false;
    for ( i = 0u; i < pEntry->ResourceCount; ++i ) {
        if ( strcmp(pEntry->Resources[i].Path, Path) == 0 ) {
            pResource = &pEntry->Resources[i];
            break;
        }
    }
    if ( pResource == NULL || pResource->Bytes > Limit ) {
        MdoSkillsSetError(pResource == NULL ?
            "Skill resource is not declared in front matter" :
            "Skill resource exceeds the caller's byte limit");
        return false;
    }
    if ( !xrtMutexLock(pCatalog->CacheLock) ) return false;
    if ( !MdoSkillsCacheResource(pEntry, pResource) ) {
        (void)xrtMutexUnlock(pCatalog->CacheLock);
        return false;
    }
    Data = (bytes)xrtMalloc(pResource->Bytes + 1u);
    if ( Data != NULL )
        memcpy(Data, pResource->CachedData, pResource->Bytes + 1u);
    (void)xrtMutexUnlock(pCatalog->CacheLock);
    if ( Data == NULL ) return false;
    memset(pContent, 0, sizeof(*pContent));
    pContent->Size = sizeof(*pContent);
    pContent->Generation = pCatalog->Generation;
    pContent->External = pEntry->External;
    pContent->Trust = pEntry->External ?
        MDO_SKILL_TRUST_EXTERNAL_REFERENCE : MDO_SKILL_TRUST_BUILTIN;
    pContent->Kind = pResource->Kind;
    pContent->Data = Data;
    pContent->Bytes = pResource->Bytes;
    return true;
}

void MdoSkillResourceContentUnit(MdoSkillResourceContent* pContent)
{
    if ( pContent == NULL ) return;
    xrtFree(pContent->Data);
    memset(pContent, 0, sizeof(*pContent));
}

size_t MdoSkillDiagnosticsCount(const MdoSkillDiagnostics* pDiagnostics)
{
    return pDiagnostics != NULL ? pDiagnostics->Count : 0u;
}

bool MdoSkillDiagnosticsAt(const MdoSkillDiagnostics* pDiagnostics,
    size_t iIndex, MdoSkillDiagnosticInfo* pInfo)
{
    const MdoSkillDiagnosticEntry* pEntry;
    if ( pDiagnostics == NULL || pInfo == NULL ||
         pInfo->Size < sizeof(*pInfo) || iIndex >= pDiagnostics->Count )
        return false;
    pEntry = &pDiagnostics->Entries[iIndex];
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->Size = sizeof(*pInfo);
    pInfo->Stage = pEntry->Stage;
    pInfo->SkillId = pEntry->SkillId;
    pInfo->SourcePath = pEntry->SourcePath;
    pInfo->Message = pEntry->Message;
    return true;
}
