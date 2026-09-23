#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../schedules/internal.h"
#include "internal.h"

static const xvalue* MdoMigrationScheduleGet(const xvalue* Object,
    const char* Key)
{
    return Object != NULL && xrtValueType(Object) == XVALUE_OBJECT ?
        xrtValueObjectGet(Object, xrtStrView(Key)) : NULL;
}

static bool MdoMigrationScheduleString(const xvalue* Object,
    const char* Key, char* Output, size_t Capacity, const char* Fallback,
    bool EmptyAllowed)
{
    const xvalue* Value = MdoMigrationScheduleGet(Object, Key);
    const char* Text = Fallback != NULL ? Fallback : "";
    size_t Size = strlen(Text);
    xstrview View;
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

static uint64 MdoMigrationScheduleUInt(const xvalue* Object,
    const char* Key, uint64 Fallback)
{
    const xvalue* Value = MdoMigrationScheduleGet(Object, Key);
    uint64 Unsigned;
    int64 Signed;
    if ( Value != NULL && xrtValueType(Value) == XVALUE_UINT &&
         xrtValueGetUInt(Value, &Unsigned) ) return Unsigned;
    if ( Value != NULL && xrtValueType(Value) == XVALUE_INT &&
         xrtValueGetInt(Value, &Signed) && Signed >= 0 ) return (uint64)Signed;
    return Fallback;
}

static bool MdoMigrationScheduleBool(const xvalue* Object, const char* Key,
    bool Fallback)
{
    const xvalue* Value = MdoMigrationScheduleGet(Object, Key);
    bool Result;
    return Value != NULL && xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, &Result) ? Result : Fallback;
}

static int64 MdoMigrationScheduleTime(uint64 Milliseconds)
{
    return Milliseconds != 0u && Milliseconds <= (uint64)INT64_MAX / 1000u ?
        (int64)(Milliseconds * 1000u) : 0;
}

static bool MdoMigrationScheduleFileId(const char* Path, char* Id,
    size_t Capacity)
{
    size_t Size;
    char* Legacy;
    bool Ok;
    if ( Path == NULL || strncmp(Path, "schedules/", 10u) != 0 )
        return false;
    Size = strlen(Path + 10u);
    if ( Size <= 5u || strcmp(Path + strlen(Path) - 5u, ".json") != 0 )
        return false;
    Legacy = xrtStrDupN(Path + 10u, Size - 5u);
    if ( Legacy == NULL ) return false;
    Ok = MdoMigrationIdentifier(Legacy, "schedule", Id, Capacity);
    xrtFree(Legacy);
    return Ok;
}

static bool MdoMigrationConvertSchedule(MdoMigrationContext* Context,
    MdoMigrationFile* File, xwork_error* Error)
{
    xvalue* Legacy = MdoMigrationParseJson(Context->SourceRoot, File, Error);
    MdoScheduleInfo Info;
    MdoScheduleInfo Validated;
    MdoMigrationProjectMap* Project;
    MdoMigrationModelMap* Model;
    char LegacyId[257];
    char Kind[32];
    char ProjectId[MDO_SCHEDULE_PROJECT_CAPACITY];
    char ModelId[MDO_SCHEDULE_IDENTITY_CAPACITY];
    char Miss[32];
    char Path[256];
    char* Json = NULL;
    size_t Size = 0u;
    uint64 Interval;
    uint64 Delay;
    uint64 Created;
    uint64 NextDue;
    int64 Now = xrtNow();
    int Written;
    bool Ok = false;
    if ( Legacy == NULL ) return false;
    if ( !MdoMigrationScheduleString(Legacy, "id", LegacyId,
            sizeof(LegacyId), "", true) ) goto invalid;
    if ( !MdoMigrationScheduleString(Legacy, "kind", Kind, sizeof(Kind),
            "once", false) ||
         !MdoMigrationScheduleString(Legacy, "project", ProjectId,
            sizeof(ProjectId), "_tasks", true) ||
         !MdoMigrationScheduleString(Legacy, "model", ModelId,
            sizeof(ModelId), "ling-gpu", true) ||
         !MdoMigrationScheduleString(Legacy, "miss", Miss, sizeof(Miss),
            "skip", false) ) goto invalid;
    if ( strcmp(Kind, "cron") == 0 ) {
        ++Context->UnsupportedSchedules;
        ++Context->Result->SkippedItems;
        Ok = true;
        goto done;
    }
    if ( strcmp(Kind, "once") != 0 && strcmp(Kind, "interval") != 0 )
        goto invalid;
    Project = MdoMigrationProjectFind(Context,
        ProjectId[0] != '\0' ? ProjectId : "_tasks");
    if ( Project == NULL ) Project = MdoMigrationProjectFind(Context,
        "_tasks");
    Model = MdoMigrationModelFind(Context,
        ModelId[0] != '\0' ? ModelId : "ling-gpu");
    if ( Model == NULL ) Model = MdoMigrationModelFind(Context, "ling-gpu");
    if ( Project == NULL || Model == NULL ) goto invalid;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    if ( LegacyId[0] == '\0' ) {
        if ( !MdoMigrationScheduleFileId(File->Path, Info.Id,
                sizeof(Info.Id)) ) goto invalid;
    } else if ( !MdoMigrationIdentifier(LegacyId, "schedule", Info.Id,
            sizeof(Info.Id)) ) goto invalid;
    Info.Revision = 1u;
    Info.UpdatedAt = Now;
    Info.RuntimeGeneration = 1u;
    Info.ClaimCount = MdoMigrationScheduleUInt(Legacy, "runCount", 0u);
    Info.LastClaimedAt = MdoMigrationScheduleTime(
        MdoMigrationScheduleUInt(Legacy, "lastRun", 0u));
    Interval = MdoMigrationScheduleUInt(Legacy, "intervalMin", 30u);
    Delay = MdoMigrationScheduleUInt(Legacy, "delayMin", 10u);
    if ( Interval == 0u ) Interval = 1u;
    if ( Interval > UINT32_MAX ) Interval = UINT32_MAX;
    if ( Delay > (uint64)INT64_MAX / (60u * 1000000u) ) Delay = 10u;
    Created = MdoMigrationScheduleUInt(Legacy, "created", 0u);
    NextDue = MdoMigrationScheduleUInt(Legacy, "nextDue", 0u);
    Info.StartAt = MdoMigrationScheduleTime(NextDue);
    if ( Info.StartAt == 0 ) Info.StartAt = MdoMigrationScheduleTime(Created);
    if ( Info.StartAt == 0 ) Info.StartAt = Now;
    if ( strcmp(Kind, "once") == 0 ) {
        Info.Frequency = XWORK_SCHEDULE_ONCE;
        if ( NextDue == 0u ) {
            int64 DelayUs = (int64)Delay * 60 * 1000000;
            if ( Info.StartAt > INT64_MAX - DelayUs ) goto invalid;
            Info.StartAt += DelayUs;
        }
        Info.Interval = 1u;
    } else {
        Info.Frequency = XWORK_SCHEDULE_MINUTELY;
        Info.Interval = (uint32)Interval;
    }
    if ( Info.StartAt <= 0 ) Info.StartAt = Now > 0 ? Now : 1;
    Info.NextOccurrenceAt = Info.StartAt;
    Info.Timezone = XWORK_SCHEDULE_TIMEZONE_UTC;
    Info.FoldPolicy = XWORK_SCHEDULE_FOLD_EARLIER;
    Info.MisfirePolicy = strcmp(Miss, "catchup") == 0 ?
        XWORK_SCHEDULE_MISFIRE_RUN_ONCE : XWORK_SCHEDULE_MISFIRE_SKIP;
    Info.MisfireGraceSeconds = 60u;
    Info.MaxCatchUp = 1u;
    Info.OverlapPolicy = XWORK_SCHEDULE_OVERLAP_SKIP;
    Info.MaxConcurrentRuns = 1u;
    Info.Protocol = Model->Protocol;
    Info.MaxOutputTokens = Model->MaxOutputTokens;
    Info.Enabled = MdoMigrationScheduleBool(Legacy, "enabled", true);
    if ( !MdoMigrationScheduleString(Legacy, "title", Info.Label,
            sizeof(Info.Label), "Imported schedule", false) ||
         !MdoMigrationScheduleString(Legacy, "prompt", Info.Input,
            sizeof(Info.Input), "Continue scheduled task", false) ||
         !MdoMigrationCopy(Info.ProjectId, sizeof(Info.ProjectId),
            Project->NewId) ||
         !MdoMigrationCopy(Info.AgentId, sizeof(Info.AgentId),
            "mdo.default") ||
         !MdoMigrationCopy(Info.ModelId, sizeof(Info.ModelId), Model->NewId) ||
         !MdoMigrationCopy(Info.ReasoningEffort,
            sizeof(Info.ReasoningEffort), Model->ReasoningEffort) ||
         !MdoMigrationCopy(Info.WorkspaceRoot, sizeof(Info.WorkspaceRoot),
            Project->WorkspaceRoot) ||
         !MdoSchedulesInternalValidate(&Info, Error) ) goto done;
    Json = MdoSchedulesInternalJson(&Info, &Size);
    if ( Json == NULL || !MdoSchedulesInternalParse(Info.Id,
            xrtStrViewN(Json, Size), &Validated) ) goto invalid;
    Written = snprintf(Path, sizeof(Path), "schedules/%s.json", Info.Id);
    if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
         !MdoMigrationStageWrite(Context, Path, Json, Size, 0600u,
            Error) ) goto done;
    if ( MdoMigrationScheduleUInt(Legacy, "maxRuns", 0u) != 0u ||
         MdoMigrationScheduleGet(Legacy, "runs") != NULL ||
         MdoMigrationScheduleGet(Legacy, "lastResult") != NULL )
        ++Context->Result->SkippedItems;
    ++Context->Result->ImportedSchedules;
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy schedule cannot be converted");
done:
    xrtFree(Json);
    xrtValueRelease(Legacy);
    return Ok;
}

bool MdoMigrationConvertSchedules(MdoMigrationContext* Context,
    xwork_error* Error)
{
    size_t i;
    for ( i = 0u; i < Context->Scan.Count; ++i )
        if ( Context->Scan.Files[i].Kind == MDO_MIGRATION_FILE_SCHEDULE &&
             !MdoMigrationConvertSchedule(Context, &Context->Scan.Files[i],
                Error) ) return false;
    return true;
}
