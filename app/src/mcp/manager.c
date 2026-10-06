#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/secrets.h"

#define MDO_MCP_SOURCE "mdo.mcp.config"
#define MDO_MCP_DIRECTORY "/app/default-home/mcp"
#define MDO_MCP_RELATIVE_DIRECTORY "mcp"
#define MDO_MCP_FILE_LIMIT (256u * 1024u)
#define MDO_MCP_SECRET_LIMIT (64u * 1024u)
#define MDO_MCP_SERVER_LIMIT 128u
#define MDO_MCP_DIAGNOSTIC_LIMIT 256u
#define MDO_MCP_ARGUMENT_LIMIT 256u
#define MDO_MCP_ENVIRONMENT_LIMIT 256u
#define MDO_MCP_FILTER_LIMIT 256u
#define MDO_MCP_STRING_LIMIT 4096u
#define MDO_MCP_DESCRIPTION_LIMIT 2048u
#define MDO_MCP_ERROR_LIMIT 1024u

typedef struct MdoMcpEnvironmentEntry {
    char* Name;
    char* Value;
} MdoMcpEnvironmentEntry;

typedef struct MdoMcpHttpHeaderEntry {
    char* Name;
    char* Value;
} MdoMcpHttpHeaderEntry;

typedef struct MdoMcpServerEntry {
    bool External;
    bool Enabled;
    bool AutoReconnect;
    bool TrustReadOnlyAnnotations;
    bool InheritEnvironment;
    MdoMcpTransport Transport;
    char* Id;
    char* Name;
    char* Description;
    char* ProtocolVersion;
    char* Program;
    char* Endpoint;
    char* WorkingDirectory;
    char* PermissionProfile;
    char* SourcePath;
    char SourceHash[65];
    char** Arguments;
    size_t ArgumentCount;
    MdoMcpEnvironmentEntry* Environment;
    size_t EnvironmentCount;
    MdoMcpHttpHeaderEntry* HttpHeaders;
    size_t HttpHeaderCount;
    char** AllowedTools;
    size_t AllowedToolCount;
    char** DeniedTools;
    size_t DeniedToolCount;
    uint32 StartupTimeoutMilliseconds;
    uint32 RequestTimeoutMilliseconds;
    size_t MaxMessageBytes;
    size_t MaxTools;
    xwork_tool_effects DefaultEffects;
} MdoMcpServerEntry;

struct MdoMcpCatalog {
    xatomic32 Refs;
    uint64 Generation;
    MdoMcpServerEntry* Entries;
    size_t Count;
    size_t Capacity;
};

typedef struct MdoMcpDiagnosticEntry {
    MdoMcpDiagnosticStage Stage;
    char* ServerId;
    char* SourcePath;
    char* SourceHash;
    char* Message;
} MdoMcpDiagnosticEntry;

struct MdoMcpDiagnostics {
    xatomic32 Refs;
    MdoMcpDiagnosticEntry* Entries;
    size_t Count;
    size_t Capacity;
};

typedef struct MdoMcpSource {
    bool External;
    char* Id;
    char* VirtualPath;
    char* RelativePath;
} MdoMcpSource;

typedef struct MdoMcpState {
    xmutex* Lock;
    xmutex* ReloadLock;
    xwork_runtime* Runtime;
    MdoMcpCatalog* Catalog;
    MdoMcpDiagnostics* Diagnostics;
    uint64 NextGeneration;
    bool Initialized;
} MdoMcpState;

static MdoMcpState g_MdoMcp;

static void MdoMcpSetError(cstr Message)
{
    xerror* pError = xrtErrorCreate(XERR_STATE, "mdo.mcp", 1,
        Message != NULL ? Message : "MCP manager operation failed");
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static void MdoMcpFormatError(char* Target, size_t Capacity,
    cstr Format, ...)
{
    va_list Arguments;
    if ( Target == NULL || Capacity == 0u ) return;
    va_start(Arguments, Format);
    (void)vsnprintf(Target, Capacity, Format, Arguments);
    va_end(Arguments);
    Target[Capacity - 1u] = '\0';
}

static bool MdoMcpMemoryError(void)
{
    const xerror* pError = xrtGetError();
    return pError != NULL && xrtErrorKind(pError) == XERR_MEMORY;
}

static bool MdoMcpGrow(void** ppItems, size_t* pCapacity, size_t Count,
    size_t ItemSize, size_t Limit)
{
    size_t Capacity;
    void* pItems;
    if ( Count > Limit || Count > SIZE_MAX / ItemSize ) return false;
    if ( Count <= *pCapacity ) return true;
    Capacity = *pCapacity != 0u ? *pCapacity : 4u;
    while ( Capacity < Count ) {
        if ( Capacity > Limit / 2u ) {
            Capacity = Limit;
            break;
        }
        Capacity *= 2u;
    }
    if ( Capacity < Count ) return false;
    pItems = xrtRealloc(*ppItems, Capacity * ItemSize);
    if ( pItems == NULL ) return false;
    *ppItems = pItems;
    *pCapacity = Capacity;
    return true;
}

static bool MdoMcpBytesContainZero(const void* Data, size_t Size)
{
    const unsigned char* pBytes = (const unsigned char*)Data;
    size_t i;
    for ( i = 0u; i < Size; ++i )
        if ( pBytes[i] == 0u ) return true;
    return false;
}

static bool MdoMcpViewEqual(xstrview View, cstr Text)
{
    size_t Size = strlen(Text);
    return View.Size == Size && memcmp(View.Data, Text, Size) == 0;
}

static bool MdoMcpStringCopy(char** pTarget, xstrview Source, size_t Limit,
    bool Required)
{
    *pTarget = NULL;
    if ( Source.Size == 0u ) return !Required;
    if ( Source.Data == NULL || Source.Size > Limit ||
         MdoMcpBytesContainZero(Source.Data, Source.Size) ) return false;
    *pTarget = xrtStrDupN(Source.Data, Source.Size);
    return *pTarget != NULL;
}

static bool MdoMcpCStringCopy(char** pTarget, cstr Source, size_t Limit,
    bool Required)
{
    xstrview View;
    View.Data = Source;
    View.Size = Source != NULL ? strlen(Source) : 0u;
    return MdoMcpStringCopy(pTarget, View, Limit, Required);
}

static bool MdoMcpIdValid(cstr Id)
{
    const unsigned char* p = (const unsigned char*)Id;
    size_t i = 0u;
    if ( p == NULL || !((p[0] >= 'a' && p[0] <= 'z') ||
                        (p[0] >= 'A' && p[0] <= 'Z')) ) return false;
    while ( p[i] != 0u ) {
        unsigned char Ch = p[i];
        if ( i >= 96u ||
             !((Ch >= 'a' && Ch <= 'z') ||
               (Ch >= 'A' && Ch <= 'Z') ||
               (Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == '_' ||
               Ch == '-') ) return false;
        ++i;
    }
    return i != 0u;
}

static bool MdoMcpPortablePathValid(cstr Path)
{
    const unsigned char* p = (const unsigned char*)Path;
    const unsigned char* pSegment;
    if ( p == NULL || p[0] == 0u || p[0] == '/' || p[0] == '\\' )
        return false;
    pSegment = p;
    for ( ; ; ++p ) {
        if ( *p == '\\' || *p == ':' || (*p != 0u && *p < 0x20u) )
            return false;
        if ( *p == '/' || *p == 0u ) {
            size_t Size = (size_t)(p - pSegment);
            if ( Size == 0u || (Size == 1u && pSegment[0] == '.') ||
                 (Size == 2u && pSegment[0] == '.' &&
                  pSegment[1] == '.') ) return false;
            if ( *p == 0u ) return true;
            pSegment = p + 1;
        }
    }
}

static bool MdoMcpHash(const void* Data, size_t Size, char Output[65])
{
    static const char Hex[] = "0123456789abcdef";
    unsigned char Digest[32];
    size_t i;
    if ( !xrtSha256(Data, Size, Digest) ) return false;
    for ( i = 0u; i < sizeof(Digest); ++i ) {
        Output[i * 2u] = Hex[Digest[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Digest[i] & 15u];
    }
    Output[64] = '\0';
    return true;
}

static void MdoMcpStringArrayUnit(char** Items, size_t Count)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) xrtFree(Items[i]);
    xrtFree(Items);
}

static void MdoMcpServerUnit(MdoMcpServerEntry* pEntry)
{
    size_t i;
    if ( pEntry == NULL ) return;
    xrtFree(pEntry->Id);
    xrtFree(pEntry->Name);
    xrtFree(pEntry->Description);
    xrtFree(pEntry->ProtocolVersion);
    xrtFree(pEntry->Program);
    xrtFree(pEntry->Endpoint);
    xrtFree(pEntry->WorkingDirectory);
    xrtFree(pEntry->PermissionProfile);
    xrtFree(pEntry->SourcePath);
    MdoMcpStringArrayUnit(pEntry->Arguments, pEntry->ArgumentCount);
    for ( i = 0u; i < pEntry->EnvironmentCount; ++i ) {
        xrtFree(pEntry->Environment[i].Name);
        if ( pEntry->Environment[i].Value != NULL ) {
            xrtSecureZero(pEntry->Environment[i].Value,
                strlen(pEntry->Environment[i].Value));
            xrtFree(pEntry->Environment[i].Value);
        }
    }
    xrtFree(pEntry->Environment);
    for ( i = 0u; i < pEntry->HttpHeaderCount; ++i ) {
        xrtFree(pEntry->HttpHeaders[i].Name);
        if ( pEntry->HttpHeaders[i].Value != NULL ) {
            xrtSecureZero(pEntry->HttpHeaders[i].Value,
                strlen(pEntry->HttpHeaders[i].Value));
            xrtFree(pEntry->HttpHeaders[i].Value);
        }
    }
    xrtFree(pEntry->HttpHeaders);
    MdoMcpStringArrayUnit(pEntry->AllowedTools, pEntry->AllowedToolCount);
    MdoMcpStringArrayUnit(pEntry->DeniedTools, pEntry->DeniedToolCount);
    memset(pEntry, 0, sizeof(*pEntry));
}

static void MdoMcpCatalogForgetSecrets(MdoMcpCatalog* pCatalog)
{
    size_t i;
    if ( pCatalog == NULL ) return;
    for ( i = 0u; i < pCatalog->Count; ++i ) {
        MdoMcpServerEntry* pEntry = &pCatalog->Entries[i];
        size_t j;
        for ( j = 0u; j < pEntry->EnvironmentCount; ++j ) {
            char* Value = pEntry->Environment[j].Value;
            if ( Value == NULL ) continue;
            xrtSecureZero(Value, strlen(Value));
            xrtFree(Value);
            pEntry->Environment[j].Value = NULL;
        }
        for ( j = 0u; j < pEntry->HttpHeaderCount; ++j ) {
            char* Value = pEntry->HttpHeaders[j].Value;
            if ( Value == NULL ) continue;
            xrtSecureZero(Value, strlen(Value));
            xrtFree(Value);
            pEntry->HttpHeaders[j].Value = NULL;
        }
    }
}

static MdoMcpCatalog* MdoMcpCatalogCreate(uint64 Generation)
{
    MdoMcpCatalog* pCatalog = (MdoMcpCatalog*)xrtCalloc(1u,
        sizeof(*pCatalog));
    if ( pCatalog == NULL ) return NULL;
    xrtAtomic32Init(&pCatalog->Refs, 1u);
    pCatalog->Generation = Generation;
    return pCatalog;
}

MdoMcpCatalog* MdoMcpCatalogRef(MdoMcpCatalog* pCatalog)
{
    uint32 Refs;
    if ( pCatalog == NULL ) return NULL;
    Refs = xrtAtomic32Load(&pCatalog->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&pCatalog->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return pCatalog;
        Refs = Expected;
    }
}

void MdoMcpCatalogRelease(MdoMcpCatalog* pCatalog)
{
    uint32 Previous;
    size_t i;
    if ( pCatalog == NULL ) return;
    Previous = xrtAtomic32FetchSub(&pCatalog->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < pCatalog->Count; ++i )
        MdoMcpServerUnit(&pCatalog->Entries[i]);
    xrtFree(pCatalog->Entries);
    xrtFree(pCatalog);
}

static MdoMcpDiagnostics* MdoMcpDiagnosticsCreate(void)
{
    MdoMcpDiagnostics* pDiagnostics = (MdoMcpDiagnostics*)xrtCalloc(1u,
        sizeof(*pDiagnostics));
    if ( pDiagnostics != NULL ) xrtAtomic32Init(&pDiagnostics->Refs, 1u);
    return pDiagnostics;
}

MdoMcpDiagnostics* MdoMcpDiagnosticsRef(MdoMcpDiagnostics* pDiagnostics)
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

void MdoMcpDiagnosticsRelease(MdoMcpDiagnostics* pDiagnostics)
{
    uint32 Previous;
    size_t i;
    if ( pDiagnostics == NULL ) return;
    Previous = xrtAtomic32FetchSub(&pDiagnostics->Refs, 1u,
        XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < pDiagnostics->Count; ++i ) {
        xrtFree(pDiagnostics->Entries[i].ServerId);
        xrtFree(pDiagnostics->Entries[i].SourcePath);
        xrtFree(pDiagnostics->Entries[i].SourceHash);
        xrtFree(pDiagnostics->Entries[i].Message);
    }
    xrtFree(pDiagnostics->Entries);
    xrtFree(pDiagnostics);
}

static bool MdoMcpDiagnosticAdd(MdoMcpDiagnostics* pDiagnostics,
    MdoMcpDiagnosticStage Stage, cstr ServerId, cstr SourcePath,
    cstr SourceHash, cstr Message)
{
    MdoMcpDiagnosticEntry* pEntry;
    if ( pDiagnostics == NULL ||
         !MdoMcpGrow((void**)&pDiagnostics->Entries,
            &pDiagnostics->Capacity, pDiagnostics->Count + 1u,
            sizeof(*pDiagnostics->Entries), MDO_MCP_DIAGNOSTIC_LIMIT) )
        return false;
    pEntry = &pDiagnostics->Entries[pDiagnostics->Count];
    memset(pEntry, 0, sizeof(*pEntry));
    pEntry->Stage = Stage;
    if ( !MdoMcpCStringCopy(&pEntry->ServerId, ServerId, 96u, false) ||
         !MdoMcpCStringCopy(&pEntry->SourcePath, SourcePath,
            MDO_MCP_STRING_LIMIT, false) ||
         !MdoMcpCStringCopy(&pEntry->SourceHash, SourceHash, 64u, false) ||
         !MdoMcpCStringCopy(&pEntry->Message, Message,
            MDO_MCP_ERROR_LIMIT - 1u, true) ) {
        xrtFree(pEntry->ServerId);
        xrtFree(pEntry->SourcePath);
        xrtFree(pEntry->SourceHash);
        xrtFree(pEntry->Message);
        memset(pEntry, 0, sizeof(*pEntry));
        return false;
    }
    ++pDiagnostics->Count;
    return true;
}

static void MdoMcpSourceUnit(MdoMcpSource* pSource)
{
    if ( pSource == NULL ) return;
    xrtFree(pSource->Id);
    xrtFree(pSource->VirtualPath);
    xrtFree(pSource->RelativePath);
    memset(pSource, 0, sizeof(*pSource));
}

static void MdoMcpSourcesUnit(MdoMcpSource* Sources, size_t Count)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) MdoMcpSourceUnit(&Sources[i]);
    xrtFree(Sources);
}

static int MdoMcpSourceCompare(const void* Left, const void* Right)
{
    const MdoMcpSource* pLeft = (const MdoMcpSource*)Left;
    const MdoMcpSource* pRight = (const MdoMcpSource*)Right;
    return strcmp(pLeft->Id, pRight->Id);
}

static bool MdoMcpDiscover(MdoMcpSource** ppSources, size_t* pCount,
    MdoMcpDiagnostics* pDiagnostics)
{
    xvfs Vfs = xsApplicationVfs();
    xdir Directory;
    xdirentry Entry;
    xdirnext Next;
    size_t Capacity = 0u;

    *ppSources = NULL;
    *pCount = 0u;
    Directory = Vfs != NULL ? xrtVfsDirOpen(Vfs, MDO_MCP_DIRECTORY,
        XDIR_STAT) : NULL;
    if ( Directory == NULL ) {
        const xerror* pError = xrtGetError();
        if ( pError != NULL && xrtErrorKind(pError) == XERR_NOT_FOUND ) {
            xrtClearError();
            return true;
        }
        (void)MdoMcpDiagnosticAdd(pDiagnostics,
            MDO_MCP_DIAGNOSTIC_DISCOVERY, NULL, MDO_MCP_DIRECTORY, NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "MCP directory cannot be opened");
        return false;
    }
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        MdoMcpSource* pSource;
        bool Exists = false;
        xfileinfo ExternalInfo;
        size_t NameSize = Entry.Name.Size;
        size_t VirtualSize = strlen(MDO_MCP_DIRECTORY);
        size_t RelativeSize = strlen(MDO_MCP_RELATIVE_DIRECTORY);
        size_t IdSize;
        char* Id = NULL;

        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u || NameSize <= 5u ||
             memcmp(Entry.Name.Data + NameSize - 5u, ".json", 5u) != 0 )
            continue;
        IdSize = NameSize - 5u;
        Id = xrtStrDupN(Entry.Name.Data, IdSize);
        if ( Id == NULL ) goto fail;
        if ( !MdoMcpIdValid(Id) ) {
            char Path[MDO_MCP_STRING_LIMIT];
            (void)snprintf(Path, sizeof(Path), "%s/%s", MDO_MCP_DIRECTORY,
                Entry.Name.Data);
            if ( !MdoMcpDiagnosticAdd(pDiagnostics,
                    MDO_MCP_DIAGNOSTIC_DISCOVERY, NULL, Path, NULL,
                    "MCP file name is not a valid server id") ) {
                xrtFree(Id);
                goto fail;
            }
            xrtFree(Id);
            continue;
        }
        if ( *pCount >= MDO_MCP_SERVER_LIMIT ||
             VirtualSize > SIZE_MAX - NameSize - 2u ||
             RelativeSize > SIZE_MAX - NameSize - 2u ||
             !MdoMcpGrow((void**)ppSources, &Capacity, *pCount + 1u,
                sizeof(**ppSources), MDO_MCP_SERVER_LIMIT) ) {
            xrtFree(Id);
            goto fail;
        }
        pSource = &(*ppSources)[*pCount];
        memset(pSource, 0, sizeof(*pSource));
        pSource->Id = Id;
        pSource->VirtualPath = (char*)xrtMalloc(
            VirtualSize + NameSize + 2u);
        pSource->RelativePath = (char*)xrtMalloc(
            RelativeSize + NameSize + 2u);
        if ( pSource->VirtualPath == NULL || pSource->RelativePath == NULL )
            goto fail;
        (void)snprintf(pSource->VirtualPath, VirtualSize + NameSize + 2u,
            "%s/%s", MDO_MCP_DIRECTORY, Entry.Name.Data);
        (void)snprintf(pSource->RelativePath, RelativeSize + NameSize + 2u,
            "%s/%s", MDO_MCP_RELATIVE_DIRECTORY, Entry.Name.Data);
        memset(&ExternalInfo, 0, sizeof(ExternalInfo));
        if ( !MdoHomeExternalStat(pSource->RelativePath, &Exists,
                &ExternalInfo) ) goto fail;
        pSource->External = Exists;
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             (Exists && ExternalInfo.Type != XFILE_TYPE_FILE) ) {
            if ( !MdoMcpDiagnosticAdd(pDiagnostics,
                    MDO_MCP_DIAGNOSTIC_READ, pSource->Id,
                    pSource->VirtualPath, NULL,
                    "MCP source is not a regular file") ) goto fail;
            MdoMcpSourceUnit(pSource);
            continue;
        }
        ++*pCount;
    }
    if ( Next == XDIR_NEXT_ERROR || !xrtDirClose(Directory) ) {
        const xerror* pError = xrtGetError();
        (void)MdoMcpDiagnosticAdd(pDiagnostics,
            MDO_MCP_DIAGNOSTIC_DISCOVERY, NULL, MDO_MCP_DIRECTORY, NULL,
            pError != NULL ? xrtErrorMessage(pError) :
            "MCP directory enumeration failed");
        MdoMcpSourcesUnit(*ppSources, *pCount);
        *ppSources = NULL;
        *pCount = 0u;
        return false;
    }
    if ( *pCount > 1u ) qsort(*ppSources, *pCount, sizeof(**ppSources),
        MdoMcpSourceCompare);
    return true;

fail:
    (void)xrtDirClose(Directory);
    if ( *pCount < Capacity && *ppSources != NULL )
        MdoMcpSourceUnit(&(*ppSources)[*pCount]);
    MdoMcpSourcesUnit(*ppSources, *pCount);
    *ppSources = NULL;
    *pCount = 0u;
    (void)MdoMcpDiagnosticAdd(pDiagnostics,
        MDO_MCP_DIAGNOSTIC_DISCOVERY, NULL, MDO_MCP_DIRECTORY, NULL,
        "MCP discovery exceeded its count or memory limit");
    return false;
}

static bool MdoMcpReadSource(const MdoMcpSource* pSource, char** ppText,
    size_t* pSize, char Error[MDO_MCP_ERROR_LIMIT])
{
    xfile File;
    xfileinfo Info;
    char* pText;
    size_t Size;
    bool Ok = true;

    *ppText = NULL;
    *pSize = 0u;
    File = pSource->External ? MdoHomeOpenRead(pSource->RelativePath) :
        MdoResourceOpenRead(pSource->RelativePath);
    if ( File == NULL ) {
        const xerror* pError = xrtGetError();
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT, "%s",
            pError != NULL ? xrtErrorMessage(pError) :
            "MCP source cannot be opened");
        return false;
    }
    memset(&Info, 0, sizeof(Info));
    if ( !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > MDO_MCP_FILE_LIMIT ||
         Info.Size > SIZE_MAX - 1u ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "MCP source must be between 1 and %u bytes",
            (unsigned)MDO_MCP_FILE_LIMIT);
        Ok = false;
        goto done;
    }
    Size = (size_t)Info.Size;
    pText = (char*)xrtMalloc(Size + 1u);
    if ( pText == NULL ) {
        Ok = false;
        goto done;
    }
    if ( !xrtReadFull(File, pText, Size, NULL) ) {
        xrtFree(pText);
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "MCP source cannot be read completely");
        Ok = false;
        goto done;
    }
    pText[Size] = '\0';
    *ppText = pText;
    *pSize = Size;

done:
    if ( !xrtClose(File) ) Ok = false;
    if ( !Ok && *ppText != NULL ) {
        xrtFree(*ppText);
        *ppText = NULL;
        *pSize = 0u;
    }
    return Ok;
}

static const xvalue* MdoMcpObjectGet(const xvalue* pObject, cstr Name)
{
    return xrtValueObjectGet(pObject, xrtStrView(Name));
}

static bool MdoMcpValueString(const xvalue* pValue, xstrview* pView)
{
    return pValue != NULL && xrtValueType(pValue) == XVALUE_STRING &&
        xrtValueGetString(pValue, pView);
}

static bool MdoMcpValueUnsigned(const xvalue* pValue, uint64* pResult)
{
    int64 Signed;
    if ( pValue == NULL ) return false;
    if ( xrtValueType(pValue) == XVALUE_UINT )
        return xrtValueGetUInt(pValue, pResult);
    if ( xrtValueType(pValue) != XVALUE_INT ||
         !xrtValueGetInt(pValue, &Signed) || Signed < 0 ) return false;
    *pResult = (uint64)Signed;
    return true;
}

static bool MdoMcpValueBool(const xvalue* pValue, bool* pResult)
{
    return pValue != NULL && xrtValueType(pValue) == XVALUE_BOOL &&
        xrtValueGetBool(pValue, pResult);
}

static bool MdoMcpObjectKeys(const xvalue* pObject,
    const char* const* Allowed, size_t AllowedCount, cstr ObjectName,
    char Error[MDO_MCP_ERROR_LIMIT])
{
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* pValue;
    xvalueiterresult Result;
    if ( pObject == NULL || xrtValueType(pObject) != XVALUE_OBJECT ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "%s must be an object", ObjectName);
        return false;
    }
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(pObject, &Iterator) ) return false;
    for ( ; ; ) {
        size_t i;
        bool Found = false;
        Result = xrtValueIterAdvance(&Iterator, &Key, &pValue);
        if ( Result == XVALUE_ITER_END ) break;
        if ( Result == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
        for ( i = 0u; i < AllowedCount; ++i ) {
            if ( MdoMcpViewEqual(Key.String, Allowed[i]) ) {
                Found = true;
                break;
            }
        }
        if ( !Found ) {
            MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                "%s contains unknown key '%.*s'", ObjectName,
                (int)Key.String.Size, Key.String.Data);
            xrtValueIterEnd(&Iterator);
            return false;
        }
    }
    xrtValueIterEnd(&Iterator);
    return true;
}

static bool MdoMcpCopyRequiredString(const xvalue* pObject, cstr Name,
    size_t Limit, char** pTarget, char Error[MDO_MCP_ERROR_LIMIT])
{
    xstrview View;
    if ( !MdoMcpValueString(MdoMcpObjectGet(pObject, Name), &View) ||
         !MdoMcpStringCopy(pTarget, View, Limit, true) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "%s must be a nonempty string of at most %zu bytes", Name, Limit);
        return false;
    }
    return true;
}

static bool MdoMcpParseStringArray(const xvalue* pValue, size_t Limit,
    size_t StringLimit, bool EmptyStrings, char*** ppItems, size_t* pCount,
    cstr Name, char Error[MDO_MCP_ERROR_LIMIT])
{
    char** Items = NULL;
    size_t Count;
    size_t i;
    *ppItems = NULL;
    *pCount = 0u;
    if ( pValue == NULL || xrtValueType(pValue) != XVALUE_ARRAY ||
         xrtValueCount(pValue) > Limit ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "%s must be an array with at most %zu entries", Name, Limit);
        return false;
    }
    Count = xrtValueCount(pValue);
    if ( Count != 0u ) {
        Items = (char**)xrtCalloc(Count, sizeof(*Items));
        if ( Items == NULL ) return false;
    }
    for ( i = 0u; i < Count; ++i ) {
        xstrview View;
        size_t j;
        if ( !MdoMcpValueString(xrtValueArrayGet(pValue, i), &View) ||
             (!EmptyStrings && View.Size == 0u) || View.Size > StringLimit ||
             MdoMcpBytesContainZero(View.Data, View.Size) ||
             !MdoMcpStringCopy(&Items[i], View, StringLimit,
                !EmptyStrings) ) {
            MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                "%s contains an invalid string", Name);
            MdoMcpStringArrayUnit(Items, Count);
            return false;
        }
        if ( EmptyStrings && View.Size == 0u ) {
            Items[i] = xrtStrDup("");
            if ( Items[i] == NULL ) {
                MdoMcpStringArrayUnit(Items, Count);
                return false;
            }
        }
        for ( j = 0u; j < i; ++j ) {
            if ( strcmp(Items[i], Items[j]) == 0 ) {
                MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                    "%s contains a duplicate entry", Name);
                MdoMcpStringArrayUnit(Items, Count);
                return false;
            }
        }
    }
    *ppItems = Items;
    *pCount = Count;
    return true;
}

static bool MdoMcpResolveSecret(xstrview Reference, char** pValue,
    char Error[MDO_MCP_ERROR_LIMIT])
{
    const xerror* pError;
    cstr Message;
    if ( MdoSecretResolve(Reference, MDO_MCP_SECRET_LIMIT, pValue) )
        return true;
    pError = xrtGetError();
    Message = pError != NULL ? xrtErrorMessage(pError) : NULL;
    MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT, "%s",
        Message != NULL ? Message : "secret_ref cannot be resolved");
    xrtClearError();
    return false;
}

static bool MdoMcpEnvironmentNameValid(cstr Name)
{
    const unsigned char* p = (const unsigned char*)Name;
    size_t i = 0u;
    if ( p == NULL || p[0] == 0u ) return false;
    while ( p[i] != 0u ) {
        if ( i >= 256u || p[i] == '=' || p[i] < 0x20u ) return false;
        ++i;
    }
    return true;
}

static bool MdoMcpHeaderNameValid(xstrview Name)
{
    size_t i;
    if ( Name.Data == NULL || Name.Size == 0u || Name.Size > 256u )
        return false;
    for ( i = 0u; i < Name.Size; ++i ) {
        unsigned char Ch = (unsigned char)Name.Data[i];
        bool Token = (Ch >= 'a' && Ch <= 'z') ||
            (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') ||
            Ch == '!' || Ch == '#' || Ch == '$' || Ch == '%' ||
            Ch == '&' || Ch == '\'' || Ch == '*' || Ch == '+' ||
            Ch == '-' || Ch == '.' || Ch == '^' || Ch == '_' ||
            Ch == '`' || Ch == '|' || Ch == '~';
        if ( !Token ) return false;
    }
    return true;
}

static unsigned char MdoMcpAsciiLower(unsigned char Ch)
{
    return Ch >= 'A' && Ch <= 'Z' ? (unsigned char)(Ch + ('a' - 'A')) : Ch;
}

static bool MdoMcpHeaderNameEqual(xstrview Left, xstrview Right)
{
    size_t i;
    if ( Left.Size != Right.Size ) return false;
    for ( i = 0u; i < Left.Size; ++i ) {
        if ( MdoMcpAsciiLower((unsigned char)Left.Data[i]) !=
             MdoMcpAsciiLower((unsigned char)Right.Data[i]) ) return false;
    }
    return true;
}

static bool MdoMcpHeaderNameEqualsText(xstrview Name, cstr Text)
{
    return MdoMcpHeaderNameEqual(Name,
        (xstrview){ Text, Text != NULL ? strlen(Text) : 0u });
}

static bool MdoMcpHeaderNameReserved(xstrview Name)
{
    bool Mcp = Name.Size >= 4u &&
        MdoMcpAsciiLower((unsigned char)Name.Data[0]) == 'm' &&
        MdoMcpAsciiLower((unsigned char)Name.Data[1]) == 'c' &&
        MdoMcpAsciiLower((unsigned char)Name.Data[2]) == 'p' &&
        Name.Data[3] == '-';
    return Mcp || MdoMcpHeaderNameEqualsText(Name, "accept") ||
        MdoMcpHeaderNameEqualsText(Name, "content-type") ||
        MdoMcpHeaderNameEqualsText(Name, "content-length") ||
        MdoMcpHeaderNameEqualsText(Name, "transfer-encoding") ||
        MdoMcpHeaderNameEqualsText(Name, "connection") ||
        MdoMcpHeaderNameEqualsText(Name, "host");
}

static bool MdoMcpHeaderValueValid(cstr Value)
{
    const unsigned char* p = (const unsigned char*)Value;
    if ( p == NULL || p[0] == 0u || strlen(Value) > MDO_MCP_SECRET_LIMIT )
        return false;
    for ( ; *p != 0u; ++p ) {
        if ( *p == '\r' || *p == '\n' || (*p < 0x20u && *p != '\t') ||
             *p == 0x7fu ) return false;
    }
    return true;
}

static bool MdoMcpHttpEndpointValid(cstr Endpoint)
{
    const unsigned char* p = (const unsigned char*)Endpoint;
    const unsigned char* Authority;
    const unsigned char* AuthorityEnd;
    const unsigned char* HostEnd;
    const unsigned char* Port = NULL;
    uint32 PortValue = 0u;
    size_t Size = Endpoint != NULL ? strlen(Endpoint) : 0u;
    size_t i;
    if ( Size == 0u || Size > MDO_MCP_STRING_LIMIT ||
         strchr(Endpoint, '\r') != NULL || strchr(Endpoint, '\n') != NULL )
        return false;
    if ( Size < 9u || MdoMcpAsciiLower(p[0]) != 'h' ||
         MdoMcpAsciiLower(p[1]) != 't' || MdoMcpAsciiLower(p[2]) != 't' ||
         MdoMcpAsciiLower(p[3]) != 'p' || MdoMcpAsciiLower(p[4]) != 's' ||
         p[5] != ':' || p[6] != '/' || p[7] != '/' ||
         strchr(Endpoint, '#') != NULL ) return false;
    Authority = p + 8u;
    AuthorityEnd = Authority;
    while ( *AuthorityEnd != 0u && *AuthorityEnd != '/' &&
            *AuthorityEnd != '?' ) ++AuthorityEnd;
    if ( Authority == AuthorityEnd ||
         memchr(Authority, '@', (size_t)(AuthorityEnd - Authority)) != NULL )
        return false;
    for ( i = 0u; i < (size_t)(AuthorityEnd - Authority); ++i ) {
        unsigned char Ch = Authority[i];
        if ( Ch <= 0x20u || Ch >= 0x7fu || Ch == '\\' ) return false;
    }
    if ( Authority[0] == '[' ) {
        const unsigned char* Close = (const unsigned char*)memchr(Authority,
            ']', (size_t)(AuthorityEnd - Authority));
        if ( Close == NULL || Close == Authority + 1u ) return false;
        HostEnd = Close + 1u;
        if ( HostEnd != AuthorityEnd ) {
            if ( *HostEnd != ':' ) return false;
            Port = HostEnd + 1u;
        }
    } else {
        const unsigned char* Cursor;
        HostEnd = AuthorityEnd;
        for ( Cursor = Authority; Cursor != AuthorityEnd; ++Cursor ) {
            unsigned char Ch = *Cursor;
            if ( Ch == ':' ) {
                if ( Port != NULL ) return false;
                HostEnd = Cursor;
                Port = Cursor + 1u;
            } else if ( !((Ch >= 'a' && Ch <= 'z') ||
                          (Ch >= 'A' && Ch <= 'Z') ||
                          (Ch >= '0' && Ch <= '9') || Ch == '-' ||
                          Ch == '.') ) return false;
        }
        if ( HostEnd == Authority ) return false;
    }
    if ( Port != NULL ) {
        const unsigned char* Cursor;
        if ( Port == AuthorityEnd ) return false;
        for ( Cursor = Port; Cursor != AuthorityEnd; ++Cursor ) {
            if ( *Cursor < '0' || *Cursor > '9' ) return false;
            PortValue = PortValue * 10u + (uint32)(*Cursor - '0');
            if ( PortValue > 65535u ) return false;
        }
        if ( PortValue == 0u ) return false;
    }
    return true;
}

static bool MdoMcpSecretReferenceSyntaxValid(xstrview Reference)
{
    static const char* const Prefixes[] = {
        "env:", "file:", "keychain:", "prompt:"
    };
    size_t i;
    if ( Reference.Data == NULL || Reference.Size == 0u ||
         Reference.Size > MDO_MCP_STRING_LIMIT ||
         MdoMcpBytesContainZero(Reference.Data, Reference.Size) ) return false;
    for ( i = 0u; i < sizeof(Prefixes) / sizeof(Prefixes[0]); ++i ) {
        size_t PrefixSize = strlen(Prefixes[i]);
        if ( Reference.Size > PrefixSize &&
             memcmp(Reference.Data, Prefixes[i], PrefixSize) == 0 ) {
            if ( i == 1u ) {
                char* Path = xrtStrDupN(Reference.Data + PrefixSize,
                    Reference.Size - PrefixSize);
                bool Valid;
                if ( Path == NULL ) return false;
                Valid = MdoMcpPortablePathValid(Path);
                xrtFree(Path);
                return Valid;
            }
            return true;
        }
    }
    return false;
}

static bool MdoMcpParseEnvironment(const xvalue* pValue,
    MdoMcpServerEntry* pEntry, char Error[MDO_MCP_ERROR_LIMIT])
{
    static const char* const Keys[] = { "name", "secret_ref", "remove" };
    size_t Count;
    size_t i;
    if ( pValue == NULL || xrtValueType(pValue) != XVALUE_ARRAY ||
         xrtValueCount(pValue) > MDO_MCP_ENVIRONMENT_LIMIT ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "transport.environment must be a bounded array");
        return false;
    }
    Count = xrtValueCount(pValue);
    if ( Count != 0u ) {
        pEntry->Environment = (MdoMcpEnvironmentEntry*)xrtCalloc(Count,
            sizeof(*pEntry->Environment));
        if ( pEntry->Environment == NULL ) return false;
    }
    for ( i = 0u; i < Count; ++i ) {
        const xvalue* pItem = xrtValueArrayGet(pValue, i);
        const xvalue* pSecret;
        const xvalue* pRemove;
        xstrview Secret;
        bool Remove = false;
        size_t j;
        if ( !MdoMcpObjectKeys(pItem, Keys,
                sizeof(Keys) / sizeof(Keys[0]), "environment entry", Error) ||
             !MdoMcpCopyRequiredString(pItem, "name", 256u,
                &pEntry->Environment[i].Name, Error) ||
             !MdoMcpEnvironmentNameValid(pEntry->Environment[i].Name) ) {
            if ( Error[0] == '\0' ) MdoMcpFormatError(Error,
                MDO_MCP_ERROR_LIMIT, "environment name is invalid");
            return false;
        }
        pEntry->EnvironmentCount = i + 1u;
        for ( j = 0u; j < i; ++j ) {
            if ( strcmp(pEntry->Environment[i].Name,
                    pEntry->Environment[j].Name) == 0 ) {
                MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                    "transport.environment contains a duplicate name");
                return false;
            }
        }
        pSecret = MdoMcpObjectGet(pItem, "secret_ref");
        pRemove = MdoMcpObjectGet(pItem, "remove");
        if ( (pSecret != NULL) == (pRemove != NULL) ) {
            MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                "environment entry needs exactly one of secret_ref or remove");
            return false;
        }
        if ( pRemove != NULL ) {
            if ( !MdoMcpValueBool(pRemove, &Remove) || !Remove ) {
                MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                    "environment remove must be true");
                return false;
            }
        } else if ( !MdoMcpValueString(pSecret, &Secret) ||
                    !MdoMcpResolveSecret(Secret,
                        &pEntry->Environment[i].Value, Error) ) {
            return false;
        }
    }
    return true;
}

static bool MdoMcpParseWorkingDirectory(const xvalue* pValue,
    char** pDirectory, char Error[MDO_MCP_ERROR_LIMIT])
{
    xstrview View;
    char* Text = NULL;
    if ( pValue == NULL || xrtValueType(pValue) == XVALUE_NULL ) {
        *pDirectory = NULL;
        return true;
    }
    if ( !MdoMcpValueString(pValue, &View) ||
         !MdoMcpStringCopy(&Text, View, MDO_MCP_STRING_LIMIT, true) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "working_directory must be null, absolute, or mdo-home relative");
        return false;
    }
    if ( strncmp(Text, "mdo-home:", 9u) == 0 ) {
        MdoHomeSnapshot Home;
        char* Joined;
        if ( !MdoMcpPortablePathValid(Text + 9u) ) {
            xrtFree(Text);
            MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                "mdo-home working_directory is not portable");
            return false;
        }
        memset(&Home, 0, sizeof(Home));
        Home.Size = sizeof(Home);
        if ( !MdoHomeGetSnapshot(&Home) ) {
            xrtFree(Text);
            return false;
        }
        Joined = xrtPathJoin(Home.Path, Text + 9u);
        xrtFree(Text);
        if ( Joined == NULL ) return false;
        *pDirectory = Joined;
        return true;
    }
    if ( !xrtPathIsAbs(Text) ) {
        xrtFree(Text);
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "working_directory must be absolute or use mdo-home:");
        return false;
    }
    *pDirectory = Text;
    return true;
}

static bool MdoMcpParseEffects(const xvalue* pValue,
    xwork_tool_effects* pEffects, char Error[MDO_MCP_ERROR_LIMIT])
{
    size_t Count;
    size_t i;
    xwork_tool_effects Effects = 0u;
    if ( pValue == NULL || xrtValueType(pValue) != XVALUE_ARRAY ||
         xrtValueCount(pValue) == 0u || xrtValueCount(pValue) > 8u ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "security.default_effects must be a nonempty effect array");
        return false;
    }
    Count = xrtValueCount(pValue);
    for ( i = 0u; i < Count; ++i ) {
        xstrview View;
        xwork_tool_effects Effect = 0u;
        if ( !MdoMcpValueString(xrtValueArrayGet(pValue, i), &View) )
            goto invalid;
        if ( MdoMcpViewEqual(View, "read") )
            Effect = XWORK_TOOL_EFFECT_READ;
        else if ( MdoMcpViewEqual(View, "workspace-write") )
            Effect = XWORK_TOOL_EFFECT_WORKSPACE_WRITE;
        else if ( MdoMcpViewEqual(View, "process") )
            Effect = XWORK_TOOL_EFFECT_PROCESS;
        else if ( MdoMcpViewEqual(View, "network") )
            Effect = XWORK_TOOL_EFFECT_NETWORK;
        else if ( MdoMcpViewEqual(View, "external-service") )
            Effect = XWORK_TOOL_EFFECT_EXTERNAL_SERVICE;
        else if ( MdoMcpViewEqual(View, "secrets") )
            Effect = XWORK_TOOL_EFFECT_SECRETS;
        else if ( MdoMcpViewEqual(View, "schedule") )
            Effect = XWORK_TOOL_EFFECT_SCHEDULE;
        else if ( MdoMcpViewEqual(View, "agent-delegation") )
            Effect = XWORK_TOOL_EFFECT_AGENT_DELEGATION;
        else goto invalid;
        if ( (Effects & Effect) != 0u ) goto invalid;
        Effects |= Effect;
    }
    *pEffects = Effects;
    return true;

invalid:
    MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
        "security.default_effects contains an unknown or duplicate effect");
    return false;
}

static bool MdoMcpParseStdio(const xvalue* pTransport,
    MdoMcpServerEntry* pEntry, char Error[MDO_MCP_ERROR_LIMIT])
{
    static const char* const Keys[] = {
        "type", "program", "arguments", "working_directory",
        "inherit_environment", "environment"
    };
    bool Inherit;
    if ( !MdoMcpObjectKeys(pTransport, Keys,
            sizeof(Keys) / sizeof(Keys[0]), "transport", Error) ||
         !MdoMcpCopyRequiredString(pTransport, "program",
            MDO_MCP_STRING_LIMIT, &pEntry->Program, Error) ||
         !MdoMcpParseStringArray(MdoMcpObjectGet(pTransport, "arguments"),
            MDO_MCP_ARGUMENT_LIMIT, MDO_MCP_STRING_LIMIT, true,
            &pEntry->Arguments, &pEntry->ArgumentCount,
            "transport.arguments", Error) ||
         !MdoMcpParseWorkingDirectory(
            MdoMcpObjectGet(pTransport, "working_directory"),
            &pEntry->WorkingDirectory, Error) ||
         !MdoMcpValueBool(MdoMcpObjectGet(pTransport,
            "inherit_environment"), &Inherit) ||
         !MdoMcpParseEnvironment(MdoMcpObjectGet(pTransport, "environment"),
            pEntry, Error) ) {
        if ( Error[0] == '\0' ) MdoMcpFormatError(Error,
            MDO_MCP_ERROR_LIMIT, "stdio transport is incomplete");
        return false;
    }
    pEntry->Transport = MDO_MCP_TRANSPORT_STDIO;
    pEntry->InheritEnvironment = Inherit;
    return true;
}

static bool MdoMcpParseHttp(const xvalue* pTransport,
    MdoMcpServerEntry* pEntry, char Error[MDO_MCP_ERROR_LIMIT])
{
    static const char* const Keys[] = { "type", "endpoint", "headers" };
    static const char* const HeaderKeys[] = { "name", "secret_ref" };
    const xvalue* pHeaders = MdoMcpObjectGet(pTransport, "headers");
    size_t Count;
    size_t i;
    if ( !MdoMcpObjectKeys(pTransport, Keys,
            sizeof(Keys) / sizeof(Keys[0]), "transport", Error) ||
         !MdoMcpCopyRequiredString(pTransport, "endpoint",
            MDO_MCP_STRING_LIMIT, &pEntry->Endpoint, Error) ||
         !MdoMcpHttpEndpointValid(pEntry->Endpoint) ||
         pHeaders == NULL || xrtValueType(pHeaders) != XVALUE_ARRAY ||
         xrtValueCount(pHeaders) > MDO_MCP_ENVIRONMENT_LIMIT ) {
        if ( Error[0] == '\0' ) MdoMcpFormatError(Error,
            MDO_MCP_ERROR_LIMIT,
            "streamable-http requires an https endpoint and bounded headers");
        return false;
    }
    Count = xrtValueCount(pHeaders);
    if ( Count != 0u ) {
        pEntry->HttpHeaders = (MdoMcpHttpHeaderEntry*)xrtCalloc(Count,
            sizeof(*pEntry->HttpHeaders));
        if ( pEntry->HttpHeaders == NULL ) return false;
    }
    for ( i = 0u; i < Count; ++i ) {
        const xvalue* pHeader = xrtValueArrayGet(pHeaders, i);
        xstrview Name;
        xstrview Reference;
        size_t j;
        if ( !MdoMcpObjectKeys(pHeader, HeaderKeys,
                sizeof(HeaderKeys) / sizeof(HeaderKeys[0]),
                "streamable-http header", Error) ||
             !MdoMcpValueString(MdoMcpObjectGet(pHeader, "name"), &Name) ||
             !MdoMcpHeaderNameValid(Name) ||
             MdoMcpHeaderNameReserved(Name) ||
             !MdoMcpValueString(MdoMcpObjectGet(pHeader, "secret_ref"),
                &Reference) ||
             !MdoMcpSecretReferenceSyntaxValid(Reference) ) {
            if ( Error[0] == '\0' ) MdoMcpFormatError(Error,
                MDO_MCP_ERROR_LIMIT,
                "streamable-http header needs a token name and secret_ref");
            return false;
        }
        if ( !MdoMcpStringCopy(&pEntry->HttpHeaders[i].Name, Name, 256u,
                true) ) return false;
        pEntry->HttpHeaderCount = i + 1u;
        for ( j = 0u; j < i; ++j ) {
            xstrview Previous = {
                pEntry->HttpHeaders[j].Name,
                strlen(pEntry->HttpHeaders[j].Name)
            };
            if ( MdoMcpHeaderNameEqual(Name, Previous) ) {
                MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                    "streamable-http headers contain a duplicate name");
                return false;
            }
        }
        if ( !MdoMcpResolveSecret(Reference,
                &pEntry->HttpHeaders[i].Value, Error) ) return false;
        if ( !MdoMcpHeaderValueValid(pEntry->HttpHeaders[i].Value) ) {
            MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
                "streamable-http header secret contains unsafe bytes");
            return false;
        }
    }
    pEntry->Transport = MDO_MCP_TRANSPORT_STREAMABLE_HTTP;
    return true;
}

static bool MdoMcpParseServer(const MdoMcpSource* pSource,
    const char* Text, size_t TextSize, MdoMcpServerEntry* pEntry,
    MdoMcpDiagnosticStage* pStage, char Error[MDO_MCP_ERROR_LIMIT])
{
    static const char* const RootKeys[] = {
        "schema_version", "id", "name", "description", "enabled",
        "transport", "protocol_version", "startup_timeout_ms",
        "request_timeout_ms", "limits", "tools", "security",
        "auto_reconnect"
    };
    static const char* const LimitKeys[] = { "message_bytes", "tools" };
    static const char* const ToolKeys[] = { "allow", "deny" };
    static const char* const SecurityKeys[] = {
        "default_effects", "permission_profile", "trust_read_only_annotations"
    };
    xjsonreadconfig Config;
    xvalue* pRoot = NULL;
    const xvalue* pTransport;
    const xvalue* pLimits;
    const xvalue* pTools;
    const xvalue* pSecurity;
    xstrview View;
    uint64 Number;
    bool Flag;
    bool Ok = false;

    *pStage = MDO_MCP_DIAGNOSTIC_PARSE;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_MCP_FILE_LIMIT;
    Config.MaxStringBytes = MDO_MCP_FILE_LIMIT;
    Config.MaxDepth = 32u;
    Config.MaxValues = 4096u;
    Config.MaxContainerItems = 1024u;
    pRoot = xrtJsonRead((xstrview){ Text, TextSize }, &Config);
    if ( pRoot == NULL ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "MCP source is not valid strict JSON");
        goto done;
    }
    if ( !MdoMcpObjectKeys(pRoot, RootKeys,
            sizeof(RootKeys) / sizeof(RootKeys[0]), "MCP document", Error) )
        goto done;
    if ( !MdoMcpValueUnsigned(MdoMcpObjectGet(pRoot, "schema_version"),
            &Number) || Number != MDO_MCP_CONFIG_SCHEMA_VERSION ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "schema_version must be 1");
        goto done;
    }
    if ( !MdoMcpCopyRequiredString(pRoot, "id", 96u, &pEntry->Id, Error) ||
         strcmp(pEntry->Id, pSource->Id) != 0 ||
         !MdoMcpIdValid(pEntry->Id) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "document id must match its MCP file name");
        goto done;
    }
    if ( !MdoMcpCopyRequiredString(pRoot, "name", 256u,
            &pEntry->Name, Error) ||
         !MdoMcpCopyRequiredString(pRoot, "description",
            MDO_MCP_DESCRIPTION_LIMIT, &pEntry->Description, Error) ||
         strchr(pEntry->Description, '\r') != NULL ||
         strchr(pEntry->Description, '\n') != NULL ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "name and single-line description are required");
        goto done;
    }
    if ( !MdoMcpValueBool(MdoMcpObjectGet(pRoot, "enabled"), &Flag) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "enabled must be boolean");
        goto done;
    }
    pEntry->Enabled = Flag;
    if ( !MdoMcpValueBool(MdoMcpObjectGet(pRoot, "auto_reconnect"), &Flag) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "auto_reconnect must be boolean");
        goto done;
    }
    pEntry->AutoReconnect = Flag;
    if ( !MdoMcpCopyRequiredString(pRoot, "protocol_version", 32u,
            &pEntry->ProtocolVersion, Error) ||
         (strcmp(pEntry->ProtocolVersion, "2026-07-28") != 0 &&
          strcmp(pEntry->ProtocolVersion, "2025-11-25") != 0) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "protocol_version is unsupported");
        goto done;
    }
    if ( !MdoMcpValueUnsigned(MdoMcpObjectGet(pRoot,
            "startup_timeout_ms"), &Number) || Number < 100u ||
         Number > 300000u ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "startup_timeout_ms must be between 100 and 300000");
        goto done;
    }
    pEntry->StartupTimeoutMilliseconds = (uint32)Number;
    if ( !MdoMcpValueUnsigned(MdoMcpObjectGet(pRoot,
            "request_timeout_ms"), &Number) || Number < 100u ||
         Number > 300000u ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "request_timeout_ms must be between 100 and 300000");
        goto done;
    }
    pEntry->RequestTimeoutMilliseconds = (uint32)Number;

    pLimits = MdoMcpObjectGet(pRoot, "limits");
    if ( !MdoMcpObjectKeys(pLimits, LimitKeys,
            sizeof(LimitKeys) / sizeof(LimitKeys[0]), "limits", Error) ||
         !MdoMcpValueUnsigned(MdoMcpObjectGet(pLimits, "message_bytes"),
            &Number) || Number < 1024u || Number > 16u * 1024u * 1024u ) {
        if ( Error[0] == '\0' ) MdoMcpFormatError(Error,
            MDO_MCP_ERROR_LIMIT, "limits.message_bytes is out of range");
        goto done;
    }
    pEntry->MaxMessageBytes = (size_t)Number;
    if ( !MdoMcpValueUnsigned(MdoMcpObjectGet(pLimits, "tools"), &Number) ||
         Number == 0u || Number > 4096u ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "limits.tools must be between 1 and 4096");
        goto done;
    }
    pEntry->MaxTools = (size_t)Number;

    pTools = MdoMcpObjectGet(pRoot, "tools");
    if ( !MdoMcpObjectKeys(pTools, ToolKeys,
            sizeof(ToolKeys) / sizeof(ToolKeys[0]), "tools", Error) ||
         !MdoMcpParseStringArray(MdoMcpObjectGet(pTools, "allow"),
            MDO_MCP_FILTER_LIMIT, 512u, false, &pEntry->AllowedTools,
            &pEntry->AllowedToolCount, "tools.allow", Error) ||
         !MdoMcpParseStringArray(MdoMcpObjectGet(pTools, "deny"),
            MDO_MCP_FILTER_LIMIT, 512u, false, &pEntry->DeniedTools,
            &pEntry->DeniedToolCount, "tools.deny", Error) ) goto done;

    pSecurity = MdoMcpObjectGet(pRoot, "security");
    if ( !MdoMcpObjectKeys(pSecurity, SecurityKeys,
            sizeof(SecurityKeys) / sizeof(SecurityKeys[0]), "security", Error) ||
         !MdoMcpParseEffects(MdoMcpObjectGet(pSecurity, "default_effects"),
            &pEntry->DefaultEffects, Error) ||
         !MdoMcpCopyRequiredString(pSecurity, "permission_profile", 96u,
            &pEntry->PermissionProfile, Error) ||
         !MdoMcpIdValid(pEntry->PermissionProfile) ||
         !MdoMcpValueBool(MdoMcpObjectGet(pSecurity,
            "trust_read_only_annotations"), &Flag) ) {
        if ( Error[0] == '\0' ) MdoMcpFormatError(Error,
            MDO_MCP_ERROR_LIMIT, "security is incomplete or invalid");
        goto done;
    }
    pEntry->TrustReadOnlyAnnotations = Flag;

    pTransport = MdoMcpObjectGet(pRoot, "transport");
    if ( pTransport == NULL || xrtValueType(pTransport) != XVALUE_OBJECT ||
         !MdoMcpValueString(MdoMcpObjectGet(pTransport, "type"), &View) ) {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "transport.type is required");
        goto done;
    }
    if ( MdoMcpViewEqual(View, "stdio") ) {
        if ( !MdoMcpParseStdio(pTransport, pEntry, Error) ) {
            *pStage = MdoMcpMemoryError() ? MDO_MCP_DIAGNOSTIC_PARSE :
                (strstr(Error, "secret") != NULL ?
                    MDO_MCP_DIAGNOSTIC_SECRET : MDO_MCP_DIAGNOSTIC_PARSE);
            goto done;
        }
    } else if ( MdoMcpViewEqual(View, "streamable-http") ) {
        if ( !MdoMcpParseHttp(pTransport, pEntry, Error) ) {
            *pStage = MdoMcpMemoryError() ? MDO_MCP_DIAGNOSTIC_PARSE :
                (strstr(Error, "secret") != NULL ?
                    MDO_MCP_DIAGNOSTIC_SECRET : MDO_MCP_DIAGNOSTIC_PARSE);
            goto done;
        }
    } else {
        MdoMcpFormatError(Error, MDO_MCP_ERROR_LIMIT,
            "transport.type is unsupported");
        goto done;
    }
    Ok = true;

done:
    xrtValueRelease(pRoot);
    return Ok;
}

static bool MdoMcpCatalogAppend(MdoMcpCatalog* pCatalog,
    MdoMcpServerEntry* pEntry)
{
    if ( !MdoMcpGrow((void**)&pCatalog->Entries, &pCatalog->Capacity,
            pCatalog->Count + 1u, sizeof(*pCatalog->Entries),
            MDO_MCP_SERVER_LIMIT) ) return false;
    pCatalog->Entries[pCatalog->Count++] = *pEntry;
    memset(pEntry, 0, sizeof(*pEntry));
    return true;
}

/* Parse only; editing a server never launches its program or sends a request. */
bool MdoMcpValidateText(cstr Id, cstr Text, char* Error, size_t Capacity)
{
    MdoMcpSource Source;
    MdoMcpServerEntry Entry;
    MdoMcpDiagnosticStage Stage;
    char Message[MDO_MCP_ERROR_LIMIT] = {0};
    bool Ok;
    memset(&Source, 0, sizeof(Source)); memset(&Entry, 0, sizeof(Entry));
    Source.Id = (char*)Id;
    Ok = Id != NULL && Text != NULL && MdoMcpParseServer(&Source, Text,
        strlen(Text), &Entry, &Stage, Message);
    MdoMcpServerUnit(&Entry);
    if (!Ok && Error != NULL && Capacity != 0u)
        snprintf(Error, Capacity, "%s", Message[0] != '\0' ? Message : "Invalid MCP definition");
    return Ok;
}

static bool MdoMcpBuildCandidate(uint64 Generation,
    MdoMcpCatalog** ppCatalog, MdoMcpDiagnostics** ppDiagnostics)
{
    MdoMcpCatalog* pCatalog = NULL;
    MdoMcpDiagnostics* pDiagnostics = NULL;
    MdoMcpSource* Sources = NULL;
    size_t SourceCount = 0u;
    size_t i;
    bool Ok = false;

    *ppCatalog = NULL;
    *ppDiagnostics = NULL;
    pCatalog = MdoMcpCatalogCreate(Generation);
    pDiagnostics = MdoMcpDiagnosticsCreate();
    if ( pCatalog == NULL || pDiagnostics == NULL ||
         !MdoMcpDiscover(&Sources, &SourceCount, pDiagnostics) ) goto done;
    for ( i = 0u; i < SourceCount; ++i ) {
        MdoMcpServerEntry Entry;
        MdoMcpDiagnosticStage Stage = MDO_MCP_DIAGNOSTIC_PARSE;
        char* Text = NULL;
        size_t TextSize = 0u;
        char Error[MDO_MCP_ERROR_LIMIT] = {0};
        bool Parsed;

        memset(&Entry, 0, sizeof(Entry));
        Entry.External = Sources[i].External;
        Entry.SourcePath = xrtStrDup(Sources[i].VirtualPath);
        if ( Entry.SourcePath == NULL ||
             !MdoMcpReadSource(&Sources[i], &Text, &TextSize, Error) ) {
            bool Fatal = MdoMcpMemoryError();
            if ( !MdoMcpDiagnosticAdd(pDiagnostics,
                    MDO_MCP_DIAGNOSTIC_READ, Sources[i].Id,
                    Sources[i].VirtualPath, NULL,
                    Error[0] != '\0' ? Error : "MCP source cannot be read") )
                Fatal = true;
            MdoMcpServerUnit(&Entry);
            xrtFree(Text);
            if ( Fatal ) goto done;
            xrtClearError();
            continue;
        }
        if ( !MdoMcpHash(Text, TextSize, Entry.SourceHash) ) {
            MdoMcpServerUnit(&Entry);
            xrtFree(Text);
            goto done;
        }
        Parsed = MdoMcpParseServer(&Sources[i], Text, TextSize, &Entry,
            &Stage, Error);
        xrtFree(Text);
        if ( !Parsed ) {
            bool Fatal = MdoMcpMemoryError();
            if ( !MdoMcpDiagnosticAdd(pDiagnostics, Stage, Sources[i].Id,
                    Sources[i].VirtualPath, Entry.SourceHash,
                    Error[0] != '\0' ? Error :
                    "MCP source does not satisfy schema version 1") )
                Fatal = true;
            MdoMcpServerUnit(&Entry);
            if ( Fatal ) goto done;
            xrtClearError();
            continue;
        }
        if ( !MdoMcpCatalogAppend(pCatalog, &Entry) ) {
            MdoMcpServerUnit(&Entry);
            goto done;
        }
    }
    *ppCatalog = pCatalog;
    *ppDiagnostics = pDiagnostics;
    pCatalog = NULL;
    pDiagnostics = NULL;
    Ok = true;

done:
    MdoMcpSourcesUnit(Sources, SourceCount);
    MdoMcpCatalogRelease(pCatalog);
    MdoMcpDiagnosticsRelease(pDiagnostics);
    return Ok;
}

static bool MdoMcpPublishCandidate(MdoMcpCatalog* pCatalog,
    MdoMcpDiagnostics* pDiagnostics)
{
    xwork_mcp_server_config* Configs = NULL;
    xwork_mcp_environment** Environment = NULL;
    xwork_mcp_http_header** HttpHeaders = NULL;
    xwork_error Error;
    MdoMcpCatalog* pOldCatalog;
    MdoMcpDiagnostics* pOldDiagnostics;
    size_t Replaced = 0u;
    size_t i;
    bool Registered = false;
    bool Ok = false;

    if ( pCatalog->Count != 0u ) {
        Configs = (xwork_mcp_server_config*)xrtCalloc(pCatalog->Count,
            sizeof(*Configs));
        Environment = (xwork_mcp_environment**)xrtCalloc(pCatalog->Count,
            sizeof(*Environment));
        HttpHeaders = (xwork_mcp_http_header**)xrtCalloc(pCatalog->Count,
            sizeof(*HttpHeaders));
        if ( Configs == NULL || Environment == NULL || HttpHeaders == NULL )
            goto done;
    }
    for ( i = 0u; i < pCatalog->Count; ++i ) {
        MdoMcpServerEntry* pEntry = &pCatalog->Entries[i];
        size_t j;
        xworkMcpServerConfigInit(&Configs[i]);
        if ( pEntry->EnvironmentCount != 0u ) {
            Environment[i] = (xwork_mcp_environment*)xrtCalloc(
                pEntry->EnvironmentCount, sizeof(*Environment[i]));
            if ( Environment[i] == NULL ) goto done;
            for ( j = 0u; j < pEntry->EnvironmentCount; ++j ) {
                Environment[i][j].sName = pEntry->Environment[j].Name;
                Environment[i][j].sValue = pEntry->Environment[j].Value;
            }
        }
        if ( pEntry->HttpHeaderCount != 0u ) {
            HttpHeaders[i] = (xwork_mcp_http_header*)xrtCalloc(
                pEntry->HttpHeaderCount, sizeof(*HttpHeaders[i]));
            if ( HttpHeaders[i] == NULL ) goto done;
            for ( j = 0u; j < pEntry->HttpHeaderCount; ++j ) {
                HttpHeaders[i][j].sName = pEntry->HttpHeaders[j].Name;
                HttpHeaders[i][j].sValue = pEntry->HttpHeaders[j].Value;
            }
        }
        Configs[i].sServerId = pEntry->Id;
        Configs[i].sSummary = pEntry->Description;
        Configs[i].sProgram = pEntry->Program;
        Configs[i].psArguments = (const char* const*)pEntry->Arguments;
        Configs[i].iArgumentCount = pEntry->ArgumentCount;
        Configs[i].sWorkingDirectory = pEntry->WorkingDirectory;
        Configs[i].pEnvironment = Environment[i];
        Configs[i].iEnvironmentCount = pEntry->EnvironmentCount;
        Configs[i].bInheritEnvironment = pEntry->InheritEnvironment;
        Configs[i].sProtocolVersion = pEntry->ProtocolVersion;
        Configs[i].uStartupTimeoutMs = pEntry->StartupTimeoutMilliseconds;
        Configs[i].uRequestTimeoutMs = pEntry->RequestTimeoutMilliseconds;
        Configs[i].iMaxMessageBytes = pEntry->MaxMessageBytes;
        Configs[i].iMaxTools = pEntry->MaxTools;
        Configs[i].uDefaultToolEffects = pEntry->DefaultEffects;
        Configs[i].bTrustReadOnlyAnnotations =
            pEntry->TrustReadOnlyAnnotations;
        Configs[i].psAllowedTools =
            (const char* const*)pEntry->AllowedTools;
        Configs[i].iAllowedToolCount = pEntry->AllowedToolCount;
        Configs[i].psDeniedTools =
            (const char* const*)pEntry->DeniedTools;
        Configs[i].iDeniedToolCount = pEntry->DeniedToolCount;
        Configs[i].bEnabled = pEntry->Enabled;
        Configs[i].bAutoReconnect = pEntry->AutoReconnect;
        Configs[i].eTransport = pEntry->Transport ==
                MDO_MCP_TRANSPORT_STREAMABLE_HTTP
            ? XWORK_MCP_TRANSPORT_STREAMABLE_HTTP
            : XWORK_MCP_TRANSPORT_STDIO;
        Configs[i].sEndpoint = pEntry->Endpoint;
        Configs[i].pHttpHeaders = HttpHeaders[i];
        Configs[i].iHttpHeaderCount = pEntry->HttpHeaderCount;
    }
    memset(&Error, 0, sizeof(Error));
    if ( !xrtMutexLock(g_MdoMcp.Lock) ) goto done;
    if ( g_MdoMcp.Initialized && g_MdoMcp.Runtime != NULL )
        Registered = xworkRuntimeReplaceMcpServersBySource(g_MdoMcp.Runtime,
            MDO_MCP_SOURCE, Configs, pCatalog->Count, &Replaced, &Error);
    /* xwork has synchronously deep-copied accepted definitions. Catalog
     * snapshots retain only names and counts, never resolved secret values. */
    MdoMcpCatalogForgetSecrets(pCatalog);
    if ( !Registered ) {
        cstr Message = Error.sMessage[0] != '\0' ? Error.sMessage :
            "xwork rejected the MCP catalog transaction";
        (void)MdoMcpDiagnosticAdd(pDiagnostics,
            MDO_MCP_DIAGNOSTIC_PUBLISH, NULL, NULL, NULL, Message);
        pOldDiagnostics = g_MdoMcp.Diagnostics;
        g_MdoMcp.Diagnostics = MdoMcpDiagnosticsRef(pDiagnostics);
        (void)xrtMutexUnlock(g_MdoMcp.Lock);
        MdoMcpDiagnosticsRelease(pOldDiagnostics);
        goto done;
    }
    pOldCatalog = g_MdoMcp.Catalog;
    pOldDiagnostics = g_MdoMcp.Diagnostics;
    g_MdoMcp.Catalog = MdoMcpCatalogRef(pCatalog);
    g_MdoMcp.Diagnostics = MdoMcpDiagnosticsRef(pDiagnostics);
    g_MdoMcp.NextGeneration = pCatalog->Generation;
    (void)xrtMutexUnlock(g_MdoMcp.Lock);
    MdoMcpCatalogRelease(pOldCatalog);
    MdoMcpDiagnosticsRelease(pOldDiagnostics);
    Ok = true;

done:
    if ( Environment != NULL ) {
        for ( i = 0u; i < pCatalog->Count; ++i ) xrtFree(Environment[i]);
    }
    if ( HttpHeaders != NULL ) {
        for ( i = 0u; i < pCatalog->Count; ++i ) xrtFree(HttpHeaders[i]);
    }
    xrtFree(HttpHeaders);
    xrtFree(Environment);
    xrtFree(Configs);
    return Ok;
}

bool MdoMcpManagerReload(void)
{
    MdoMcpCatalog* pCatalog = NULL;
    MdoMcpDiagnostics* pDiagnostics = NULL;
    uint64 Generation;
    bool Ok;
    if ( g_MdoMcp.ReloadLock == NULL ||
         !xrtMutexLock(g_MdoMcp.ReloadLock) ) {
        MdoMcpSetError("MCP manager is not initialized");
        return false;
    }
    if ( !g_MdoMcp.Initialized || !xrtMutexLock(g_MdoMcp.Lock) ) {
        (void)xrtMutexUnlock(g_MdoMcp.ReloadLock);
        MdoMcpSetError("MCP manager is not initialized");
        return false;
    }
    Generation = g_MdoMcp.NextGeneration + 1u;
    (void)xrtMutexUnlock(g_MdoMcp.Lock);
    Ok = MdoMcpBuildCandidate(Generation, &pCatalog, &pDiagnostics) &&
        MdoMcpPublishCandidate(pCatalog, pDiagnostics);
    if ( !Ok && xrtGetError() == NULL )
        MdoMcpSetError("MCP catalog reload failed");
    MdoMcpCatalogRelease(pCatalog);
    MdoMcpDiagnosticsRelease(pDiagnostics);
    (void)xrtMutexUnlock(g_MdoMcp.ReloadLock);
    return Ok;
}

bool MdoMcpManagerInit(xwork_runtime* pRuntime)
{
    if ( pRuntime == NULL ) {
        MdoMcpSetError("xwork runtime is required by the MCP manager");
        return false;
    }
    if ( g_MdoMcp.Initialized ) return g_MdoMcp.Runtime == pRuntime;
    memset(&g_MdoMcp, 0, sizeof(g_MdoMcp));
    g_MdoMcp.Lock = xrtMutexCreate();
    g_MdoMcp.ReloadLock = xrtMutexCreate();
    g_MdoMcp.Catalog = MdoMcpCatalogCreate(0u);
    g_MdoMcp.Diagnostics = MdoMcpDiagnosticsCreate();
    if ( g_MdoMcp.Lock == NULL || g_MdoMcp.ReloadLock == NULL ||
         g_MdoMcp.Catalog == NULL || g_MdoMcp.Diagnostics == NULL ) {
        MdoMcpManagerUnit();
        MdoMcpSetError("cannot allocate MCP manager state");
        return false;
    }
    g_MdoMcp.Runtime = pRuntime;
    g_MdoMcp.Initialized = true;
    if ( !MdoMcpManagerReload() ) {
        MdoMcpManagerUnit();
        return false;
    }
    return true;
}

void MdoMcpManagerUnit(void)
{
    MdoMcpCatalog* pCatalog = g_MdoMcp.Catalog;
    MdoMcpDiagnostics* pDiagnostics = g_MdoMcp.Diagnostics;
    xmutex* pLock = g_MdoMcp.Lock;
    xmutex* pReloadLock = g_MdoMcp.ReloadLock;
    if ( g_MdoMcp.Runtime != NULL ) {
        xwork_error Error;
        size_t Replaced = 0u;
        memset(&Error, 0, sizeof(Error));
        (void)xworkRuntimeReplaceMcpServersBySource(g_MdoMcp.Runtime,
            MDO_MCP_SOURCE, NULL, 0u, &Replaced, &Error);
    }
    memset(&g_MdoMcp, 0, sizeof(g_MdoMcp));
    MdoMcpCatalogRelease(pCatalog);
    MdoMcpDiagnosticsRelease(pDiagnostics);
    if ( pLock != NULL ) xrtMutexDestroy(pLock);
    if ( pReloadLock != NULL ) xrtMutexDestroy(pReloadLock);
}

uint64 MdoMcpManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( g_MdoMcp.Lock != NULL && xrtMutexLock(g_MdoMcp.Lock) ) {
        Generation = g_MdoMcp.NextGeneration;
        (void)xrtMutexUnlock(g_MdoMcp.Lock);
    }
    return Generation;
}

MdoMcpCatalog* MdoMcpCatalogSnapshot(void)
{
    MdoMcpCatalog* pCatalog = NULL;
    if ( g_MdoMcp.Lock != NULL && xrtMutexLock(g_MdoMcp.Lock) ) {
        pCatalog = MdoMcpCatalogRef(g_MdoMcp.Catalog);
        (void)xrtMutexUnlock(g_MdoMcp.Lock);
    }
    return pCatalog;
}

size_t MdoMcpCatalogCount(const MdoMcpCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->Count : 0u;
}

static bool MdoMcpServerInfoCopy(const MdoMcpCatalog* pCatalog,
    const MdoMcpServerEntry* pEntry, MdoMcpServerInfo* pInfo)
{
    if ( pInfo == NULL || pInfo->Size < sizeof(*pInfo) ) return false;
    pInfo->Generation = pCatalog->Generation;
    pInfo->External = pEntry->External;
    pInfo->Enabled = pEntry->Enabled;
    pInfo->AutoReconnect = pEntry->AutoReconnect;
    pInfo->TrustReadOnlyAnnotations = pEntry->TrustReadOnlyAnnotations;
    pInfo->Transport = pEntry->Transport;
    pInfo->Id = pEntry->Id;
    pInfo->Name = pEntry->Name;
    pInfo->Description = pEntry->Description;
    pInfo->ProtocolVersion = pEntry->ProtocolVersion;
    pInfo->Program = pEntry->Program;
    pInfo->Endpoint = pEntry->Endpoint;
    pInfo->WorkingDirectory = pEntry->WorkingDirectory;
    pInfo->PermissionProfile = pEntry->PermissionProfile;
    pInfo->SourcePath = pEntry->SourcePath;
    pInfo->SourceHash = pEntry->SourceHash;
    pInfo->ArgumentCount = pEntry->ArgumentCount;
    pInfo->EnvironmentCount = pEntry->EnvironmentCount;
    pInfo->HttpHeaderCount = pEntry->HttpHeaderCount;
    pInfo->AllowedToolCount = pEntry->AllowedToolCount;
    pInfo->DeniedToolCount = pEntry->DeniedToolCount;
    pInfo->StartupTimeoutMilliseconds =
        pEntry->StartupTimeoutMilliseconds;
    pInfo->RequestTimeoutMilliseconds =
        pEntry->RequestTimeoutMilliseconds;
    pInfo->MaxMessageBytes = pEntry->MaxMessageBytes;
    pInfo->MaxTools = pEntry->MaxTools;
    pInfo->DefaultEffects = pEntry->DefaultEffects;
    return true;
}

bool MdoMcpCatalogAt(const MdoMcpCatalog* pCatalog, size_t Index,
    MdoMcpServerInfo* pInfo)
{
    return pCatalog != NULL && Index < pCatalog->Count &&
        MdoMcpServerInfoCopy(pCatalog, &pCatalog->Entries[Index], pInfo);
}

bool MdoMcpCatalogFind(const MdoMcpCatalog* pCatalog, cstr ServerId,
    MdoMcpServerInfo* pInfo)
{
    size_t i;
    if ( pCatalog == NULL || ServerId == NULL ) return false;
    for ( i = 0u; i < pCatalog->Count; ++i )
        if ( strcmp(pCatalog->Entries[i].Id, ServerId) == 0 )
            return MdoMcpServerInfoCopy(pCatalog,
                &pCatalog->Entries[i], pInfo);
    return false;
}

bool MdoMcpManagerGetStatus(cstr ServerId, MdoMcpServerStatus* pStatus)
{
    MdoMcpCatalog* pCatalog;
    MdoMcpServerInfo CatalogInfo;
    xwork_mcp_server_info RuntimeInfo;
    bool Found;
    if ( pStatus == NULL || pStatus->Size < sizeof(*pStatus) ) return false;
    pCatalog = MdoMcpCatalogSnapshot();
    memset(&CatalogInfo, 0, sizeof(CatalogInfo));
    CatalogInfo.Size = sizeof(CatalogInfo);
    Found = MdoMcpCatalogFind(pCatalog, ServerId, &CatalogInfo);
    if ( !Found || g_MdoMcp.Runtime == NULL ) {
        MdoMcpCatalogRelease(pCatalog);
        return false;
    }
    xworkMcpServerInfoInit(&RuntimeInfo);
    Found = xworkRuntimeMcpServerGetInfo(g_MdoMcp.Runtime, ServerId,
        &RuntimeInfo);
    if ( Found ) {
        pStatus->CatalogGeneration = CatalogInfo.Generation;
        pStatus->State = RuntimeInfo.eState;
        pStatus->SchemaGeneration = RuntimeInfo.uSchemaGeneration;
        pStatus->SchemaExpiresMicroseconds = RuntimeInfo.uSchemaExpiresUs;
        pStatus->DiscoveredToolCount = RuntimeInfo.iDiscoveredToolCount;
        pStatus->RequestsCompleted = RuntimeInfo.uRequestsCompleted;
        pStatus->Enabled = RuntimeInfo.bEnabled;
        pStatus->Connected = RuntimeInfo.bConnected;
        pStatus->ToolsDiscovered = RuntimeInfo.bToolsDiscovered;
        pStatus->SupportsToolListChanges =
            RuntimeInfo.bServerSupportsToolListChanges;
    }
    MdoMcpCatalogRelease(pCatalog);
    return Found;
}

bool MdoMcpManagerSetEnabled(cstr ServerId, bool Enabled,
    xwork_error* pError)
{
    return g_MdoMcp.Initialized &&
        xworkRuntimeSetMcpServerEnabled(g_MdoMcp.Runtime, ServerId, Enabled,
            pError);
}

bool MdoMcpManagerDisconnect(cstr ServerId, xwork_error* pError)
{
    return g_MdoMcp.Initialized &&
        xworkRuntimeDisconnectMcpServer(g_MdoMcp.Runtime, ServerId, pError);
}

bool MdoMcpManagerRefresh(cstr ServerId, xcancel* pCancel, uint64 Deadline,
    xwork_error* pError)
{
    return g_MdoMcp.Initialized &&
        xworkRuntimeRefreshMcpServer(g_MdoMcp.Runtime, ServerId, pCancel,
            Deadline, pError);
}

MdoMcpDiagnostics* MdoMcpDiagnosticsSnapshot(void)
{
    MdoMcpDiagnostics* pDiagnostics = NULL;
    if ( g_MdoMcp.Lock != NULL && xrtMutexLock(g_MdoMcp.Lock) ) {
        pDiagnostics = MdoMcpDiagnosticsRef(g_MdoMcp.Diagnostics);
        (void)xrtMutexUnlock(g_MdoMcp.Lock);
    }
    return pDiagnostics;
}

size_t MdoMcpDiagnosticsCount(const MdoMcpDiagnostics* pDiagnostics)
{
    return pDiagnostics != NULL ? pDiagnostics->Count : 0u;
}

bool MdoMcpDiagnosticsAt(const MdoMcpDiagnostics* pDiagnostics,
    size_t Index, MdoMcpDiagnosticInfo* pInfo)
{
    const MdoMcpDiagnosticEntry* pEntry;
    if ( pDiagnostics == NULL || Index >= pDiagnostics->Count ||
         pInfo == NULL || pInfo->Size < sizeof(*pInfo) ) return false;
    pEntry = &pDiagnostics->Entries[Index];
    pInfo->Stage = pEntry->Stage;
    pInfo->ServerId = pEntry->ServerId;
    pInfo->SourcePath = pEntry->SourcePath;
    pInfo->SourceHash = pEntry->SourceHash;
    pInfo->Message = pEntry->Message;
    return true;
}
