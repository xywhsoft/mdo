#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/settings.h"
#include "../../include/mdo/skills.h"

static bool MdoApiConfigDomain(xstrview Name, MdoConfigDomain* Domain,
    cstr* Canonical)
{
    if ( Name.Size == 8u && memcmp(Name.Data, "settings", 8u) == 0 ) {
        *Domain = MDO_CONFIG_SETTINGS;
        *Canonical = "settings";
        return true;
    }
    if ( Name.Size == 6u && memcmp(Name.Data, "models", 6u) == 0 ) {
        *Domain = MDO_CONFIG_MODELS;
        *Canonical = "models";
        return true;
    }
    if ( Name.Size == 11u && memcmp(Name.Data, "permissions", 11u) == 0 ) {
        *Domain = MDO_CONFIG_PERMISSIONS;
        *Canonical = "permissions";
        return true;
    }
    return false;
}

static bool MdoApiConfigMergeObject(xvalue* Target, const xvalue* Patch)
{
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* Value;
    xvalueiterresult Result;

    if ( Target == NULL || Patch == NULL ||
         xrtValueType(Target) != XVALUE_OBJECT ||
         xrtValueType(Patch) != XVALUE_OBJECT ) return false;
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(Patch, &Iterator) ) return false;
    for ( ; ; ) {
        xvalue* Copy = NULL;
        xvalue* Current;
        Result = xrtValueIterAdvance(&Iterator, &Key, &Value);
        if ( Result == XVALUE_ITER_END ) break;
        if ( Result == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
        Current = xrtValueObjectGet(Target, Key.String);
        if ( Current != NULL && xrtValueType(Current) == XVALUE_OBJECT &&
             xrtValueType(Value) == XVALUE_OBJECT ) {
            Copy = xrtValueDeepClone(Current);
            if ( Copy == NULL || !MdoApiConfigMergeObject(Copy, Value) ) {
                xrtValueRelease(Copy);
                xrtValueIterEnd(&Iterator);
                return false;
            }
        } else {
            Copy = xrtValueDeepClone(Value);
            if ( Copy == NULL ) {
                xrtValueIterEnd(&Iterator);
                return false;
            }
        }
        if ( !xrtValueObjectSetTake(Target, Key.String, &Copy) ) {
            xrtValueRelease(Copy);
            xrtValueIterEnd(&Iterator);
            return false;
        }
    }
    xrtValueIterEnd(&Iterator);
    return true;
}

static str MdoApiConfigMergeDocument(MdoConfigDomain Domain,
    const xvalue* Incoming, size_t* Size)
{
    str ExistingText = NULL;
    size_t ExistingSize = 0u;
    xvalue* Existing = NULL;
    xvalue* Patch = NULL;
    xvalue* Document = NULL;
    const xvalue* ExistingPatch;
    const xvalue* IncomingPatch;
    str Merged = NULL;

    ExistingText = MdoConfigExport(Domain, false, &ExistingSize);
    if ( ExistingText == NULL ) goto done;
    Existing = xrtJsonParse(xrtStrViewN(ExistingText, ExistingSize));
    ExistingPatch = Existing != NULL ? xrtValueObjectGet(Existing,
        XRT_STR_LITERAL("patch")) : NULL;
    IncomingPatch = Incoming != NULL ? xrtValueObjectGet(Incoming,
        XRT_STR_LITERAL("patch")) : NULL;
    if ( xrtValueType(ExistingPatch) != XVALUE_OBJECT ||
         xrtValueType(IncomingPatch) != XVALUE_OBJECT ) goto done;
    Patch = xrtValueDeepClone(ExistingPatch);
    Document = xrtValueObject();
    if ( Patch == NULL || Document == NULL ||
         !MdoApiConfigMergeObject(Patch, IncomingPatch) ||
         !xrtValueObjectSetNew(Document, XRT_STR_LITERAL("schema_version"),
            xrtValueUInt(MDO_CONFIG_SCHEMA_VERSION)) ||
         !xrtValueObjectSetTake(Document, XRT_STR_LITERAL("patch"), &Patch) )
        goto done;
    Merged = xrtJsonStringify(Document, true, Size);

done:
    xrtValueRelease(Document);
    xrtValueRelease(Patch);
    xrtValueRelease(Existing);
    xrtFree(ExistingText);
    return Merged;
}

static bool MdoApiConfigInputPreview(MdoConfigDomain Domain,
    const MdoApiJsonBody* Body, bool Merge, MdoConfigPreview* Preview,
    str* Document, size_t* DocumentSize)
{
    MdoConfigPreview Input;
    *Document = NULL;
    *DocumentSize = 0u;
    if ( !Merge ) return MdoConfigPreviewImport(Domain,
        xrtStrViewN(Body->Document, Body->Size), Preview);
    memset(&Input, 0, sizeof(Input));
    Input.Size = sizeof(Input);
    if ( !MdoConfigPreviewImport(Domain,
            xrtStrViewN(Body->Document, Body->Size), &Input) ) {
        *Preview = Input;
        return false;
    }
    *Document = MdoApiConfigMergeDocument(Domain, Body->Value,
        DocumentSize);
    return *Document != NULL && MdoConfigPreviewImport(Domain,
        xrtStrViewN(*Document, *DocumentSize), Preview);
}

bool MdoApiSettingsPreviewRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoConfigDomain Domain;
    MdoConfigPreview Preview;
    MdoConfigSnapshot Snapshot;
    cstr DomainName;
    str Document = NULL;
    size_t DocumentSize = 0u;
    xvalue* Data;
    bool Ok;
    bool Merge = Context->Request->head->MethodCode == XHTTP_METHOD_PATCH;

    if ( Context->ParamCount != 1u ||
         !MdoApiConfigDomain(Context->Params[0], &Domain, &DomainName) ) {
        return MdoApiReplyError(Context, 404u, "config_domain_not_found",
            "The requested configuration domain does not exist", NULL);
    }
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    if ( !MdoApiConfigInputPreview(Domain, &Body, Merge, &Preview,
            &Document, &DocumentSize) ) {
        bool MergeUnavailable = Merge && Document == NULL &&
            Preview.Message[0] == '\0';
        xrtFree(Document);
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, MergeUnavailable ? 503u : 422u,
            MergeUnavailable ? "configuration_merge_unavailable" :
                "configuration_invalid", MergeUnavailable ?
                "The current configuration could not be merged" :
                (Preview.Message[0] != '\0' ? Preview.Message :
                    "The configuration document is invalid"), NULL);
    }
    xrtFree(Document);
    MdoApiJsonBodyUnit(&Body);
    memset(&Snapshot, 0, sizeof(Snapshot));
    Snapshot.Size = sizeof(Snapshot);
    Data = xrtValueObject();
    Ok = Data != NULL && MdoConfigGetSnapshot(&Snapshot) &&
        MdoApiValueSetString(Data, "domain", DomainName) &&
        MdoApiValueSetBool(Data, "valid", Preview.Valid) &&
        MdoApiValueSetBool(Data, "changes", Preview.Changes) &&
        MdoApiValueSetUInt(Data, "patch_bytes", Preview.PatchBytes) &&
        MdoApiValueSetString(Data, "message", Preview.Message) &&
        MdoApiValueSetUInt(Data, "current_revision", Snapshot.Revision);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "preview_unavailable",
            "The configuration preview could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

typedef enum MdoApiPreconditionStatus {
    MDO_API_PRECONDITION_OK = 0,
    MDO_API_PRECONDITION_MISSING,
    MDO_API_PRECONDITION_INVALID
} MdoApiPreconditionStatus;

static MdoApiPreconditionStatus MdoApiExpectedRevision(
    const MdoApiContext* Context, uint64* Revision)
{
    static const char Prefix[] = "\"mdo-config-";
    const xhttpfield* Field = NULL;
    xhttpnext Next;
    xstrview Value;
    uint64 Number = 0u;
    size_t Index;

    Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("If-Match"),
        &Field);
    if ( Next == XHTTP_NEXT_END ) return MDO_API_PRECONDITION_MISSING;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL )
        return MDO_API_PRECONDITION_INVALID;
    Value = xrtStrTrim(Field->Value);
    if ( Value.Size <= sizeof(Prefix) ||
         memcmp(Value.Data, Prefix, sizeof(Prefix) - 1u) != 0 ||
         Value.Data[Value.Size - 1u] != '"' )
        return MDO_API_PRECONDITION_INVALID;
    for ( Index = sizeof(Prefix) - 1u; Index + 1u < Value.Size; Index++ ) {
        uint64 Digit;
        if ( Value.Data[Index] < '0' || Value.Data[Index] > '9' )
            return MDO_API_PRECONDITION_INVALID;
        Digit = (uint64)(Value.Data[Index] - '0');
        if ( Number > (UINT64_MAX - Digit) / 10u )
            return MDO_API_PRECONDITION_INVALID;
        Number = Number * 10u + Digit;
    }
    if ( Number == 0u || Number == UINT64_MAX )
        return MDO_API_PRECONDITION_INVALID;
    *Revision = Number;
    return MDO_API_PRECONDITION_OK;
}

static bool MdoApiSettingsFailure(MdoApiContext* Context,
    const MdoSettingsResult* Result)
{
    switch ( Result->Status ) {
    case MDO_SETTINGS_STATUS_CONFLICT:
        return MdoApiReplyError(Context, 412u, "revision_conflict",
            Result->Message, NULL);
    case MDO_SETTINGS_STATUS_VALIDATION:
        return MdoApiReplyError(Context, 422u, "configuration_invalid",
            Result->Message, NULL);
    case MDO_SETTINGS_STATUS_RUNTIME_REJECTED:
        return MdoApiReplyError(Context, 409u, "runtime_rejected",
            Result->Message, NULL);
    case MDO_SETTINGS_STATUS_ROLLBACK_FAILED:
        return MdoApiReplyError(Context, 503u, "runtime_inconsistent",
            Result->Message, NULL);
    case MDO_SETTINGS_STATUS_UNAVAILABLE:
        return MdoApiReplyError(Context, 503u, "settings_unavailable",
            Result->Message, NULL);
    case MDO_SETTINGS_STATUS_PERSISTENCE:
        return MdoApiReplyError(Context, 500u, "configuration_persistence_failed",
            Result->Message, NULL);
    default:
        return MdoApiReplyError(Context, 400u, "invalid_settings_request",
            Result->Message, NULL);
    }
}

static bool MdoApiSettingsResult(MdoApiContext* Context, cstr Domain,
    const MdoSettingsResult* Result)
{
    xvalue* Data = xrtValueObject();
    bool Ok = Data != NULL &&
        MdoApiValueSetString(Data, "domain", Domain) &&
        MdoApiValueSetBool(Data, "changed", Result->Changed) &&
        MdoApiValueSetBool(Data, "restored", Result->Restored) &&
        MdoApiValueSetUInt(Data, "previous_revision",
            Result->PreviousRevision) &&
        MdoApiValueSetUInt(Data, "revision", Result->Revision) &&
        MdoApiValueSetUInt(Data, "model_generation",
            Result->ModelGeneration) &&
        MdoApiValueSetUInt(Data, "web_generation", Result->WebGeneration) &&
        MdoApiValueSetUInt(Data, "schedule_generation",
            Result->ScheduleGeneration) &&
        MdoApiValueSetString(Data, "message", Result->Message);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "settings_result_unavailable",
            "The settings transaction completed but its result is unavailable",
            NULL);
    }
    return MdoApiReplySuccessTakeRevision(Context, 200u, Data,
        Result->Revision);
}

bool MdoApiSettingsMutationRoute(MdoApiContext* Context)
{
    MdoConfigDomain Domain;
    MdoSettingsResult Result;
    MdoApiPreconditionStatus Precondition;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    cstr DomainName;
    str Document = NULL;
    size_t DocumentSize = 0u;
    uint64 ExpectedRevision = 0u;
    bool Ok;

    if ( Context->ParamCount != 1u ||
         !MdoApiConfigDomain(Context->Params[0], &Domain, &DomainName) ) {
        return MdoApiReplyError(Context, 404u, "config_domain_not_found",
            "The requested configuration domain does not exist", NULL);
    }
    Precondition = MdoApiExpectedRevision(Context, &ExpectedRevision);
    if ( Precondition == MDO_API_PRECONDITION_MISSING ) {
        return MdoApiReplyError(Context, 428u, "precondition_required",
            "If-Match must contain the current configuration ETag", NULL);
    }
    if ( Precondition != MDO_API_PRECONDITION_OK ) {
        return MdoApiReplyError(Context, 400u, "invalid_precondition",
            "If-Match must use the form \"mdo-config-N\"", NULL);
    }
    memset(&Result, 0, sizeof(Result));
    Result.Size = sizeof(Result);
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ) {
        const xhttp1head* Head = Context->Request->head;
        if ( ((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
              Head->ContentLength != 0u) ||
             (Head->Flags & (uint32)XHTTP1_TRANSFER_ENCODING) != 0u ) {
            return MdoApiReplyError(Context, 400u, "body_not_allowed",
                "DELETE settings requests do not accept a body", NULL);
        }
        Ok = MdoSettingsRestore(Domain, ExpectedRevision, &Result);
    } else {
        MdoConfigPreview Preview;
        bool Merge = Context->Request->head->MethodCode ==
            XHTTP_METHOD_PATCH;
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        memset(&Preview, 0, sizeof(Preview));
        Preview.Size = sizeof(Preview);
        if ( !MdoApiConfigInputPreview(Domain, &Body, Merge, &Preview,
                &Document, &DocumentSize) ) {
            bool MergeUnavailable = Merge && Document == NULL &&
                Preview.Message[0] == '\0';
            xrtFree(Document);
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context,
                MergeUnavailable ? 503u : 422u,
                MergeUnavailable ? "configuration_merge_unavailable" :
                    "configuration_invalid", MergeUnavailable ?
                    "The current configuration could not be merged" :
                    (Preview.Message[0] != '\0' ? Preview.Message :
                        "The configuration document is invalid"), NULL);
        }
        Ok = MdoSettingsApply(Domain, Merge ?
            xrtStrViewN(Document, DocumentSize) :
            xrtStrViewN(Body.Document, Body.Size), ExpectedRevision, &Result);
        xrtFree(Document);
        MdoApiJsonBodyUnit(&Body);
    }
    if ( !Ok ) return MdoApiSettingsFailure(Context, &Result);
    return MdoApiSettingsResult(Context, DomainName, &Result);
}

static bool MdoApiReloadFailure(MdoApiContext* Context, cstr Resource)
{
    char Message[160];
    int Written = snprintf(Message, sizeof(Message),
        "The %s catalog could not be reloaded; the previous generation remains active",
        Resource);
    if ( Written <= 0 || (size_t)Written >= sizeof(Message) )
        return MdoApiReplyError(Context, 409u, "reload_failed",
            "The catalog could not be reloaded", NULL);
    return MdoApiReplyError(Context, 409u, "reload_failed", Message, NULL);
}

bool MdoApiModelsReloadRoute(MdoApiContext* Context)
{
    MdoModelCatalog* Catalog;
    xvalue* Data;
    bool Ok;

    if ( !MdoModelManagerReload() )
        return MdoApiReloadFailure(Context, "model");
    Catalog = MdoModelCatalogSnapshot();
    Data = xrtValueObject();
    Ok = Catalog != NULL && Data != NULL &&
        MdoApiValueSetString(Data, "resource", "models") &&
        MdoApiValueSetUInt(Data, "generation", MdoModelManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "providers",
            MdoModelCatalogProviderCount(Catalog)) &&
        MdoApiValueSetUInt(Data, "models", MdoModelCatalogModelCount(Catalog));
    MdoModelCatalogRelease(Catalog);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "reload_result_unavailable",
            "The model catalog reloaded but its result is unavailable", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiSkillsReloadRoute(MdoApiContext* Context)
{
    MdoSkillCatalog* Catalog;
    MdoSkillDiagnostics* Diagnostics;
    xvalue* Data;
    bool Ok;

    if ( !MdoSkillManagerReload() )
        return MdoApiReloadFailure(Context, "Skill");
    Catalog = MdoSkillCatalogSnapshot();
    Diagnostics = MdoSkillDiagnosticsSnapshot();
    Data = xrtValueObject();
    Ok = Catalog != NULL && Diagnostics != NULL && Data != NULL &&
        MdoApiValueSetString(Data, "resource", "skills") &&
        MdoApiValueSetUInt(Data, "generation", MdoSkillManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "items", MdoSkillCatalogCount(Catalog)) &&
        MdoApiValueSetUInt(Data, "diagnostics",
            MdoSkillDiagnosticsCount(Diagnostics));
    MdoSkillDiagnosticsRelease(Diagnostics);
    MdoSkillCatalogRelease(Catalog);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "reload_result_unavailable",
            "The Skill catalog reloaded but its result is unavailable", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}

bool MdoApiMcpReloadRoute(MdoApiContext* Context)
{
    MdoMcpCatalog* Catalog;
    MdoMcpDiagnostics* Diagnostics;
    xvalue* Data;
    bool Ok;

    if ( !MdoMcpManagerReload() )
        return MdoApiReloadFailure(Context, "MCP");
    Catalog = MdoMcpCatalogSnapshot();
    Diagnostics = MdoMcpDiagnosticsSnapshot();
    Data = xrtValueObject();
    Ok = Catalog != NULL && Diagnostics != NULL && Data != NULL &&
        MdoApiValueSetString(Data, "resource", "mcp") &&
        MdoApiValueSetUInt(Data, "generation", MdoMcpManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "servers", MdoMcpCatalogCount(Catalog)) &&
        MdoApiValueSetUInt(Data, "diagnostics",
            MdoMcpDiagnosticsCount(Diagnostics));
    MdoMcpDiagnosticsRelease(Diagnostics);
    MdoMcpCatalogRelease(Catalog);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "reload_result_unavailable",
            "The MCP catalog reloaded but its result is unavailable", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
