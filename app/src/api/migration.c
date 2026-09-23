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

bool MdoApiLegacyMigrationsRoute(MdoApiContext* Context)
{
    MdoMigrationPreview Previews[2];
    xwork_error Error;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Count = 0u;
    size_t Index;
    bool Ok;
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
