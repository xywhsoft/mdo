#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/schedules.h"

#define MDO_API_SCHEDULE_LIST_LIMIT 100u

typedef enum MdoApiSchedulePreconditionStatus {
    MDO_API_SCHEDULE_PRECONDITION_OK = 0,
    MDO_API_SCHEDULE_PRECONDITION_MISSING,
    MDO_API_SCHEDULE_PRECONDITION_INVALID
} MdoApiSchedulePreconditionStatus;

static cstr MdoApiScheduleFrequencyText(xwork_schedule_frequency Frequency)
{
    switch ( Frequency ) {
    case XWORK_SCHEDULE_ONCE: return "once";
    case XWORK_SCHEDULE_MINUTELY: return "minutely";
    case XWORK_SCHEDULE_HOURLY: return "hourly";
    case XWORK_SCHEDULE_DAILY: return "daily";
    case XWORK_SCHEDULE_WEEKLY: return "weekly";
    default: return "unknown";
    }
}

static cstr MdoApiScheduleTimezoneText(xwork_schedule_timezone Timezone)
{
    switch ( Timezone ) {
    case XWORK_SCHEDULE_TIMEZONE_UTC: return "utc";
    case XWORK_SCHEDULE_TIMEZONE_FIXED_OFFSET: return "fixed_offset";
    case XWORK_SCHEDULE_TIMEZONE_SYSTEM_LOCAL: return "system_local";
    default: return "unknown";
    }
}

static cstr MdoApiScheduleMisfireText(xwork_schedule_misfire_policy Policy)
{
    switch ( Policy ) {
    case XWORK_SCHEDULE_MISFIRE_SKIP: return "skip";
    case XWORK_SCHEDULE_MISFIRE_RUN_ONCE: return "run_once";
    case XWORK_SCHEDULE_MISFIRE_CATCH_UP: return "catch_up";
    default: return "unknown";
    }
}

static cstr MdoApiScheduleOverlapText(xwork_schedule_overlap_policy Policy)
{
    switch ( Policy ) {
    case XWORK_SCHEDULE_OVERLAP_SKIP: return "skip";
    case XWORK_SCHEDULE_OVERLAP_QUEUE_ONE: return "queue_one";
    default: return "unknown";
    }
}

static bool MdoApiScheduleFrequency(cstr Text,
    xwork_schedule_frequency* Frequency)
{
    if ( strcmp(Text, "once") == 0 ) *Frequency = XWORK_SCHEDULE_ONCE;
    else if ( strcmp(Text, "minutely") == 0 )
        *Frequency = XWORK_SCHEDULE_MINUTELY;
    else if ( strcmp(Text, "hourly") == 0 )
        *Frequency = XWORK_SCHEDULE_HOURLY;
    else if ( strcmp(Text, "daily") == 0 )
        *Frequency = XWORK_SCHEDULE_DAILY;
    else if ( strcmp(Text, "weekly") == 0 )
        *Frequency = XWORK_SCHEDULE_WEEKLY;
    else return false;
    return true;
}

static bool MdoApiScheduleTimezone(cstr Text,
    xwork_schedule_timezone* Timezone)
{
    if ( strcmp(Text, "utc") == 0 ) *Timezone = XWORK_SCHEDULE_TIMEZONE_UTC;
    else if ( strcmp(Text, "fixed_offset") == 0 )
        *Timezone = XWORK_SCHEDULE_TIMEZONE_FIXED_OFFSET;
    else if ( strcmp(Text, "system_local") == 0 )
        *Timezone = XWORK_SCHEDULE_TIMEZONE_SYSTEM_LOCAL;
    else return false;
    return true;
}

static bool MdoApiScheduleProtocol(cstr Text, MdoModelProtocol* Protocol)
{
    if ( strcmp(Text, "default") == 0 )
        *Protocol = MDO_SCHEDULE_PROTOCOL_DEFAULT;
    else if ( strcmp(Text, "openai-chat-completions") == 0 )
        *Protocol = MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
    else if ( strcmp(Text, "openai-responses") == 0 )
        *Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    else if ( strcmp(Text, "anthropic-messages") == 0 )
        *Protocol = MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;
    else return false;
    return true;
}

static bool MdoApiScheduleFold(cstr Text, xwork_schedule_fold_policy* Fold)
{
    if ( strcmp(Text, "earlier") == 0 )
        *Fold = XWORK_SCHEDULE_FOLD_EARLIER;
    else if ( strcmp(Text, "later") == 0 )
        *Fold = XWORK_SCHEDULE_FOLD_LATER;
    else return false;
    return true;
}

static bool MdoApiScheduleMisfire(cstr Text,
    xwork_schedule_misfire_policy* Misfire)
{
    if ( strcmp(Text, "skip") == 0 )
        *Misfire = XWORK_SCHEDULE_MISFIRE_SKIP;
    else if ( strcmp(Text, "run_once") == 0 )
        *Misfire = XWORK_SCHEDULE_MISFIRE_RUN_ONCE;
    else if ( strcmp(Text, "catch_up") == 0 )
        *Misfire = XWORK_SCHEDULE_MISFIRE_CATCH_UP;
    else return false;
    return true;
}

static bool MdoApiScheduleOverlap(cstr Text,
    xwork_schedule_overlap_policy* Overlap)
{
    if ( strcmp(Text, "skip") == 0 )
        *Overlap = XWORK_SCHEDULE_OVERLAP_SKIP;
    else if ( strcmp(Text, "queue_one") == 0 )
        *Overlap = XWORK_SCHEDULE_OVERLAP_QUEUE_ONE;
    else return false;
    return true;
}

static bool MdoApiScheduleInfoValue(const MdoScheduleInfo* Info,
    bool IncludeInput, xvalue** Value)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetString(Item, "id", Info->Id) &&
        MdoApiValueSetString(Item, "label", Info->Label) &&
        MdoApiValueSetString(Item, "notify", Info->Notify) &&
        MdoApiValueSetString(Item, "project_id", Info->ProjectId) &&
        MdoApiValueSetString(Item, "agent_id", Info->AgentId) &&
        MdoApiValueSetString(Item, "model_id", Info->ModelId) &&
        MdoApiValueSetString(Item, "protocol",
            Info->Protocol == MDO_SCHEDULE_PROTOCOL_DEFAULT ? "default" :
            MdoModelProtocolName(Info->Protocol)) &&
        MdoApiValueSetString(Item, "reasoning_effort",
            Info->ReasoningEffort) &&
        MdoApiValueSetString(Item, "workspace_root", Info->WorkspaceRoot) &&
        MdoApiValueSetString(Item, "frequency",
            MdoApiScheduleFrequencyText(Info->Frequency)) &&
        MdoApiValueSetString(Item, "timezone",
            MdoApiScheduleTimezoneText(Info->Timezone)) &&
        MdoApiValueSetString(Item, "fold_policy",
            Info->FoldPolicy == XWORK_SCHEDULE_FOLD_LATER ? "later" :
            "earlier") &&
        MdoApiValueSetString(Item, "misfire_policy",
            MdoApiScheduleMisfireText(Info->MisfirePolicy)) &&
        MdoApiValueSetString(Item, "overlap_policy",
            MdoApiScheduleOverlapText(Info->OverlapPolicy)) &&
        MdoApiValueSetUInt(Item, "revision", Info->Revision) &&
        MdoApiValueSetInt(Item, "updated_at", Info->UpdatedAt) &&
        MdoApiValueSetUInt(Item, "runtime_generation",
            Info->RuntimeGeneration) &&
        MdoApiValueSetInt(Item, "next_occurrence_at",
            Info->NextOccurrenceAt) &&
        MdoApiValueSetInt(Item, "last_claimed_at", Info->LastClaimedAt) &&
        MdoApiValueSetUInt(Item, "claim_count", Info->ClaimCount) &&
        MdoApiValueSetUInt(Item, "misfire_count", Info->MisfireCount) &&
        MdoApiValueSetUInt(Item, "active_runs", Info->ActiveRuns) &&
        MdoApiValueSetUInt(Item, "interval", Info->Interval) &&
        MdoApiValueSetInt(Item, "start_at", Info->StartAt) &&
        MdoApiValueSetUInt(Item, "weekday_mask", Info->WeekdayMask) &&
        MdoApiValueSetInt(Item, "utc_offset_seconds",
            Info->UtcOffsetSeconds) &&
        MdoApiValueSetUInt(Item, "misfire_grace_seconds",
            Info->MisfireGraceSeconds) &&
        MdoApiValueSetUInt(Item, "max_catch_up", Info->MaxCatchUp) &&
        MdoApiValueSetUInt(Item, "max_concurrent_runs",
            Info->MaxConcurrentRuns) &&
        MdoApiValueSetUInt(Item, "max_output_tokens",
            Info->MaxOutputTokens) &&
        MdoApiValueSetUInt(Item, "input_bytes", strlen(Info->Input)) &&
        MdoApiValueSetBool(Item, "enabled", Info->Enabled) &&
        MdoApiValueSetBool(Item, "runnable", Info->Runnable);
    if ( Ok && IncludeInput )
        Ok = MdoApiValueSetString(Item, "input", Info->Input);
    if ( !Ok ) {
        xrtValueRelease(Item);
        return false;
    }
    *Value = Item;
    return true;
}

static bool MdoApiScheduleReply(MdoApiContext* Context, uint16 Status,
    const MdoScheduleInfo* Info)
{
    char EntityTag[128];
    xvalue* Data = NULL;
    int Count = snprintf(EntityTag, sizeof(EntityTag),
        "\"mdo-schedule-%s-%llu\"", Info->Id,
        (unsigned long long)Info->Revision);
    if ( Count <= 0 || (size_t)Count >= sizeof(EntityTag) ||
         !MdoApiScheduleInfoValue(Info, true, &Data) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "schedule_result_unavailable",
            "The schedule result could not be created", NULL);
    }
    return MdoApiReplySuccessTakeEntityTag(Context, Status, Data, EntityTag);
}

static bool MdoApiSchedulePath(const MdoApiContext* Context,
    char ScheduleId[MDO_SCHEDULE_ID_CAPACITY])
{
    size_t i;
    xstrview Param;
    if ( Context->ParamCount != 1u ) return false;
    Param = Context->Params[0];
    if ( Param.Size == 0u || Param.Size >= MDO_SCHEDULE_ID_CAPACITY )
        return false;
    for ( i = 0u; i < Param.Size; ++i ) {
        unsigned char Byte = (unsigned char)Param.Data[i];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && i != 0u) ) continue;
        return false;
    }
    memcpy(ScheduleId, Param.Data, Param.Size);
    ScheduleId[Param.Size] = '\0';
    return true;
}

static bool MdoApiScheduleNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return !(((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u);
}

static bool MdoApiScheduleString(const xvalue* Object, cstr Name,
    char* Output, size_t Capacity, bool Required, bool EmptyAllowed,
    size_t* Present, bool* Supplied)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    xstrview Text;
    *Supplied = Value != NULL;
    if ( Value == NULL ) return !Required;
    (*Present)++;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size >= Capacity ||
         (!EmptyAllowed && Text.Size == 0u) ||
         memchr(Text.Data, 0, Text.Size) != NULL ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoApiScheduleUInt32(const xvalue* Object, cstr Name,
    uint32* Output, bool Required, size_t* Present, bool* Supplied)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    uint64 Number;
    int64 Signed;
    *Supplied = Value != NULL;
    if ( Value == NULL ) return !Required;
    (*Present)++;
    if ( xrtValueType(Value) == XVALUE_UINT ) {
        if ( !xrtValueGetUInt(Value, &Number) ) return false;
    } else if ( xrtValueType(Value) == XVALUE_INT ) {
        if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
        Number = (uint64)Signed;
    } else return false;
    if ( Number > UINT32_MAX ) return false;
    *Output = (uint32)Number;
    return true;
}

static bool MdoApiScheduleInt64(const xvalue* Object, cstr Name,
    int64* Output, bool Required, size_t* Present, bool* Supplied)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    uint64 Unsigned;
    *Supplied = Value != NULL;
    if ( Value == NULL ) return !Required;
    (*Present)++;
    if ( xrtValueType(Value) == XVALUE_INT )
        return xrtValueGetInt(Value, Output);
    if ( xrtValueType(Value) != XVALUE_UINT ||
         !xrtValueGetUInt(Value, &Unsigned) || Unsigned > INT64_MAX )
        return false;
    *Output = (int64)Unsigned;
    return true;
}

static bool MdoApiScheduleBool(const xvalue* Object, cstr Name,
    bool* Output, bool Required, size_t* Present, bool* Supplied)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    *Supplied = Value != NULL;
    if ( Value == NULL ) return !Required;
    (*Present)++;
    return xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Output);
}

static MdoApiSchedulePreconditionStatus MdoApiScheduleExpectedRevision(
    const MdoApiContext* Context, const char* ScheduleId, uint64* Revision,
    bool* MatchesSchedule)
{
    static const char Prefix[] = "\"mdo-schedule-";
    const xhttpfield* Field = NULL;
    xhttpnext Next;
    xstrview Value;
    uint64 Number = 0u;
    size_t PrefixSize = sizeof(Prefix) - 1u;
    size_t Dash;
    size_t Index;
    size_t IdSize = strlen(ScheduleId);
    *MatchesSchedule = false;
    Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("If-Match"),
        &Field);
    if ( Next == XHTTP_NEXT_END )
        return MDO_API_SCHEDULE_PRECONDITION_MISSING;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL )
        return MDO_API_SCHEDULE_PRECONDITION_INVALID;
    Value = xrtStrTrim(Field->Value);
    if ( Value.Size < PrefixSize + 4u ||
         memcmp(Value.Data, Prefix, PrefixSize) != 0 ||
         Value.Data[Value.Size - 1u] != '"' )
        return MDO_API_SCHEDULE_PRECONDITION_INVALID;
    Dash = Value.Size - 2u;
    while ( Dash > PrefixSize && Value.Data[Dash] != '-' ) Dash--;
    if ( Dash == PrefixSize || Value.Data[Dash] != '-' ||
         Dash + 1u >= Value.Size - 1u )
        return MDO_API_SCHEDULE_PRECONDITION_INVALID;
    for ( Index = PrefixSize; Index < Dash; ++Index ) {
        unsigned char Byte = (unsigned char)Value.Data[Index];
        size_t IdIndex = Index - PrefixSize;
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             (Byte == '.' && IdIndex != 0u) ) continue;
        return MDO_API_SCHEDULE_PRECONDITION_INVALID;
    }
    for ( Index = Dash + 1u; Index + 1u < Value.Size; ++Index ) {
        uint64 Digit;
        if ( Value.Data[Index] < '0' || Value.Data[Index] > '9' )
            return MDO_API_SCHEDULE_PRECONDITION_INVALID;
        Digit = (uint64)(Value.Data[Index] - '0');
        if ( Number > (UINT64_MAX - Digit) / 10u )
            return MDO_API_SCHEDULE_PRECONDITION_INVALID;
        Number = Number * 10u + Digit;
    }
    if ( Number == 0u ) return MDO_API_SCHEDULE_PRECONDITION_INVALID;
    *MatchesSchedule = Dash - PrefixSize == IdSize &&
        memcmp(Value.Data + PrefixSize, ScheduleId, IdSize) == 0;
    *Revision = Number;
    return MDO_API_SCHEDULE_PRECONDITION_OK;
}

static bool MdoApiScheduleFind(const char* ScheduleId, MdoScheduleInfo* Info,
    uint64* Generation, bool* Available)
{
    xwork_error Error;
    MdoScheduleCatalog* Catalog;
    bool Found;
    memset(&Error, 0, sizeof(Error));
    Catalog = MdoScheduleCatalogSnapshot(&Error);
    *Available = Catalog != NULL;
    if ( Catalog == NULL ) return false;
    Found = MdoScheduleCatalogFind(Catalog, ScheduleId, Info);
    if ( Generation != NULL ) *Generation = MdoScheduleCatalogGeneration(Catalog);
    MdoScheduleCatalogRelease(Catalog);
    return Found;
}

static bool MdoApiScheduleLookupFailure(MdoApiContext* Context,
    bool Available)
{
    if ( !Available )
        return MdoApiReplyError(Context, 503u,
            "schedule_service_unavailable",
            "The schedule service is unavailable", NULL);
    return MdoApiReplyError(Context, 404u, "schedule_not_found",
        "The requested schedule does not exist", NULL);
}

static bool MdoApiScheduleMutationFailure(MdoApiContext* Context,
    const char* ScheduleId, uint64 ExpectedRevision, const xwork_error* Error,
    bool Removing)
{
    MdoScheduleInfo Fresh;
    bool Available = false;
    memset(&Fresh, 0, sizeof(Fresh)); Fresh.Size = sizeof(Fresh);
    if ( MdoApiScheduleFind(ScheduleId, &Fresh, NULL, &Available) ) {
        if ( Fresh.Revision != ExpectedRevision )
            return MdoApiReplyError(Context, 412u, "revision_conflict",
                Removing ?
                    "The schedule changed; reload it before deleting" :
                    "The schedule changed; reload it before updating",
                NULL);
    } else return MdoApiScheduleLookupFailure(Context, Available);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        return MdoApiReplyError(Context, 500u,
            "schedule_persistence_failed",
            Removing ? "The schedule could not be deleted" :
                "The schedule update could not be persisted",
            NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_OUT_OF_MEMORY )
        return MdoApiReplyError(Context, 503u,
            "schedule_service_unavailable",
            "The schedule service could not complete the request", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_LIMIT )
        return MdoApiReplyError(Context, 409u,
            "schedule_revision_exhausted",
            "The schedule revision cannot be advanced", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_CONTEXT )
        return MdoApiReplyError(Context, 409u,
            Removing ? "schedule_busy" : "schedule_state_conflict",
            Removing ?
                "The schedule cannot be deleted in its current state" :
                "The schedule cannot be updated in its current state",
            NULL);
    return MdoApiReplyError(Context, 500u, "schedule_update_failed",
        "The schedule request could not be completed", NULL);
}

static bool MdoApiScheduleCreateFailure(MdoApiContext* Context,
    const xwork_error* Error)
{
    if ( Error != NULL && Error->eCode == XWORK_ERROR_CONTEXT )
        return MdoApiReplyError(Context, 409u, "schedule_conflict",
            "The schedule ID already exists", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_LIMIT )
        return MdoApiReplyError(Context, 429u, "schedule_limit_reached",
            "The schedule limit was reached", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_IO )
        return MdoApiReplyError(Context, 500u, "schedule_persistence_failed",
            "The schedule could not be persisted", NULL);
    if ( Error != NULL && Error->eCode == XWORK_ERROR_INVALID_ARGUMENT )
        return MdoApiReplyError(Context, 422u, "schedule_invalid",
            "The schedule definition is invalid", NULL);
    return MdoApiReplyError(Context, 503u, "schedule_service_unavailable",
        "The schedule service is unavailable", NULL);
}

static bool MdoApiScheduleCreateRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoScheduleCreateOptions Options;
    MdoScheduleInfo Info;
    xwork_error Error;
    char Id[MDO_SCHEDULE_ID_CAPACITY] = { 0 };
    char Label[MDO_SCHEDULE_LABEL_CAPACITY] = { 0 };
    char Notify[MDO_SCHEDULE_NOTIFY_CAPACITY] = { 0 };
    char Project[MDO_SCHEDULE_PROJECT_CAPACITY] = { 0 };
    char Agent[MDO_SCHEDULE_IDENTITY_CAPACITY] = { 0 };
    char Model[MDO_SCHEDULE_IDENTITY_CAPACITY] = { 0 };
    char Protocol[32] = { 0 };
    char Reasoning[MDO_SCHEDULE_REASONING_CAPACITY] = { 0 };
    char Workspace[MDO_SCHEDULE_WORKSPACE_CAPACITY] = { 0 };
    char Input[MDO_SCHEDULE_INPUT_CAPACITY] = { 0 };
    char Frequency[16] = { 0 };
    char Timezone[32] = { 0 };
    char Fold[16] = { 0 };
    char Misfire[16] = { 0 };
    char Overlap[16] = { 0 };
    int64 Offset = 0;
    uint32 Weekday = 0u;
    size_t Present = 0u;
    bool Supplied;
    bool Valid;
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    MdoScheduleCreateOptionsInit(&Options);
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT;
    Valid = Valid && MdoApiScheduleString(Body.Value, "id", Id,
        sizeof(Id), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Options.Id = Id;
    Valid = Valid && MdoApiScheduleString(Body.Value, "label", Label,
        sizeof(Label), true, false, &Present, &Supplied);
    if ( Valid ) Options.Label = Label;
    Valid = Valid && MdoApiScheduleString(Body.Value, "notify", Notify,
        sizeof(Notify), false, true, &Present, &Supplied);
    if ( Valid && Supplied ) Options.Notify = Notify;
    Valid = Valid && MdoApiScheduleString(Body.Value, "project_id", Project,
        sizeof(Project), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Options.ProjectId = Project;
    Valid = Valid && MdoApiScheduleString(Body.Value, "agent_id", Agent,
        sizeof(Agent), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Options.AgentId = Agent;
    Valid = Valid && MdoApiScheduleString(Body.Value, "model_id", Model,
        sizeof(Model), false, true, &Present, &Supplied);
    if ( Valid && Supplied ) Options.ModelId = Model[0] != '\0' ? Model : NULL;
    Valid = Valid && MdoApiScheduleString(Body.Value, "protocol", Protocol,
        sizeof(Protocol), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Valid = MdoApiScheduleProtocol(Protocol,
        &Options.Protocol);
    Valid = Valid && MdoApiScheduleString(Body.Value, "reasoning_effort",
        Reasoning, sizeof(Reasoning), false, true, &Present, &Supplied);
    if ( Valid && Supplied ) Options.ReasoningEffort =
        Reasoning[0] != '\0' ? Reasoning : NULL;
    Valid = Valid && MdoApiScheduleUInt32(Body.Value, "max_output_tokens",
        &Options.MaxOutputTokens, false, &Present, &Supplied);
    Valid = Valid && MdoApiScheduleString(Body.Value, "workspace_root",
        Workspace, sizeof(Workspace), false, true, &Present, &Supplied);
    if ( Valid && Supplied ) Options.WorkspaceRoot =
        Workspace[0] != '\0' ? Workspace : NULL;
    Valid = Valid && MdoApiScheduleString(Body.Value, "input", Input,
        sizeof(Input), true, false, &Present, &Supplied);
    if ( Valid ) Options.Input = Input;
    Valid = Valid && MdoApiScheduleString(Body.Value, "frequency", Frequency,
        sizeof(Frequency), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Valid = MdoApiScheduleFrequency(Frequency,
        &Options.Frequency);
    Valid = Valid && MdoApiScheduleUInt32(Body.Value, "interval",
        &Options.Interval, false, &Present, &Supplied);
    Valid = Valid && MdoApiScheduleInt64(Body.Value, "start_at",
        &Options.StartAt, true, &Present, &Supplied);
    Valid = Valid && MdoApiScheduleUInt32(Body.Value, "weekday_mask",
        &Weekday, false, &Present, &Supplied) && Weekday <= UINT8_MAX;
    if ( Valid && Supplied ) Options.WeekdayMask = (uint8)Weekday;
    Valid = Valid && MdoApiScheduleString(Body.Value, "timezone", Timezone,
        sizeof(Timezone), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Valid = MdoApiScheduleTimezone(Timezone,
        &Options.Timezone);
    Valid = Valid && MdoApiScheduleInt64(Body.Value, "utc_offset_seconds",
        &Offset, false, &Present, &Supplied) && Offset >= INT32_MIN &&
        Offset <= INT32_MAX;
    if ( Valid && Supplied ) Options.UtcOffsetSeconds = (int32)Offset;
    Valid = Valid && MdoApiScheduleString(Body.Value, "fold_policy", Fold,
        sizeof(Fold), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Valid = MdoApiScheduleFold(Fold,
        &Options.FoldPolicy);
    Valid = Valid && MdoApiScheduleString(Body.Value, "misfire_policy",
        Misfire, sizeof(Misfire), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Valid = MdoApiScheduleMisfire(Misfire,
        &Options.MisfirePolicy);
    Valid = Valid && MdoApiScheduleUInt32(Body.Value,
        "misfire_grace_seconds", &Options.MisfireGraceSeconds, false,
        &Present, &Supplied);
    Valid = Valid && MdoApiScheduleUInt32(Body.Value, "max_catch_up",
        &Options.MaxCatchUp, false, &Present, &Supplied);
    Valid = Valid && MdoApiScheduleString(Body.Value, "overlap_policy",
        Overlap, sizeof(Overlap), false, false, &Present, &Supplied);
    if ( Valid && Supplied ) Valid = MdoApiScheduleOverlap(Overlap,
        &Options.OverlapPolicy);
    Valid = Valid && MdoApiScheduleUInt32(Body.Value,
        "max_concurrent_runs", &Options.MaxConcurrentRuns, false,
        &Present, &Supplied);
    Valid = Valid && MdoApiScheduleBool(Body.Value, "enabled",
        &Options.Enabled, false, &Present, &Supplied) &&
        Present == xrtValueCount(Body.Value);
    if ( !Valid ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "schedule_invalid",
            "The schedule definition is invalid", NULL);
    }
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    memset(&Error, 0, sizeof(Error));
    Valid = MdoScheduleCreate(&Options, &Info, &Error);
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoApiScheduleCreateFailure(Context, &Error);
    return MdoApiScheduleReply(Context, 201u, &Info);
}

bool MdoApiSchedulesRoute(MdoApiContext* Context)
{
    xwork_error Error;
    MdoScheduleCatalog* Catalog;
    MdoScheduleExecutorSnapshot Executor;
    xvalue* Data;
    xvalue* Items;
    size_t Count;
    size_t Index;
    bool Ok;
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_POST )
        return MdoApiScheduleCreateRoute(Context);
    Data = xrtValueObject();
    Items = xrtValueArray();
    memset(&Error, 0, sizeof(Error));
    memset(&Executor, 0, sizeof(Executor)); Executor.Size = sizeof(Executor);
    Catalog = MdoScheduleCatalogSnapshot(&Error);
    Count = Catalog != NULL ? MdoScheduleCatalogCount(Catalog) : 0u;
    Ok = Catalog != NULL && Data != NULL && Items != NULL &&
        MdoScheduleExecutorGetSnapshot(&Executor);
    if ( Count > MDO_API_SCHEDULE_LIST_LIMIT )
        Count = MDO_API_SCHEDULE_LIST_LIMIT;
    for ( Index = 0u; Ok && Index < Count; ++Index ) {
        MdoScheduleInfo Info;
        xvalue* Item = NULL;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoScheduleCatalogAt(Catalog, Index, &Info) &&
            MdoApiScheduleInfoValue(&Info, false, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation",
            MdoScheduleCatalogGeneration(Catalog)) &&
        MdoApiValueSetBool(Data, "enabled", MdoScheduleManagerEnabled()) &&
        MdoApiValueSetBool(Data, "automatic", Executor.Automatic) &&
        MdoApiValueSetBool(Data, "persistence_fault",
            Executor.PersistenceFault) &&
        MdoApiValueSetUInt(Data, "active_runs", Executor.ActiveRuns) &&
        MdoApiValueSetUInt(Data, "claims_started", Executor.ClaimsStarted) &&
        MdoApiValueSetUInt(Data, "runs_completed", Executor.RunsCompleted) &&
        MdoApiValueSetUInt(Data, "runs_failed", Executor.RunsFailed) &&
        MdoApiValueSetString(Data, "last_error", Executor.LastError) &&
        MdoApiValueSetUInt(Data, "total", MdoScheduleCatalogCount(Catalog)) &&
        MdoApiValueSetBool(Data, "truncated",
            MdoScheduleCatalogCount(Catalog) > MDO_API_SCHEDULE_LIST_LIMIT) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoScheduleCatalogRelease(Catalog);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "schedules_unavailable",
            "Schedules could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiScheduleRoute(MdoApiContext* Context)
{
    char ScheduleId[MDO_SCHEDULE_ID_CAPACITY];
    MdoApiSchedulePreconditionStatus Precondition;
    MdoScheduleInfo Info;
    xwork_error Error;
    xvalue* Data;
    uint64 ExpectedRevision = 0u;
    bool Available = false;
    bool MatchesSchedule = false;
    if ( !MdoApiSchedulePath(Context, ScheduleId) )
        return MdoApiReplyError(Context, 400u, "invalid_schedule_path",
            "The schedule ID is invalid", NULL);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( Context->Request->head->MethodCode != XHTTP_METHOD_DELETE ) {
        if ( !MdoApiScheduleFind(ScheduleId, &Info, NULL, &Available) )
            return MdoApiScheduleLookupFailure(Context, Available);
        return MdoApiScheduleReply(Context, 200u, &Info);
    }
    if ( !MdoApiScheduleNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "This operation does not accept a request body", NULL);
    Precondition = MdoApiScheduleExpectedRevision(Context, ScheduleId,
        &ExpectedRevision, &MatchesSchedule);
    if ( Precondition == MDO_API_SCHEDULE_PRECONDITION_MISSING )
        return MdoApiReplyError(Context, 428u, "precondition_required",
            "If-Match must contain the current schedule ETag", NULL);
    if ( Precondition != MDO_API_SCHEDULE_PRECONDITION_OK )
        return MdoApiReplyError(Context, 400u, "invalid_precondition",
            "If-Match must use the form \"mdo-schedule-ID-N\"", NULL);
    if ( !MdoApiScheduleFind(ScheduleId, &Info, NULL, &Available) )
        return MdoApiScheduleLookupFailure(Context, Available);
    if ( !MatchesSchedule || Info.Revision != ExpectedRevision )
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The schedule changed; reload it before deleting", NULL);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoScheduleRemove(ScheduleId, ExpectedRevision, &Error) )
        return MdoApiScheduleMutationFailure(Context, ScheduleId,
            ExpectedRevision, &Error, true);
    Data = xrtValueObject();
    if ( Data == NULL ||
         !MdoApiValueSetString(Data, "id", ScheduleId) ||
         !MdoApiValueSetUInt(Data, "revision", ExpectedRevision) ||
         !MdoApiValueSetBool(Data, "removed", true) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "schedule_result_unavailable",
            "The schedule was deleted but its result could not be created",
            NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiScheduleEnabledRoute(MdoApiContext* Context)
{
    char ScheduleId[MDO_SCHEDULE_ID_CAPACITY];
    MdoApiSchedulePreconditionStatus Precondition;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoScheduleInfo Info;
    xwork_error Error;
    uint64 ExpectedRevision = 0u;
    size_t Present = 0u;
    bool Available = false;
    bool MatchesSchedule = false;
    bool Supplied = false;
    bool Enabled = false;
    if ( !MdoApiSchedulePath(Context, ScheduleId) )
        return MdoApiReplyError(Context, 400u, "invalid_schedule_path",
            "The schedule ID is invalid", NULL);
    Precondition = MdoApiScheduleExpectedRevision(Context, ScheduleId,
        &ExpectedRevision, &MatchesSchedule);
    if ( Precondition == MDO_API_SCHEDULE_PRECONDITION_MISSING )
        return MdoApiReplyError(Context, 428u, "precondition_required",
            "If-Match must contain the current schedule ETag", NULL);
    if ( Precondition != MDO_API_SCHEDULE_PRECONDITION_OK )
        return MdoApiReplyError(Context, 400u, "invalid_precondition",
            "If-Match must use the form \"mdo-schedule-ID-N\"", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    if ( xrtValueType(Body.Value) != XVALUE_OBJECT ||
         !MdoApiScheduleBool(Body.Value, "enabled", &Enabled, true,
            &Present, &Supplied) || !Supplied || Present != 1u ||
         xrtValueCount(Body.Value) != 1u ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "schedule_update_invalid",
            "The schedule update document is invalid", NULL);
    }
    MdoApiJsonBodyUnit(&Body);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoApiScheduleFind(ScheduleId, &Info, NULL, &Available) )
        return MdoApiScheduleLookupFailure(Context, Available);
    if ( !MatchesSchedule || Info.Revision != ExpectedRevision )
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            "The schedule changed; reload it before updating", NULL);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoScheduleSetEnabled(ScheduleId, ExpectedRevision, Enabled,
            &Info, &Error) )
        return MdoApiScheduleMutationFailure(Context, ScheduleId,
            ExpectedRevision, &Error, false);
    return MdoApiScheduleReply(Context, 200u, &Info);
}
