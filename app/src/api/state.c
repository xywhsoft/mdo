#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/bootstrap.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/power.h"
#include "../../include/mdo/settings.h"

#define MDO_API_LIST_LIMIT 100u
#define MDO_API_ARTIFACT_DEFAULT_BYTES (64u * 1024u)
#define MDO_API_ARTIFACT_MAX_BYTES (64u * 1024u)
#define MDO_API_SESSION_ARTIFACT_MAX_FILE (8u * 1024u * 1024u)

static bool MdoApiStateReply(MdoApiContext* Context, xvalue* Data)
{
    if ( Data == NULL ) {
        return MdoApiReplyError(Context, 500u, "state_unavailable",
            "The requested state could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static cstr MdoApiSessionStatusText(MdoSessionStatus Status)
{
    switch ( Status ) {
    case MDO_SESSION_ACTIVE: return "active";
    case MDO_SESSION_ARCHIVED: return "archived";
    case MDO_SESSION_TRASH: return "trash";
    default: return "unknown";
    }
}

static bool MdoApiSettingsStringField(xvalue* Target, cstr TargetName,
    const xvalue* Source, cstr SourceName)
{
    const xvalue* Value = Source != NULL ? xrtValueObjectGet(Source,
        xrtStrView(SourceName)) : NULL;
    xstrview Text;
    return Value != NULL && xrtValueGetString(Value, &Text) &&
        MdoApiValueSetStringView(Target, TargetName, Text);
}

static bool MdoApiSettingsBoolField(xvalue* Target, cstr TargetName,
    const xvalue* Source, cstr SourceName)
{
    const xvalue* Value = Source != NULL ? xrtValueObjectGet(Source,
        xrtStrView(SourceName)) : NULL;
    bool Boolean;
    return Value != NULL && xrtValueGetBool(Value, &Boolean) &&
        MdoApiValueSetBool(Target, TargetName, Boolean);
}

bool MdoApiSettingsRoute(MdoApiContext* Context)
{
    MdoSettingsServiceSnapshot Service;
    MdoConfigTransportSettings Transport;
    MdoPowerManagerStatus PowerRuntime;
    str EffectiveJson;
    size_t EffectiveSize = 0u;
    xvalue* Effective;
    const xvalue* EffectiveSettings;
    const xvalue* EffectiveAppearance;
    const xvalue* EffectiveComposer;
    const xvalue* EffectiveNotifications;
    const xvalue* EffectivePower;
    const xvalue* EffectiveAgent;
    const xvalue* EffectiveWorkspace;
    xvalue* Data = xrtValueObject();
    xvalue* Patches = xrtValueObject();
    xvalue* AppearanceValue = xrtValueObject();
    xvalue* ComposerValue = xrtValueObject();
    xvalue* NotificationsValue = xrtValueObject();
    xvalue* PowerValue = xrtValueObject();
    xvalue* PowerRuntimeValue = xrtValueObject();
    xvalue* AgentValue = xrtValueObject();
    xvalue* TransportValue = xrtValueObject();
    xvalue* ProxyValue = xrtValueObject();
    xvalue* WorkspaceValue = xrtValueObject();
    xvalue* ServiceValue = xrtValueObject();
    bool Ok;

    memset(&Service, 0, sizeof(Service)); Service.Size = sizeof(Service);
    memset(&Transport, 0, sizeof(Transport));
    Transport.Size = sizeof(Transport);
    memset(&PowerRuntime, 0, sizeof(PowerRuntime));
    PowerRuntime.Size = sizeof(PowerRuntime);
    EffectiveJson = MdoConfigEffectiveJson(&EffectiveSize);
    Effective = EffectiveJson != NULL ?
        xrtJsonParse(xrtStrViewN(EffectiveJson, EffectiveSize)) : NULL;
    xrtFree(EffectiveJson);
    EffectiveSettings = Effective != NULL ? xrtValueObjectGet(Effective,
        XRT_STR_LITERAL("settings")) : NULL;
    EffectiveAppearance = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("appearance")) : NULL;
    EffectiveComposer = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("composer")) : NULL;
    EffectiveNotifications = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("notifications")) : NULL;
    EffectivePower = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings, XRT_STR_LITERAL("power")) : NULL;
    EffectiveAgent = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings, XRT_STR_LITERAL("agent")) : NULL;
    EffectiveWorkspace = EffectiveSettings != NULL ?
        xrtValueObjectGet(EffectiveSettings,
            XRT_STR_LITERAL("workspace")) : NULL;
    Ok = Data != NULL && Patches != NULL && AppearanceValue != NULL &&
        ComposerValue != NULL && NotificationsValue != NULL &&
        PowerValue != NULL && PowerRuntimeValue != NULL &&
        AgentValue != NULL && TransportValue != NULL && ProxyValue != NULL &&
        WorkspaceValue != NULL &&
        ServiceValue != NULL && EffectiveSettings != NULL &&
        MdoSettingsServiceGetSnapshot(&Service) &&
        MdoConfigGetTransportSettings(&Transport) &&
        MdoPowerManagerGetStatus(&PowerRuntime);
    if ( Ok ) Ok =
        MdoApiValueSetBool(Patches, "settings",
            Service.Config.UserPatch[MDO_CONFIG_SETTINGS]) &&
        MdoApiValueSetBool(Patches, "models",
            Service.Config.UserPatch[MDO_CONFIG_MODELS]) &&
        MdoApiValueSetBool(Patches, "permissions",
            Service.Config.UserPatch[MDO_CONFIG_PERMISSIONS]) &&
        MdoApiValueSetUInt(Data, "schema_version",
            Service.Config.SchemaVersion) &&
        MdoApiValueSetUInt(Data, "revision", Service.Config.Revision) &&
        MdoApiValueSetBool(Data, "runtime_override",
            Service.Config.RuntimeOverride) &&
        MdoApiValueSetUInt(Data, "effective_bytes",
            Service.Config.EffectiveBytes) &&
        MdoApiValueSetTake(Data, "user_patches", &Patches) &&
        MdoApiSettingsStringField(Data, "locale", EffectiveSettings,
            "locale") &&
        MdoApiSettingsStringField(AppearanceValue, "theme",
            EffectiveAppearance, "theme") &&
        MdoApiSettingsStringField(AppearanceValue, "font_size",
            EffectiveAppearance, "font_size") &&
        MdoApiSettingsStringField(AppearanceValue, "density",
            EffectiveAppearance, "density") &&
        MdoApiValueSetTake(Data, "appearance", &AppearanceValue);
    if ( Ok ) Ok =
        MdoApiSettingsStringField(ComposerValue, "submit_mode",
            EffectiveComposer, "submit_mode") &&
        MdoApiValueSetTake(Data, "composer", &ComposerValue);
    if ( Ok ) Ok =
        (EffectiveNotifications != NULL ?
            MdoApiSettingsBoolField(NotificationsValue, "sound",
                EffectiveNotifications, "sound") :
            MdoApiValueSetBool(NotificationsValue, "sound", false)) &&
        MdoApiValueSetTake(Data, "notifications", &NotificationsValue);
    if ( Ok ) Ok =
        MdoApiSettingsBoolField(PowerValue, "prevent_sleep",
            EffectivePower, "prevent_sleep") &&
        MdoApiValueSetTake(Data, "power", &PowerValue) &&
        MdoApiValueSetBool(PowerRuntimeValue, "checked",
            PowerRuntime.Checked) &&
        MdoApiValueSetBool(PowerRuntimeValue, "available",
            PowerRuntime.Available) &&
        MdoApiValueSetBool(PowerRuntimeValue, "running",
            PowerRuntime.Running) &&
        MdoApiValueSetBool(PowerRuntimeValue, "active",
            PowerRuntime.Active) &&
        MdoApiValueSetTake(Data, "power_runtime", &PowerRuntimeValue);
    if ( Ok ) Ok =
        MdoApiSettingsStringField(AgentValue, "user_instructions",
            EffectiveAgent, "user_instructions") &&
        MdoApiValueSetString(AgentValue, "reply_language",
            Service.Agent.ReplyLanguage) &&
        MdoApiSettingsBoolField(AgentValue, "web_search", EffectiveAgent,
            "web_search") &&
        MdoApiValueSetBool(AgentValue, "memory", Service.Agent.MemoryEnabled) &&
        MdoApiValueSetBool(AgentValue, "schedules",
            Service.Agent.SchedulesEnabled) &&
        MdoApiValueSetUInt(AgentValue, "max_parallel_tools",
            Service.Agent.MaxParallelTools) &&
        MdoApiValueSetUInt(AgentValue, "max_parallel_subagents",
            Service.Agent.MaxParallelSubagents) &&
        MdoApiValueSetString(AgentValue, "reasoning_effort",
            Service.Agent.ReasoningEffort) &&
        MdoApiValueSetString(AgentValue, "permission_profile",
            Service.Agent.PermissionProfile) &&
        MdoApiValueSetTake(Data, "agent", &AgentValue);
    if ( Ok ) Ok =
        MdoApiValueSetString(TransportValue, "ca_pem_path",
            Transport.CaPemPath) &&
        MdoApiValueSetString(ProxyValue, "kind", Transport.ProxyKind) &&
        MdoApiValueSetString(ProxyValue, "host", Transport.ProxyHost) &&
        MdoApiValueSetUInt(ProxyValue, "port", Transport.ProxyPort) &&
        MdoApiValueSetString(ProxyValue, "user", Transport.ProxyUser) &&
        MdoApiValueSetString(ProxyValue, "bypass", Transport.ProxyBypass) &&
        MdoApiValueSetBool(ProxyValue, "credential_configured",
            Transport.ProxySecretRef[0] != '\0') &&
        MdoApiValueSetTake(TransportValue, "proxy", &ProxyValue) &&
        MdoApiValueSetTake(Data, "transport", &TransportValue);
    if ( Ok ) Ok =
        MdoApiSettingsStringField(WorkspaceValue, "open_mode",
            EffectiveWorkspace, "open_mode") &&
        MdoApiSettingsBoolField(WorkspaceValue, "confirm_external_write",
            EffectiveWorkspace, "confirm_external_write") &&
        MdoApiValueSetTake(Data, "workspace", &WorkspaceValue);
    if ( Ok ) Ok =
        MdoApiValueSetBool(ServiceValue, "runtime_consistent",
            !Service.Degraded) &&
        MdoApiValueSetUInt(ServiceValue, "transactions",
            Service.Transactions) &&
        MdoApiValueSetUInt(ServiceValue, "rollbacks", Service.Rollbacks) &&
        MdoApiValueSetString(ServiceValue, "last_error", Service.LastError) &&
        MdoApiValueSetTake(Data, "transaction_service", &ServiceValue);
    xrtValueRelease(Patches); xrtValueRelease(AppearanceValue);
    xrtValueRelease(ComposerValue); xrtValueRelease(NotificationsValue);
    xrtValueRelease(PowerValue); xrtValueRelease(PowerRuntimeValue);
    xrtValueRelease(AgentValue);
    xrtValueRelease(TransportValue);
    xrtValueRelease(ProxyValue);
    xrtValueRelease(WorkspaceValue); xrtValueRelease(ServiceValue);
    xrtValueRelease(Effective);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiStateReply(Context, NULL);
    return MdoApiReplySuccessTakeRevision(Context, 200u, Data,
        Service.Config.Revision);
}

bool MdoApiSessionsRoute(MdoApiContext* Context)
{
    MdoSessionQuery Query;
    xwork_error Error;
    MdoSessionCatalog* Catalog;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Index;
    bool Ok;

    if ( Context->Request->head->MethodCode == XHTTP_METHOD_POST )
        return MdoApiSessionCreateRoute(Context);
    MdoSessionQueryInit(&Query);
    Query.Limit = MDO_API_LIST_LIMIT;
    memset(&Error, 0, sizeof(Error));
    Catalog = MdoSessionCatalogSearch(&Query, &Error);
    Ok = Catalog != NULL && Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < MdoSessionCatalogCount(Catalog); Index++ ) {
        MdoSessionInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoSessionCatalogAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "project_id", Info.ProjectId) &&
            MdoApiValueSetString(Item, "parent_session_id",
                Info.ParentSessionId) &&
            MdoApiValueSetString(Item, "title", Info.Title) &&
            MdoApiValueSetString(Item, "agent_id", Info.AgentId) &&
            MdoApiValueSetString(Item, "model_id", Info.ModelId) &&
            MdoApiValueSetString(Item, "protocol",
                MdoModelProtocolName(Info.Protocol)) &&
            MdoApiValueSetString(Item, "reasoning_effort",
                Info.ReasoningEffort) &&
            MdoApiValueSetString(Item, "workspace_root", Info.WorkspaceRoot) &&
            MdoApiValueSetString(Item, "status",
                MdoApiSessionStatusText(Info.Status)) &&
            MdoApiValueSetUInt(Item, "revision", Info.Revision) &&
            MdoApiValueSetInt(Item, "created_at", Info.CreatedAt) &&
            MdoApiValueSetInt(Item, "updated_at", Info.UpdatedAt) &&
            MdoApiValueSetBool(Item, "pinned", Info.Pinned) &&
            MdoApiValueSetBool(Item, "runtime_open", Info.RuntimeOpen) &&
            MdoApiValueSetUInt(Item, "max_output_tokens",
                Info.MaxOutputTokens) &&
            MdoApiValueSetUInt(Item, "forked_through_sequence",
                Info.ForkedThroughSequence) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation",
            MdoSessionCatalogGeneration(Catalog)) &&
        MdoApiValueSetUInt(Data, "limit", MDO_API_LIST_LIMIT) &&
        MdoApiValueSetBool(Data, "possibly_truncated",
            MdoSessionCatalogCount(Catalog) == MDO_API_LIST_LIMIT) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoSessionCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}

bool MdoApiArtifactsRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Total;
    size_t Start;
    size_t Index;
    bool Ok;

    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The artifact runtime is unavailable", NULL);
    Total = xworkRuntimeArtifactCount(Runtime);
    Start = Total > MDO_API_LIST_LIMIT ? Total - MDO_API_LIST_LIMIT : 0u;
    Ok = Data != NULL && Items != NULL;
    for ( Index = Start; Ok && Index < Total; Index++ ) {
        xwork_artifact_info Info;
        xvalue* Item = xrtValueObject();
        xworkArtifactInfoInit(&Info);
        Ok = Item != NULL && xworkRuntimeArtifactAt(Runtime, Index, &Info) &&
            MdoApiValueSetUInt(Item, "id", Info.uArtifactId) &&
            MdoApiValueSetString(Item, "kind", "tool_output") &&
            MdoApiValueSetInt(Item, "created_at", Info.iCreatedAtUs) &&
            MdoApiValueSetUInt(Item, "agent_id", Info.uAgentId) &&
            MdoApiValueSetUInt(Item, "run_id", Info.uRunId) &&
            MdoApiValueSetUInt(Item, "task_id", Info.uTaskId) &&
            MdoApiValueSetString(Item, "source", Info.sSource) &&
            MdoApiValueSetString(Item, "path", Info.sPath) &&
            MdoApiValueSetString(Item, "media_type", Info.sMediaType) &&
            MdoApiValueSetUInt(Item, "size_bytes", Info.uSizeBytes) &&
            MdoApiValueSetString(Item, "sha256", Info.sSha256) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetBool(Data, "truncated", Start != 0u) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}

static bool MdoApiArtifactUnsigned(xstrview Text, uint64 Maximum,
    uint64* Value)
{
    uint64 Number = 0u;
    size_t Index;
    if ( Text.Size == 0u || Value == NULL ) return false;
    for ( Index = 0u; Index < Text.Size; Index++ ) {
        uint64 Digit;
        if ( Text.Data[Index] < '0' || Text.Data[Index] > '9' ) return false;
        Digit = (uint64)(Text.Data[Index] - '0');
        if ( Number > (Maximum - Digit) / 10u ) return false;
        Number = Number * 10u + Digit;
    }
    *Value = Number;
    return true;
}

static bool MdoApiArtifactQueryPart(xstrview Query, size_t* Position,
    xstrview* Name, xstrview* Value)
{
    size_t End = *Position;
    size_t Equal = SIZE_MAX;
    while ( End < Query.Size && Query.Data[End] != '&' ) {
        if ( Query.Data[End] == '=' && Equal == SIZE_MAX ) Equal = End;
        End++;
    }
    if ( End == *Position || Equal == SIZE_MAX || Equal == *Position ||
         Equal + 1u == End ) return false;
    *Name = xrtStrViewN(Query.Data + *Position, Equal - *Position);
    *Value = xrtStrViewN(Query.Data + Equal + 1u, End - Equal - 1u);
    *Position = End + (End < Query.Size ? 1u : 0u);
    if ( *Position == Query.Size && End < Query.Size ) return false;
    return true;
}

static bool MdoApiArtifactQuery(xstrview Query, uint64* Offset,
    size_t* Limit)
{
    size_t Position = 0u;
    unsigned Seen = 0u;
    *Offset = 0u;
    *Limit = MDO_API_ARTIFACT_DEFAULT_BYTES;
    while ( Position < Query.Size ) {
        xstrview Name;
        xstrview Value;
        uint64 Number;
        unsigned Bit;
        uint64 Maximum;
        if ( !MdoApiArtifactQueryPart(Query, &Position, &Name, &Value) )
            return false;
        if ( Name.Size == 6u && memcmp(Name.Data, "offset", 6u) == 0 ) {
            Bit = 1u; Maximum = UINT64_MAX;
        } else if ( Name.Size == 5u &&
                    memcmp(Name.Data, "limit", 5u) == 0 ) {
            Bit = 2u; Maximum = MDO_API_ARTIFACT_MAX_BYTES;
        } else return false;
        if ( (Seen & Bit) != 0u ||
             !MdoApiArtifactUnsigned(Value, Maximum, &Number) ||
             (Bit == 2u && Number == 0u) ) return false;
        Seen |= Bit;
        if ( Bit == 1u ) *Offset = Number;
        else *Limit = (size_t)Number;
    }
    return true;
}

bool MdoApiArtifactRoute(MdoApiContext* Context)
{
    xwork_runtime* Runtime = MdoBootstrapRuntime();
    xwork_artifact_info Info;
    xwork_artifact_chunk Chunk;
    xwork_error Error;
    xvalue* Data = NULL;
    str Encoded = NULL;
    uint64 ArtifactId;
    uint64 Offset;
    size_t Limit;
    bool Ok;
    if ( Context->ParamCount != 1u ||
         !MdoApiArtifactUnsigned(Context->Params[0], UINT64_MAX,
            &ArtifactId) || ArtifactId == 0u )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "The artifact identifier must be a nonzero decimal integer", NULL);
    if ( !MdoApiArtifactQuery(Context->Target.Query, &Offset, &Limit) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only unique numeric offset and bounded limit parameters are accepted",
            NULL);
    if ( Runtime == NULL ) return MdoApiReplyError(Context, 503u,
        "runtime_unavailable", "The artifact runtime is unavailable", NULL);
    xworkArtifactInfoInit(&Info);
    if ( !xworkRuntimeArtifactGetInfo(Runtime, ArtifactId, &Info) )
        return MdoApiReplyError(Context, 404u, "artifact_not_found",
            "The requested artifact does not exist", NULL);
    if ( Offset > Info.uSizeBytes )
        return MdoApiReplyError(Context, 416u,
            "artifact_offset_out_of_range",
            "The artifact offset exceeds its size", NULL);
    xworkArtifactChunkInit(&Chunk);
    memset(&Error, 0, sizeof(Error));
    if ( !xworkRuntimeReadArtifact(Runtime, ArtifactId, Offset, Limit,
            &Chunk, &Error) ) {
        xworkArtifactChunkUnit(&Chunk);
        return MdoApiReplyError(Context, 409u, "artifact_read_failed",
            "The artifact is unavailable or changed", NULL);
    }
    Encoded = xrtBase64EncodeNew(Chunk.pData, Chunk.iSize, NULL);
    Data = xrtValueObject();
    Ok = Encoded != NULL && Data != NULL &&
        MdoApiValueSetUInt(Data, "artifact_id", ArtifactId) &&
        MdoApiValueSetString(Data, "encoding", "base64") &&
        MdoApiValueSetUInt(Data, "offset", Chunk.uOffset) &&
        MdoApiValueSetUInt(Data, "next",
            Chunk.uOffset + (uint64)Chunk.iSize) &&
        MdoApiValueSetUInt(Data, "total_size", Chunk.uTotalSize) &&
        MdoApiValueSetUInt(Data, "bytes", Chunk.iSize) &&
        MdoApiValueSetBool(Data, "eof", Chunk.bEof) &&
        MdoApiValueSetString(Data, "sha256", Info.sSha256) &&
        MdoApiValueSetString(Data, "media_type", Info.sMediaType) &&
        MdoApiValueSetString(Data, "data", Encoded);
    xrtFree(Encoded);
    xworkArtifactChunkUnit(&Chunk);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "artifact_read_failed", "The artifact chunk could not be encoded",
        NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

/* The journal is the durable authority for a conversation artifact. xwork's
 * process-local artifact registry cannot identify it after an exe restart. */
static bool MdoApiSessionArtifactPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* Project, const char* Session,
    const MdoSessionEventInfo* Event)
{
    const char* Saved = Event->ArtifactPath;
    size_t Length;
    size_t FileStart;
    size_t RunStart;
    size_t FileLength;
    size_t Index;
    char Run[25];
    char Prefix[22];
    int Written;
    if ( Saved == NULL || Event->RunId == 0u || Event->ArtifactId == 0u )
        return false;
    Length = strlen(Saved);
    FileStart = Length;
    while ( FileStart != 0u && Saved[FileStart - 1u] != '/' &&
            Saved[FileStart - 1u] != '\\' ) --FileStart;
    if ( FileStart == 0u || FileStart == Length ) return false;
    RunStart = FileStart - 1u;
    while ( RunStart != 0u && Saved[RunStart - 1u] != '/' &&
            Saved[RunStart - 1u] != '\\' ) --RunStart;
    if ( RunStart == FileStart - 1u ) return false;
    snprintf(Run, sizeof(Run), "run-%020llu",
        (unsigned long long)Event->RunId);
    if ( FileStart - 1u - RunStart != 24u ||
         memcmp(Saved + RunStart, Run, 24u) != 0 ) return false;
    snprintf(Prefix, sizeof(Prefix), "%020llu-",
        (unsigned long long)Event->ArtifactId);
    FileLength = Length - FileStart;
    if ( FileLength < 26u || FileLength > 153u ||
         memcmp(Saved + FileStart, Prefix, 21u) != 0 ||
         memcmp(Saved + Length - 4u, ".txt", 4u) != 0 ) return false;
    for ( Index = FileStart + 21u; Index < Length - 4u; ++Index ) {
        unsigned char Ch = (unsigned char)Saved[Index];
        if ( (Ch >= 'a' && Ch <= 'z') ||
             (Ch >= 'A' && Ch <= 'Z') ||
             (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' ) continue;
        return false;
    }
    Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/artifacts/%s/%s", Project, Session, Run,
        Saved + FileStart);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoApiSessionArtifactRead(const char* Path,
    uint8** Bytes, size_t* Size)
{
    xfile File = MdoHomeOpenRead(Path);
    xfileinfo Info;
    bool Ok = false;
    *Bytes = NULL;
    *Size = 0u;
    if ( File == NULL ) return false;
    if ( !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Type != XFILE_TYPE_FILE ||
         Info.Size > MDO_API_SESSION_ARTIFACT_MAX_FILE ) goto done;
    *Size = (size_t)Info.Size;
    *Bytes = (uint8*)xrtMalloc(*Size ? *Size : 1u);
    if ( *Bytes == NULL ||
         (*Size && !xrtReadFull(File, *Bytes, *Size, NULL)) ) goto done;
    Ok = true;
done:
    if ( !xrtClose(File) ) Ok = false;
    if ( !Ok ) { xrtFree(*Bytes); *Bytes = NULL; *Size = 0u; }
    return Ok;
}

static bool MdoApiStateCaptureId(MdoApiContext* Context, size_t Index,
    char* Output, size_t Capacity)
{
    xstrview Value;
    size_t Position;
    if ( Context == NULL || Index >= Context->ParamCount || Capacity == 0u )
        return false;
    Value = Context->Params[Index];
    if ( Value.Size == 0u || Value.Size >= Capacity ) return false;
    for ( Position = 0u; Position < Value.Size; ++Position ) {
        unsigned char Ch = (unsigned char)Value.Data[Position];
        if ( (Ch >= 'a' && Ch <= 'z') ||
             (Ch >= 'A' && Ch <= 'Z') ||
             (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' ||
             Ch == '.' ) continue;
        return false;
    }
    memcpy(Output, Value.Data, Value.Size);
    Output[Value.Size] = '\0';
    return true;
}

bool MdoApiSessionArtifactRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY];
    char Session[MDO_SESSION_ID_CAPACITY];
    char Path[MDO_SESSION_PATH_CAPACITY];
    MdoSession* Handle;
    MdoSessionEventSnapshot* Snapshot;
    MdoSessionEventInfo Event;
    xwork_error Error;
    uint64 EventId;
    uint64 Offset;
    size_t Limit;
    uint8* Bytes = NULL;
    size_t Size = 0u;
    size_t Count;
    uint8 Digest[32];
    char Hex[65];
    static const char Digits[] = "0123456789abcdef";
    size_t Index;
    str Encoded;
    xvalue* Data;
    bool Ok;
    if ( Context->ParamCount != 3u ||
         !MdoApiStateCaptureId(Context, 0u, Project, sizeof(Project)) ||
         !MdoApiStateCaptureId(Context, 1u, Session, sizeof(Session)) ||
         !MdoApiArtifactUnsigned(Context->Params[2], UINT64_MAX,
            &EventId) || EventId == 0u )
        return MdoApiReplyError(Context, 400u, "invalid_path",
            "Project, session or event identifier is invalid", NULL);
    if ( !MdoApiArtifactQuery(Context->Target.Query, &Offset, &Limit) )
        return MdoApiReplyError(Context, 400u, "invalid_query",
            "Only unique numeric offset and bounded limit parameters are accepted",
            NULL);
    memset(&Error, 0, sizeof(Error));
    Handle = MdoSessionLoad(Project, Session, &Error);
    if ( Handle == NULL ) return MdoApiReplyError(Context, 404u,
        "session_not_found", "The requested session does not exist", NULL);
    MdoSessionRelease(Handle);
    Snapshot = MdoSessionEventReplay(Project, Session, EventId - 1u,
        1u, &Error);
    if ( Snapshot == NULL ) return MdoApiReplyError(Context, 409u,
        "session_events_unavailable", "The session journal cannot be read", NULL);
    memset(&Event, 0, sizeof(Event)); Event.Size = sizeof(Event);
    Ok = MdoSessionEventSnapshotCount(Snapshot) == 1u &&
         MdoSessionEventSnapshotAt(Snapshot, 0u, &Event) &&
         Event.EventId == EventId &&
         (Event.Kind == XWORK_EVENT_TOOL_DONE ||
          Event.Kind == XWORK_EVENT_ARTIFACT_CREATED) &&
         MdoApiSessionArtifactPath(Path, Project, Session, &Event);
    MdoSessionEventSnapshotRelease(Snapshot);
    if ( !Ok ) return MdoApiReplyError(Context, 404u,
        "artifact_not_found", "The requested session artifact does not exist", NULL);
    if ( !MdoApiSessionArtifactRead(Path, &Bytes, &Size) )
        return MdoApiReplyError(Context, 409u, "artifact_read_failed",
            "The session artifact is unavailable or changed", NULL);
    if ( Offset > Size ) {
        xrtFree(Bytes);
        return MdoApiReplyError(Context, 416u,
            "artifact_offset_out_of_range",
            "The artifact offset exceeds its size", NULL);
    }
    Count = Size - (size_t)Offset;
    if ( Count > Limit ) Count = Limit;
    if ( !xrtSha256(Bytes, Size, Digest) ) {
        xrtFree(Bytes);
        return MdoApiReplyError(Context, 500u, "artifact_hash_failed",
            "The artifact could not be hashed", NULL);
    }
    for ( Index = 0u; Index < sizeof(Digest); ++Index ) {
        Hex[Index * 2u] = Digits[Digest[Index] >> 4];
        Hex[Index * 2u + 1u] = Digits[Digest[Index] & 15u];
    }
    Hex[64] = '\0';
    Encoded = xrtBase64EncodeNew(Bytes + (size_t)Offset, Count, NULL);
    xrtFree(Bytes);
    Data = xrtValueObject();
    Ok = Encoded != NULL && Data != NULL &&
        MdoApiValueSetUInt(Data, "event_id", EventId) &&
        MdoApiValueSetUInt(Data, "artifact_id", Event.ArtifactId) &&
        MdoApiValueSetString(Data, "encoding", "base64") &&
        MdoApiValueSetUInt(Data, "offset", Offset) &&
        MdoApiValueSetUInt(Data, "next", Offset + Count) &&
        MdoApiValueSetUInt(Data, "total_size", Size) &&
        MdoApiValueSetUInt(Data, "bytes", Count) &&
        MdoApiValueSetBool(Data, "eof", Offset + Count == Size) &&
        MdoApiValueSetString(Data, "sha256", Hex) &&
        MdoApiValueSetString(Data, "media_type",
            "text/plain; charset=utf-8") &&
        MdoApiValueSetString(Data, "data", Encoded);
    xrtFree(Encoded);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "artifact_read_failed", "The artifact chunk could not be encoded",
        NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiStorageRoute(MdoApiContext* Context)
{
    MdoBootstrapSnapshot Snapshot;
    xvalue* Data = xrtValueObject();
    bool Ok;

    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    Ok = Data != NULL && MdoBootstrapGetSnapshot(&Snapshot) &&
        MdoApiValueSetString(Data, "home_path", Snapshot.Home.Path) &&
        MdoApiValueSetString(Data, "persistence",
            Snapshot.Home.Persistence == MDO_PERSISTENCE_EXTERNAL ?
                "external" : (Snapshot.Home.Persistence ==
                MDO_PERSISTENCE_EPHEMERAL ? "ephemeral" : "lazy")) &&
        MdoApiValueSetBool(Data, "external_overlay",
            Snapshot.Home.ExternalOverlay) &&
        MdoApiValueSetUInt(Data, "config_bytes",
            Snapshot.Config.EffectiveBytes) &&
        MdoApiValueSetUInt(Data, "session_count", Snapshot.SessionCount) &&
        MdoApiValueSetUInt(Data, "schedule_count", Snapshot.ScheduleCount) &&
        MdoApiValueSetUInt(Data, "global_memory_count",
            Snapshot.GlobalMemoryCount) &&
        MdoApiValueSetUInt(Data, "artifact_count",
            MdoBootstrapRuntime() != NULL ?
                xworkRuntimeArtifactCount(MdoBootstrapRuntime()) : 0u);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiStateReply(Context, Data);
}
