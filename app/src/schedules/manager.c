#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/config.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/schedules.h"

#define MDO_SCHEDULE_SCHEMA_VERSION 1u
#define MDO_SCHEDULE_MAX 128u
#define MDO_SCHEDULE_DIAGNOSTIC_MAX 256u
#define MDO_SCHEDULE_STORE_LIMIT (128u * 1024u)
#define MDO_SCHEDULE_AUDIT_LIMIT (8u * 1024u * 1024u)
#define MDO_SCHEDULE_AUDIT_RETAIN (4u * 1024u * 1024u)
#define MDO_SCHEDULE_RESULT_LIMIT (MDO_SCHEDULE_RESULT_CAPACITY - 1u)

typedef struct MdoScheduleEntry {
    MdoScheduleInfo Info;
    bool Registered;
} MdoScheduleEntry;

struct MdoScheduleCatalog {
    xatomic32 Refs;
    uint64 Generation;
    MdoScheduleInfo* Items;
    size_t Count;
    MdoScheduleDiagnostic* Diagnostics;
    size_t DiagnosticCount;
};

typedef struct MdoScheduleState {
    xmutex* Lock;
    xfile WriterLock;
    xwork_runtime* Runtime;
    MdoScheduleEntry* Entries;
    size_t Count;
    size_t Capacity;
    MdoScheduleDiagnostic* Diagnostics;
    size_t DiagnosticCount;
    size_t DiagnosticCapacity;
    uint64 Generation;
    bool Enabled;
    bool PersistenceFault;
    bool Initialized;
} MdoScheduleState;

static MdoScheduleState g_MdoSchedules;

static void MdoSchedulesError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
        Message != NULL ? Message : "schedule operation failed");
}

static void MdoSchedulesXrtError(xwork_error* Error, const char* Fallback)
{
    const xerror* Cause = xrtGetError();
    MdoSchedulesError(Error, XWORK_ERROR_IO,
        Cause != NULL && xrtErrorMessage(Cause) != NULL ?
        xrtErrorMessage(Cause) : Fallback);
}

static bool MdoSchedulesGrow(void** Items, size_t* Capacity, size_t Count,
    size_t ItemSize)
{
    size_t Next;
    void* Value;
    if ( Count <= *Capacity ) return true;
    Next = *Capacity != 0u ? *Capacity : 8u;
    while ( Next < Count ) {
        if ( Next > SIZE_MAX / 2u ) return false;
        Next *= 2u;
    }
    if ( Next > SIZE_MAX / ItemSize ) return false;
    Value = xrtRealloc(*Items, Next * ItemSize);
    if ( Value == NULL ) return false;
    *Items = Value;
    *Capacity = Next;
    return true;
}

static bool MdoSchedulesText(const char* Text, size_t Capacity,
    bool EmptyAllowed)
{
    size_t Size = 0u;
    if ( Text == NULL ) return false;
    while ( Size < Capacity && Text[Size] != '\0' ) ++Size;
    return Size < Capacity && (EmptyAllowed || Size != 0u) &&
        xrtUtf8Valid(xrtStrViewN(Text, Size), NULL);
}

static bool MdoSchedulesId(const char* Text, size_t Capacity)
{
    size_t i;
    if ( !MdoSchedulesText(Text, Capacity, false) ) return false;
    for ( i = 0u; Text[i] != '\0'; ++i ) {
        unsigned char Byte = (unsigned char)Text[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    return !(i == 1u && Text[0] == '.') &&
        !(i == 2u && Text[0] == '.' && Text[1] == '.');
}

static bool MdoSchedulesCopyView(char* Target, size_t Capacity,
    xstrview View, bool EmptyAllowed)
{
    if ( View.Size >= Capacity || (!EmptyAllowed && View.Size == 0u) ||
         memchr(View.Data, '\0', View.Size) != NULL ||
         !xrtUtf8Valid(View, NULL) ) return false;
    memcpy(Target, View.Data, View.Size);
    Target[View.Size] = '\0';
    return true;
}

static bool MdoSchedulesPath(char Path[MDO_SCHEDULE_PATH_CAPACITY],
    const char* ScheduleId)
{
    int Written = snprintf(Path, MDO_SCHEDULE_PATH_CAPACITY,
        "schedules/%s.json", ScheduleId);
    return Written > 0 && (size_t)Written < MDO_SCHEDULE_PATH_CAPACITY;
}

static bool MdoSchedulesHistoryPath(char Path[MDO_SCHEDULE_PATH_CAPACITY],
    const char* ScheduleId)
{
    int Written = snprintf(Path, MDO_SCHEDULE_PATH_CAPACITY,
        "schedules/history/%s.jsonl", ScheduleId);
    return Written > 0 && (size_t)Written < MDO_SCHEDULE_PATH_CAPACITY;
}

static const char* MdoSchedulesProtocolName(MdoModelProtocol Protocol)
{
    if ( Protocol == MDO_SCHEDULE_PROTOCOL_DEFAULT ) return "default";
    if ( Protocol == MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS )
        return "openai-chat-completions";
    if ( Protocol == MDO_MODEL_PROTOCOL_OPENAI_RESPONSES )
        return "openai-responses";
    if ( Protocol == MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES )
        return "anthropic-messages";
    return NULL;
}

static MdoModelProtocol MdoSchedulesProtocolParse(xstrview Text)
{
    if ( Text.Size == 7u && memcmp(Text.Data, "default", 7u) == 0 )
        return MDO_SCHEDULE_PROTOCOL_DEFAULT;
    if ( Text.Size == 23u &&
         memcmp(Text.Data, "openai-chat-completions", 23u) == 0 )
        return MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
    if ( Text.Size == 16u &&
         memcmp(Text.Data, "openai-responses", 16u) == 0 )
        return MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    if ( Text.Size == 18u &&
         memcmp(Text.Data, "anthropic-messages", 18u) == 0 )
        return MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;
    return (MdoModelProtocol)-1;
}

static const char* MdoSchedulesFrequencyName(xwork_schedule_frequency Value)
{
    switch ( Value ) {
    case XWORK_SCHEDULE_ONCE: return "once";
    case XWORK_SCHEDULE_MINUTELY: return "minutely";
    case XWORK_SCHEDULE_HOURLY: return "hourly";
    case XWORK_SCHEDULE_DAILY: return "daily";
    case XWORK_SCHEDULE_WEEKLY: return "weekly";
    default: return NULL;
    }
}

static xwork_schedule_frequency MdoSchedulesFrequencyParse(xstrview Text)
{
    if ( Text.Size == 4u && memcmp(Text.Data, "once", 4u) == 0 )
        return XWORK_SCHEDULE_ONCE;
    if ( Text.Size == 8u && memcmp(Text.Data, "minutely", 8u) == 0 )
        return XWORK_SCHEDULE_MINUTELY;
    if ( Text.Size == 6u && memcmp(Text.Data, "hourly", 6u) == 0 )
        return XWORK_SCHEDULE_HOURLY;
    if ( Text.Size == 5u && memcmp(Text.Data, "daily", 5u) == 0 )
        return XWORK_SCHEDULE_DAILY;
    if ( Text.Size == 6u && memcmp(Text.Data, "weekly", 6u) == 0 )
        return XWORK_SCHEDULE_WEEKLY;
    return (xwork_schedule_frequency)-1;
}

static const char* MdoSchedulesTimezoneName(xwork_schedule_timezone Value)
{
    switch ( Value ) {
    case XWORK_SCHEDULE_TIMEZONE_UTC: return "utc";
    case XWORK_SCHEDULE_TIMEZONE_FIXED_OFFSET: return "fixed-offset";
    case XWORK_SCHEDULE_TIMEZONE_SYSTEM_LOCAL: return "system-local";
    default: return NULL;
    }
}

static xwork_schedule_timezone MdoSchedulesTimezoneParse(xstrview Text)
{
    if ( Text.Size == 3u && memcmp(Text.Data, "utc", 3u) == 0 )
        return XWORK_SCHEDULE_TIMEZONE_UTC;
    if ( Text.Size == 12u && memcmp(Text.Data, "fixed-offset", 12u) == 0 )
        return XWORK_SCHEDULE_TIMEZONE_FIXED_OFFSET;
    if ( Text.Size == 12u && memcmp(Text.Data, "system-local", 12u) == 0 )
        return XWORK_SCHEDULE_TIMEZONE_SYSTEM_LOCAL;
    return (xwork_schedule_timezone)-1;
}

static const char* MdoSchedulesFoldName(xwork_schedule_fold_policy Value)
{
    if ( Value == XWORK_SCHEDULE_FOLD_EARLIER ) return "earlier";
    if ( Value == XWORK_SCHEDULE_FOLD_LATER ) return "later";
    return NULL;
}

static xwork_schedule_fold_policy MdoSchedulesFoldParse(xstrview Text)
{
    if ( Text.Size == 7u && memcmp(Text.Data, "earlier", 7u) == 0 )
        return XWORK_SCHEDULE_FOLD_EARLIER;
    if ( Text.Size == 5u && memcmp(Text.Data, "later", 5u) == 0 )
        return XWORK_SCHEDULE_FOLD_LATER;
    return (xwork_schedule_fold_policy)-1;
}

static const char* MdoSchedulesMisfireName(xwork_schedule_misfire_policy Value)
{
    switch ( Value ) {
    case XWORK_SCHEDULE_MISFIRE_SKIP: return "skip";
    case XWORK_SCHEDULE_MISFIRE_RUN_ONCE: return "run-once";
    case XWORK_SCHEDULE_MISFIRE_CATCH_UP: return "catch-up";
    default: return NULL;
    }
}

static xwork_schedule_misfire_policy MdoSchedulesMisfireParse(xstrview Text)
{
    if ( Text.Size == 4u && memcmp(Text.Data, "skip", 4u) == 0 )
        return XWORK_SCHEDULE_MISFIRE_SKIP;
    if ( Text.Size == 8u && memcmp(Text.Data, "run-once", 8u) == 0 )
        return XWORK_SCHEDULE_MISFIRE_RUN_ONCE;
    if ( Text.Size == 8u && memcmp(Text.Data, "catch-up", 8u) == 0 )
        return XWORK_SCHEDULE_MISFIRE_CATCH_UP;
    return (xwork_schedule_misfire_policy)-1;
}

static const char* MdoSchedulesOverlapName(xwork_schedule_overlap_policy Value)
{
    if ( Value == XWORK_SCHEDULE_OVERLAP_SKIP ) return "skip";
    if ( Value == XWORK_SCHEDULE_OVERLAP_QUEUE_ONE ) return "queue-one";
    return NULL;
}

static xwork_schedule_overlap_policy MdoSchedulesOverlapParse(xstrview Text)
{
    if ( Text.Size == 4u && memcmp(Text.Data, "skip", 4u) == 0 )
        return XWORK_SCHEDULE_OVERLAP_SKIP;
    if ( Text.Size == 9u && memcmp(Text.Data, "queue-one", 9u) == 0 )
        return XWORK_SCHEDULE_OVERLAP_QUEUE_ONE;
    return (xwork_schedule_overlap_policy)-1;
}

static bool MdoSchedulesObjectTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoSchedulesObjectString(xvalue* Object, const char* Key,
    const char* Value)
{
    return MdoSchedulesObjectTake(Object, Key,
        xrtValueString(xrtStrView(Value != NULL ? Value : "")));
}

static bool MdoSchedulesValueUInt(const xvalue* Object, const char* Key,
    uint64* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    int64 Signed;
    if ( Value == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Result = (uint64)Signed;
    return true;
}

static bool MdoSchedulesValueInt(const xvalue* Object, const char* Key,
    int64* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    uint64 Unsigned;
    if ( Value == NULL ) return false;
    if ( xrtValueType(Value) == XVALUE_INT )
        return xrtValueGetInt(Value, Result);
    if ( xrtValueType(Value) != XVALUE_UINT ||
         !xrtValueGetUInt(Value, &Unsigned) || Unsigned > INT64_MAX )
        return false;
    *Result = (int64)Unsigned;
    return true;
}

static bool MdoSchedulesValueString(const xvalue* Object, const char* Key,
    xstrview* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Result) &&
        memchr(Result->Data, '\0', Result->Size) == NULL;
}

static bool MdoSchedulesValueBool(const xvalue* Object, const char* Key,
    bool* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return Value != NULL && xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Result);
}

static void MdoSchedulesConfig(const MdoScheduleInfo* Info,
    bool EffectiveEnabled, xwork_schedule_config* Config)
{
    xworkScheduleConfigInit(Config);
    Config->sScheduleId = Info->Id;
    Config->sLabel = Info->Label;
    Config->sNotify = Info->Notify[0] != '\0' ? Info->Notify : NULL;
    Config->sAgentProfile = Info->AgentId;
    Config->sInput = Info->Input;
    Config->eFrequency = Info->Frequency;
    Config->uInterval = Info->Interval;
    Config->iStartAtUs = Info->StartAt;
    Config->uWeekdayMask = Info->WeekdayMask;
    Config->eTimezone = Info->Timezone;
    Config->iUtcOffsetSeconds = Info->UtcOffsetSeconds;
    Config->eFoldPolicy = Info->FoldPolicy;
    Config->eMisfirePolicy = Info->MisfirePolicy;
    Config->uMisfireGraceSeconds = Info->MisfireGraceSeconds;
    Config->uMaxCatchUp = Info->MaxCatchUp;
    Config->eOverlapPolicy = Info->OverlapPolicy;
    Config->uMaxConcurrentRuns = Info->MaxConcurrentRuns;
    Config->bEnabled = EffectiveEnabled;
}

static void MdoSchedulesRuntimeInfo(MdoScheduleInfo* Target,
    const xwork_schedule_info* Source)
{
    Target->RuntimeGeneration = Source->uGeneration;
    Target->NextOccurrenceAt = Source->iNextOccurrenceAtUs;
    Target->LastClaimedAt = Source->iLastClaimedAtUs;
    Target->ClaimCount = Source->uClaimCount;
    Target->MisfireCount = Source->uMisfireCount;
    Target->ActiveRuns = Source->iActiveRuns;
}

static bool MdoSchedulesDefinitionValid(const MdoScheduleInfo* Info,
    xwork_error* Error)
{
    xwork_schedule_config Config;
    int64 Next = 0;
    bool Has = false;
    if ( Info == NULL || Info->StartAt <= 0 ||
         !MdoSchedulesId(Info->Id, sizeof(Info->Id)) ||
         !MdoSchedulesText(Info->Label, sizeof(Info->Label), false) ||
         !MdoSchedulesText(Info->Notify, sizeof(Info->Notify), true) ||
         !MdoSchedulesId(Info->ProjectId, sizeof(Info->ProjectId)) ||
         !MdoSchedulesId(Info->AgentId, sizeof(Info->AgentId)) ||
         !MdoSchedulesText(Info->ModelId, sizeof(Info->ModelId), true) ||
         (Info->ModelId[0] != '\0' &&
          !MdoSchedulesId(Info->ModelId, sizeof(Info->ModelId))) ||
         !MdoSchedulesText(Info->ReasoningEffort,
            sizeof(Info->ReasoningEffort), true) ||
         !MdoSchedulesText(Info->WorkspaceRoot,
            sizeof(Info->WorkspaceRoot), true) ||
         !MdoSchedulesText(Info->Input, sizeof(Info->Input), false) ||
         MdoSchedulesProtocolName(Info->Protocol) == NULL ||
         MdoSchedulesFrequencyName(Info->Frequency) == NULL ||
         MdoSchedulesTimezoneName(Info->Timezone) == NULL ||
         MdoSchedulesFoldName(Info->FoldPolicy) == NULL ||
         MdoSchedulesMisfireName(Info->MisfirePolicy) == NULL ||
         MdoSchedulesOverlapName(Info->OverlapPolicy) == NULL ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule definition");
        return false;
    }
    MdoSchedulesConfig(Info, Info->Enabled, &Config);
    return xworkScheduleNextOccurrence(&Config, Info->StartAt - 1,
        &Next, &Has, Error) && Has;
}

static char* MdoSchedulesJson(const MdoScheduleInfo* Info, size_t* Size)
{
    xvalue* Root = xrtValueObject();
    char* Json = NULL;
    if ( Root == NULL ||
         !MdoSchedulesObjectTake(Root, "schema_version",
            xrtValueUInt(MDO_SCHEDULE_SCHEMA_VERSION)) ||
         !MdoSchedulesObjectTake(Root, "revision",
            xrtValueUInt(Info->Revision)) ||
         !MdoSchedulesObjectTake(Root, "updated_at_us",
            xrtValueInt(Info->UpdatedAt)) ||
         !MdoSchedulesObjectString(Root, "id", Info->Id) ||
         !MdoSchedulesObjectString(Root, "label", Info->Label) ||
         !MdoSchedulesObjectString(Root, "notify", Info->Notify) ||
         !MdoSchedulesObjectString(Root, "project_id", Info->ProjectId) ||
         !MdoSchedulesObjectString(Root, "agent_id", Info->AgentId) ||
         !MdoSchedulesObjectString(Root, "model_id", Info->ModelId) ||
         !MdoSchedulesObjectString(Root, "protocol",
            MdoSchedulesProtocolName(Info->Protocol)) ||
         !MdoSchedulesObjectString(Root, "reasoning_effort",
            Info->ReasoningEffort) ||
         !MdoSchedulesObjectTake(Root, "max_output_tokens",
            xrtValueUInt(Info->MaxOutputTokens)) ||
         !MdoSchedulesObjectString(Root, "workspace_root",
            Info->WorkspaceRoot) ||
         !MdoSchedulesObjectString(Root, "input", Info->Input) ||
         !MdoSchedulesObjectString(Root, "frequency",
            MdoSchedulesFrequencyName(Info->Frequency)) ||
         !MdoSchedulesObjectTake(Root, "interval",
            xrtValueUInt(Info->Interval)) ||
         !MdoSchedulesObjectTake(Root, "start_at_us",
            xrtValueInt(Info->StartAt)) ||
         !MdoSchedulesObjectTake(Root, "weekday_mask",
            xrtValueUInt(Info->WeekdayMask)) ||
         !MdoSchedulesObjectString(Root, "timezone",
            MdoSchedulesTimezoneName(Info->Timezone)) ||
         !MdoSchedulesObjectTake(Root, "utc_offset_seconds",
            xrtValueInt(Info->UtcOffsetSeconds)) ||
         !MdoSchedulesObjectString(Root, "fold_policy",
            MdoSchedulesFoldName(Info->FoldPolicy)) ||
         !MdoSchedulesObjectString(Root, "misfire_policy",
            MdoSchedulesMisfireName(Info->MisfirePolicy)) ||
         !MdoSchedulesObjectTake(Root, "misfire_grace_seconds",
            xrtValueUInt(Info->MisfireGraceSeconds)) ||
         !MdoSchedulesObjectTake(Root, "max_catch_up",
            xrtValueUInt(Info->MaxCatchUp)) ||
         !MdoSchedulesObjectString(Root, "overlap_policy",
            MdoSchedulesOverlapName(Info->OverlapPolicy)) ||
         !MdoSchedulesObjectTake(Root, "max_concurrent_runs",
            xrtValueUInt(Info->MaxConcurrentRuns)) ||
         !MdoSchedulesObjectTake(Root, "enabled",
            xrtValueBool(Info->Enabled)) ||
         !MdoSchedulesObjectTake(Root, "runtime_generation",
            xrtValueUInt(Info->RuntimeGeneration)) ||
         !MdoSchedulesObjectTake(Root, "next_occurrence_at_us",
            xrtValueInt(Info->NextOccurrenceAt)) ||
         !MdoSchedulesObjectTake(Root, "last_claimed_at_us",
            xrtValueInt(Info->LastClaimedAt)) ||
         !MdoSchedulesObjectTake(Root, "claim_count",
            xrtValueUInt(Info->ClaimCount)) ||
         !MdoSchedulesObjectTake(Root, "misfire_count",
            xrtValueUInt(Info->MisfireCount)) ) goto done;
    Json = xrtJsonStringify(Root, true, Size);
    if ( Json != NULL && *Size > MDO_SCHEDULE_STORE_LIMIT ) {
        xrtFree(Json);
        Json = NULL;
    }
done:
    xrtValueRelease(Root);
    return Json;
}

static bool MdoSchedulesRead(const char* Path, char** Data, size_t* Size)
{
    xfile File = NULL;
    xfileinfo Info;
    char* Bytes = NULL;
    bool Ok = false;
    *Data = NULL;
    *Size = 0u;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL || !xrtFileStat(File, &Info) ||
         Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_SCHEDULE_STORE_LIMIT || Info.Size > SIZE_MAX - 1u )
        goto done;
    Bytes = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if ( Bytes == NULL || (Info.Size != 0u &&
         !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    *Data = Bytes;
    *Size = (size_t)Info.Size;
    Bytes = NULL;
    Ok = true;
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    xrtFree(Bytes);
    if ( !Ok ) {
        xrtFree(*Data);
        *Data = NULL;
        *Size = 0u;
    }
    return Ok;
}

static bool MdoSchedulesParse(const char* ExpectedId, xstrview Json,
    MdoScheduleInfo* Info)
{
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    xstrview Id, Label, Notify, Project, Agent, Model, Protocol, Reasoning;
    xstrview Workspace, Input, Frequency, Timezone, Fold, Misfire, Overlap;
    uint64 Schema, Revision, MaxOutput, Interval, Weekday, Grace, CatchUp;
    uint64 MaxConcurrent, RuntimeGeneration, ClaimCount, MisfireCount;
    int64 Updated, Start, Offset, Next, Last;
    bool Enabled;
    bool Ok = false;
    memset(Info, 0, sizeof(*Info));
    Info->Size = sizeof(*Info);
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_SCHEDULE_STORE_LIMIT;
    Config.MaxDepth = 6u;
    Config.MaxValues = 128u;
    Config.MaxContainerItems = 64u;
    Root = xrtJsonRead(Json, &Config);
    if ( Root == NULL || xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 32u ||
         !MdoSchedulesValueUInt(Root, "schema_version", &Schema) ||
         Schema != MDO_SCHEDULE_SCHEMA_VERSION ||
         !MdoSchedulesValueUInt(Root, "revision", &Revision) || Revision == 0u ||
         !MdoSchedulesValueInt(Root, "updated_at_us", &Updated) || Updated < 0 ||
         !MdoSchedulesValueString(Root, "id", &Id) ||
         !MdoSchedulesValueString(Root, "label", &Label) ||
         !MdoSchedulesValueString(Root, "notify", &Notify) ||
         !MdoSchedulesValueString(Root, "project_id", &Project) ||
         !MdoSchedulesValueString(Root, "agent_id", &Agent) ||
         !MdoSchedulesValueString(Root, "model_id", &Model) ||
         !MdoSchedulesValueString(Root, "protocol", &Protocol) ||
         !MdoSchedulesValueString(Root, "reasoning_effort", &Reasoning) ||
         !MdoSchedulesValueUInt(Root, "max_output_tokens", &MaxOutput) ||
         MaxOutput > UINT32_MAX ||
         !MdoSchedulesValueString(Root, "workspace_root", &Workspace) ||
         !MdoSchedulesValueString(Root, "input", &Input) ||
         !MdoSchedulesValueString(Root, "frequency", &Frequency) ||
         !MdoSchedulesValueUInt(Root, "interval", &Interval) ||
         Interval > UINT32_MAX ||
         !MdoSchedulesValueInt(Root, "start_at_us", &Start) ||
         !MdoSchedulesValueUInt(Root, "weekday_mask", &Weekday) ||
         Weekday > UINT8_MAX ||
         !MdoSchedulesValueString(Root, "timezone", &Timezone) ||
         !MdoSchedulesValueInt(Root, "utc_offset_seconds", &Offset) ||
         Offset < INT32_MIN || Offset > INT32_MAX ||
         !MdoSchedulesValueString(Root, "fold_policy", &Fold) ||
         !MdoSchedulesValueString(Root, "misfire_policy", &Misfire) ||
         !MdoSchedulesValueUInt(Root, "misfire_grace_seconds", &Grace) ||
         Grace > UINT32_MAX ||
         !MdoSchedulesValueUInt(Root, "max_catch_up", &CatchUp) ||
         CatchUp > UINT32_MAX ||
         !MdoSchedulesValueString(Root, "overlap_policy", &Overlap) ||
         !MdoSchedulesValueUInt(Root, "max_concurrent_runs", &MaxConcurrent) ||
         MaxConcurrent > UINT32_MAX ||
         !MdoSchedulesValueBool(Root, "enabled", &Enabled) ||
         !MdoSchedulesValueUInt(Root, "runtime_generation",
            &RuntimeGeneration) || RuntimeGeneration == 0u ||
         !MdoSchedulesValueInt(Root, "next_occurrence_at_us", &Next) || Next < 0 ||
         !MdoSchedulesValueInt(Root, "last_claimed_at_us", &Last) || Last < 0 ||
         !MdoSchedulesValueUInt(Root, "claim_count", &ClaimCount) ||
         !MdoSchedulesValueUInt(Root, "misfire_count", &MisfireCount) )
        goto done;
    Info->Revision = Revision;
    Info->UpdatedAt = Updated;
    Info->RuntimeGeneration = RuntimeGeneration;
    Info->NextOccurrenceAt = Next;
    Info->LastClaimedAt = Last;
    Info->ClaimCount = ClaimCount;
    Info->MisfireCount = MisfireCount;
    Info->Frequency = MdoSchedulesFrequencyParse(Frequency);
    Info->Interval = (uint32)Interval;
    Info->StartAt = Start;
    Info->WeekdayMask = (uint8)Weekday;
    Info->Timezone = MdoSchedulesTimezoneParse(Timezone);
    Info->UtcOffsetSeconds = (int32)Offset;
    Info->FoldPolicy = MdoSchedulesFoldParse(Fold);
    Info->MisfirePolicy = MdoSchedulesMisfireParse(Misfire);
    Info->MisfireGraceSeconds = (uint32)Grace;
    Info->MaxCatchUp = (uint32)CatchUp;
    Info->OverlapPolicy = MdoSchedulesOverlapParse(Overlap);
    Info->MaxConcurrentRuns = (uint32)MaxConcurrent;
    Info->Protocol = MdoSchedulesProtocolParse(Protocol);
    Info->MaxOutputTokens = (uint32)MaxOutput;
    Info->Enabled = Enabled;
    if ( !MdoSchedulesCopyView(Info->Id, sizeof(Info->Id), Id, false) ||
         !MdoSchedulesCopyView(Info->Label, sizeof(Info->Label), Label, false) ||
         !MdoSchedulesCopyView(Info->Notify, sizeof(Info->Notify), Notify, true) ||
         !MdoSchedulesCopyView(Info->ProjectId, sizeof(Info->ProjectId),
            Project, false) ||
         !MdoSchedulesCopyView(Info->AgentId, sizeof(Info->AgentId), Agent,
            false) ||
         !MdoSchedulesCopyView(Info->ModelId, sizeof(Info->ModelId), Model, true) ||
         !MdoSchedulesCopyView(Info->ReasoningEffort,
            sizeof(Info->ReasoningEffort), Reasoning, true) ||
         !MdoSchedulesCopyView(Info->WorkspaceRoot,
            sizeof(Info->WorkspaceRoot), Workspace, true) ||
         !MdoSchedulesCopyView(Info->Input, sizeof(Info->Input), Input, false) ||
         strcmp(Info->Id, ExpectedId) != 0 ||
         !MdoSchedulesDefinitionValid(Info, NULL) ) goto done;
    Ok = true;
done:
    xrtValueRelease(Root);
    return Ok;
}

static bool MdoSchedulesWriteStore(const MdoScheduleInfo* Info,
    xwork_error* Error)
{
    char Path[MDO_SCHEDULE_PATH_CAPACITY];
    char* Json;
    size_t Size = 0u;
    bool Ok;
    if ( !MdoSchedulesPath(Path, Info->Id) ) return false;
    Json = MdoSchedulesJson(Info, &Size);
    if ( Json == NULL ) {
        MdoSchedulesError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot serialize schedule definition");
        return false;
    }
    Ok = MdoHomeAtomicWrite(Path, Json, Size, true);
    xrtFree(Json);
    if ( !Ok ) MdoSchedulesXrtError(Error,
        "cannot publish schedule definition");
    return Ok;
}

static bool MdoSchedulesWriterLock(xwork_error* Error)
{
    xfile File;
    if ( g_MdoSchedules.WriterLock != NULL ) return true;
    File = MdoHomeOpenWrite("schedules/.writer.lock",
        XFILE_READ | XFILE_CREATE | XFILE_SYNC);
    if ( File == NULL || !xrtFileLock(File, XFILE_LOCK_EXCLUSIVE, false) ) {
        if ( File != NULL ) (void)xrtClose(File);
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule store is locked by another process");
        return false;
    }
    g_MdoSchedules.WriterLock = File;
    return true;
}

static bool MdoSchedulesAppendBounded(const char* Path, const char* Json,
    size_t Size, xwork_error* Error)
{
    xfileinfo Info;
    bool Exists = false;
    xfile File = NULL;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) goto io;
    if ( Exists && (Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_SCHEDULE_AUDIT_LIMIT) ) goto limit;
    if ( Exists && Info.Size > MDO_SCHEDULE_AUDIT_LIMIT - Size - 1u ) {
        xfile Input = MdoHomeOpenRead(Path);
        char* Data = NULL;
        size_t DataSize = 0u;
        size_t Start;
        if ( Input == NULL || Info.Size > SIZE_MAX - 1u ) {
            if ( Input != NULL ) (void)xrtClose(Input);
            goto io;
        }
        Data = (char*)xrtMalloc((size_t)Info.Size + 1u);
        if ( Data == NULL || (Info.Size != 0u &&
             !xrtReadFull(Input, Data, (size_t)Info.Size, NULL)) ||
             !xrtClose(Input) ) {
            xrtFree(Data);
            goto io;
        }
        DataSize = (size_t)Info.Size;
        Start = DataSize > MDO_SCHEDULE_AUDIT_RETAIN ?
            DataSize - MDO_SCHEDULE_AUDIT_RETAIN : 0u;
        while ( Start < DataSize && Start != 0u && Data[Start] != '\n' )
            ++Start;
        if ( Start < DataSize && Start != 0u ) ++Start;
        if ( !MdoHomeAtomicWrite(Path, Data + Start, DataSize - Start,
                false) ) {
            xrtFree(Data);
            goto io;
        }
        xrtFree(Data);
    }
    File = MdoHomeOpenWrite(Path, XFILE_CREATE | XFILE_APPEND | XFILE_SYNC);
    if ( File == NULL || !xrtWriteFull(File, Json, Size, NULL) ||
         !xrtWriteFull(File, "\n", 1u, NULL) || !xrtFlush(File) ) goto io;
    if ( !xrtClose(File) ) { File = NULL; goto io; }
    return true;
limit:
    MdoSchedulesError(Error, XWORK_ERROR_LIMIT,
        "schedule audit or history reached its bounded limit");
    return false;
io:
    if ( File != NULL ) (void)xrtClose(File);
    MdoSchedulesXrtError(Error, "cannot persist schedule audit or history");
    return false;
}

static bool MdoSchedulesHash(const char* Text, char Output[65])
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Digest[XRT_SHA256_SIZE];
    size_t i;
    size_t Size = Text != NULL ? strlen(Text) : 0u;
    if ( !xrtSha256(Text != NULL ? Text : "", Size, Digest) ) return false;
    for ( i = 0u; i < XRT_SHA256_SIZE; ++i ) {
        Output[i * 2u] = Hex[Digest[i] >> 4u];
        Output[i * 2u + 1u] = Hex[Digest[i] & 0x0fu];
    }
    Output[64] = '\0';
    return true;
}

static bool MdoSchedulesAudit(const char* Operation,
    const MdoScheduleInfo* Info, uint64 Previous, uint64 Next,
    uint64 TaskId, int64 OccurrenceAt, xwork_error* Error)
{
    xvalue* Root = xrtValueObject();
    char* AuditId = xrtXidMakeString();
    char Hash[65];
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = false;
    if ( !MdoSchedulesHash(Info->Input, Hash) || Root == NULL ||
         AuditId == NULL ||
         !MdoSchedulesObjectTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoSchedulesObjectString(Root, "audit_id", AuditId) ||
         !MdoSchedulesObjectTake(Root, "occurred_at_us", xrtValueInt(xrtNow())) ||
         !MdoSchedulesObjectString(Root, "phase", "prepared") ||
         !MdoSchedulesObjectString(Root, "operation", Operation) ||
         !MdoSchedulesObjectString(Root, "schedule_id", Info->Id) ||
         !MdoSchedulesObjectTake(Root, "previous_revision",
            xrtValueUInt(Previous)) ||
         !MdoSchedulesObjectTake(Root, "next_revision", xrtValueUInt(Next)) ||
         !MdoSchedulesObjectTake(Root, "task_id", xrtValueUInt(TaskId)) ||
         !MdoSchedulesObjectTake(Root, "occurrence_at_us",
            xrtValueInt(OccurrenceAt)) ||
         !MdoSchedulesObjectTake(Root, "input_bytes",
            xrtValueUInt(strlen(Info->Input))) ||
         !MdoSchedulesObjectString(Root, "input_sha256", Hash) ) goto memory;
    Json = xrtJsonStringify(Root, false, &Size);
    if ( Json == NULL || Size > 4096u ) goto memory;
    Ok = MdoSchedulesAppendBounded("schedules/audit.jsonl", Json, Size,
        Error);
    goto done;
memory:
    MdoSchedulesError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate schedule audit record");
done:
    xrtFree(Json);
    xrtFree(AuditId);
    xrtValueRelease(Root);
    return Ok;
}

static MdoScheduleEntry* MdoSchedulesFind(const char* ScheduleId)
{
    size_t i;
    for ( i = 0u; i < g_MdoSchedules.Count; ++i )
        if ( strcmp(g_MdoSchedules.Entries[i].Info.Id, ScheduleId) == 0 )
            return &g_MdoSchedules.Entries[i];
    return NULL;
}

static bool MdoSchedulesAddEntry(const MdoScheduleInfo* Info,
    bool Registered)
{
    MdoScheduleEntry* Entry;
    if ( g_MdoSchedules.Count >= MDO_SCHEDULE_MAX ||
         MdoSchedulesFind(Info->Id) != NULL ||
         !MdoSchedulesGrow((void**)&g_MdoSchedules.Entries,
            &g_MdoSchedules.Capacity, g_MdoSchedules.Count + 1u,
            sizeof(*g_MdoSchedules.Entries)) ) return false;
    Entry = &g_MdoSchedules.Entries[g_MdoSchedules.Count++];
    memset(Entry, 0, sizeof(*Entry));
    Entry->Info = *Info;
    Entry->Registered = Registered;
    return true;
}

static void MdoSchedulesDiagnostic(const char* Path, const char* Message)
{
    MdoScheduleDiagnostic* Diagnostic;
    if ( g_MdoSchedules.DiagnosticCount >= MDO_SCHEDULE_DIAGNOSTIC_MAX ||
         !MdoSchedulesGrow((void**)&g_MdoSchedules.Diagnostics,
            &g_MdoSchedules.DiagnosticCapacity,
            g_MdoSchedules.DiagnosticCount + 1u,
            sizeof(*g_MdoSchedules.Diagnostics)) ) return;
    Diagnostic = &g_MdoSchedules.Diagnostics[g_MdoSchedules.DiagnosticCount++];
    memset(Diagnostic, 0, sizeof(*Diagnostic));
    Diagnostic->Size = sizeof(*Diagnostic);
    snprintf(Diagnostic->Path, sizeof(Diagnostic->Path), "%s", Path);
    snprintf(Diagnostic->Message, sizeof(Diagnostic->Message), "%s", Message);
}

static bool MdoSchedulesFilename(xstrview Name,
    char Id[MDO_SCHEDULE_ID_CAPACITY], bool* Backup)
{
    static const char Json[] = ".json";
    static const char Bak[] = ".json.bak";
    size_t Size;
    *Backup = false;
    if ( Name.Size > sizeof(Bak) - 1u &&
         memcmp(Name.Data + Name.Size - (sizeof(Bak) - 1u), Bak,
            sizeof(Bak) - 1u) == 0 ) {
        Size = Name.Size - (sizeof(Bak) - 1u);
        *Backup = true;
    } else if ( Name.Size > sizeof(Json) - 1u &&
                memcmp(Name.Data + Name.Size - (sizeof(Json) - 1u), Json,
                    sizeof(Json) - 1u) == 0 ) {
        Size = Name.Size - (sizeof(Json) - 1u);
    } else return false;
    if ( Size >= MDO_SCHEDULE_ID_CAPACITY ||
         memchr(Name.Data, '\0', Size) != NULL ) return false;
    memcpy(Id, Name.Data, Size);
    Id[Size] = '\0';
    return MdoSchedulesId(Id, MDO_SCHEDULE_ID_CAPACITY);
}

static bool MdoSchedulesRestoreOne(const char* Id, const char* Path)
{
    char* Json = NULL;
    size_t Size = 0u;
    MdoScheduleInfo Info;
    xwork_schedule_info RuntimeInfo;
    xwork_schedule_config Config;
    xwork_error Error;
    bool Registered = false;
    if ( !MdoSchedulesRead(Path, &Json, &Size) ||
         !MdoSchedulesParse(Id, xrtStrViewN(Json, Size), &Info) ) {
        xrtFree(Json);
        MdoSchedulesDiagnostic(Path,
            "schedule definition does not satisfy schema version 1");
        return true;
    }
    xrtFree(Json);
    MdoSchedulesConfig(&Info, Info.Enabled && g_MdoSchedules.Enabled, &Config);
    xworkScheduleInfoInit(&RuntimeInfo);
    RuntimeInfo.tConfig = Config;
    RuntimeInfo.uGeneration = Info.RuntimeGeneration;
    RuntimeInfo.iNextOccurrenceAtUs = Info.NextOccurrenceAt;
    RuntimeInfo.iLastClaimedAtUs = Info.LastClaimedAt;
    RuntimeInfo.uClaimCount = Info.ClaimCount;
    RuntimeInfo.uMisfireCount = Info.MisfireCount;
    if ( xworkRuntimeRestoreSchedule(g_MdoSchedules.Runtime, &RuntimeInfo,
            &Error) ) {
        Registered = true;
    } else {
        MdoSchedulesDiagnostic(Path,
            Error.sMessage[0] != '\0' ? Error.sMessage :
            "cannot restore schedule into runtime");
    }
    if ( !MdoSchedulesAddEntry(&Info, Registered) ) return false;
    return true;
}

static bool MdoSchedulesLoad(void)
{
    bool Exists = false;
    xfileinfo Info;
    xdir Directory = NULL;
    xdirentry Entry;
    xdirnext Next;
    if ( !MdoHomeExternalStat("schedules", &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_DIRECTORY ) {
        MdoSchedulesDiagnostic("schedules",
            "schedule storage is not a directory");
        return true;
    }
    Directory = MdoHomeOpenDirectory("schedules", XDIR_STAT);
    if ( Directory == NULL ) return false;
    memset(&Entry, 0, sizeof(Entry));
    while ( (Next = xrtDirNext(Directory, &Entry)) == XDIR_NEXT_ITEM ) {
        char Id[MDO_SCHEDULE_ID_CAPACITY];
        char Path[MDO_SCHEDULE_PATH_CAPACITY];
        bool Backup = false;
        int Written;
        if ( (Entry.Flags & XDIR_ENTRY_UTF8) == 0u ) {
            MdoSchedulesDiagnostic("schedules", "non-UTF-8 schedule entry");
            continue;
        }
        if ( Entry.Info.Type == XFILE_TYPE_DIRECTORY &&
             Entry.Name.Size == 7u &&
             memcmp(Entry.Name.Data, "history", 7u) == 0 ) continue;
        if ( Entry.Info.Type == XFILE_TYPE_FILE &&
              ((Entry.Name.Size == 11u &&
                memcmp(Entry.Name.Data, "audit.jsonl", 11u) == 0) ||
              (Entry.Name.Size == 12u &&
               memcmp(Entry.Name.Data, ".writer.lock", 12u) == 0)) ) continue;
        if ( Entry.Info.Type != XFILE_TYPE_FILE ||
             !MdoSchedulesFilename(Entry.Name, Id, &Backup) ) {
            MdoSchedulesDiagnostic("schedules",
                "unknown or unsafe schedule entry");
            continue;
        }
        if ( Backup ) continue;
        Written = snprintf(Path, sizeof(Path), "schedules/%s.json", Id);
        if ( Written <= 0 || (size_t)Written >= sizeof(Path) ||
             !MdoSchedulesRestoreOne(Id, Path) ) {
            (void)xrtDirClose(Directory);
            return false;
        }
    }
    if ( Next == XDIR_NEXT_ERROR ) {
        (void)xrtDirClose(Directory);
        return false;
    }
    return xrtDirClose(Directory);
}

bool MdoScheduleManagerInit(xwork_runtime* Runtime)
{
    MdoConfigAgentSettings Settings;
    if ( g_MdoSchedules.Initialized ) return true;
    if ( Runtime == NULL ) return false;
    memset(&Settings, 0, sizeof(Settings));
    Settings.Size = sizeof(Settings);
    if ( !MdoConfigGetAgentSettings(&Settings) ) return false;
    memset(&g_MdoSchedules, 0, sizeof(g_MdoSchedules));
    g_MdoSchedules.Lock = xrtMutexCreate();
    g_MdoSchedules.Runtime = xworkRuntimeRef(Runtime);
    g_MdoSchedules.Enabled = Settings.SchedulesEnabled;
    if ( g_MdoSchedules.Lock == NULL || g_MdoSchedules.Runtime == NULL ) {
        MdoScheduleManagerUnit();
        return false;
    }
    g_MdoSchedules.Generation = 1u;
    g_MdoSchedules.Initialized = true;
    if ( !MdoSchedulesLoad() ) {
        MdoScheduleManagerUnit();
        return false;
    }
    return true;
}

void MdoScheduleManagerUnit(void)
{
    size_t i;
    xwork_error Error;
    if ( g_MdoSchedules.Runtime != NULL ) {
        for ( i = 0u; i < g_MdoSchedules.Count; ++i )
            if ( g_MdoSchedules.Entries[i].Registered )
                (void)xworkRuntimeUnregisterSchedule(g_MdoSchedules.Runtime,
                    g_MdoSchedules.Entries[i].Info.Id, &Error);
    }
    if ( g_MdoSchedules.WriterLock != NULL ) {
        (void)xrtFileUnlock(g_MdoSchedules.WriterLock);
        (void)xrtClose(g_MdoSchedules.WriterLock);
    }
    xrtFree(g_MdoSchedules.Entries);
    xrtFree(g_MdoSchedules.Diagnostics);
    if ( g_MdoSchedules.Runtime != NULL )
        xworkRuntimeRelease(g_MdoSchedules.Runtime);
    if ( g_MdoSchedules.Lock != NULL ) xrtMutexDestroy(g_MdoSchedules.Lock);
    memset(&g_MdoSchedules, 0, sizeof(g_MdoSchedules));
}

uint64 MdoScheduleManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( !g_MdoSchedules.Initialized ) return 0u;
    xrtMutexLock(g_MdoSchedules.Lock);
    Generation = g_MdoSchedules.Generation;
    xrtMutexUnlock(g_MdoSchedules.Lock);
    return Generation;
}

bool MdoScheduleManagerReloadSettings(xwork_error* Error)
{
    MdoConfigAgentSettings Settings;
    bool PreviousEnabled;
    size_t Index;
    size_t Updated = 0u;
    bool RollbackOk = true;

    xworkErrorInit(Error);
    memset(&Settings, 0, sizeof(Settings));
    Settings.Size = sizeof(Settings);
    if ( !g_MdoSchedules.Initialized ||
         !MdoConfigGetAgentSettings(&Settings) ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule settings are unavailable");
        return false;
    }
    if ( !xrtMutexLock(g_MdoSchedules.Lock) ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule manager is unavailable");
        return false;
    }
    PreviousEnabled = g_MdoSchedules.Enabled;
    if ( PreviousEnabled == Settings.SchedulesEnabled ) {
        (void)xrtMutexUnlock(g_MdoSchedules.Lock);
        return true;
    }
    if ( g_MdoSchedules.Generation == UINT64_MAX ) {
        (void)xrtMutexUnlock(g_MdoSchedules.Lock);
        MdoSchedulesError(Error, XWORK_ERROR_LIMIT,
            "schedule generation is exhausted");
        return false;
    }
    for ( Index = 0u; Index < g_MdoSchedules.Count; Index++ ) {
        MdoScheduleEntry* Entry = &g_MdoSchedules.Entries[Index];
        if ( !Entry->Registered ) continue;
        if ( !xworkRuntimeSetScheduleEnabled(g_MdoSchedules.Runtime,
                Entry->Info.Id,
                Entry->Info.Enabled && Settings.SchedulesEnabled, Error) )
            break;
        Updated = Index + 1u;
    }
    if ( Index != g_MdoSchedules.Count ) {
        for ( Index = 0u; Index < Updated; Index++ ) {
            MdoScheduleEntry* Entry = &g_MdoSchedules.Entries[Index];
            xwork_error RollbackError;
            if ( !Entry->Registered ) continue;
            xworkErrorInit(&RollbackError);
            if ( !xworkRuntimeSetScheduleEnabled(g_MdoSchedules.Runtime,
                    Entry->Info.Id,
                    Entry->Info.Enabled && PreviousEnabled, &RollbackError) )
                RollbackOk = false;
        }
        if ( !RollbackOk ) {
            for ( Index = 0u; Index < g_MdoSchedules.Count; Index++ ) {
                MdoScheduleEntry* Entry = &g_MdoSchedules.Entries[Index];
                xwork_error DisableError;
                if ( !Entry->Registered ) continue;
                xworkErrorInit(&DisableError);
                (void)xworkRuntimeSetScheduleEnabled(g_MdoSchedules.Runtime,
                    Entry->Info.Id, false, &DisableError);
                Entry->Info.Runnable = false;
            }
            g_MdoSchedules.Enabled = false;
            g_MdoSchedules.Generation++;
            MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
                "schedule settings rollback failed; scheduling was disabled");
        }
        (void)xrtMutexUnlock(g_MdoSchedules.Lock);
        return false;
    }
    g_MdoSchedules.Enabled = Settings.SchedulesEnabled;
    g_MdoSchedules.Generation++;
    for ( Index = 0u; Index < g_MdoSchedules.Count; Index++ )
        g_MdoSchedules.Entries[Index].Info.Runnable =
            g_MdoSchedules.Entries[Index].Registered &&
            g_MdoSchedules.Entries[Index].Info.Enabled &&
            g_MdoSchedules.Enabled;
    (void)xrtMutexUnlock(g_MdoSchedules.Lock);
    return true;
}

bool MdoScheduleManagerEnabled(void)
{
    bool Enabled = false;
    if ( g_MdoSchedules.Initialized && g_MdoSchedules.Lock != NULL &&
         xrtMutexLock(g_MdoSchedules.Lock) ) {
        Enabled = g_MdoSchedules.Enabled;
        (void)xrtMutexUnlock(g_MdoSchedules.Lock);
    }
    return Enabled;
}

void MdoScheduleCreateOptionsInit(MdoScheduleCreateOptions* Options)
{
    if ( Options == NULL ) return;
    memset(Options, 0, sizeof(*Options));
    Options->Size = sizeof(*Options);
    Options->AgentId = "mdo.default";
    Options->ProjectId = "tasks";
    Options->Frequency = XWORK_SCHEDULE_ONCE;
    Options->Interval = 1u;
    Options->Timezone = XWORK_SCHEDULE_TIMEZONE_UTC;
    Options->FoldPolicy = XWORK_SCHEDULE_FOLD_EARLIER;
    Options->MisfirePolicy = XWORK_SCHEDULE_MISFIRE_RUN_ONCE;
    Options->MisfireGraceSeconds = 60u;
    Options->MaxCatchUp = 1u;
    Options->OverlapPolicy = XWORK_SCHEDULE_OVERLAP_SKIP;
    Options->MaxConcurrentRuns = 1u;
    Options->Enabled = true;
}

void MdoScheduleClaimInit(MdoScheduleClaim* Claim)
{
    if ( Claim == NULL ) return;
    memset(Claim, 0, sizeof(*Claim));
    Claim->Size = sizeof(*Claim);
}

static bool MdoSchedulesInfoFromOptions(const MdoScheduleCreateOptions* Options,
    const char* GeneratedId, MdoScheduleInfo* Info, xwork_error* Error)
{
    const char* Id = Options->Id != NULL ? Options->Id : GeneratedId;
    const char* Label = Options->Label != NULL ? Options->Label :
        "Scheduled task";
    const char* Notify = Options->Notify != NULL ? Options->Notify : "";
    const char* Model = Options->ModelId != NULL ? Options->ModelId : "";
    const char* Reasoning = Options->ReasoningEffort != NULL ?
        Options->ReasoningEffort : "";
    const char* Workspace = Options->WorkspaceRoot != NULL ?
        Options->WorkspaceRoot : "";
    if ( !MdoSchedulesId(Id, MDO_SCHEDULE_ID_CAPACITY) ||
         !MdoSchedulesText(Label, MDO_SCHEDULE_LABEL_CAPACITY, false) ||
         !MdoSchedulesText(Notify, MDO_SCHEDULE_NOTIFY_CAPACITY, true) ||
         !MdoSchedulesId(Options->ProjectId, MDO_SCHEDULE_PROJECT_CAPACITY) ||
         !MdoSchedulesId(Options->AgentId, MDO_SCHEDULE_IDENTITY_CAPACITY) ||
         !MdoSchedulesText(Model, MDO_SCHEDULE_IDENTITY_CAPACITY, true) ||
         (Model[0] != '\0' &&
          !MdoSchedulesId(Model, MDO_SCHEDULE_IDENTITY_CAPACITY)) ||
         !MdoSchedulesText(Reasoning, MDO_SCHEDULE_REASONING_CAPACITY, true) ||
         !MdoSchedulesText(Workspace, MDO_SCHEDULE_WORKSPACE_CAPACITY, true) ||
         !MdoSchedulesText(Options->Input, MDO_SCHEDULE_INPUT_CAPACITY, false) ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid or overlong schedule identity or input");
        return false;
    }
    memset(Info, 0, sizeof(*Info));
    Info->Size = sizeof(*Info);
    Info->Revision = 1u;
    Info->UpdatedAt = xrtNow();
    Info->Frequency = Options->Frequency;
    Info->Interval = Options->Interval;
    Info->StartAt = Options->StartAt;
    Info->WeekdayMask = Options->WeekdayMask;
    Info->Timezone = Options->Timezone;
    Info->UtcOffsetSeconds = Options->UtcOffsetSeconds;
    Info->FoldPolicy = Options->FoldPolicy;
    Info->MisfirePolicy = Options->MisfirePolicy;
    Info->MisfireGraceSeconds = Options->MisfireGraceSeconds;
    Info->MaxCatchUp = Options->MaxCatchUp;
    Info->OverlapPolicy = Options->OverlapPolicy;
    Info->MaxConcurrentRuns = Options->MaxConcurrentRuns;
    Info->Protocol = Options->Protocol;
    Info->MaxOutputTokens = Options->MaxOutputTokens;
    Info->Enabled = Options->Enabled;
    snprintf(Info->Id, sizeof(Info->Id), "%s", Id != NULL ? Id : "");
    snprintf(Info->Label, sizeof(Info->Label), "%s",
        Label);
    snprintf(Info->Notify, sizeof(Info->Notify), "%s",
        Notify);
    snprintf(Info->ProjectId, sizeof(Info->ProjectId), "%s",
        Options->ProjectId != NULL ? Options->ProjectId : "");
    snprintf(Info->AgentId, sizeof(Info->AgentId), "%s",
        Options->AgentId != NULL ? Options->AgentId : "");
    snprintf(Info->ModelId, sizeof(Info->ModelId), "%s",
        Model);
    snprintf(Info->ReasoningEffort, sizeof(Info->ReasoningEffort), "%s",
        Reasoning);
    snprintf(Info->WorkspaceRoot, sizeof(Info->WorkspaceRoot), "%s",
        Workspace);
    snprintf(Info->Input, sizeof(Info->Input), "%s",
        Options->Input);
    return MdoSchedulesDefinitionValid(Info, Error);
}

static bool MdoSchedulesReturnInfo(const MdoScheduleInfo* Source,
    MdoScheduleInfo* Target)
{
    uint32 Size;
    if ( Target == NULL ) return true;
    if ( Target->Size < sizeof(*Target) ) return false;
    Size = Target->Size;
    *Target = *Source;
    Target->Size = Size;
    return true;
}

bool MdoScheduleCreate(const MdoScheduleCreateOptions* Options,
    MdoScheduleInfo* Info, xwork_error* Error)
{
    char* GeneratedId = NULL;
    MdoScheduleInfo Candidate;
    xwork_schedule_config Config;
    xwork_schedule_info RuntimeInfo;
    char Path[MDO_SCHEDULE_PATH_CAPACITY];
    bool Exists = false;
    xfileinfo FileInfo;
    bool Registered = false;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoSchedules.Initialized || Options == NULL ||
         Options->Size < sizeof(*Options) ||
         (Info != NULL && Info->Size < sizeof(*Info)) ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule create request");
        return false;
    }
    if ( Options->Id == NULL ) GeneratedId = xrtXidMakeString();
    if ( (Options->Id == NULL && GeneratedId == NULL) ||
         !MdoSchedulesInfoFromOptions(Options, GeneratedId, &Candidate,
            Error) || !MdoSchedulesPath(Path, Candidate.Id) ) goto done;
    xrtMutexLock(g_MdoSchedules.Lock);
    if ( g_MdoSchedules.PersistenceFault ) {
        MdoSchedulesError(Error, XWORK_ERROR_IO,
            "schedule persistence is faulted; restart after repairing storage");
        goto done_locked;
    }
    if ( !MdoSchedulesWriterLock(Error) ) goto done_locked;
    if ( MdoSchedulesFind(Candidate.Id) != NULL ||
         !MdoHomeExternalStat(Path, &Exists, &FileInfo) ) {
        if ( MdoSchedulesFind(Candidate.Id) == NULL )
            MdoSchedulesXrtError(Error, "cannot inspect schedule definition");
        else MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule id already exists");
        goto done_locked;
    }
    if ( Exists ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule definition already exists on disk");
        goto done_locked;
    }
    MdoSchedulesConfig(&Candidate,
        Candidate.Enabled && g_MdoSchedules.Enabled, &Config);
    if ( !xworkRuntimeRegisterSchedule(g_MdoSchedules.Runtime, &Config,
            Error) ) goto done_locked;
    Registered = true;
    xworkScheduleInfoInit(&RuntimeInfo);
    if ( !xworkRuntimeScheduleGetInfo(g_MdoSchedules.Runtime, Candidate.Id,
            &RuntimeInfo) ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect registered schedule");
        goto rollback_locked;
    }
    MdoSchedulesRuntimeInfo(&Candidate, &RuntimeInfo);
    if ( !MdoSchedulesAudit("create", &Candidate, 0u, 1u, 0u, 0,
            Error) || !MdoSchedulesWriteStore(&Candidate, Error) )
        goto rollback_locked;
    if ( !MdoSchedulesAddEntry(&Candidate, true) ) {
        MdoSchedulesError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot publish schedule catalog entry");
        (void)MdoHomeRemove(Path, false);
        goto rollback_locked;
    }
    if ( g_MdoSchedules.Generation != UINT64_MAX )
        ++g_MdoSchedules.Generation;
    Candidate.Runnable = Candidate.Enabled && g_MdoSchedules.Enabled;
    if ( !MdoSchedulesReturnInfo(&Candidate, Info) ) goto rollback_entry_locked;
    Ok = true;
    goto done_locked;
rollback_entry_locked:
    --g_MdoSchedules.Count;
    (void)MdoHomeRemove(Path, false);
rollback_locked:
    if ( Registered )
        (void)xworkRuntimeUnregisterSchedule(g_MdoSchedules.Runtime,
            Candidate.Id, Error);
done_locked:
    xrtMutexUnlock(g_MdoSchedules.Lock);
done:
    xrtFree(GeneratedId);
    return Ok;
}

bool MdoScheduleSetEnabled(const char* ScheduleId, uint64 ExpectedRevision,
    bool Enabled, MdoScheduleInfo* Info, xwork_error* Error)
{
    MdoScheduleEntry* Entry;
    MdoScheduleInfo Previous;
    xwork_schedule_info RuntimeInfo;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoSchedules.Initialized ||
         !MdoSchedulesId(ScheduleId, MDO_SCHEDULE_ID_CAPACITY) ||
         (Info != NULL && Info->Size < sizeof(*Info)) ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule enable request");
        return false;
    }
    xrtMutexLock(g_MdoSchedules.Lock);
    Entry = MdoSchedulesFind(ScheduleId);
    if ( Entry == NULL || !Entry->Registered ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "schedule was not found or restored");
        goto done;
    }
    if ( g_MdoSchedules.PersistenceFault ) {
        MdoSchedulesError(Error, XWORK_ERROR_IO,
            "schedule persistence is faulted; restart after repairing storage");
        goto done;
    }
    if ( ExpectedRevision != UINT64_MAX &&
         Entry->Info.Revision != ExpectedRevision ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule revision changed; reload before updating");
        goto done;
    }
    if ( Entry->Info.Enabled == Enabled ) {
        MdoScheduleInfo Value = Entry->Info;
        Value.Runnable = Value.Enabled && g_MdoSchedules.Enabled;
        Ok = MdoSchedulesReturnInfo(&Value, Info);
        goto done;
    }
    if ( Entry->Info.Revision == UINT64_MAX ) {
        MdoSchedulesError(Error, XWORK_ERROR_LIMIT,
            "schedule revision is exhausted");
        goto done;
    }
    if ( !MdoSchedulesWriterLock(Error) ) goto done;
    Previous = Entry->Info;
    if ( !xworkRuntimeSetScheduleEnabled(g_MdoSchedules.Runtime, ScheduleId,
            Enabled && g_MdoSchedules.Enabled, Error) ) goto done;
    Entry->Info.Enabled = Enabled;
    ++Entry->Info.Revision;
    Entry->Info.UpdatedAt = xrtNow();
    xworkScheduleInfoInit(&RuntimeInfo);
    if ( !xworkRuntimeScheduleGetInfo(g_MdoSchedules.Runtime, ScheduleId,
            &RuntimeInfo) ) goto rollback;
    MdoSchedulesRuntimeInfo(&Entry->Info, &RuntimeInfo);
    if ( !MdoSchedulesAudit("set-enabled", &Entry->Info,
            Previous.Revision, Entry->Info.Revision, 0u, 0, Error) ||
         !MdoSchedulesWriteStore(&Entry->Info, Error) ) goto rollback;
    if ( g_MdoSchedules.Generation != UINT64_MAX )
        ++g_MdoSchedules.Generation;
    {
        MdoScheduleInfo Value = Entry->Info;
        Value.Runnable = Value.Enabled && g_MdoSchedules.Enabled;
        Ok = MdoSchedulesReturnInfo(&Value, Info);
    }
    goto done;
rollback:
    (void)xworkRuntimeSetScheduleEnabled(g_MdoSchedules.Runtime, ScheduleId,
        Previous.Enabled && g_MdoSchedules.Enabled, NULL);
    Entry->Info = Previous;
done:
    xrtMutexUnlock(g_MdoSchedules.Lock);
    return Ok;
}

bool MdoScheduleRemove(const char* ScheduleId, uint64 ExpectedRevision,
    xwork_error* Error)
{
    MdoScheduleEntry* Entry;
    MdoScheduleInfo Previous;
    xwork_schedule_info RuntimeInfo;
    char Path[MDO_SCHEDULE_PATH_CAPACITY];
    char Backup[MDO_SCHEDULE_PATH_CAPACITY + 5u];
    size_t Index;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoSchedules.Initialized ||
         !MdoSchedulesId(ScheduleId, MDO_SCHEDULE_ID_CAPACITY) ||
         !MdoSchedulesPath(Path, ScheduleId) ||
         snprintf(Backup, sizeof(Backup), "%s.bak", Path) <= 0 ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule remove request");
        return false;
    }
    xrtMutexLock(g_MdoSchedules.Lock);
    Entry = MdoSchedulesFind(ScheduleId);
    if ( Entry == NULL || !Entry->Registered ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "schedule was not found or restored");
        goto done;
    }
    if ( g_MdoSchedules.PersistenceFault ) {
        MdoSchedulesError(Error, XWORK_ERROR_IO,
            "schedule persistence is faulted; restart after repairing storage");
        goto done;
    }
    if ( ExpectedRevision != UINT64_MAX &&
         Entry->Info.Revision != ExpectedRevision ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule revision changed; reload before removing");
        goto done;
    }
    xworkScheduleInfoInit(&RuntimeInfo);
    if ( !xworkRuntimeScheduleGetInfo(g_MdoSchedules.Runtime, ScheduleId,
            &RuntimeInfo) ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "cannot inspect the schedule before removing it");
        goto done;
    }
    if ( RuntimeInfo.iActiveRuns != 0u ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "cannot remove a schedule while one of its runs is active");
        goto done;
    }
    if ( !MdoSchedulesWriterLock(Error) ) goto done;
    Previous = Entry->Info;
    if ( !MdoSchedulesAudit("remove", &Previous, Previous.Revision, 0u,
            0u, 0, Error) || !MdoHomeRemove(Path, false) ) {
        if ( Error == NULL || Error->eCode == XWORK_ERROR_NONE )
            MdoSchedulesXrtError(Error, "cannot remove schedule definition");
        goto done;
    }
    if ( !xworkRuntimeUnregisterSchedule(g_MdoSchedules.Runtime, ScheduleId,
            Error) ) {
        if ( !MdoSchedulesWriteStore(&Previous, NULL) )
            g_MdoSchedules.PersistenceFault = true;
        goto done;
    }
    (void)MdoHomeRemove(Backup, false);
    Index = (size_t)(Entry - g_MdoSchedules.Entries);
    g_MdoSchedules.Entries[Index] =
        g_MdoSchedules.Entries[g_MdoSchedules.Count - 1u];
    --g_MdoSchedules.Count;
    if ( g_MdoSchedules.Generation != UINT64_MAX )
        ++g_MdoSchedules.Generation;
    Ok = true;
done:
    xrtMutexUnlock(g_MdoSchedules.Lock);
    return Ok;
}

static bool MdoSchedulesCopyClaim(const MdoScheduleEntry* Entry,
    const xwork_schedule_claim* Source, int64 NextWake,
    MdoScheduleClaim* Claim)
{
    uint32 Size = Claim->Size;
    memset(Claim, 0, sizeof(*Claim));
    Claim->Size = Size;
    Claim->Claimed = true;
    Claim->NextWakeAt = NextWake;
    Claim->TaskId = Source->uTaskId;
    Claim->RuntimeGeneration = Source->uScheduleGeneration;
    Claim->DefinitionRevision = Entry->Info.Revision;
    Claim->OccurrenceAt = Source->iOccurrenceAtUs;
    Claim->Protocol = Entry->Info.Protocol;
    Claim->MaxOutputTokens = Entry->Info.MaxOutputTokens;
    snprintf(Claim->ScheduleId, sizeof(Claim->ScheduleId), "%s",
        Entry->Info.Id);
    snprintf(Claim->ProjectId, sizeof(Claim->ProjectId), "%s",
        Entry->Info.ProjectId);
    snprintf(Claim->AgentId, sizeof(Claim->AgentId), "%s",
        Entry->Info.AgentId);
    snprintf(Claim->ModelId, sizeof(Claim->ModelId), "%s",
        Entry->Info.ModelId);
    snprintf(Claim->ReasoningEffort, sizeof(Claim->ReasoningEffort), "%s",
        Entry->Info.ReasoningEffort);
    snprintf(Claim->WorkspaceRoot, sizeof(Claim->WorkspaceRoot), "%s",
        Entry->Info.WorkspaceRoot);
    snprintf(Claim->Input, sizeof(Claim->Input), "%s", Entry->Info.Input);
    return true;
}

static bool MdoSchedulesClaimPreflight(int64 Now, bool* Due,
    int64* NextWake, xwork_error* Error)
{
    size_t i;
    *Due = false;
    *NextWake = 0;
    for ( i = 0u; i < g_MdoSchedules.Count; ++i ) {
        MdoScheduleEntry* Entry = &g_MdoSchedules.Entries[i];
        xwork_schedule_info RuntimeInfo;
        if ( !Entry->Registered || !Entry->Info.Enabled ) continue;
        xworkScheduleInfoInit(&RuntimeInfo);
        if ( !xworkRuntimeScheduleGetInfo(g_MdoSchedules.Runtime,
                Entry->Info.Id, &RuntimeInfo) ) {
            MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
                "cannot inspect schedule readiness");
            return false;
        }
        if ( RuntimeInfo.iNextOccurrenceAtUs == 0 ) continue;
        if ( *NextWake == 0 ||
             RuntimeInfo.iNextOccurrenceAtUs < *NextWake )
            *NextWake = RuntimeInfo.iNextOccurrenceAtUs;
        if ( RuntimeInfo.iNextOccurrenceAtUs <= Now ) *Due = true;
    }
    return true;
}

static bool MdoSchedulesRuntimeChanged(const MdoScheduleInfo* Info,
    const xwork_schedule_info* RuntimeInfo)
{
    return Info->RuntimeGeneration != RuntimeInfo->uGeneration ||
        Info->NextOccurrenceAt != RuntimeInfo->iNextOccurrenceAtUs ||
        Info->LastClaimedAt != RuntimeInfo->iLastClaimedAtUs ||
        Info->ClaimCount != RuntimeInfo->uClaimCount ||
        Info->MisfireCount != RuntimeInfo->uMisfireCount;
}

static bool MdoSchedulesSyncRuntime(const xwork_schedule_claim* Claim,
    bool Claimed, xwork_error* Error)
{
    size_t i;
    bool Changed = false;
    bool Ok = true;
    for ( i = 0u; i < g_MdoSchedules.Count; ++i ) {
        MdoScheduleEntry* Entry = &g_MdoSchedules.Entries[i];
        xwork_schedule_info RuntimeInfo;
        uint64 Previous;
        bool IsClaim;
        if ( !Entry->Registered ) continue;
        xworkScheduleInfoInit(&RuntimeInfo);
        if ( !xworkRuntimeScheduleGetInfo(g_MdoSchedules.Runtime,
                Entry->Info.Id, &RuntimeInfo) ) {
            MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
                "cannot synchronize schedule runtime state");
            Ok = false;
            break;
        }
        Entry->Info.ActiveRuns = RuntimeInfo.iActiveRuns;
        if ( !MdoSchedulesRuntimeChanged(&Entry->Info, &RuntimeInfo) )
            continue;
        Previous = Entry->Info.Revision;
        if ( Previous == UINT64_MAX ) {
            MdoSchedulesError(Error, XWORK_ERROR_LIMIT,
                "schedule revision is exhausted");
            Ok = false;
            break;
        }
        MdoSchedulesRuntimeInfo(&Entry->Info, &RuntimeInfo);
        ++Entry->Info.Revision;
        Entry->Info.UpdatedAt = xrtNow();
        IsClaim = Claimed && Claim != NULL && Claim->sScheduleId != NULL &&
            strcmp(Entry->Info.Id, Claim->sScheduleId) == 0;
        if ( !MdoSchedulesAudit(IsClaim ? "claim" : "advance", &Entry->Info,
                Previous, Entry->Info.Revision,
                IsClaim ? Claim->uTaskId : 0u,
                IsClaim ? Claim->iOccurrenceAtUs : 0, Error) ||
             !MdoSchedulesWriteStore(&Entry->Info, Error) ) {
            g_MdoSchedules.PersistenceFault = true;
            Ok = false;
            break;
        }
        Changed = true;
    }
    if ( Changed && g_MdoSchedules.Generation != UINT64_MAX )
        ++g_MdoSchedules.Generation;
    return Ok;
}

bool MdoScheduleClaimDue(int64 Now, MdoScheduleClaim* Claim,
    xwork_error* Error)
{
    xwork_schedule_claim RuntimeClaim;
    MdoScheduleEntry* Entry;
    bool Claimed = false;
    bool Due = false;
    int64 NextWake = 0;
    uint32 ClaimSize;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoSchedules.Initialized || Now <= 0 || Claim == NULL ||
         Claim->Size < sizeof(*Claim) ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule claim request");
        return false;
    }
    ClaimSize = Claim->Size;
    memset(Claim, 0, sizeof(*Claim));
    Claim->Size = ClaimSize;
    xrtMutexLock(g_MdoSchedules.Lock);
    if ( !g_MdoSchedules.Enabled ) { Ok = true; goto done; }
    if ( g_MdoSchedules.PersistenceFault ) {
        MdoSchedulesError(Error, XWORK_ERROR_IO,
            "schedule persistence is faulted; restart after repairing storage");
        goto done;
    }
    if ( !MdoSchedulesClaimPreflight(Now, &Due, &NextWake, Error) ) goto done;
    Claim->NextWakeAt = NextWake;
    if ( !Due ) { Ok = true; goto done; }
    if ( !MdoSchedulesWriterLock(Error) ) goto done;
    xworkScheduleClaimInit(&RuntimeClaim);
    if ( !xworkRuntimeClaimDueSchedule(g_MdoSchedules.Runtime, Now,
            &RuntimeClaim, &Claimed, &NextWake, Error) ) {
        xwork_error RuntimeError = Error != NULL ? *Error : (xwork_error){0};
        xwork_error SyncError;
        if ( !MdoSchedulesSyncRuntime(NULL, false, &SyncError) ) {
            if ( Error != NULL ) *Error = SyncError;
        } else if ( Error != NULL ) {
            *Error = RuntimeError;
        }
        goto done;
    }
    Claim->NextWakeAt = NextWake;
    if ( !MdoSchedulesSyncRuntime(&RuntimeClaim, Claimed, Error) ) {
        if ( Claimed ) goto fail_task;
        goto done;
    }
    if ( !Claimed ) { Ok = true; goto done; }
    Entry = MdoSchedulesFind(RuntimeClaim.sScheduleId);
    if ( Entry == NULL || !Entry->Registered ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "claimed schedule is missing from the product catalog");
        goto fail_task;
    }
    MdoSchedulesCopyClaim(Entry, &RuntimeClaim, NextWake, Claim);
    Ok = true;
    goto done;
fail_task:
    (void)xworkRuntimeFinishScheduledTask(g_MdoSchedules.Runtime,
        RuntimeClaim.uTaskId, XWORK_RESULT_ERROR,
        "schedule claim was not durably recorded", NULL);
done:
    xrtMutexUnlock(g_MdoSchedules.Lock);
    return Ok;
}

static bool MdoSchedulesHistory(uint64 TaskId, uint64 AgentRunId,
    const xwork_task_info* Task, xwork_result Result, const char* ResultText,
    xwork_error* Error)
{
    xvalue* Root = xrtValueObject();
    char Path[MDO_SCHEDULE_PATH_CAPACITY];
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = false;
    if ( Root == NULL || !MdoSchedulesHistoryPath(Path, Task->sScheduleId) ||
         !MdoSchedulesObjectTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoSchedulesObjectTake(Root, "task_id", xrtValueUInt(TaskId)) ||
         !MdoSchedulesObjectTake(Root, "agent_run_id",
            xrtValueUInt(AgentRunId)) ||
         !MdoSchedulesObjectString(Root, "schedule_id", Task->sScheduleId) ||
         !MdoSchedulesObjectTake(Root, "scheduled_at_us",
            xrtValueInt(Task->iScheduledAtUs)) ||
         !MdoSchedulesObjectTake(Root, "finished_at_us", xrtValueInt(xrtNow())) ||
         !MdoSchedulesObjectTake(Root, "result", xrtValueInt(Result)) ||
         !MdoSchedulesObjectString(Root, "text",
            ResultText != NULL ? ResultText : "") ) goto memory;
    Json = xrtJsonStringify(Root, false, &Size);
    if ( Json == NULL || Size > MDO_SCHEDULE_RESULT_LIMIT + 4096u ) goto memory;
    Ok = MdoSchedulesAppendBounded(Path, Json, Size, Error);
    goto done;
memory:
    MdoSchedulesError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate schedule history record");
done:
    xrtFree(Json);
    xrtValueRelease(Root);
    return Ok;
}

bool MdoScheduleFinishTaskWithRun(uint64 TaskId, uint64 AgentRunId,
    xwork_result Result, const char* ResultText, xwork_error* Error)
{
    xwork_task_snapshot* Snapshot = NULL;
    xwork_task_info Task;
    bool Ok = false;
    xworkErrorInit(Error);
    if ( !g_MdoSchedules.Initialized || TaskId == 0u ||
         (Result != XWORK_RESULT_OK && Result != XWORK_RESULT_ERROR &&
          Result != XWORK_RESULT_CANCELLED && Result != XWORK_RESULT_LIMIT &&
          Result != XWORK_RESULT_TIMEOUT) ||
         !MdoSchedulesText(ResultText != NULL ? ResultText : "",
            MDO_SCHEDULE_RESULT_LIMIT + 1u, true) ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "invalid schedule finish request");
        return false;
    }
    xrtMutexLock(g_MdoSchedules.Lock);
    if ( !MdoSchedulesWriterLock(Error) ) goto done;
    Snapshot = xworkRuntimeTaskSnapshot(g_MdoSchedules.Runtime, 0u, Error);
    xworkTaskInfoInit(&Task);
    if ( Snapshot == NULL || !xworkTaskSnapshotFind(Snapshot, TaskId, &Task) ||
         Task.eKind != XWORK_TASK_SCHEDULED || Task.sScheduleId == NULL ) {
        MdoSchedulesError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "scheduled task was not found");
        goto done;
    }
    if ( !xworkRuntimeFinishScheduledTask(g_MdoSchedules.Runtime, TaskId,
            Result, ResultText, Error) ) goto done;
    if ( !MdoSchedulesHistory(TaskId, AgentRunId, &Task, Result, ResultText,
            Error) ) {
        g_MdoSchedules.PersistenceFault = true;
        goto done;
    }
    if ( g_MdoSchedules.Generation != UINT64_MAX )
        ++g_MdoSchedules.Generation;
    Ok = true;
done:
    xworkTaskSnapshotRelease(Snapshot);
    xrtMutexUnlock(g_MdoSchedules.Lock);
    return Ok;
}

bool MdoScheduleFinishTask(uint64 TaskId, xwork_result Result,
    const char* ResultText, xwork_error* Error)
{
    return MdoScheduleFinishTaskWithRun(TaskId, 0u, Result, ResultText,
        Error);
}

static int MdoSchedulesCatalogCompare(const void* LeftValue,
    const void* RightValue)
{
    const MdoScheduleInfo* Left = (const MdoScheduleInfo*)LeftValue;
    const MdoScheduleInfo* Right = (const MdoScheduleInfo*)RightValue;
    if ( Left->Enabled != Right->Enabled ) return Left->Enabled ? -1 : 1;
    if ( Left->NextOccurrenceAt != Right->NextOccurrenceAt ) {
        if ( Left->NextOccurrenceAt == 0 ) return 1;
        if ( Right->NextOccurrenceAt == 0 ) return -1;
        return Left->NextOccurrenceAt < Right->NextOccurrenceAt ? -1 : 1;
    }
    return strcmp(Left->Id, Right->Id);
}

MdoScheduleCatalog* MdoScheduleCatalogSnapshot(xwork_error* Error)
{
    MdoScheduleCatalog* Catalog;
    size_t i;
    xworkErrorInit(Error);
    if ( !g_MdoSchedules.Initialized ) {
        MdoSchedulesError(Error, XWORK_ERROR_CONTEXT,
            "schedule manager is not initialized");
        return NULL;
    }
    Catalog = (MdoScheduleCatalog*)xrtCalloc(1u, sizeof(*Catalog));
    if ( Catalog == NULL ) goto memory;
    xrtAtomic32Init(&Catalog->Refs, 1u);
    xrtMutexLock(g_MdoSchedules.Lock);
    Catalog->Generation = g_MdoSchedules.Generation;
    Catalog->Count = g_MdoSchedules.Count;
    Catalog->DiagnosticCount = g_MdoSchedules.DiagnosticCount;
    if ( Catalog->Count != 0u )
        Catalog->Items = (MdoScheduleInfo*)xrtCalloc(Catalog->Count,
            sizeof(*Catalog->Items));
    if ( Catalog->DiagnosticCount != 0u )
        Catalog->Diagnostics = (MdoScheduleDiagnostic*)xrtCalloc(
            Catalog->DiagnosticCount, sizeof(*Catalog->Diagnostics));
    if ( (Catalog->Count != 0u && Catalog->Items == NULL) ||
         (Catalog->DiagnosticCount != 0u && Catalog->Diagnostics == NULL) ) {
        xrtMutexUnlock(g_MdoSchedules.Lock);
        goto memory;
    }
    for ( i = 0u; i < Catalog->Count; ++i ) {
        xwork_schedule_info RuntimeInfo;
        Catalog->Items[i] = g_MdoSchedules.Entries[i].Info;
        Catalog->Items[i].Runnable = Catalog->Items[i].Enabled &&
            g_MdoSchedules.Enabled && g_MdoSchedules.Entries[i].Registered &&
            !g_MdoSchedules.PersistenceFault;
        if ( g_MdoSchedules.Entries[i].Registered ) {
            xworkScheduleInfoInit(&RuntimeInfo);
            if ( xworkRuntimeScheduleGetInfo(g_MdoSchedules.Runtime,
                    Catalog->Items[i].Id, &RuntimeInfo) )
                MdoSchedulesRuntimeInfo(&Catalog->Items[i], &RuntimeInfo);
        }
    }
    if ( Catalog->DiagnosticCount != 0u )
        memcpy(Catalog->Diagnostics, g_MdoSchedules.Diagnostics,
            Catalog->DiagnosticCount * sizeof(*Catalog->Diagnostics));
    xrtMutexUnlock(g_MdoSchedules.Lock);
    if ( Catalog->Count > 1u ) qsort(Catalog->Items, Catalog->Count,
        sizeof(*Catalog->Items), MdoSchedulesCatalogCompare);
    return Catalog;
memory:
    MdoScheduleCatalogRelease(Catalog);
    MdoSchedulesError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate schedule catalog snapshot");
    return NULL;
}

MdoScheduleCatalog* MdoScheduleCatalogRef(MdoScheduleCatalog* Catalog)
{
    uint32 Refs;
    if ( Catalog == NULL ) return NULL;
    Refs = xrtAtomic32Load(&Catalog->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&Catalog->Refs, &Expected, Refs + 1u,
                XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return Catalog;
        Refs = Expected;
    }
}

void MdoScheduleCatalogRelease(MdoScheduleCatalog* Catalog)
{
    uint32 Previous;
    if ( Catalog == NULL ) return;
    Previous = xrtAtomic32FetchSub(&Catalog->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    xrtFree(Catalog->Items);
    xrtFree(Catalog->Diagnostics);
    memset(Catalog, 0, sizeof(*Catalog));
    xrtFree(Catalog);
}

uint64 MdoScheduleCatalogGeneration(const MdoScheduleCatalog* Catalog)
{
    return Catalog != NULL ? Catalog->Generation : 0u;
}

size_t MdoScheduleCatalogCount(const MdoScheduleCatalog* Catalog)
{
    return Catalog != NULL ? Catalog->Count : 0u;
}

bool MdoScheduleCatalogAt(const MdoScheduleCatalog* Catalog, size_t Index,
    MdoScheduleInfo* Info)
{
    return Catalog != NULL && Index < Catalog->Count &&
        MdoSchedulesReturnInfo(&Catalog->Items[Index], Info);
}

bool MdoScheduleCatalogFind(const MdoScheduleCatalog* Catalog,
    const char* ScheduleId, MdoScheduleInfo* Info)
{
    size_t i;
    if ( Catalog == NULL || ScheduleId == NULL ) return false;
    for ( i = 0u; i < Catalog->Count; ++i )
        if ( strcmp(Catalog->Items[i].Id, ScheduleId) == 0 )
            return MdoSchedulesReturnInfo(&Catalog->Items[i], Info);
    return false;
}

size_t MdoScheduleCatalogDiagnosticCount(const MdoScheduleCatalog* Catalog)
{
    return Catalog != NULL ? Catalog->DiagnosticCount : 0u;
}

bool MdoScheduleCatalogDiagnosticAt(const MdoScheduleCatalog* Catalog,
    size_t Index, MdoScheduleDiagnostic* Diagnostic)
{
    uint32 Size;
    if ( Catalog == NULL || Index >= Catalog->DiagnosticCount ||
         Diagnostic == NULL || Diagnostic->Size < sizeof(*Diagnostic) )
        return false;
    Size = Diagnostic->Size;
    *Diagnostic = Catalog->Diagnostics[Index];
    Diagnostic->Size = Size;
    return true;
}
