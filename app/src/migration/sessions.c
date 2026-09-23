#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xllm-session.h>

#include "../sessions/internal.h"
#include "internal.h"

#define MDO_MIGRATION_SESSION_PROMPT_CAPACITY (64u * 1024u + 1u)

static const xvalue* MdoMigrationSessionGet(const xvalue* Object,
    const char* Key)
{
    return Object != NULL && xrtValueType(Object) == XVALUE_OBJECT ?
        xrtValueObjectGet(Object, xrtStrView(Key)) : NULL;
}

static bool MdoMigrationSessionString(const xvalue* Object, const char* Key,
    char* Output, size_t Capacity, const char* Fallback, bool EmptyAllowed)
{
    const xvalue* Value = MdoMigrationSessionGet(Object, Key);
    xstrview View;
    const char* Text = Fallback != NULL ? Fallback : "";
    size_t Size = strlen(Text);
    if ( Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
         xrtValueGetString(Value, &View) ) {
        if ( memchr(View.Data, '\0', View.Size) != NULL ||
             !xrtUtf8Valid(View, NULL) ) return false;
        Text = View.Data;
        Size = View.Size;
    }
    if ( Size >= Capacity || (!EmptyAllowed && Size == 0u) ) return false;
    memcpy(Output, Text, Size);
    Output[Size] = '\0';
    return true;
}

static uint64 MdoMigrationSessionUInt(const xvalue* Object, const char* Key,
    uint64 Fallback)
{
    const xvalue* Value = MdoMigrationSessionGet(Object, Key);
    uint64 Unsigned;
    int64 Signed;
    if ( Value != NULL && xrtValueType(Value) == XVALUE_UINT &&
         xrtValueGetUInt(Value, &Unsigned) ) return Unsigned;
    if ( Value != NULL && xrtValueType(Value) == XVALUE_INT &&
         xrtValueGetInt(Value, &Signed) && Signed >= 0 ) return (uint64)Signed;
    return Fallback;
}

static bool MdoMigrationSessionBool(const xvalue* Object, const char* Key,
    bool Fallback)
{
    const xvalue* Value = MdoMigrationSessionGet(Object, Key);
    bool Result;
    return Value != NULL && xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, &Result) ? Result : Fallback;
}

static int64 MdoMigrationSessionTime(uint64 Milliseconds, int64 Fallback)
{
    return Milliseconds != 0u && Milliseconds <= (uint64)INT64_MAX / 1000u ?
        (int64)(Milliseconds * 1000u) : Fallback;
}

static bool MdoMigrationSessionParts(const char* Path, char* Bucket,
    size_t BucketCapacity, char* Session, size_t SessionCapacity)
{
    static const char Prefix[] = "projects/";
    static const char Middle[] = "/sessions/";
    static const char Suffix[] = ".meta.json";
    const char* BucketStart;
    const char* BucketEnd;
    const char* SessionStart;
    size_t BucketSize;
    size_t SessionSize;
    size_t PathSize;
    if ( Path == NULL || strncmp(Path, Prefix, sizeof(Prefix) - 1u) != 0 )
        return false;
    BucketStart = Path + sizeof(Prefix) - 1u;
    BucketEnd = strstr(BucketStart, Middle);
    PathSize = strlen(Path);
    if ( BucketEnd == NULL || BucketEnd == BucketStart ||
         PathSize <= sizeof(Suffix) - 1u ||
         strcmp(Path + PathSize - (sizeof(Suffix) - 1u), Suffix) != 0 )
        return false;
    SessionStart = BucketEnd + sizeof(Middle) - 1u;
    BucketSize = (size_t)(BucketEnd - BucketStart);
    SessionSize = PathSize - (size_t)(SessionStart - Path) -
        (sizeof(Suffix) - 1u);
    if ( BucketSize >= BucketCapacity || SessionSize == 0u ||
         SessionSize >= SessionCapacity ||
         memchr(SessionStart, '/', SessionSize) != NULL ) return false;
    memcpy(Bucket, BucketStart, BucketSize);
    Bucket[BucketSize] = '\0';
    memcpy(Session, SessionStart, SessionSize);
    Session[SessionSize] = '\0';
    return true;
}

static bool MdoMigrationSessionPath(char* Output, size_t Capacity,
    const char* Project, const char* Session, const char* File)
{
    int Written = snprintf(Output, Capacity, "sessions/%s/%s/%s",
        Project, Session, File);
    return Written > 0 && (size_t)Written < Capacity;
}

static bool MdoMigrationLegacyPeer(char* Output, size_t Capacity,
    const char* MetaPath, const char* Suffix)
{
    size_t Size = strlen(MetaPath);
    static const char MetaSuffix[] = ".meta.json";
    int Written;
    if ( Size <= sizeof(MetaSuffix) - 1u ||
         strcmp(MetaPath + Size - (sizeof(MetaSuffix) - 1u), MetaSuffix) != 0 )
        return false;
    Written = snprintf(Output, Capacity, "%.*s%s",
        (int)(Size - (sizeof(MetaSuffix) - 1u)), MetaPath, Suffix);
    return Written > 0 && (size_t)Written < Capacity;
}

static bool MdoMigrationSessionLedger(MdoMigrationContext* Context,
    const MdoMigrationFile* MetaFile, const char* ProjectId,
    const char* SessionId, const MdoMigrationModelMap* Model,
    xwork_error* Error)
{
    char SnapshotRelative[512];
    char JournalRelative[512];
    char LegacyPath[MDO_MIGRATION_MAX_PATH + 1u];
    MdoMigrationFile* LegacySnapshot;
    MdoMigrationFile* LegacyJournal;
    char* SnapshotPath = NULL;
    char* JournalPath = NULL;
    xllm_session_config Config;
    xllm_session* Ledger = NULL;
    xllm_error ModelError;
    bool Ok = false;
    if ( !MdoMigrationSessionPath(SnapshotRelative,
            sizeof(SnapshotRelative), ProjectId, SessionId, "snapshot.json") ||
         !MdoMigrationSessionPath(JournalRelative,
            sizeof(JournalRelative), ProjectId, SessionId, "journal.jsonl") ||
         !MdoMigrationLegacyPeer(LegacyPath, sizeof(LegacyPath),
            MetaFile->Path, ".snap") ) goto invalid;
    LegacySnapshot = MdoMigrationFind(&Context->Scan, LegacyPath);
    if ( !MdoMigrationLegacyPeer(LegacyPath, sizeof(LegacyPath),
            MetaFile->Path, ".jsonl") ) goto invalid;
    LegacyJournal = MdoMigrationFind(&Context->Scan, LegacyPath);
    if ( LegacySnapshot != NULL && !MdoMigrationStageCopy(Context,
            LegacySnapshot, SnapshotRelative, 0600u, Error) ) goto done;
    if ( LegacyJournal != NULL && !MdoMigrationStageCopy(Context,
            LegacyJournal, JournalRelative, 0600u, Error) ) goto done;
    SnapshotPath = xrtPathJoin(Context->StagePath, SnapshotRelative);
    JournalPath = xrtPathJoin(Context->StagePath, JournalRelative);
    if ( SnapshotPath == NULL || JournalPath == NULL ) goto memory;
    xllmSessionConfigInit(&Config);
    Config.eWindowMode = XLLM_WINDOW_SHARED_CONTEXT;
    Config.uContextWindowTokens = Model->ContextTokens;
    Config.uMaxInputTokens = Model->ContextTokens > 1u ?
        Model->ContextTokens - 1u : 1u;
    Config.uMaxOutputTokens = Model->MaxOutputTokens;
    Config.uOutputReserveTokens = Model->MaxOutputTokens / 2u;
    Config.uSummaryMaxTokens = Model->MaxOutputTokens / 4u;
    xllmErrorInit(&ModelError);
    Ledger = xllmSessionRecover(SnapshotPath, JournalPath, &Config,
        &ModelError);
    if ( Ledger == NULL || !xllmSessionCheckpoint(Ledger, SnapshotPath,
            &ModelError) ) {
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            ModelError.sMessage[0] != '\0' ? ModelError.sMessage :
            "legacy session ledger failed validation");
        goto done;
    }
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy session path cannot be converted");
    goto done;
memory:
    MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate migrated session path");
done:
    if ( Ledger != NULL ) xllmSessionDestroy(Ledger);
    xrtFree(JournalPath);
    xrtFree(SnapshotPath);
    return Ok;
}

static bool MdoMigrationConvertSession(MdoMigrationContext* Context,
    const MdoMigrationFile* File, xwork_error* Error)
{
    char Bucket[257];
    char LegacyId[257];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char Title[MDO_SESSION_TITLE_CAPACITY];
    char Prompt[MDO_MIGRATION_SESSION_PROMPT_CAPACITY];
    char Relative[512];
    xvalue* Legacy = NULL;
    MdoMigrationProjectMap* Project;
    MdoMigrationModelMap* Model;
    MdoSessionInfo Info;
    MdoSessionInfo Validated;
    char* Json = NULL;
    size_t Size = 0u;
    int64 Now = xrtNow();
    uint64 CreatedMs;
    uint64 UpdatedMs;
    bool Ok = false;
    if ( !MdoMigrationSessionParts(File->Path, Bucket, sizeof(Bucket),
            LegacyId, sizeof(LegacyId)) ||
         !MdoMigrationIdentifier(LegacyId, "session", SessionId,
            sizeof(SessionId)) ) goto invalid;
    Project = MdoMigrationProjectFind(Context, Bucket);
    if ( Project == NULL ) goto invalid;
    Legacy = MdoMigrationParseJson(Context->SourceRoot, File, Error);
    if ( Legacy == NULL ) goto done;
    if ( !MdoMigrationSessionString(Legacy, "title", Title, sizeof(Title),
            "Imported session", false) ||
         !MdoMigrationSessionString(Legacy, "model", ModelId,
            sizeof(ModelId), "ling-gpu", false) ||
         !MdoMigrationSessionString(Legacy, "userPrompt", Prompt,
            sizeof(Prompt), "", true) ) goto invalid;
    Model = MdoMigrationModelFind(Context, ModelId);
    if ( Model == NULL ) Model = MdoMigrationModelFind(Context, "ling-gpu");
    if ( Model == NULL ) goto invalid;
    if ( !MdoMigrationSessionPath(Relative, sizeof(Relative), Project->NewId,
            SessionId, "artifacts") ||
         !MdoMigrationStageDirectory(Context, Relative, Error) ||
         !MdoMigrationSessionLedger(Context, File, Project->NewId, SessionId,
            Model, Error) ) goto done;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Info.Revision = 1u;
    CreatedMs = MdoMigrationSessionUInt(Legacy, "createdAt", 0u);
    UpdatedMs = MdoMigrationSessionUInt(Legacy, "updatedAt", CreatedMs);
    Info.CreatedAt = MdoMigrationSessionTime(CreatedMs, Now);
    Info.UpdatedAt = MdoMigrationSessionTime(UpdatedMs, Info.CreatedAt);
    if ( Info.UpdatedAt < Info.CreatedAt ) Info.UpdatedAt = Info.CreatedAt;
    Info.Pinned = MdoMigrationSessionBool(Legacy, "pinned", false);
    Info.Status = MDO_SESSION_ACTIVE;
    Info.PreviousStatus = MDO_SESSION_ACTIVE;
    Info.Protocol = Model->Protocol;
    Info.MaxOutputTokens = Model->MaxOutputTokens;
    Info.ConfigRevision = 1u;
    Info.ModelGeneration = 1u;
    Info.ModuleGeneration = 1u;
    Info.SkillGeneration = 1u;
    if ( !MdoMigrationCopy(Info.Id, sizeof(Info.Id), SessionId) ||
         !MdoMigrationCopy(Info.ProjectId, sizeof(Info.ProjectId),
            Project->NewId) ||
         !MdoMigrationCopy(Info.Title, sizeof(Info.Title), Title) ||
         !MdoMigrationCopy(Info.AgentId, sizeof(Info.AgentId), "mdo.default") ||
         !MdoMigrationCopy(Info.ModelId, sizeof(Info.ModelId), Model->NewId) ||
         !MdoMigrationCopy(Info.ReasoningEffort,
            sizeof(Info.ReasoningEffort), Model->ReasoningEffort) ||
         !MdoMigrationCopy(Info.WorkspaceRoot, sizeof(Info.WorkspaceRoot),
            Project->WorkspaceRoot) ) goto invalid;
    Json = MdoSessionsInternalMetaJson(&Info, &Size);
    if ( Json == NULL || !MdoSessionsInternalMetaParse(Project->NewId,
            SessionId, xrtStrViewN(Json, Size), &Validated) ||
         !MdoMigrationSessionPath(Relative, sizeof(Relative), Project->NewId,
            SessionId, "meta.json") ||
         !MdoMigrationStageWrite(Context, Relative, Json, Size, 0600u,
            Error) ) goto done;
    if ( Prompt[0] != '\0' ) {
        int Written = snprintf(Relative, sizeof(Relative),
            "migration/session-prompts/%s/%s.txt", Project->NewId,
            SessionId);
        if ( Written <= 0 || (size_t)Written >= sizeof(Relative) ||
             !MdoMigrationStageWrite(Context, Relative, Prompt,
                strlen(Prompt), 0600u, Error) ) goto done;
        ++Context->Result->SkippedItems;
    }
    ++Context->Result->ImportedSessions;
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy session metadata cannot be converted");
done:
    xrtFree(Json);
    xrtValueRelease(Legacy);
    return Ok;
}

bool MdoMigrationConvertSessions(MdoMigrationContext* Context,
    xwork_error* Error)
{
    size_t i;
    for ( i = 0u; i < Context->Scan.Count; ++i )
        if ( Context->Scan.Files[i].Kind == MDO_MIGRATION_FILE_SESSION_META &&
             !MdoMigrationConvertSession(Context, &Context->Scan.Files[i],
                Error) ) return false;
    return true;
}
