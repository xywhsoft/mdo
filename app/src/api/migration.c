#include <string.h>

#include "internal.h"
#include "../../include/mdo/migration.h"

static xvalue* MdoApiMigrationPreviewValue(
    const MdoMigrationPreview* Preview)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetString(Item, "source_id", Preview->SourceId) &&
        MdoApiValueSetString(Item, "source_path", Preview->SourcePath) &&
        MdoApiValueSetString(Item, "target_path", Preview->TargetPath) &&
        MdoApiValueSetBool(Item, "found", Preview->Found) &&
        MdoApiValueSetBool(Item, "valid", Preview->Valid) &&
        MdoApiValueSetBool(Item, "importable", Preview->Importable) &&
        MdoApiValueSetBool(Item, "target_available",
            Preview->TargetAvailable) &&
        MdoApiValueSetUInt(Item, "file_count", Preview->FileCount) &&
        MdoApiValueSetUInt(Item, "project_count", Preview->ProjectCount) &&
        MdoApiValueSetUInt(Item, "session_count", Preview->SessionCount) &&
        MdoApiValueSetUInt(Item, "model_count", Preview->ModelCount) &&
        MdoApiValueSetUInt(Item, "schedule_count", Preview->ScheduleCount) &&
        MdoApiValueSetUInt(Item, "memory_file_count",
            Preview->MemoryFileCount) &&
        MdoApiValueSetUInt(Item, "unsupported_count",
            Preview->UnsupportedCount) &&
        MdoApiValueSetUInt(Item, "conflict_count", Preview->ConflictCount) &&
        MdoApiValueSetUInt(Item, "estimated_write_count",
            Preview->EstimatedWriteCount) &&
        MdoApiValueSetUInt(Item, "total_bytes", Preview->TotalBytes) &&
        MdoApiValueSetString(Item, "preview_token", Preview->PreviewToken) &&
        MdoApiValueSetString(Item, "message", Preview->Message);
    if ( !Ok ) {
        xrtValueRelease(Item);
        return NULL;
    }
    return Item;
}

static bool MdoApiMigrationText(const xvalue* Object, const char* Key,
    char* Output, size_t Capacity)
{
    const xvalue* Value = Object != NULL &&
        xrtValueType(Object) == XVALUE_OBJECT ?
        xrtValueObjectGet(Object, xrtStrView(Key)) : NULL;
    xstrview Text;
    if ( Value == NULL || xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) || Text.Size == 0u ||
         Text.Size >= Capacity || memchr(Text.Data, '\0', Text.Size) != NULL ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

static bool MdoApiLegacyMigrationApplyRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoMigrationApplyOptions Options;
    MdoMigrationApplyResult Result;
    xwork_error Error;
    char SourceId[MDO_MIGRATION_SOURCE_ID_CAPACITY];
    char Token[MDO_MIGRATION_TOKEN_CAPACITY];
    xvalue* Data;
    bool Ok;
    BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    if ( BodyStatus != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, BodyStatus);
    Ok = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == 2u &&
        MdoApiMigrationText(Body.Value, "source_id", SourceId,
            sizeof(SourceId)) &&
        MdoApiMigrationText(Body.Value, "preview_token", Token,
            sizeof(Token));
    MdoApiJsonBodyUnit(&Body);
    if ( !Ok ) return MdoApiReplyError(Context, 422u,
        "migration_request_invalid",
        "Migration requires exactly source_id and preview_token", NULL);
    MdoMigrationApplyOptionsInit(&Options);
    Options.SourceId = SourceId;
    Options.PreviewToken = Token;
    memset(&Result, 0, sizeof(Result));
    Result.Size = sizeof(Result);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoLegacyMigrationApply(&Options, &Result, &Error) ) {
        uint16 Status = Error.eCode == XWORK_ERROR_CONTEXT ? 409u :
            (Error.eCode == XWORK_ERROR_INVALID_ARGUMENT ? 422u : 500u);
        return MdoApiReplyError(Context, Status,
            Status == 409u ? "migration_conflict" :
            (Status == 422u ? "migration_invalid" : "migration_failed"),
            Error.sMessage[0] != '\0' ? Error.sMessage :
                "Legacy migration could not be completed", NULL);
    }
    Data = xrtValueObject();
    Ok = Data != NULL &&
        MdoApiValueSetBool(Data, "restart_required",
            Result.RestartRequired) &&
        MdoApiValueSetString(Data, "target_path", Result.TargetPath) &&
        MdoApiValueSetString(Data, "report_path", Result.ReportPath) &&
        MdoApiValueSetUInt(Data, "imported_models",
            Result.ImportedModels) &&
        MdoApiValueSetUInt(Data, "imported_projects",
            Result.ImportedProjects) &&
        MdoApiValueSetUInt(Data, "imported_sessions",
            Result.ImportedSessions) &&
        MdoApiValueSetUInt(Data, "imported_schedules",
            Result.ImportedSchedules) &&
        MdoApiValueSetUInt(Data, "imported_memory_entries",
            Result.ImportedMemoryEntries) &&
        MdoApiValueSetUInt(Data, "skipped_items", Result.SkippedItems) &&
        MdoApiValueSetUInt(Data, "written_files", Result.WrittenFiles) &&
        MdoApiValueSetUInt(Data, "written_bytes", Result.WrittenBytes);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u,
            "migration_result_unavailable",
            "Migration completed but its result could not be serialized",
            NULL);
    }
    return MdoApiReplySuccessTake(Context, 201u, Data, NULL);
}

bool MdoApiLegacyMigrationsRoute(MdoApiContext* Context)
{
    MdoMigrationPreview Previews[2];
    xwork_error Error;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count = 0u;
    size_t Index;
    bool Ok;
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_POST )
        return MdoApiLegacyMigrationApplyRoute(Context);
    memset(Previews, 0, sizeof(Previews));
    Previews[0].Size = sizeof(Previews[0]);
    Previews[1].Size = sizeof(Previews[1]);
    memset(&Error, 0, sizeof(Error));
    Ok = Data != NULL && Items != NULL &&
        MdoLegacyMigrationDiscover(Previews,
            sizeof(Previews) / sizeof(Previews[0]), &Count, &Error);
    for ( Index = 0u; Ok && Index < Count; ++Index ) {
        xvalue* Item = MdoApiMigrationPreviewValue(&Previews[Index]);
        Ok = Item != NULL && MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = MdoApiValueSetUInt(Data, "count", Count) &&
        MdoApiValueSetBool(Data, "requires_confirmation", true) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "migration_preview_failed",
            Error.sMessage[0] != '\0' ? Error.sMessage :
            "Legacy migration preview could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
