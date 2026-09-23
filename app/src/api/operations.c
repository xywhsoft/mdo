#include <string.h>

#include "internal.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/operations.h"

#define MDO_API_OPERATION_LIMIT 64u

static cstr MdoApiOperationKindText(MdoOperationKind Kind)
{
    switch ( Kind ) {
    case MDO_OPERATION_MODULE_RELOAD: return "module_reload";
    case MDO_OPERATION_MCP_REFRESH: return "mcp_refresh";
    default: return "unknown";
    }
}

static cstr MdoApiOperationMcpStateText(xwork_mcp_server_state State)
{
    switch ( State ) {
    case XWORK_MCP_SERVER_DISCONNECTED: return "disconnected";
    case XWORK_MCP_SERVER_READY: return "ready";
    case XWORK_MCP_SERVER_FAILED: return "failed";
    case XWORK_MCP_SERVER_DISABLED: return "disabled";
    default: return "unknown";
    }
}

static cstr MdoApiOperationStateText(MdoOperationState State)
{
    switch ( State ) {
    case MDO_OPERATION_PENDING: return "pending";
    case MDO_OPERATION_RUNNING: return "running";
    case MDO_OPERATION_SUCCEEDED: return "succeeded";
    case MDO_OPERATION_FAILED: return "failed";
    case MDO_OPERATION_CANCELLED: return "cancelled";
    default: return "unknown";
    }
}

static bool MdoApiOperationTerminal(MdoOperationState State)
{
    return State == MDO_OPERATION_SUCCEEDED ||
        State == MDO_OPERATION_FAILED || State == MDO_OPERATION_CANCELLED;
}

static bool MdoApiOperationValue(const MdoOperationInfo* Info,
    xvalue** Value)
{
    xvalue* Item = xrtValueObject();
    xvalue* Result = xrtValueObject();
    bool Ok = Item != NULL && Result != NULL &&
        MdoApiValueSetString(Item, "id", Info->Id) &&
        MdoApiValueSetUInt(Item, "sequence", Info->Sequence) &&
        MdoApiValueSetString(Item, "kind",
            MdoApiOperationKindText(Info->Kind)) &&
        MdoApiValueSetString(Item, "state",
            MdoApiOperationStateText(Info->State)) &&
        MdoApiValueSetInt(Item, "created_at", Info->CreatedAt) &&
        MdoApiValueSetInt(Item, "started_at", Info->StartedAt) &&
        MdoApiValueSetInt(Item, "ended_at", Info->EndedAt) &&
        MdoApiValueSetBool(Item, "terminal",
            MdoApiOperationTerminal(Info->State)) &&
        MdoApiValueSetBool(Item, "cancel_requested",
            Info->CancelRequested) &&
        MdoApiValueSetString(Item, "target", Info->Target) &&
        MdoApiValueSetString(Item, "message", Info->Message) &&
        MdoApiValueSetUInt(Result, "catalog_generation", Info->Generation);
    if ( Ok && Info->Kind == MDO_OPERATION_MODULE_RELOAD ) Ok =
        MdoApiValueSetUInt(Result, "modules", Info->ItemCount) &&
        MdoApiValueSetUInt(Result, "tools", Info->SecondaryCount) &&
        MdoApiValueSetUInt(Result, "agents", Info->TertiaryCount) &&
        MdoApiValueSetUInt(Result, "diagnostics", Info->DiagnosticCount);
    if ( Ok && Info->Kind == MDO_OPERATION_MCP_REFRESH ) Ok =
        MdoApiValueSetUInt(Result, "schema_generation",
            Info->AuxiliaryGeneration) &&
        MdoApiValueSetUInt(Result, "discovered_tools", Info->ItemCount) &&
        MdoApiValueSetUInt(Result, "requests_completed",
            Info->CompletedCount) &&
        MdoApiValueSetString(Result, "server_state",
            MdoApiOperationMcpStateText(
                (xwork_mcp_server_state)Info->RuntimeState)) &&
        MdoApiValueSetBool(Result, "enabled", Info->Enabled) &&
        MdoApiValueSetBool(Result, "connected", Info->Connected) &&
        MdoApiValueSetBool(Result, "tools_discovered",
            Info->ToolsDiscovered);
    if ( Ok ) Ok = MdoApiValueSetTake(Item, "result", &Result);
    xrtValueRelease(Result);
    if ( !Ok ) {
        xrtValueRelease(Item);
        return false;
    }
    *Value = Item;
    return true;
}

bool MdoApiModulesReloadRoute(MdoApiContext* Context)
{
    MdoOperationInfo Info;
    xvalue* Data = NULL;
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    if ( !MdoOperationStartModuleReload(&Info) ) {
        return MdoApiReplyError(Context, 503u, "operation_unavailable",
            "The module reload operation could not be queued", NULL);
    }
    if ( !MdoApiOperationValue(&Info, &Data) ) {
        return MdoApiReplyError(Context, 500u, "operation_result_unavailable",
            "The module reload was queued but its result is unavailable",
            NULL);
    }
    return MdoApiReplySuccessTake(Context, 202u, Data, NULL);
}

bool MdoApiOperationsRoute(MdoApiContext* Context)
{
    MdoOperationInfo* Infos;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count;
    size_t Index;
    bool Ok;

    Infos = (MdoOperationInfo*)xrtCalloc(MDO_API_OPERATION_LIMIT,
        sizeof(*Infos));
    if ( Infos == NULL || Data == NULL || Items == NULL ) {
        xrtFree(Infos);
        xrtValueRelease(Data);
        xrtValueRelease(Items);
        return MdoApiReplyError(Context, 500u, "operations_unavailable",
            "The operation list could not be created", NULL);
    }
    for ( Index = 0u; Index < MDO_API_OPERATION_LIMIT; Index++ )
        Infos[Index].Size = sizeof(Infos[Index]);
    Count = MdoOperationList(Infos, MDO_API_OPERATION_LIMIT);
    Ok = true;
    for ( Index = 0u; Ok && Index < Count; Index++ ) {
        xvalue* Item = NULL;
        Ok = MdoApiOperationValue(&Infos[Index], &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "total", Count) &&
        MdoApiValueSetUInt(Data, "limit", MDO_API_OPERATION_LIMIT) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtFree(Infos);
    xrtValueRelease(Items);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "operations_unavailable",
            "The operation list could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiOperationRoute(MdoApiContext* Context)
{
    char Id[MDO_OPERATION_ID_CAPACITY];
    MdoOperationInfo Info;
    xvalue* Data = NULL;
    bool Found;

    if ( Context->ParamCount != 1u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= sizeof(Id) ) {
        return MdoApiReplyError(Context, 400u, "invalid_operation_id",
            "The operation ID is invalid", NULL);
    }
    memcpy(Id, Context->Params[0].Data, Context->Params[0].Size);
    Id[Context->Params[0].Size] = '\0';
    memset(&Info, 0, sizeof(Info));
    Info.Size = sizeof(Info);
    Found = Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ?
        MdoOperationCancel(Id, &Info) : MdoOperationGet(Id, &Info);
    if ( !Found ) {
        return MdoApiReplyError(Context, 404u, "operation_not_found",
            "The requested operation does not exist", NULL);
    }
    if ( !MdoApiOperationValue(&Info, &Data) ) {
        return MdoApiReplyError(Context, 500u, "operation_result_unavailable",
            "The operation result could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context,
        Context->Request->head->MethodCode == XHTTP_METHOD_DELETE &&
        !MdoApiOperationTerminal(Info.State) ? 202u : 200u, Data, NULL);
}

static bool MdoApiMcpServerId(MdoApiContext* Context, char Id[128])
{
    if ( Context->ParamCount != 1u || Context->Params[0].Size == 0u ||
         Context->Params[0].Size >= 128u ) return false;
    memcpy(Id, Context->Params[0].Data, Context->Params[0].Size);
    Id[Context->Params[0].Size] = '\0';
    return true;
}

static bool MdoApiMcpStatusValue(cstr ServerId,
    const MdoMcpServerStatus* Status, xvalue** Value)
{
    xvalue* Data = xrtValueObject();
    bool Ok = Data != NULL &&
        MdoApiValueSetString(Data, "id", ServerId) &&
        MdoApiValueSetUInt(Data, "catalog_generation",
            Status->CatalogGeneration) &&
        MdoApiValueSetString(Data, "state",
            MdoApiOperationMcpStateText(Status->State)) &&
        MdoApiValueSetUInt(Data, "schema_generation",
            Status->SchemaGeneration) &&
        MdoApiValueSetUInt(Data, "schema_expires_at",
            Status->SchemaExpiresMicroseconds) &&
        MdoApiValueSetUInt(Data, "discovered_tools",
            Status->DiscoveredToolCount) &&
        MdoApiValueSetUInt(Data, "requests_completed",
            Status->RequestsCompleted) &&
        MdoApiValueSetBool(Data, "enabled", Status->Enabled) &&
        MdoApiValueSetBool(Data, "connected", Status->Connected) &&
        MdoApiValueSetBool(Data, "tools_discovered",
            Status->ToolsDiscovered) &&
        MdoApiValueSetBool(Data, "supports_tool_list_changes",
            Status->SupportsToolListChanges);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return false;
    }
    *Value = Data;
    return true;
}

static bool MdoApiMcpNoBody(MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return !(((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u);
}

bool MdoApiMcpEnabledRoute(MdoApiContext* Context)
{
    char ServerId[128];
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoMcpServerStatus Status;
    const xvalue* EnabledValue;
    xwork_error Error;
    xvalue* Data = NULL;
    bool Enabled;

    if ( !MdoApiMcpServerId(Context, ServerId) )
        return MdoApiReplyError(Context, 400u, "invalid_mcp_server_id",
            "The MCP server ID is invalid", NULL);
    memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
    if ( !MdoMcpManagerGetStatus(ServerId, &Status) )
        return MdoApiReplyError(Context, 404u, "mcp_server_not_found",
            "The requested MCP server does not exist", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    EnabledValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("enabled")) : NULL;
    if ( EnabledValue == NULL || xrtValueType(EnabledValue) != XVALUE_BOOL ||
         xrtValueCount(Body.Value) != 1u ||
         !xrtValueGetBool(EnabledValue, &Enabled) ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "mcp_state_invalid",
            "The request must contain only one boolean enabled field", NULL);
    }
    MdoApiJsonBodyUnit(&Body);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoMcpManagerSetEnabled(ServerId, Enabled, &Error) ||
         !MdoMcpManagerGetStatus(ServerId, &Status) ) {
        return MdoApiReplyError(Context, 409u, "mcp_state_change_failed",
            "The MCP server state could not be changed", NULL);
    }
    if ( !MdoApiMcpStatusValue(ServerId, &Status, &Data) )
        return MdoApiReplyError(Context, 500u, "mcp_status_unavailable",
            "The MCP server changed state but its status is unavailable", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiMcpDisconnectRoute(MdoApiContext* Context)
{
    char ServerId[128];
    MdoMcpServerStatus Status;
    xwork_error Error;
    xvalue* Data = NULL;

    if ( !MdoApiMcpServerId(Context, ServerId) )
        return MdoApiReplyError(Context, 400u, "invalid_mcp_server_id",
            "The MCP server ID is invalid", NULL);
    memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
    if ( !MdoMcpManagerGetStatus(ServerId, &Status) )
        return MdoApiReplyError(Context, 404u, "mcp_server_not_found",
            "The requested MCP server does not exist", NULL);
    if ( !MdoApiMcpNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "MCP disconnect requests do not accept a body", NULL);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoMcpManagerDisconnect(ServerId, &Error) ||
         !MdoMcpManagerGetStatus(ServerId, &Status) ) {
        return MdoApiReplyError(Context, 409u, "mcp_disconnect_failed",
            "The MCP server could not be disconnected", NULL);
    }
    if ( !MdoApiMcpStatusValue(ServerId, &Status, &Data) )
        return MdoApiReplyError(Context, 500u, "mcp_status_unavailable",
            "The MCP server disconnected but its status is unavailable", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiMcpRefreshRoute(MdoApiContext* Context)
{
    char ServerId[128];
    MdoMcpServerStatus Status;
    MdoOperationInfo Info;
    xvalue* Data = NULL;

    if ( !MdoApiMcpServerId(Context, ServerId) )
        return MdoApiReplyError(Context, 400u, "invalid_mcp_server_id",
            "The MCP server ID is invalid", NULL);
    memset(&Status, 0, sizeof(Status)); Status.Size = sizeof(Status);
    if ( !MdoMcpManagerGetStatus(ServerId, &Status) )
        return MdoApiReplyError(Context, 404u, "mcp_server_not_found",
            "The requested MCP server does not exist", NULL);
    if ( !Status.Enabled )
        return MdoApiReplyError(Context, 409u, "mcp_server_disabled",
            "Enable the MCP server before refreshing it", NULL);
    if ( !MdoApiMcpNoBody(Context) )
        return MdoApiReplyError(Context, 400u, "body_not_allowed",
            "MCP refresh requests do not accept a body", NULL);
    memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
    if ( !MdoOperationStartMcpRefresh(ServerId, &Info) )
        return MdoApiReplyError(Context, 503u, "operation_unavailable",
            "The MCP refresh operation could not be queued", NULL);
    if ( !MdoApiOperationValue(&Info, &Data) )
        return MdoApiReplyError(Context, 500u, "operation_result_unavailable",
            "The MCP refresh was queued but its result is unavailable", NULL);
    return MdoApiReplySuccessTake(Context, 202u, Data, NULL);
}
