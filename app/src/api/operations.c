#include <string.h>

#include "internal.h"
#include "../../include/mdo/operations.h"

#define MDO_API_OPERATION_LIMIT 64u

static cstr MdoApiOperationKindText(MdoOperationKind Kind)
{
    switch ( Kind ) {
    case MDO_OPERATION_MODULE_RELOAD: return "module_reload";
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
        MdoApiValueSetString(Item, "message", Info->Message) &&
        MdoApiValueSetUInt(Result, "generation", Info->Generation) &&
        MdoApiValueSetUInt(Result, "modules", Info->ItemCount) &&
        MdoApiValueSetUInt(Result, "tools", Info->SecondaryCount) &&
        MdoApiValueSetUInt(Result, "agents", Info->TertiaryCount) &&
        MdoApiValueSetUInt(Result, "diagnostics", Info->DiagnosticCount) &&
        MdoApiValueSetTake(Item, "result", &Result);
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
