#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/modules.h"
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

bool MdoApiSettingsPreviewRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    MdoConfigDomain Domain;
    MdoConfigPreview Preview;
    MdoConfigSnapshot Snapshot;
    cstr DomainName;
    xvalue* Data;
    bool Ok;

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
    if ( !MdoConfigPreviewImport(Domain,
            xrtStrViewN(Body.Document, Body.Size), &Preview) ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "configuration_invalid",
            Preview.Message[0] != '\0' ? Preview.Message :
                "The configuration document is invalid", NULL);
    }
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

bool MdoApiModulesReloadRoute(MdoApiContext* Context)
{
    MdoModuleCatalog* Catalog;
    MdoModuleDiagnostics* Diagnostics;
    xvalue* Data;
    bool Ok;

    if ( !MdoModuleManagerReload() )
        return MdoApiReloadFailure(Context, "module");
    Catalog = MdoModuleCatalogSnapshot();
    Diagnostics = MdoModuleDiagnosticsSnapshot();
    Data = xrtValueObject();
    Ok = Catalog != NULL && Diagnostics != NULL && Data != NULL &&
        MdoApiValueSetString(Data, "resource", "modules") &&
        MdoApiValueSetUInt(Data, "generation", MdoModuleManagerGeneration()) &&
        MdoApiValueSetUInt(Data, "modules",
            MdoModuleCatalogModuleCount(Catalog)) &&
        MdoApiValueSetUInt(Data, "tools",
            MdoModuleCatalogToolCount(Catalog)) &&
        MdoApiValueSetUInt(Data, "agents",
            MdoModuleCatalogAgentCount(Catalog)) &&
        MdoApiValueSetUInt(Data, "diagnostics",
            MdoModuleDiagnosticsCount(Diagnostics));
    MdoModuleDiagnosticsRelease(Diagnostics);
    MdoModuleCatalogRelease(Catalog);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "reload_result_unavailable",
            "The module catalog reloaded but its result is unavailable", NULL);
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
