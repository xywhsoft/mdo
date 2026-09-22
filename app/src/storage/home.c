#include <stdio.h>
#include <string.h>

#include "../../include/mdo/home.h"

#define MDO_RESOURCE_PREFIX "/app/default-home/"
#define MDO_HOME_ERROR_DOMAIN "mdo.home"

typedef enum MdoHomeError {
    MDO_HOME_ERROR_ARGUMENT = 1,
    MDO_HOME_ERROR_STATE,
    MDO_HOME_ERROR_PATH,
    MDO_HOME_ERROR_STORAGE
} MdoHomeError;

typedef struct MdoHomeState {
    xmutex* Lock;
    xvfs ApplicationVfs;
    xvfsdisk OverlayDisk;
    xvfsmount OverlayMount;
    xroot Root;
    char* Path;
    char* BuiltinDefaults;
    size_t BuiltinDefaultsSize;
    MdoPersistenceMode Persistence;
    bool ExternalOverlay;
    bool Initialized;
    char Message[256];
} MdoHomeState;

static MdoHomeState g_MdoHome;

static void MdoHomeErrorSet(xerrkind Kind, MdoHomeError Code, cstr Message)
{
    xerror* pError = xrtErrorCreate(Kind, MDO_HOME_ERROR_DOMAIN,
        (int32)Code, Message);
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static bool MdoHomePathValid(cstr Path)
{
    const unsigned char* p;
    const unsigned char* pSegment;

    if ( Path == NULL || Path[0] == '\0' || Path[0] == '/' ||
         Path[0] == '\\' ) return false;
    p = (const unsigned char*)Path;
    pSegment = p;
    for ( ; ; p++ ) {
        if ( *p == '\\' || *p == ':' || (*p != '\0' && *p < 0x20u) )
            return false;
        if ( *p == '/' || *p == '\0' ) {
            size_t iLength = (size_t)(p - pSegment);
            if ( iLength == 0u ||
                 (iLength == 1u && pSegment[0] == '.') ||
                 (iLength == 2u && pSegment[0] == '.' &&
                  pSegment[1] == '.') ) return false;
            if ( *p == '\0' ) return true;
            pSegment = p + 1;
        }
    }
}

static char* MdoHomeResolvePath(void)
{
    cstr sCommandLine = NULL;
    char* sEnvironment = NULL;
    char* sResult;
    uint32 i;

    for ( i = 0u; i < xsAppArgumentCount(); i++ ) {
        cstr sArgument = xsAppArgument(i);
        cstr sValue = NULL;

        if ( sArgument == NULL ) continue;
        if ( strcmp(sArgument, "--home") == 0 ) {
            if ( i + 1u >= xsAppArgumentCount() ) {
                MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
                    "--home requires a path");
                return NULL;
            }
            sValue = xsAppArgument(++i);
        } else if ( strncmp(sArgument, "--home=", 7u) == 0 ) {
            sValue = sArgument + 7u;
        }
        if ( sValue != NULL ) {
            if ( sCommandLine != NULL || sValue[0] == '\0' ) {
                MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
                    "--home must specify one nonempty path");
                return NULL;
            }
            sCommandLine = sValue;
        }
    }
    if ( sCommandLine != NULL ) return xrtPathAbs(sCommandLine);

    if ( !xrtEnvLookup("MDO_HOME", &sEnvironment) ) return NULL;
    if ( sEnvironment != NULL && sEnvironment[0] != '\0' ) {
        sResult = xrtPathAbs(sEnvironment);
        xrtFree(sEnvironment);
        return sResult;
    }
    xrtFree(sEnvironment);
    return xrtPathJoin(xsAppPath(), "mdo-home");
}

static char* MdoResourcePath(cstr Path)
{
    size_t iPrefix;
    size_t iPath;
    char* sResult;

    if ( !MdoHomePathValid(Path) ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_PATH,
            "resource path must be a portable relative path");
        return NULL;
    }
    iPrefix = strlen(MDO_RESOURCE_PREFIX);
    iPath = strlen(Path);
    if ( iPath > SIZE_MAX - iPrefix - 1u ) {
        MdoHomeErrorSet(XERR_RANGE, MDO_HOME_ERROR_PATH,
            "resource path is too long");
        return NULL;
    }
    sResult = (char*)xrtMalloc(iPrefix + iPath + 1u);
    if ( sResult == NULL ) return NULL;
    memcpy(sResult, MDO_RESOURCE_PREFIX, iPrefix);
    memcpy(sResult + iPrefix, Path, iPath + 1u);
    return sResult;
}

static bool MdoHomeReadFileBounded(xfile File, size_t iLimit,
    char** ppData, size_t* pSize)
{
    xfileinfo Info;
    char* pData = NULL;
    size_t iSize;

    if ( File == NULL || ppData == NULL || pSize == NULL ||
         !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u || Info.Size > iLimit ||
         Info.Size > SIZE_MAX - 1u ) return false;
    iSize = (size_t)Info.Size;
    pData = (char*)xrtMalloc(iSize + 1u);
    if ( pData == NULL ) return false;
    if ( iSize != 0u && !xrtReadFull(File, pData, iSize, NULL) ) {
        xrtFree(pData);
        return false;
    }
    pData[iSize] = '\0';
    *ppData = pData;
    *pSize = iSize;
    return true;
}

static bool MdoHomeCaptureBuiltinDefaults(void)
{
    xfileoptions Options;
    xfile File;
    char* sPath = MdoResourcePath("config/defaults.json");
    bool bOk;

    if ( sPath == NULL ) return false;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ;
    File = xrtVfsOpen(g_MdoHome.ApplicationVfs, sPath, &Options);
    xrtFree(sPath);
    if ( File == NULL ) return false;
    bOk = MdoHomeReadFileBounded(File, 1024u * 1024u,
        &g_MdoHome.BuiltinDefaults, &g_MdoHome.BuiltinDefaultsSize);
    if ( !xrtClose(File) ) bOk = false;
    if ( !bOk ) {
        xrtFree(g_MdoHome.BuiltinDefaults);
        g_MdoHome.BuiltinDefaults = NULL;
        g_MdoHome.BuiltinDefaultsSize = 0u;
    }
    return bOk;
}

static void MdoHomeRememberFailure(cstr Fallback)
{
    const xerror* pError = xrtGetError();
    cstr sMessage = pError != NULL ? xrtErrorMessage(pError) : NULL;

    snprintf(g_MdoHome.Message, sizeof(g_MdoHome.Message), "%s",
        (sMessage != NULL && sMessage[0] != '\0') ? sMessage : Fallback);
    g_MdoHome.Persistence = MDO_PERSISTENCE_EPHEMERAL;
}

static bool MdoHomeMountLocked(void)
{
#if defined(_WIN32) || defined(_WIN64)
    const xvfscase CaseMode = XVFS_CASE_ASCII_INSENSITIVE;
#else
    const xvfscase CaseMode = XVFS_CASE_SENSITIVE;
#endif
    xroot Root = NULL;
    xvfsdisk Disk = NULL;
    xvfsmount Mount = NULL;

    Root = xrtRootOpen(g_MdoHome.Path);
    if ( Root == NULL ) goto fail;
    Disk = xrtVfsDiskCreate(g_MdoHome.Path, XVFS_DISK_READ);
    if ( Disk == NULL ) goto fail;
    Mount = xrtVfsDiskMount(g_MdoHome.ApplicationVfs,
        "/app/default-home", 1000, CaseMode, Disk, 0u);
    if ( Mount == NULL ) goto fail;

    g_MdoHome.Root = Root;
    g_MdoHome.OverlayDisk = Disk;
    g_MdoHome.OverlayMount = Mount;
    g_MdoHome.ExternalOverlay = true;
    g_MdoHome.Persistence = MDO_PERSISTENCE_EXTERNAL;
    g_MdoHome.Message[0] = '\0';
    return true;

fail:
    {
        xerror* pError = xrtTakeError();
        if ( Mount != NULL ) xrtVfsMountDestroy(Mount);
        if ( Disk != NULL ) xrtVfsDiskDestroy(Disk);
        if ( Root != NULL ) (void)xrtRootClose(Root);
        xrtClearError();
        if ( pError != NULL ) xrtSetErrorTake(pError);
        else MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STORAGE,
            "cannot mount external Home");
    }
    return false;
}

static bool MdoHomeEnsureLocked(void)
{
    if ( g_MdoHome.Persistence == MDO_PERSISTENCE_EXTERNAL ) return true;
    if ( g_MdoHome.Persistence == MDO_PERSISTENCE_EPHEMERAL ) {
        MdoHomeErrorSet(XERR_PERMISSION, MDO_HOME_ERROR_STORAGE,
            g_MdoHome.Message[0] != '\0' ? g_MdoHome.Message :
            "external Home is unavailable; the process is ephemeral");
        return false;
    }
    if ( !xrtDirCreateAll(g_MdoHome.Path) || !MdoHomeMountLocked() ) {
        MdoHomeRememberFailure("cannot create or mount external Home");
        return false;
    }
    return true;
}

static bool MdoHomeEnsureParents(xroot Root, cstr Path)
{
    char* sParent = xrtStrDup(Path);
    char* p;

    if ( sParent == NULL ) return false;
    for ( p = sParent; *p != '\0'; p++ ) {
        xfileinfo Info;
        const xerror* pError;

        if ( *p != '/' ) continue;
        *p = '\0';
        if ( xrtRootStat(Root, sParent, false, &Info) ) {
            if ( Info.Type != XFILE_TYPE_DIRECTORY ) {
                xrtFree(sParent);
                MdoHomeErrorSet(XERR_TYPE, MDO_HOME_ERROR_PATH,
                    "a Home path parent is not a directory");
                return false;
            }
        } else {
            pError = xrtGetError();
            if ( pError == NULL || xrtErrorKind(pError) != XERR_NOT_FOUND ) {
                xrtFree(sParent);
                return false;
            }
            xrtClearError();
            if ( !xrtRootDirCreate(Root, sParent, 0700u) ) {
                xrtFree(sParent);
                return false;
            }
        }
        *p = '/';
    }
    xrtFree(sParent);
    return true;
}

bool MdoHomeInit(void)
{
    char* sPath = NULL;
    xvfs ApplicationVfs;
    xfileinfo Info;
    const xerror* pError;

    if ( g_MdoHome.Initialized ) return true;
    memset(&g_MdoHome, 0, sizeof(g_MdoHome));
    g_MdoHome.Lock = xrtMutexCreate();
    ApplicationVfs = xsApplicationVfs();
    if ( g_MdoHome.Lock == NULL || ApplicationVfs == NULL ) goto fail;
    xrtVfsRef(ApplicationVfs);
    g_MdoHome.ApplicationVfs = ApplicationVfs;

    if ( !MdoHomeCaptureBuiltinDefaults() ) goto fail;

    sPath = MdoHomeResolvePath();
    if ( sPath == NULL ) goto fail;
    g_MdoHome.Path = sPath;
    g_MdoHome.Persistence = MDO_PERSISTENCE_LAZY;

    if ( xrtPathStat(sPath, false, &Info) ) {
        if ( Info.Type != XFILE_TYPE_DIRECTORY ) {
            MdoHomeErrorSet(XERR_TYPE, MDO_HOME_ERROR_PATH,
                "external Home path exists but is not a directory");
            goto fail;
        }
        if ( !MdoHomeMountLocked() ) goto fail;
    } else {
        pError = xrtGetError();
        if ( pError == NULL || xrtErrorKind(pError) != XERR_NOT_FOUND )
            goto fail;
        xrtClearError();
    }
    g_MdoHome.Initialized = true;
    return true;

fail:
    {
        xerror* pSaved = xrtTakeError();
        MdoHomeUnit();
        xrtClearError();
        if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
        else MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STATE,
            "Home initialization failed");
    }
    return false;
}

void MdoHomeUnit(void)
{
    if ( g_MdoHome.OverlayMount != NULL ) {
        (void)xrtVfsUnmount(g_MdoHome.OverlayMount);
        xrtVfsMountDestroy(g_MdoHome.OverlayMount);
    }
    if ( g_MdoHome.OverlayDisk != NULL )
        xrtVfsDiskDestroy(g_MdoHome.OverlayDisk);
    if ( g_MdoHome.Root != NULL ) (void)xrtRootClose(g_MdoHome.Root);
    if ( g_MdoHome.ApplicationVfs != NULL )
        xrtVfsDestroy(g_MdoHome.ApplicationVfs);
    xrtFree(g_MdoHome.Path);
    xrtFree(g_MdoHome.BuiltinDefaults);
    if ( g_MdoHome.Lock != NULL ) xrtMutexDestroy(g_MdoHome.Lock);
    memset(&g_MdoHome, 0, sizeof(g_MdoHome));
}

bool MdoBuiltinDefaults(xstrview* pText)
{
    if ( pText == NULL || !g_MdoHome.Initialized ||
         g_MdoHome.BuiltinDefaults == NULL ) {
        MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STATE,
            "built-in defaults are unavailable");
        return false;
    }
    pText->Data = g_MdoHome.BuiltinDefaults;
    pText->Size = g_MdoHome.BuiltinDefaultsSize;
    return true;
}

bool MdoHomeGetSnapshot(MdoHomeSnapshot* pSnapshot)
{
    if ( pSnapshot == NULL || pSnapshot->Size < sizeof(*pSnapshot) ||
         !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid Home snapshot request");
        return false;
    }
    xrtMutexLock(g_MdoHome.Lock);
    pSnapshot->Persistence = g_MdoHome.Persistence;
    pSnapshot->ExternalOverlay = g_MdoHome.ExternalOverlay;
    pSnapshot->Path = g_MdoHome.Path;
    snprintf(pSnapshot->Message, sizeof(pSnapshot->Message), "%s",
        g_MdoHome.Message);
    xrtMutexUnlock(g_MdoHome.Lock);
    return true;
}

xfile MdoResourceOpenRead(cstr Path)
{
    xfileoptions Options;
    xfile File = NULL;
    char* sVirtual;

    if ( !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STATE,
            "Home is not initialized");
        return NULL;
    }
    sVirtual = MdoResourcePath(Path);
    if ( sVirtual == NULL ) return NULL;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ;
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Root != NULL ) {
        File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
        if ( File != NULL ) {
            xrtMutexUnlock(g_MdoHome.Lock);
            xrtFree(sVirtual);
            return File;
        }
        if ( xrtGetError() == NULL ||
             xrtErrorKind(xrtGetError()) != XERR_NOT_FOUND ) {
            xrtMutexUnlock(g_MdoHome.Lock);
            xrtFree(sVirtual);
            return NULL;
        }
        xrtClearError();
    }
    xrtMutexUnlock(g_MdoHome.Lock);
    File = xrtVfsOpen(g_MdoHome.ApplicationVfs, sVirtual, &Options);
    xrtFree(sVirtual);
    return File;
}

xfile MdoHomeOpenRead(cstr Path)
{
    xfileoptions Options;
    xfile File;

    if ( !MdoHomePathValid(Path) || !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid external Home read request");
        return NULL;
    }
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Root == NULL ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        MdoHomeErrorSet(XERR_NOT_FOUND, MDO_HOME_ERROR_STORAGE,
            "external Home does not exist");
        return NULL;
    }
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    xrtMutexUnlock(g_MdoHome.Lock);
    return File;
}

bool MdoHomeExternalStat(cstr Path, bool* pExists, xfileinfo* pInfo)
{
    xfileinfo Info;

    if ( !MdoHomePathValid(Path) || pExists == NULL ||
         !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid external Home stat request");
        return false;
    }
    *pExists = false;
    memset(&Info, 0, sizeof(Info));
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Root == NULL ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        if ( pInfo != NULL ) memset(pInfo, 0, sizeof(*pInfo));
        return true;
    }
    if ( xrtRootStat(g_MdoHome.Root, Path, false, &Info) ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        *pExists = true;
        if ( pInfo != NULL ) *pInfo = Info;
        return true;
    }
    if ( xrtGetError() != NULL &&
         xrtErrorKind(xrtGetError()) == XERR_NOT_FOUND ) {
        xrtClearError();
        xrtMutexUnlock(g_MdoHome.Lock);
        if ( pInfo != NULL ) memset(pInfo, 0, sizeof(*pInfo));
        return true;
    }
    xrtMutexUnlock(g_MdoHome.Lock);
    return false;
}

xfile MdoHomeOpenWrite(cstr Path, uint32 Flags)
{
    const uint32 Known = XFILE_READ | XFILE_WRITE | XFILE_CREATE |
        XFILE_TRUNCATE | XFILE_APPEND | XFILE_EXCLUSIVE | XFILE_NOFOLLOW |
        XFILE_SYNC | XFILE_ASYNC;
    xfileoptions Options;
    xfile File;
    xroot Root;

    if ( !MdoHomePathValid(Path) || (Flags & ~Known) != 0u ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid Home write path or flags");
        return NULL;
    }
    if ( !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_STATE, MDO_HOME_ERROR_STATE,
            "Home is not initialized");
        return NULL;
    }
    xrtMutexLock(g_MdoHome.Lock);
    if ( !MdoHomeEnsureLocked() ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        return NULL;
    }
    Root = g_MdoHome.Root;
    if ( !MdoHomeEnsureParents(Root, Path) ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        return NULL;
    }
    xrtFileOptionsInit(&Options);
    Options.Flags = Flags | XFILE_WRITE | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(Root, Path, &Options);
    xrtMutexUnlock(g_MdoHome.Lock);
    return File;
}

static char* MdoHomeSiblingPath(cstr Path, cstr Suffix)
{
    size_t iPath = strlen(Path);
    size_t iSuffix = strlen(Suffix);
    char* sResult;

    if ( iPath > SIZE_MAX - iSuffix - 1u ) {
        MdoHomeErrorSet(XERR_RANGE, MDO_HOME_ERROR_PATH,
            "Home path is too long");
        return NULL;
    }
    sResult = (char*)xrtMalloc(iPath + iSuffix + 1u);
    if ( sResult == NULL ) return NULL;
    memcpy(sResult, Path, iPath);
    memcpy(sResult + iPath, Suffix, iSuffix + 1u);
    return sResult;
}

static bool MdoHomeRootFileExistsLocked(cstr Path, bool* pExists)
{
    xfileinfo Info;

    if ( xrtRootStat(g_MdoHome.Root, Path, false, &Info) ) {
        if ( Info.Type != XFILE_TYPE_FILE ) {
            MdoHomeErrorSet(XERR_TYPE, MDO_HOME_ERROR_PATH,
                "Home configuration path is not a regular file");
            return false;
        }
        *pExists = true;
        return true;
    }
    if ( xrtGetError() != NULL &&
         xrtErrorKind(xrtGetError()) == XERR_NOT_FOUND ) {
        xrtClearError();
        *pExists = false;
        return true;
    }
    return false;
}

static bool MdoHomeWriteTempLocked(cstr Path, const void* pData, size_t iSize)
{
    xfileoptions Options;
    xfile File;
    bool bOk;

    (void)xrtRootRemove(g_MdoHome.Root, Path);
    xrtClearError();
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_SYNC | XFILE_NOFOLLOW;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    if ( File == NULL ) return false;
    bOk = (iSize == 0u || xrtWriteFull(File, pData, iSize, NULL)) &&
        xrtFlush(File);
    if ( !xrtClose(File) ) bOk = false;
    if ( !bOk ) {
        xerror* pSaved = xrtTakeError();
        (void)xrtRootRemove(g_MdoHome.Root, Path);
        xrtClearError();
        if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
    }
    return bOk;
}

static bool MdoHomeCopyFileLocked(cstr Source, cstr Target)
{
    unsigned char arrBuffer[64u * 1024u];
    xfileoptions Options;
    xfile Input = NULL;
    xfile Output = NULL;
    bool bOk = false;

    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_READ | XFILE_NOFOLLOW;
    Input = xrtRootFileOpen(g_MdoHome.Root, Source, &Options);
    if ( Input == NULL ) goto done;
    (void)xrtRootRemove(g_MdoHome.Root, Target);
    xrtClearError();
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_SYNC | XFILE_NOFOLLOW;
    Output = xrtRootFileOpen(g_MdoHome.Root, Target, &Options);
    if ( Output == NULL ) goto done;
    for ( ; ; ) {
        size_t iRead = 0u;
        if ( !xrtRead(Input, arrBuffer, sizeof(arrBuffer), &iRead) ) goto done;
        if ( iRead == 0u ) break;
        if ( !xrtWriteFull(Output, arrBuffer, iRead, NULL) ) goto done;
    }
    if ( !xrtFlush(Output) ) goto done;
    bOk = true;

done:
    {
        xerror* pSaved = !bOk ? xrtTakeError() : NULL;
        if ( Input != NULL && !xrtClose(Input) && bOk ) {
            bOk = false;
            pSaved = xrtTakeError();
        }
        if ( Output != NULL && !xrtClose(Output) && bOk ) {
            bOk = false;
            pSaved = xrtTakeError();
        }
        if ( !bOk ) {
            (void)xrtRootRemove(g_MdoHome.Root, Target);
            xrtClearError();
            if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
        }
    }
    return bOk;
}

static bool MdoHomePublishTempLocked(cstr Temporary, cstr Target)
{
    char* sTemporary = xrtPathJoin(g_MdoHome.Path, Temporary);
    char* sTarget = xrtPathJoin(g_MdoHome.Path, Target);
    bool bOk = false;

    if ( sTemporary != NULL && sTarget != NULL )
        bOk = xrtPathRename(sTemporary, sTarget, true);
    xrtFree(sTemporary);
    xrtFree(sTarget);
    return bOk;
}

static bool MdoHomeBackupLocked(cstr Path)
{
    char* sBackup = MdoHomeSiblingPath(Path, ".bak");
    char* sTemporary = MdoHomeSiblingPath(Path, ".bak.tmp");
    bool bOk = false;

    if ( sBackup == NULL || sTemporary == NULL ) goto done;
    if ( !MdoHomeCopyFileLocked(Path, sTemporary) ) goto done;
    if ( !MdoHomePublishTempLocked(sTemporary, sBackup) ) goto done;
    bOk = true;

done:
    if ( !bOk && sTemporary != NULL ) {
        xerror* pSaved = xrtTakeError();
        (void)xrtRootRemove(g_MdoHome.Root, sTemporary);
        xrtClearError();
        if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
    }
    xrtFree(sBackup);
    xrtFree(sTemporary);
    return bOk;
}

bool MdoHomeAtomicWrite(cstr Path, const void* pData, size_t iSize,
    bool Backup)
{
    char* sTemporary = NULL;
    bool bExists = false;
    bool bOk = false;

    if ( !MdoHomePathValid(Path) || (pData == NULL && iSize != 0u) ||
         !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid atomic Home write request");
        return false;
    }
    sTemporary = MdoHomeSiblingPath(Path, ".tmp");
    if ( sTemporary == NULL ) return false;
    xrtMutexLock(g_MdoHome.Lock);
    if ( !MdoHomeEnsureLocked() ||
         !MdoHomeEnsureParents(g_MdoHome.Root, Path) ||
         !MdoHomeRootFileExistsLocked(Path, &bExists) ||
         !MdoHomeWriteTempLocked(sTemporary, pData, iSize) ) goto done;
    if ( Backup && bExists && !MdoHomeBackupLocked(Path) ) goto done;
    if ( !MdoHomePublishTempLocked(sTemporary, Path) ) goto done;
    bOk = true;

done:
    if ( !bOk ) {
        xerror* pSaved = xrtTakeError();
        if ( g_MdoHome.Root != NULL )
            (void)xrtRootRemove(g_MdoHome.Root, sTemporary);
        xrtClearError();
        if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
    }
    xrtMutexUnlock(g_MdoHome.Lock);
    xrtFree(sTemporary);
    return bOk;
}

bool MdoHomeRemove(cstr Path, bool Backup)
{
    bool bExists = false;
    bool bOk = false;

    if ( !MdoHomePathValid(Path) || !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid Home remove request");
        return false;
    }
    xrtMutexLock(g_MdoHome.Lock);
    if ( g_MdoHome.Root == NULL ) {
        bOk = true;
        goto done;
    }
    if ( !MdoHomeRootFileExistsLocked(Path, &bExists) ) goto done;
    if ( !bExists ) {
        bOk = true;
        goto done;
    }
    if ( Backup && !MdoHomeBackupLocked(Path) ) goto done;
    bOk = xrtRootRemove(g_MdoHome.Root, Path);

done:
    xrtMutexUnlock(g_MdoHome.Lock);
    return bOk;
}

bool MdoResourceMaterialize(cstr Path)
{
    unsigned char arrBuffer[64u * 1024u];
    xfile Source = NULL;
    xfile Target = NULL;
    xfileinfo Info;
    xroot Root;
    bool bOk = false;

    if ( !MdoHomePathValid(Path) || !g_MdoHome.Initialized ) {
        MdoHomeErrorSet(XERR_ARGUMENT, MDO_HOME_ERROR_ARGUMENT,
            "invalid materialize request");
        return false;
    }
    xrtMutexLock(g_MdoHome.Lock);
    if ( !MdoHomeEnsureLocked() ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        return false;
    }
    Root = g_MdoHome.Root;
    if ( xrtRootStat(Root, Path, false, &Info) ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        if ( Info.Type == XFILE_TYPE_FILE ) return true;
        MdoHomeErrorSet(XERR_TYPE, MDO_HOME_ERROR_PATH,
            "materialize destination exists and is not a file");
        return false;
    }
    if ( xrtGetError() == NULL || xrtErrorKind(xrtGetError()) != XERR_NOT_FOUND ) {
        xrtMutexUnlock(g_MdoHome.Lock);
        return false;
    }
    xrtClearError();
    xrtMutexUnlock(g_MdoHome.Lock);

    Source = MdoResourceOpenRead(Path);
    if ( Source == NULL ) return false;
    Target = MdoHomeOpenWrite(Path,
        XFILE_CREATE | XFILE_EXCLUSIVE | XFILE_SYNC);
    if ( Target == NULL ) goto done;
    for ( ; ; ) {
        size_t iRead = 0u;
        if ( !xrtRead(Source, arrBuffer, sizeof(arrBuffer), &iRead) ) goto done;
        if ( iRead == 0u ) break;
        if ( !xrtWriteFull(Target, arrBuffer, iRead, NULL) ) goto done;
    }
    if ( !xrtFlush(Target) ) goto done;
    if ( !xrtClose(Target) ) {
        Target = NULL;
        goto done;
    }
    Target = NULL;
    bOk = true;

done:
    {
        xerror* pSaved = !bOk ? xrtTakeError() : NULL;
        if ( Source != NULL ) (void)xrtClose(Source);
        if ( Target != NULL ) (void)xrtClose(Target);
        if ( !bOk ) {
            xrtMutexLock(g_MdoHome.Lock);
            if ( g_MdoHome.Root != NULL )
                (void)xrtRootRemove(g_MdoHome.Root, Path);
            xrtMutexUnlock(g_MdoHome.Lock);
            xrtClearError();
            if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
            else MdoHomeErrorSet(XERR_IO, MDO_HOME_ERROR_STORAGE,
                "resource materialization failed");
        }
    }
    return bOk;
}
