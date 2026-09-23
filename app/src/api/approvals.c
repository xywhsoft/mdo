#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/approvals.h"

static const char* MdoApiApprovalRisk(xwork_risk_level Risk)
{
    switch ( Risk ) {
    case XWORK_RISK_LOW: return "low";
    case XWORK_RISK_MEDIUM: return "medium";
    case XWORK_RISK_HIGH: return "high";
    default: return "unknown";
    }
}

static const char* MdoApiApprovalResourceKind(xwork_resource_kind Kind)
{
    switch ( Kind ) {
    case XWORK_RESOURCE_PATH: return "path";
    case XWORK_RESOURCE_COMMAND: return "command";
    case XWORK_RESOURCE_PROCESS: return "process";
    case XWORK_RESOURCE_NETWORK: return "network";
    case XWORK_RESOURCE_EXTERNAL_SERVICE: return "external_service";
    case XWORK_RESOURCE_SECRET: return "secret";
    case XWORK_RESOURCE_SCHEDULE: return "schedule";
    case XWORK_RESOURCE_AGENT: return "agent";
    default: return "unknown";
    }
}

static bool MdoApiApprovalAccess(xvalue* Item, xwork_resource_access Access)
{
    static const struct {
        xwork_resource_access Flag;
        const char* Name;
    } Values[] = {
        { XWORK_RESOURCE_ACCESS_READ, "read" },
        { XWORK_RESOURCE_ACCESS_WRITE, "write" },
        { XWORK_RESOURCE_ACCESS_EXECUTE, "execute" },
        { XWORK_RESOURCE_ACCESS_CONTROL, "control" },
        { XWORK_RESOURCE_ACCESS_CONNECT, "connect" },
        { XWORK_RESOURCE_ACCESS_USE, "use" }
    };
    xvalue* Names = xrtValueArray();
    size_t Index;
    bool Ok = Names != NULL;
    for ( Index = 0u; Ok && Index < sizeof(Values) / sizeof(Values[0]);
          ++Index ) {
        if ( (Access & Values[Index].Flag) != 0u )
            Ok = MdoApiValueAppendString(Names, Values[Index].Name);
    }
    if ( Ok ) Ok = MdoApiValueSetUInt(Item, "access_code", Access) &&
        MdoApiValueSetTake(Item, "access", &Names);
    xrtValueRelease(Names);
    return Ok;
}

static bool MdoApiApprovalEffects(xvalue* Item, uint64 Effects)
{
    static const struct {
        uint64 Flag;
        const char* Name;
    } Values[] = {
        { XWORK_TOOL_EFFECT_READ, "read" },
        { XWORK_TOOL_EFFECT_WORKSPACE_WRITE, "workspace_write" },
        { XWORK_TOOL_EFFECT_PROCESS, "process" },
        { XWORK_TOOL_EFFECT_NETWORK, "network" },
        { XWORK_TOOL_EFFECT_EXTERNAL_SERVICE, "external_service" },
        { XWORK_TOOL_EFFECT_SECRETS, "secrets" },
        { XWORK_TOOL_EFFECT_SCHEDULE, "schedule" },
        { XWORK_TOOL_EFFECT_AGENT_DELEGATION, "agent_delegation" }
    };
    xvalue* Names = xrtValueArray();
    size_t Index;
    bool Ok = Names != NULL;
    for ( Index = 0u; Ok && Index < sizeof(Values) / sizeof(Values[0]);
          ++Index ) {
        if ( (Effects & Values[Index].Flag) != 0u )
            Ok = MdoApiValueAppendString(Names, Values[Index].Name);
    }
    if ( Ok ) Ok = MdoApiValueSetUInt(Item, "effects_code", Effects) &&
        MdoApiValueSetTake(Item, "effects", &Names);
    xrtValueRelease(Names);
    return Ok;
}

static bool MdoApiApprovalValue(const MdoApprovalInfo* Info, xvalue** Value)
{
    xvalue* Item = xrtValueObject();
    xvalue* Resources = xrtValueArray();
    uint64 Now = xrtClock();
    uint64 Remaining = Info->ExpiresAt > Now ? Info->ExpiresAt - Now : 0u;
    size_t Index;
    bool Ok = Item != NULL && Resources != NULL &&
        MdoApiValueSetUInt(Item, "id", Info->RequestId) &&
        MdoApiValueSetUInt(Item, "agent_id", Info->AgentId) &&
        MdoApiValueSetUInt(Item, "run_id", Info->RunId) &&
        MdoApiValueSetUInt(Item, "catalog_generation",
            Info->CatalogGeneration) &&
        MdoApiValueSetUInt(Item, "agent_turn", Info->AgentTurn) &&
        MdoApiValueSetString(Item, "tool", Info->ToolName) &&
        MdoApiValueSetString(Item, "tool_call_id", Info->ToolCallId) &&
        MdoApiValueSetString(Item, "risk", MdoApiApprovalRisk(Info->Risk)) &&
        MdoApiValueSetString(Item, "arguments_json", Info->ArgumentsJson) &&
        MdoApiValueSetString(Item, "workspace_root", Info->WorkspaceRoot) &&
        MdoApiValueSetInt(Item, "created_at", Info->CreatedAt) &&
        MdoApiValueSetUInt(Item, "expires_in_ms", Remaining / 1000u) &&
        MdoApiApprovalEffects(Item, Info->Effects);
    for ( Index = 0u; Ok && Index < Info->ResourceCount; ++Index ) {
        const MdoApprovalResourceInfo* Source = &Info->Resources[Index];
        xvalue* Resource = xrtValueObject();
        Ok = Resource != NULL &&
            MdoApiValueSetString(Resource, "kind",
                MdoApiApprovalResourceKind(Source->Kind)) &&
            MdoApiValueSetString(Resource, "resource", Source->Resource) &&
            MdoApiApprovalAccess(Resource, Source->Access) &&
            MdoApiValueAppendTake(Resources, &Resource);
        xrtValueRelease(Resource);
    }
    if ( Ok ) Ok = MdoApiValueSetTake(Item, "resources", &Resources);
    xrtValueRelease(Resources);
    if ( !Ok ) {
        xrtValueRelease(Item);
        return false;
    }
    *Value = Item;
    return true;
}

static bool MdoApiApprovalId(const MdoApiContext* Context, uint64* RequestId)
{
    uint64 Value = 0u;
    size_t Index;
    if ( Context->ParamCount != 1u || Context->Params[0].Size == 0u )
        return false;
    for ( Index = 0u; Index < Context->Params[0].Size; ++Index ) {
        uint64 Digit;
        char Byte = Context->Params[0].Data[Index];
        if ( Byte < '0' || Byte > '9' ) return false;
        Digit = (uint64)(Byte - '0');
        if ( Value > (UINT64_MAX - Digit) / 10u ) return false;
        Value = Value * 10u + Digit;
    }
    if ( Value == 0u ) return false;
    *RequestId = Value;
    return true;
}

bool MdoApiApprovalsRoute(MdoApiContext* Context)
{
    MdoApprovalSnapshot* Snapshot;
    xwork_error Error;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Total;
    size_t Count;
    size_t Index;
    bool Ok;
    memset(&Error, 0, sizeof(Error));
    Snapshot = MdoApprovalSnapshotCreate(&Error);
    Total = MdoApprovalSnapshotCount(Snapshot);
    Count = Total < MDO_APPROVAL_API_LIMIT ? Total : MDO_APPROVAL_API_LIMIT;
    Ok = Snapshot != NULL && Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < Count; ++Index ) {
        MdoApprovalInfo Info;
        xvalue* Item = NULL;
        memset(&Info, 0, sizeof(Info));
        Info.Size = sizeof(Info);
        Ok = MdoApprovalSnapshotAt(Snapshot, Index, &Info) &&
            MdoApiApprovalValue(&Info, &Item) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetUInt(Data, "limit", MDO_APPROVAL_API_LIMIT) &&
        MdoApiValueSetBool(Data, "truncated", Total > Count) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoApprovalSnapshotRelease(Snapshot);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 503u, "approvals_unavailable",
            "Pending approvals could not be read", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiApprovalRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    const xvalue* DecisionValue;
    xstrview DecisionText;
    xwork_permission_decision Decision;
    xwork_error Error;
    xvalue* Data;
    uint64 RequestId;
    const char* DecisionName;
    if ( !MdoApiApprovalId(Context, &RequestId) )
        return MdoApiReplyError(Context, 400u, "invalid_approval_id",
            "The approval ID must be a nonzero decimal integer", NULL);
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    DecisionValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("decision")) : NULL;
    if ( DecisionValue == NULL || xrtValueCount(Body.Value) != 1u ||
         xrtValueType(DecisionValue) != XVALUE_STRING ||
         !xrtValueGetString(DecisionValue, &DecisionText) ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "approval_decision_invalid",
            "The request must contain only an allow or deny decision", NULL);
    }
    if ( DecisionText.Size == 5u &&
         memcmp(DecisionText.Data, "allow", 5u) == 0 ) {
        Decision = XWORK_PERMISSION_ALLOW;
        DecisionName = "allow";
    } else if ( DecisionText.Size == 4u &&
                memcmp(DecisionText.Data, "deny", 4u) == 0 ) {
        Decision = XWORK_PERMISSION_DENY;
        DecisionName = "deny";
    } else {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "approval_decision_invalid",
            "The request must contain only an allow or deny decision", NULL);
    }
    MdoApiJsonBodyUnit(&Body);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoApprovalDecide(RequestId, Decision, &Error) )
        return MdoApiReplyError(Context,
            Error.eCode == XWORK_ERROR_POLICY ? 404u : 503u,
            Error.eCode == XWORK_ERROR_POLICY ? "approval_not_found" :
                "approvals_unavailable",
            Error.eCode == XWORK_ERROR_POLICY ?
                "The approval is no longer pending" :
                "The approval decision could not be applied", NULL);
    Data = xrtValueObject();
    if ( Data == NULL || !MdoApiValueSetUInt(Data, "id", RequestId) ||
         !MdoApiValueSetString(Data, "decision", DecisionName) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "approval_result_unavailable",
            "The decision was applied but its result could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
