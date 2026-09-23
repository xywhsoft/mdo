#include <stdlib.h>
#include <string.h>

#include "internal.h"

static bool MdoMigrationTokenValid(const char* Token)
{
    size_t i;
    if ( Token == NULL || strlen(Token) != 64u ) return false;
    for ( i = 0u; i < 64u; ++i )
        if ( !((Token[i] >= '0' && Token[i] <= '9') ||
               (Token[i] >= 'a' && Token[i] <= 'f')) ) return false;
    return true;
}

static void MdoMigrationApplyFailure(xwork_error* Error,
    xwork_error_code Code, const char* Message)
{
    if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE )
        MdoMigrationError(Error, Code, Message);
}

static char* MdoMigrationStagePath(const char* Target)
{
    char* Id = xrtXidMakeString();
    size_t TargetSize;
    size_t IdSize;
    char* Path;
    if ( Id == NULL ) return NULL;
    TargetSize = strlen(Target);
    IdSize = strlen(Id);
    if ( TargetSize > SIZE_MAX - IdSize - 11u ) {
        xrtFree(Id);
        return NULL;
    }
    Path = (char*)xrtMalloc(TargetSize + IdSize + 11u);
    if ( Path != NULL )
        snprintf(Path, TargetSize + IdSize + 11u, "%s.migrate-%s", Target, Id);
    xrtFree(Id);
    return Path;
}

static bool MdoMigrationPrepareStage(MdoMigrationContext* Context,
    xwork_error* Error)
{
    char* Parent = xrtPathParent(Context->Preview.TargetPath);
    xfileinfo Info;
    const xerror* Cause;
    if ( Parent == NULL || !xrtDirCreateAll(Parent) ) {
        xrtFree(Parent);
        MdoMigrationXrtError(Error, "cannot create migration target parent");
        return false;
    }
    xrtFree(Parent);
    if ( xrtPathStat(Context->Preview.TargetPath, false, &Info) ) {
        MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
            "target Home appeared after migration preview");
        return false;
    }
    Cause = xrtGetError();
    if ( Cause == NULL || xrtErrorKind(Cause) != XERR_NOT_FOUND ) {
        MdoMigrationXrtError(Error, "cannot inspect migration target");
        return false;
    }
    xrtClearError();
    Context->StagePath = MdoMigrationStagePath(Context->Preview.TargetPath);
    if ( Context->StagePath == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot allocate migration staging path");
        return false;
    }
    if ( !xrtDirCreateMode(Context->StagePath, 0700u) ) {
        MdoMigrationXrtError(Error, "cannot create migration staging directory");
        return false;
    }
    Context->StageRoot = xrtRootOpen(Context->StagePath);
    if ( Context->StageRoot == NULL ) {
        MdoMigrationXrtError(Error, "cannot anchor migration staging directory");
        return false;
    }
    return true;
}

void MdoMigrationApplyOptionsInit(MdoMigrationApplyOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
}

bool MdoLegacyMigrationApply(const MdoMigrationApplyOptions* Options,
    MdoMigrationApplyResult* Result, xwork_error* Error)
{
    MdoMigrationContext Context;
    MdoMigrationPreview FinalPreview;
    xfileinfo Info;
    const xerror* Cause;
    int Written;
    bool Published = false;
    bool Ok = false;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( Options == NULL || Options->Size < sizeof(*Options) ||
         Result == NULL || Result->Size < sizeof(*Result) ||
         Options->SourceId == NULL ||
         !MdoMigrationTokenValid(Options->PreviewToken) ) {
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid legacy migration apply request");
        return false;
    }
    memset(Result, 0, sizeof(*Result));
    Result->Size = sizeof(*Result);
    memset(&Context, 0, sizeof(Context));
    Context.Result = Result;
    Context.Preview.Size = sizeof(Context.Preview);
    if ( !MdoLegacyMigrationPreview(Options->SourceId, &Context.Preview,
            Error) ) goto done;
    if ( !Context.Preview.Found || !Context.Preview.Valid ||
         !Context.Preview.Importable ) {
        MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
            Context.Preview.Message[0] != '\0' ? Context.Preview.Message :
            "legacy migration source is not importable");
        goto done;
    }
    if ( strcmp(Options->PreviewToken, Context.Preview.PreviewToken) != 0 ) {
        MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
            "legacy migration preview token is stale");
        goto done;
    }
    Written = snprintf(Result->ReportPath, sizeof(Result->ReportPath),
        "%s/migration/report.json", Context.Preview.TargetPath);
    if ( !MdoMigrationCopy(Result->TargetPath, sizeof(Result->TargetPath),
            Context.Preview.TargetPath) || Written <= 0 ||
         (size_t)Written >= sizeof(Result->ReportPath) ) {
        MdoMigrationError(Error, XWORK_ERROR_LIMIT,
            "migration result path is too long");
        goto done;
    }
    Context.SourceRoot = xrtRootOpen(Context.Preview.SourcePath);
    if ( Context.SourceRoot == NULL || !MdoMigrationScanDirectory(
            Context.SourceRoot, ".", 0u, &Context.Scan, Error) ) goto done;
    if ( Context.Scan.Count > 1u ) qsort(Context.Scan.Files,
        Context.Scan.Count, sizeof(*Context.Scan.Files),
        MdoMigrationFileCompare);
    if ( !MdoMigrationPrepareStage(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_IO,
            "cannot prepare migration staging directory");
        goto done;
    }
    if ( !MdoMigrationConvertConfig(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy configuration cannot be converted");
        goto done;
    }
    if ( !MdoMigrationConvertProjects(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy projects cannot be converted");
        goto done;
    }
    if ( !MdoMigrationConvertSessions(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy sessions cannot be converted");
        goto done;
    }
    if ( !MdoMigrationConvertMemory(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy memory cannot be converted");
        goto done;
    }
    if ( !MdoMigrationConvertSchedules(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy schedules cannot be converted");
        goto done;
    }
    if ( !MdoMigrationMeasureStage(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_IO,
            "cannot verify migration staging directory");
        goto done;
    }
    if ( !MdoMigrationWriteReport(&Context, Error) ) {
        MdoMigrationApplyFailure(Error, XWORK_ERROR_IO,
            "cannot write migration report");
        goto done;
    }
    if ( Context.StageRoot != NULL ) {
        if ( !xrtRootClose(Context.StageRoot) ) {
            Context.StageRoot = NULL;
            MdoMigrationXrtError(Error,
                "cannot close migration staging directory");
            goto done;
        }
        Context.StageRoot = NULL;
    }
    if ( Context.SourceRoot != NULL ) {
        if ( !xrtRootClose(Context.SourceRoot) ) {
            Context.SourceRoot = NULL;
            MdoMigrationXrtError(Error,
                "cannot close legacy migration source");
            goto done;
        }
        Context.SourceRoot = NULL;
    }
    memset(&FinalPreview, 0, sizeof(FinalPreview));
    FinalPreview.Size = sizeof(FinalPreview);
    if ( !MdoLegacyMigrationPreview(Options->SourceId, &FinalPreview, Error) ||
         !FinalPreview.Importable ||
         strcmp(FinalPreview.PreviewToken, Options->PreviewToken) != 0 ) {
        if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE )
            MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
                "legacy source or target changed during migration");
        goto done;
    }
    if ( xrtPathStat(Context.Preview.TargetPath, false, &Info) ) {
        MdoMigrationError(Error, XWORK_ERROR_CONTEXT,
            "target Home appeared during migration");
        goto done;
    }
    Cause = xrtGetError();
    if ( Cause == NULL || xrtErrorKind(Cause) != XERR_NOT_FOUND ) {
        MdoMigrationXrtError(Error, "cannot recheck migration target");
        goto done;
    }
    xrtClearError();
    if ( !xrtPathRename(Context.StagePath, Context.Preview.TargetPath, false) ) {
        MdoMigrationXrtError(Error, "cannot atomically publish migrated Home");
        goto done;
    }
    Published = true;
    Result->RestartRequired = true;
    Ok = true;
done:
    if ( Context.StageRoot != NULL ) {
        (void)xrtRootClose(Context.StageRoot);
        Context.StageRoot = NULL;
    }
    if ( Context.SourceRoot != NULL ) {
        (void)xrtRootClose(Context.SourceRoot);
        Context.SourceRoot = NULL;
    }
    if ( !Published && Context.StagePath != NULL ) {
        size_t TargetSize = strlen(Context.Preview.TargetPath);
        if ( strncmp(Context.StagePath, Context.Preview.TargetPath,
                TargetSize) == 0 &&
             strncmp(Context.StagePath + TargetSize, ".migrate-", 9u) == 0 ) {
            (void)xrtDirRemoveAll(Context.StagePath);
            xrtClearError();
        }
    }
    MdoMigrationContextUnit(&Context);
    if ( !Ok && !Published ) memset(Result, 0, sizeof(*Result));
    if ( !Ok && !Published ) Result->Size = sizeof(*Result);
    return Ok;
}
