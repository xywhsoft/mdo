/* Validate an unpublished profile with the production transport, then commit
 * only that profile. Failed/cancelled probes never install model or key files. */
static bool MdoApiModelAddRun(MdoApiContext* Context, MdoApiJsonBody Body, uint64 Revision)
{
    MdoConfigSnapshot Snapshot = {0}; MdoConfigPreview Preview = {0};
    MdoSettingsResult Result = {0}; MdoSettingsServiceSnapshot Settings = {0};
    MdoModelCatalog* Catalog = NULL; MdoModelInfo Info = {0}; xllm_error Error = {0};
    xvalue *Root = NULL, *Document = NULL, *Patch = NULL, *Provider = NULL, *Data = NULL, *Evidence = NULL;
    char *Json = NULL; size_t Bytes = 0u; uint64 ReplyMs = 0u, ToolMs = 0u;
    char Reference[MDO_SECRET_VAULT_REFERENCE_CAPACITY] = {0}, Fingerprint[65], Current[65], FailureCode[96];
    bool Committed = false, MakeDefault = false, NeedsTools = false, EvidenceSaved = false;
    cstr Code = "configuration_invalid", Message = "Invalid model draft"; uint16 Status = 422u;
    const xvalue* InputProvider = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("provider"));
    const xvalue* Model = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("model"));
    const xvalue* KeyValue = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("key"));
    cstr Key = MdoApiModelText(Body.Value, "key", 4096u);
    cstr Id = MdoApiModelText(Model, "id", 128u), ProviderId = MdoApiModelText(InputProvider, "id", 128u);
    Snapshot.Size = sizeof(Snapshot); Preview.Size = sizeof(Preview); Result.Size = sizeof(Result);
    Settings.Size = sizeof(Settings); Info.Size = sizeof(Info);
    if (!Id || !ProviderId || xrtValueType(Body.Value) != XVALUE_OBJECT || xrtValueCount(Body.Value) > 4u ||
        (KeyValue && !Key)) goto done;
    const xvalue* DefaultValue = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("make_default"));
    if (DefaultValue && !xrtValueGetBool(DefaultValue, &MakeDefault)) goto done;
    if (xrtValueCount(Body.Value) != 2u + (KeyValue != NULL) + (DefaultValue != NULL)) goto done;
    if (Key) for (size_t n = 0u; Key[n]; ++n) if ((unsigned char)Key[n] < 33u || (unsigned char)Key[n] > 126u) goto done;
    if (!MdoSettingsServiceGetSnapshot(&Settings) || !Settings.Initialized || Settings.Degraded ||
        !MdoConfigGetSnapshot(&Snapshot) || Snapshot.RuntimeOverride || Snapshot.Revision != Revision) {
        Code = "revision_conflict"; Message = "Model settings changed; refresh before adding"; Status = 412u; goto done;
    }
    Json = MdoConfigEffectiveJson(&Bytes);
    if (Json) Root = xrtJsonParse(xrtStrViewN(Json, Bytes));
    xrtFree(Json); Json = NULL;
    Patch = Root ? xrtValueDeepClone(xrtValueObjectGet(Root, XRT_STR_LITERAL("models"))) : NULL;
    Document = xrtValueObject();
    if (!Patch || !Document || !MdoApiValueSetUInt(Document, "schema_version", 1u) ||
        !MdoApiValueSetTake(Document, "patch", &Patch)) { xrtValueRelease(Patch); Patch = NULL; goto done; }
    Patch = xrtValueObjectGet(Document, XRT_STR_LITERAL("patch"));
    xvalue* Providers = xrtValueObjectGet(Patch, XRT_STR_LITERAL("providers"));
    xvalue* Items = xrtValueObjectGet(Patch, XRT_STR_LITERAL("items"));
    if (MdoModelVerificationFind(Items, Id)) {
        Code = "model_already_exists"; Message = "This model is already configured"; Status = 409u; goto done;
    }
    Provider = (xvalue*)MdoModelVerificationFind(Providers, ProviderId);
    if (Provider) {
        if (Key || !xrtValueEqual(Provider, InputProvider)) {
            Code = "provider_config_changed"; Message = "Use the provider editor to change an existing connection"; Status = 409u; goto done;
        }
    } else {
        Provider = xrtValueDeepClone(InputProvider);
        if (!Provider || !xrtValueArrayAppend(Providers, Provider)) { xrtValueRelease(Provider); Provider = NULL; goto done; }
        xrtValueRelease(Provider); Provider = (xvalue*)MdoModelVerificationFind(Providers, ProviderId);
        if (Key) {
            xvalue* Credential = xrtValueObject();
            bool Ok = Credential && MdoApiValueSetString(Credential, "secret_ref",
                "vault:0000000000000000000000000000000000000000000000000000000000000000") &&
                MdoApiValueSetTake(Provider, "credential", &Credential);
            xrtValueRelease(Credential); if (!Ok) goto done;
        }
    }
    if (!xrtValueArrayAppend(Items, (xvalue*)Model)) goto done;
    Json = xrtJsonStringify(Document, false, &Bytes);
    if (!Json || !MdoConfigPreviewImport(MDO_CONFIG_MODELS, xrtStrViewN(Json, Bytes), &Preview)) {
        if (Preview.Message[0]) Message = Preview.Message;
        goto done;
    }
    xrtFree(Json); Json = NULL;
    Catalog = MdoModelCatalogDraft(Provider, Model, Key);
    if (!Catalog || !MdoModelCatalogModelFind(Catalog, Id, &Info)) goto done;
    const xvalue* Transport = xrtValueObjectGet(xrtValueObjectGet(Root, XRT_STR_LITERAL("settings")), XRT_STR_LITERAL("transport"));
    if (!MdoModelVerificationKey(Patch, Transport, Id, Fingerprint)) goto done;
    NeedsTools = (Info.Capabilities & XLLM_CAP_TOOL_CALL_OUT) != 0u;
    bool Ok = MdoApiModelProbeExecute(Context, Catalog, Id, Info.MaxOutputTokens, false, &ReplyMs, &Error);
    bool ToolPhase = Ok && NeedsTools;
    if (ToolPhase && MdoApiDownloadLive(Context))
        Ok = MdoApiModelProbeExecute(Context, Catalog, Id, Info.MaxOutputTokens, true, &ToolMs, &Error);
    else if (ToolPhase) Ok = false;
    if (!Ok) {
        static char const ReplyFailure[] = "model_test_failed", ToolFailure[] = "model_tools_test_failed";
        Code = ToolPhase ? ToolFailure : ReplyFailure;
        Message = Error.eCode == XLLM_ERROR_NONE ? (ToolPhase ? "The tool response did not pass verification" : "The reply did not pass verification") : MdoModelErrorMessage(&Error);
        if (Error.eCode != XLLM_ERROR_NONE) { snprintf(FailureCode, sizeof(FailureCode), "model_%s", MdoModelErrorKind(&Error)); Code = FailureCode; }
        goto done;
    }
    if (!MdoApiDownloadLive(Context)) { Code = "model_verification_cancelled"; Message = "Verification stopped before saving"; Status = 409u; goto done; }
    if (!MdoConfigGetSnapshot(&Snapshot) || Snapshot.Revision != Revision ||
        !MdoModelVerificationKey(Patch, Transport, Id, Current) || strcmp(Current, Fingerprint)) {
        Code = "model_config_changed"; Message = "Configuration changed during verification; review before retrying"; Status = 409u; goto done;
    }
    if (Key) {
        if (!MdoSecretStore(Key, Reference)) { Code = "credential_save_failed"; Message = "Could not save the verified connection key"; Status = 500u; goto done; }
        if (!MdoApiValueSetString(xrtValueObjectGet(Provider, XRT_STR_LITERAL("credential")), "secret_ref", Reference)) goto done;
    }
    if (MakeDefault && NeedsTools && !MdoApiValueSetString(Patch, "default_model", Id)) goto done;
    Json = xrtJsonStringify(Document, false, &Bytes);
    if (!Json || !MdoSettingsApply(MDO_CONFIG_MODELS, xrtStrViewN(Json, Bytes), Revision, &Result)) {
        Code = "model_save_failed"; Message = Result.Message[0] ? Result.Message : "Could not save the verified model";
        Status = Result.Status == MDO_SETTINGS_STATUS_CONFLICT ? 412u : 500u; goto done;
    }
    Committed = true;
    MdoApiLiveChanged(NULL);
    if (MdoModelVerificationKey(Patch, Transport, Id, Fingerprint) && MdoApiModelVerificationSnapshot(Id, Current) && !strcmp(Fingerprint, Current)) {
        Evidence = MdoApiModelVerificationUpdate(Id, Fingerprint, false, NeedsTools, "passed", ReplyMs, "");
        if (Evidence && NeedsTools) { xrtValueRelease(Evidence); Evidence = MdoApiModelVerificationUpdate(Id, Fingerprint, true, true, "passed", ToolMs, ""); }
        EvidenceSaved = Evidence != NULL;
    }
    xrtValueRelease(Data); Data = xrtValueObject();
    Ok = Data && MdoApiValueSetBool(Data, "added", true) && MdoApiValueSetBool(Data, "verified", true) &&
        MdoApiValueSetBool(Data, "agent_ready", NeedsTools) && MdoApiValueSetBool(Data, "verification_saved", EvidenceSaved) &&
        MdoApiValueSetString(Data, "model_id", Id) && MdoApiValueSetUInt(Data, "revision", Result.Revision) &&
        (!Evidence || xrtValueObjectSet(Data, XRT_STR_LITERAL("verification"), Evidence));
    if (!Ok) { Code = "model_result_unavailable"; Message = "Model was added; refresh the model list"; Status = 500u; }
done:
    if (!Committed && Reference[0] && Result.Status != MDO_SETTINGS_STATUS_ROLLBACK_FAILED) (void)MdoSecretDiscard(Reference);
    MdoModelCatalogRelease(Catalog); xrtFree(Json); xrtValueRelease(Evidence); xrtValueRelease(Document); xrtValueRelease(Root); xrtClearError();
    if (Committed && Data && xrtValueObjectHas(Data, XRT_STR_LITERAL("added"))) return MdoApiReplySuccessTakeRevision(Context, 201u, Data, Result.Revision);
    xrtValueRelease(Data); return MdoApiReplyError(Context, Status, Code, Message, NULL);
}
