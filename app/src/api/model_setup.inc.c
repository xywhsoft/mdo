/* Model configuration and newly supplied keys form one acknowledged write.
 * Fresh immutable vault entries are prepared first and removed on rollback.
 * Existing entries remain available to active clients and config backups. */
static void MdoApiModelSecretBodyUnit(MdoApiJsonBody* Body)
{
    xvalue* Keys = Body->Value ? xrtValueObjectGet(Body->Value, XRT_STR_LITERAL("keys")) : NULL;
    size_t i;
    if ( xrtValueType(Keys) == XVALUE_ARRAY ) for ( i = 0u; i < xrtValueCount(Keys); i++ ) {
        xstrview Key;
        xvalue* Item = xrtValueArrayGet(Keys, i);
        if ( xrtValueGetString(xrtValueObjectGet(Item, XRT_STR_LITERAL("value")), &Key) )
            xrtSecureZero((void*)Key.Data, Key.Size);
    }
    if ( Body->Document ) xrtSecureZero(Body->Document, Body->Size);
    MdoApiJsonBodyUnit(Body);
}

static cstr MdoApiModelText(const xvalue* Object, cstr Name, size_t Limit)
{
    xstrview Text;
    if ( xrtValueType(Object) != XVALUE_OBJECT ||
         !xrtValueGetString(xrtValueObjectGet(Object, xrtStrView(Name)), &Text) ||
         !Text.Size || Text.Size > Limit || memchr(Text.Data, 0, Text.Size) ) return NULL;
    return Text.Data;
}

bool MdoApiModelSetupRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body = {0}; MdoSettingsResult Result = {0};
    MdoConfigSnapshot Snapshot = {0}; MdoConfigPreview Preview = {0};
    MdoApiBodyStatus Status; uint64 Revision = 0u;
    xvalue* Document = NULL; xvalue* Patch; xvalue* Providers; xvalue* Keys;
    char References[128][MDO_SECRET_VAULT_REFERENCE_CAPACITY] = {{0}};
    size_t i, j, Count = 0u, Created = 0u, Bytes = 0u;
    str Json = NULL; bool Ok = false, Valid = true;
    MdoApiPreconditionStatus Precondition = MdoApiExpectedRevision(Context, &Revision);
    if ( Precondition != MDO_API_PRECONDITION_OK )
        return MdoApiReplyError(Context, Precondition == MDO_API_PRECONDITION_MISSING ? 428u : 400u,
            "precondition_required", "Refresh the model settings before saving", NULL);
    Status = MdoApiJsonBodyRead(Context, &Body);
    if ( Status != MDO_API_BODY_OK ) {
        MdoApiModelSecretBodyUnit(&Body);
        return MdoApiReplyBodyError(Context, Status);
    }
    Snapshot.Size = sizeof(Snapshot); Result.Size = sizeof(Result); Preview.Size = sizeof(Preview);
    if ( !MdoConfigGetSnapshot(&Snapshot) || Snapshot.RuntimeOverride || Snapshot.Revision != Revision ) {
        MdoApiModelSecretBodyUnit(&Body);
        return MdoApiReplyError(Context, Snapshot.Revision != Revision ? 412u : 409u,
            Snapshot.Revision != Revision ? "revision_conflict" : "runtime_override",
            "Model settings changed or contain startup overrides; refresh before saving", NULL);
    }
    Keys = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("keys"));
    Patch = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("patch"));
    if ( xrtValueType(Body.Value) != XVALUE_OBJECT || xrtValueCount(Body.Value) != 2u ||
         xrtValueType(Patch) != XVALUE_OBJECT || xrtValueType(Keys) != XVALUE_ARRAY ||
         (Count = xrtValueCount(Keys)) > 128u ) goto invalid;
    Document = xrtValueObject();
    if ( !Document || !MdoApiValueSetUInt(Document, "schema_version", 1u) ||
         !xrtValueObjectSetNew(Document, XRT_STR_LITERAL("patch"), xrtValueDeepClone(Patch)) ) goto done;
    Patch = xrtValueObjectGet(Document, XRT_STR_LITERAL("patch"));
    Providers = xrtValueObjectGet(Patch, XRT_STR_LITERAL("providers"));
    if ( xrtValueType(Providers) != XVALUE_ARRAY ) goto invalid;
    /* Validate all keys and descriptors before creating any credential files. */
    for ( i = 0u; i < Count; i++ ) {
        xvalue* Item = xrtValueArrayGet(Keys, i); xvalue* Provider = NULL;
        cstr Id = MdoApiModelText(Item, "provider", 128u);
        cstr Value = MdoApiModelText(Item, "value", 4096u);
        if ( !Id || !Value || xrtValueCount(Item) != 2u ) goto invalid;
        for ( j = 0u; Value[j]; j++ ) if ((unsigned char)Value[j] < 33u || (unsigned char)Value[j] > 126u) goto invalid;
        for ( j = 0u; j < i; j++ ) if (strcmp(Id, MdoApiModelText(xrtValueArrayGet(Keys, j), "provider", 128u)) == 0) goto invalid;
        for ( j = 0u; j < xrtValueCount(Providers); j++ ) {
            xvalue* Candidate = xrtValueArrayGet(Providers, j);
            cstr CandidateId = MdoApiModelText(Candidate, "id", 128u);
            if ( CandidateId && strcmp(Id, CandidateId) == 0 ) { Provider = Candidate; break; }
        }
        bool Builtin = true;
        if ( !Provider || !xrtValueGetBool(xrtValueObjectGet(Provider, XRT_STR_LITERAL("builtin")), &Builtin) || Builtin ) goto invalid;
        xvalue* Credential = xrtValueObject();
        if (!Credential || !MdoApiValueSetString(Credential, "secret_ref", "vault:0000000000000000000000000000000000000000000000000000000000000000") ||
            !xrtValueObjectSetTake(Provider, XRT_STR_LITERAL("credential"), &Credential)) { xrtValueRelease(Credential); goto done; }
    }
    Json = xrtJsonStringify(Document, false, &Bytes);
    if ( !Json || !MdoConfigPreviewImport(MDO_CONFIG_MODELS, xrtStrViewN(Json, Bytes), &Preview) ) goto invalid;
    xrtFree(Json); Json = NULL;
    for ( i = 0u; i < Count; i++ ) {
        cstr Id = MdoApiModelText(xrtValueArrayGet(Keys, i), "provider", 128u);
        if ( !MdoSecretStore(MdoApiModelText(xrtValueArrayGet(Keys, i), "value", 4096u), References[i]) ) goto done;
        Created++;
        for ( j = 0u; j < xrtValueCount(Providers); j++ ) {
            xvalue* Provider = xrtValueArrayGet(Providers, j);
            if ( strcmp(Id, MdoApiModelText(Provider, "id", 128u)) == 0 ) {
                xvalue* Credential = xrtValueObjectGet(Provider, XRT_STR_LITERAL("credential"));
                if ( !MdoApiValueSetString(Credential, "secret_ref", References[i]) ) goto done;
                break;
            }
        }
    }
    Json = xrtJsonStringify(Document, false, &Bytes);
    if ( Json ) Ok = MdoSettingsApply(MDO_CONFIG_MODELS, xrtStrViewN(Json, Bytes), Revision, &Result);
    goto done;
invalid:
    Valid = false;
done:
    if ( !Ok && Result.Status != MDO_SETTINGS_STATUS_ROLLBACK_FAILED )
        for ( i = 0u; i < Created; i++ ) (void)MdoSecretDiscard(References[i]);
    xrtFree(Json); xrtValueRelease(Document); MdoApiModelSecretBodyUnit(&Body); xrtClearError();
    if ( Ok ) return MdoApiSettingsResult(Context, "models", &Result);
    if ( !Valid ) return MdoApiReplyError(Context, 422u, "configuration_invalid",
        Preview.Message[0] ? Preview.Message : "Invalid supplier configuration or API key", NULL);
    if ( Result.Status != MDO_SETTINGS_STATUS_OK ) return MdoApiSettingsFailure(Context, &Result);
    return MdoApiReplyError(Context, 500u, "credential_save_failed", "API key could not be saved; the previous configuration remains active", NULL);
}
