#include <string.h>

#include "internal.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/skills.h"

static bool MdoApiCatalogReply(MdoApiContext* Context, xvalue* Data)
{
    if ( Data == NULL ) {
        return MdoApiReplyError(Context, 500u, "catalog_unavailable",
            "The requested catalog could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

static cstr MdoApiModuleKindText(MdoModuleKind Kind)
{
    switch ( Kind ) {
    case MDO_MODULE_TOOLS: return "tools";
    case MDO_MODULE_AGENTS: return "agents";
    case MDO_MODULE_SUBAGENTS: return "subagents";
    default: return "unknown";
    }
}

static cstr MdoApiMcpTransportText(MdoMcpTransport Transport)
{
    switch ( Transport ) {
    case MDO_MCP_TRANSPORT_STDIO: return "stdio";
    case MDO_MCP_TRANSPORT_STREAMABLE_HTTP: return "streamable_http";
    default: return "unknown";
    }
}

static cstr MdoApiMcpStateText(xwork_mcp_server_state State)
{
    switch ( State ) {
    case XWORK_MCP_SERVER_DISCONNECTED: return "disconnected";
    case XWORK_MCP_SERVER_READY: return "ready";
    case XWORK_MCP_SERVER_FAILED: return "failed";
    case XWORK_MCP_SERVER_DISABLED: return "disabled";
    default: return "unknown";
    }
}

bool MdoApiModelsRoute(MdoApiContext* Context)
{
    MdoModelCatalog* Catalog = MdoModelCatalogSnapshot();
    xvalue* Data = xrtValueObject();
    xvalue* Providers = xrtValueArray();
    xvalue* Models = xrtValueArray();
    size_t Index;
    bool Ok = Catalog != NULL && Data != NULL && Providers != NULL &&
        Models != NULL;

    for ( Index = 0u; Ok && Index < MdoModelCatalogProviderCount(Catalog);
          Index++ ) {
        MdoProviderInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info));
        Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoModelCatalogProviderAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "name", Info.Name) &&
            MdoApiValueSetUInt(Item, "generation", Info.Generation) &&
            MdoApiValueSetBool(Item, "builtin", Info.Builtin) &&
            MdoApiValueSetBool(Item, "editable", Info.Editable) &&
            MdoApiValueSetBool(Item, "removable", Info.Removable) &&
            MdoApiValueSetBool(Item, "verify_peer", Info.VerifyPeer) &&
            MdoApiValueSetBool(Item, "credential_configured",
                Info.HasCredentialReference) &&
            MdoApiValueSetUInt(Item, "protocols", Info.Protocols) &&
            MdoApiValueSetUInt(Item, "timeout_ms", Info.TimeoutMilliseconds) &&
            MdoApiValueSetString(Item, "chat_completions_endpoint",
                Info.ChatCompletionsEndpoint) &&
            MdoApiValueSetString(Item, "responses_endpoint",
                Info.ResponsesEndpoint) &&
            MdoApiValueSetString(Item, "anthropic_messages_endpoint",
                Info.AnthropicMessagesEndpoint) &&
            MdoApiValueAppendTake(Providers, &Item);
        xrtValueRelease(Item);
    }
    for ( Index = 0u; Ok && Index < MdoModelCatalogModelCount(Catalog);
          Index++ ) {
        MdoModelInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info));
        Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoModelCatalogModelAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "name", Info.Name) &&
            MdoApiValueSetString(Item, "provider_id", Info.ProviderId) &&
            MdoApiValueSetString(Item, "wire_model", Info.WireModel) &&
            MdoApiValueSetUInt(Item, "generation", Info.Generation) &&
            MdoApiValueSetBool(Item, "builtin", Info.Builtin) &&
            MdoApiValueSetBool(Item, "free", Info.Free) &&
            MdoApiValueSetBool(Item, "editable", Info.Editable) &&
            MdoApiValueSetBool(Item, "removable", Info.Removable) &&
            MdoApiValueSetUInt(Item, "protocols", Info.Protocols) &&
            MdoApiValueSetString(Item, "default_protocol",
                MdoModelProtocolName(Info.DefaultProtocol)) &&
            MdoApiValueSetUInt(Item, "capabilities", Info.Capabilities) &&
            MdoApiValueSetUInt(Item, "window_mode", Info.WindowMode) &&
            MdoApiValueSetUInt(Item, "context_window_tokens",
                Info.ContextWindowTokens) &&
            MdoApiValueSetUInt(Item, "max_input_tokens", Info.MaxInputTokens) &&
            MdoApiValueSetUInt(Item, "max_output_tokens", Info.MaxOutputTokens) &&
            MdoApiValueSetUInt(Item, "output_reserve_tokens",
                Info.OutputReserveTokens) &&
            MdoApiValueSetUInt(Item, "summary_tokens", Info.SummaryTokens) &&
            MdoApiValueSetString(Item, "default_reasoning_effort",
                Info.DefaultReasoningEffort) &&
            MdoApiValueSetStrings(Item, "reasoning_efforts",
                Info.ReasoningEfforts, Info.ReasoningEffortCount) &&
            MdoApiValueSetUInt(Item, "attachments", Info.Attachments) &&
            MdoApiValueAppendTake(Models, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation", MdoModelManagerGeneration()) &&
        MdoApiValueSetTake(Data, "providers", &Providers) &&
        MdoApiValueSetTake(Data, "models", &Models);
    xrtValueRelease(Providers);
    xrtValueRelease(Models);
    MdoModelCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiCatalogReply(Context, Data);
}

static bool MdoApiModuleAgentValue(const MdoModuleAgentInfo* Info,
    xvalue** pItem)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetString(Item, "module_id", Info->ModuleId) &&
        MdoApiValueSetString(Item, "id", Info->Id) &&
        MdoApiValueSetString(Item, "name", Info->Name) &&
        MdoApiValueSetString(Item, "description", Info->Description) &&
        MdoApiValueSetString(Item, "model", Info->Model) &&
        MdoApiValueSetString(Item, "reasoning_effort", Info->ReasoningEffort) &&
        MdoApiValueSetString(Item, "permission_profile",
            Info->PermissionProfile) &&
        MdoApiValueSetUInt(Item, "generation", Info->Generation) &&
        MdoApiValueSetUInt(Item, "allowed_effects", Info->AllowedEffects) &&
        MdoApiValueSetUInt(Item, "flags", Info->Flags) &&
        MdoApiValueSetUInt(Item, "context_window_tokens",
            Info->ContextWindowTokens) &&
        MdoApiValueSetUInt(Item, "max_input_tokens", Info->MaxInputTokens) &&
        MdoApiValueSetUInt(Item, "max_output_tokens", Info->MaxOutputTokens) &&
        MdoApiValueSetUInt(Item, "max_turns", Info->MaxTurns) &&
        MdoApiValueSetUInt(Item, "timeout_ms", Info->TimeoutMilliseconds) &&
        MdoApiValueSetUInt(Item, "max_final_bytes", Info->MaxFinalBytes) &&
        MdoApiValueSetUInt(Item, "max_depth", Info->MaxDepth) &&
        MdoApiValueSetUInt(Item, "system_prompt_bytes",
            Info->SystemPrompt != NULL ? strlen(Info->SystemPrompt) : 0u) &&
        MdoApiValueSetStrings(Item, "tools", Info->Tools, Info->ToolCount) &&
        MdoApiValueSetStrings(Item, "skills", Info->Skills, Info->SkillCount);
    if ( !Ok ) { xrtValueRelease(Item); return false; }
    *pItem = Item;
    return true;
}

bool MdoApiAgentsRoute(MdoApiContext* Context)
{
    MdoModuleCatalog* Catalog = MdoModuleCatalogSnapshot();
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Index;
    bool Ok = Catalog != NULL && Data != NULL && Items != NULL;

    for ( Index = 0u; Ok && Index < MdoModuleCatalogAgentCount(Catalog);
          Index++ ) {
        MdoModuleAgentInfo Info;
        xvalue* Item = NULL;
        memset(&Info, 0, sizeof(Info));
        Info.Size = sizeof(Info);
        Ok = MdoModuleCatalogAgentAt(Catalog, Index, &Info) &&
            MdoApiModuleAgentValue(&Info, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation", MdoModuleManagerGeneration()) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoModuleCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiCatalogReply(Context, Data);
}

bool MdoApiModulesRoute(MdoApiContext* Context)
{
    MdoModuleCatalog* Catalog = MdoModuleCatalogSnapshot();
    xvalue* Data = xrtValueObject();
    xvalue* Modules = xrtValueArray();
    xvalue* Tools = xrtValueArray();
    size_t Index;
    bool Ok = Catalog != NULL && Data != NULL && Modules != NULL && Tools != NULL;

    for ( Index = 0u; Ok && Index < MdoModuleCatalogModuleCount(Catalog);
          Index++ ) {
        MdoModuleInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoModuleCatalogModuleAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "name", Info.Name) &&
            MdoApiValueSetString(Item, "description", Info.Description) &&
            MdoApiValueSetString(Item, "version", Info.Version) &&
            MdoApiValueSetString(Item, "kind", MdoApiModuleKindText(Info.Kind)) &&
            MdoApiValueSetUInt(Item, "generation", Info.Generation) &&
            MdoApiValueSetBool(Item, "external", Info.External) &&
            MdoApiValueSetString(Item, "source_path", Info.SourcePath) &&
            MdoApiValueSetString(Item, "source_hash", Info.SourceHash) &&
            MdoApiValueSetUInt(Item, "capabilities", Info.Capabilities) &&
            MdoApiValueSetUInt(Item, "tool_count", Info.ToolCount) &&
            MdoApiValueSetUInt(Item, "agent_count", Info.AgentCount) &&
            MdoApiValueAppendTake(Modules, &Item);
        xrtValueRelease(Item);
    }
    for ( Index = 0u; Ok && Index < MdoModuleCatalogToolCount(Catalog);
          Index++ ) {
        MdoModuleToolInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoModuleCatalogToolAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "module_id", Info.ModuleId) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "name", Info.Name) &&
            MdoApiValueSetString(Item, "description", Info.Description) &&
            MdoApiValueSetUInt(Item, "generation", Info.Generation) &&
            MdoApiValueSetUInt(Item, "effects", Info.Effects) &&
            MdoApiValueSetUInt(Item, "flags", Info.Flags) &&
            MdoApiValueSetString(Item, "serial_group", Info.SerialGroup) &&
            MdoApiValueSetString(Item, "permission_resource",
                Info.PermissionResource) &&
            MdoApiValueSetUInt(Item, "max_result_bytes", Info.MaxResultBytes) &&
            MdoApiValueSetUInt(Item, "parameter_schema_bytes",
                Info.ParametersJson != NULL ? strlen(Info.ParametersJson) : 0u) &&
            MdoApiValueAppendTake(Tools, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation", MdoModuleManagerGeneration()) &&
        MdoApiValueSetTake(Data, "modules", &Modules) &&
        MdoApiValueSetTake(Data, "tools", &Tools);
    xrtValueRelease(Modules); xrtValueRelease(Tools);
    MdoModuleCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiCatalogReply(Context, Data);
}

bool MdoApiSkillsRoute(MdoApiContext* Context)
{
    MdoSkillCatalog* Catalog = MdoSkillCatalogSnapshot();
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Index;
    bool Ok = Catalog != NULL && Data != NULL && Items != NULL;

    for ( Index = 0u; Ok && Index < MdoSkillCatalogCount(Catalog); Index++ ) {
        MdoSkillInfo Info;
        xvalue* Item = xrtValueObject();
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = Item != NULL && MdoSkillCatalogAt(Catalog, Index, &Info) &&
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "name", Info.Name) &&
            MdoApiValueSetString(Item, "description", Info.Description) &&
            MdoApiValueSetString(Item, "version", Info.Version) &&
            MdoApiValueSetString(Item, "license", Info.License) &&
            MdoApiValueSetString(Item, "compatibility", Info.Compatibility) &&
            MdoApiValueSetString(Item, "source_path", Info.SourcePath) &&
            MdoApiValueSetString(Item, "metadata_hash", Info.MetadataHash) &&
            MdoApiValueSetUInt(Item, "generation", Info.Generation) &&
            MdoApiValueSetBool(Item, "external", Info.External) &&
            MdoApiValueSetString(Item, "trust",
                Info.Trust == MDO_SKILL_TRUST_BUILTIN ? "builtin" :
                "external_reference") &&
            MdoApiValueSetUInt(Item, "body_bytes", Info.BodyBytes) &&
            MdoApiValueSetUInt(Item, "estimated_tokens", Info.EstimatedTokens) &&
            MdoApiValueSetUInt(Item, "resource_count", Info.ResourceCount) &&
            MdoApiValueSetStrings(Item, "required_tools", Info.RequiredTools,
                Info.RequiredToolCount) &&
            MdoApiValueSetStrings(Item, "required_mcp_servers",
                Info.RequiredMcpServers, Info.RequiredMcpServerCount) &&
            MdoApiValueSetStrings(Item, "required_permissions",
                Info.RequiredPermissions, Info.RequiredPermissionCount) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation",
            MdoSkillCatalogGeneration(Catalog)) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoSkillCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiCatalogReply(Context, Data);
}

bool MdoApiMcpRoute(MdoApiContext* Context)
{
    MdoMcpCatalog* Catalog = MdoMcpCatalogSnapshot();
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Index;
    bool Ok = Catalog != NULL && Data != NULL && Items != NULL;

    for ( Index = 0u; Ok && Index < MdoMcpCatalogCount(Catalog); Index++ ) {
        MdoMcpServerInfo Info;
        MdoMcpServerStatus Status;
        xvalue* Item = xrtValueObject();
        bool HasStatus;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
        Ok = Item != NULL && MdoMcpCatalogAt(Catalog, Index, &Info);
        HasStatus = Ok && MdoMcpManagerGetStatus(Info.Id, &Status);
        if ( Ok ) Ok =
            MdoApiValueSetString(Item, "id", Info.Id) &&
            MdoApiValueSetString(Item, "name", Info.Name) &&
            MdoApiValueSetString(Item, "description", Info.Description) &&
            MdoApiValueSetString(Item, "transport",
                MdoApiMcpTransportText(Info.Transport)) &&
            MdoApiValueSetString(Item, "protocol_version", Info.ProtocolVersion) &&
            MdoApiValueSetString(Item, "program", Info.Program) &&
            MdoApiValueSetString(Item, "endpoint", Info.Endpoint) &&
            MdoApiValueSetString(Item, "working_directory",
                Info.WorkingDirectory) &&
            MdoApiValueSetString(Item, "permission_profile",
                Info.PermissionProfile) &&
            MdoApiValueSetString(Item, "source_path", Info.SourcePath) &&
            MdoApiValueSetString(Item, "source_hash", Info.SourceHash) &&
            MdoApiValueSetUInt(Item, "generation", Info.Generation) &&
            MdoApiValueSetBool(Item, "external", Info.External) &&
            MdoApiValueSetBool(Item, "enabled", Info.Enabled) &&
            MdoApiValueSetBool(Item, "auto_reconnect", Info.AutoReconnect) &&
            MdoApiValueSetBool(Item, "trust_read_only_annotations",
                Info.TrustReadOnlyAnnotations) &&
            MdoApiValueSetUInt(Item, "argument_count", Info.ArgumentCount) &&
            MdoApiValueSetUInt(Item, "environment_count", Info.EnvironmentCount) &&
            MdoApiValueSetUInt(Item, "http_header_count", Info.HttpHeaderCount) &&
            MdoApiValueSetUInt(Item, "allowed_tool_count", Info.AllowedToolCount) &&
            MdoApiValueSetUInt(Item, "denied_tool_count", Info.DeniedToolCount) &&
            MdoApiValueSetUInt(Item, "startup_timeout_ms",
                Info.StartupTimeoutMilliseconds) &&
            MdoApiValueSetUInt(Item, "request_timeout_ms",
                Info.RequestTimeoutMilliseconds) &&
            MdoApiValueSetUInt(Item, "max_message_bytes", Info.MaxMessageBytes) &&
            MdoApiValueSetUInt(Item, "max_tools", Info.MaxTools) &&
            MdoApiValueSetUInt(Item, "default_effects", Info.DefaultEffects) &&
            MdoApiValueSetBool(Item, "status_available", HasStatus);
        if ( Ok && HasStatus ) Ok =
            MdoApiValueSetString(Item, "state", MdoApiMcpStateText(Status.State)) &&
            MdoApiValueSetUInt(Item, "schema_generation",
                Status.SchemaGeneration) &&
            MdoApiValueSetUInt(Item, "discovered_tool_count",
                Status.DiscoveredToolCount) &&
            MdoApiValueSetUInt(Item, "requests_completed",
                Status.RequestsCompleted) &&
            MdoApiValueSetBool(Item, "connected", Status.Connected) &&
            MdoApiValueSetBool(Item, "tools_discovered", Status.ToolsDiscovered) &&
            MdoApiValueSetBool(Item, "supports_tool_list_changes",
                Status.SupportsToolListChanges);
        if ( Ok ) Ok = MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "generation", MdoMcpManagerGeneration()) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoMcpCatalogRelease(Catalog);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    return MdoApiCatalogReply(Context, Data);
}
