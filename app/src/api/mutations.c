#include <string.h>

#include "internal.h"
#include "../../include/mdo/config.h"

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
