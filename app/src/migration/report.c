#include <stdlib.h>
#include <string.h>

#include "internal.h"

static bool MdoMigrationReportTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Object != NULL && Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoMigrationReportTakeOwned(xvalue* Object, const char* Key,
    xvalue** Value)
{
    return Object != NULL && Value != NULL && *Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), Value);
}

static bool MdoMigrationReportString(xvalue* Object, const char* Key,
    const char* Text)
{
    return MdoMigrationReportTake(Object, Key,
        xrtValueString(xrtStrView(Text != NULL ? Text : "")));
}

static xvalue* MdoMigrationReportCounts(MdoMigrationContext* Context)
{
    xvalue* Counts = xrtValueObject();
    if ( Counts == NULL ||
         !MdoMigrationReportTake(Counts, "models",
            xrtValueUInt(Context->Result->ImportedModels)) ||
         !MdoMigrationReportTake(Counts, "projects",
            xrtValueUInt(Context->Result->ImportedProjects)) ||
         !MdoMigrationReportTake(Counts, "sessions",
            xrtValueUInt(Context->Result->ImportedSessions)) ||
         !MdoMigrationReportTake(Counts, "schedules",
            xrtValueUInt(Context->Result->ImportedSchedules)) ||
         !MdoMigrationReportTake(Counts, "memory_entries",
            xrtValueUInt(Context->Result->ImportedMemoryEntries)) ||
         !MdoMigrationReportTake(Counts, "skipped_items",
            xrtValueUInt(Context->Result->SkippedItems)) ||
         !MdoMigrationReportTake(Counts, "written_files",
            xrtValueUInt(Context->Result->WrittenFiles + 1u)) ) {
        xrtValueRelease(Counts);
        return NULL;
    }
    return Counts;
}

static xvalue* MdoMigrationReportFlags(MdoMigrationContext* Context)
{
    xvalue* Flags = xrtValueObject();
    if ( Flags == NULL ||
         !MdoMigrationReportTake(Flags, "legacy_system_prompt_preserved",
            xrtValueBool(Context->LegacySystemPrompt)) ||
         !MdoMigrationReportTake(Flags, "legacy_network_settings_preserved",
            xrtValueBool(Context->LegacyNetworkSettings)) ||
         !MdoMigrationReportTake(Flags, "legacy_window_settings_skipped",
            xrtValueBool(Context->LegacyWindowSettings)) ||
         !MdoMigrationReportTake(Flags, "legacy_audit_preserved_in_source",
            xrtValueBool(Context->LegacyAudit)) ||
         !MdoMigrationReportTake(Flags, "unsupported_schedules",
            xrtValueUInt(Context->UnsupportedSchedules)) ||
         !MdoMigrationReportTake(Flags, "unsupported_memory_entries",
            xrtValueUInt(Context->UnsupportedMemory)) ||
         !MdoMigrationReportTake(Flags, "unsupported_files",
            xrtValueUInt(Context->UnsupportedFiles)) ) {
        xrtValueRelease(Flags);
        return NULL;
    }
    return Flags;
}

static xvalue* MdoMigrationReportModelMap(MdoMigrationContext* Context)
{
    xvalue* Array = xrtValueArray();
    size_t i;
    if ( Array == NULL ) return NULL;
    for ( i = 0u; i < Context->ModelCount; ++i ) {
        xvalue* Item = xrtValueObject();
        if ( Item == NULL ||
             !MdoMigrationReportString(Item, "legacy_id",
                Context->Models[i].OldId) ||
             !MdoMigrationReportString(Item, "model_id",
                Context->Models[i].NewId) ||
             !xrtValueArrayAppendTake(Array, &Item) ) {
            xrtValueRelease(Item);
            xrtValueRelease(Array);
            return NULL;
        }
    }
    return Array;
}

static xvalue* MdoMigrationReportProjectMap(MdoMigrationContext* Context)
{
    xvalue* Array = xrtValueArray();
    size_t i;
    if ( Array == NULL ) return NULL;
    for ( i = 0u; i < Context->ProjectCount; ++i ) {
        xvalue* Item = xrtValueObject();
        if ( Item == NULL ||
             !MdoMigrationReportString(Item, "legacy_id",
                Context->Projects[i].OldId) ||
             !MdoMigrationReportString(Item, "project_id",
                Context->Projects[i].NewId) ||
             !MdoMigrationReportString(Item, "workspace_root",
                Context->Projects[i].WorkspaceRoot) ||
             !xrtValueArrayAppendTake(Array, &Item) ) {
            xrtValueRelease(Item);
            xrtValueRelease(Array);
            return NULL;
        }
    }
    return Array;
}

bool MdoMigrationWriteReport(MdoMigrationContext* Context,
    xwork_error* Error)
{
    xvalue* Root = xrtValueObject();
    xvalue* Counts = NULL;
    xvalue* Flags = NULL;
    xvalue* Models = NULL;
    xvalue* Projects = NULL;
    char* Json = NULL;
    size_t Size = 0u;
    size_t i;
    bool Ok = false;
    for ( i = 0u; i < Context->Scan.Count; ++i ) {
        switch ( Context->Scan.Files[i].Kind ) {
        case MDO_MIGRATION_FILE_AUDIT:
            Context->LegacyAudit = true;
            ++Context->Result->SkippedItems;
            break;
        case MDO_MIGRATION_FILE_SESSION_UI:
        case MDO_MIGRATION_FILE_OTHER:
            ++Context->UnsupportedFiles;
            ++Context->Result->SkippedItems;
            break;
        default:
            break;
        }
    }
    Counts = MdoMigrationReportCounts(Context);
    Flags = MdoMigrationReportFlags(Context);
    Models = MdoMigrationReportModelMap(Context);
    Projects = MdoMigrationReportProjectMap(Context);
    if ( Root == NULL || Counts == NULL || Flags == NULL || Models == NULL ||
         Projects == NULL ||
         !MdoMigrationReportTake(Root, "schema_version", xrtValueUInt(1u)) ||
         !MdoMigrationReportString(Root, "source_id",
            Context->Preview.SourceId) ||
         !MdoMigrationReportString(Root, "source_path",
            Context->Preview.SourcePath) ||
         !MdoMigrationReportString(Root, "target_path",
            Context->Preview.TargetPath) ||
         !MdoMigrationReportString(Root, "preview_token",
            Context->Preview.PreviewToken) ||
         !MdoMigrationReportTake(Root, "migrated_at_us",
            xrtValueInt(xrtNow())) ||
         !MdoMigrationReportTake(Root, "source_preserved",
            xrtValueBool(true)) ||
         !MdoMigrationReportTake(Root, "restart_required",
            xrtValueBool(true)) ||
         !MdoMigrationReportTakeOwned(Root, "counts", &Counts) ||
         !MdoMigrationReportTakeOwned(Root, "compatibility", &Flags) ||
         !MdoMigrationReportTakeOwned(Root, "model_mappings", &Models) ||
         !MdoMigrationReportTakeOwned(Root, "project_mappings", &Projects) )
        goto memory;
    Json = xrtJsonStringify(Root, true, &Size);
    if ( Json == NULL ) goto memory;
    if ( !MdoMigrationStageWrite(Context, "migration/report.json", Json,
            Size, 0600u, Error) ) goto done;
    Ok = true;
    goto done;
memory:
    MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot build migration report");
done:
    xrtFree(Json);
    xrtValueRelease(Projects);
    xrtValueRelease(Models);
    xrtValueRelease(Flags);
    xrtValueRelease(Counts);
    xrtValueRelease(Root);
    return Ok;
}
