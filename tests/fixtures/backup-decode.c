/* Test-only adapter to the sealed reader. The production decoder must run on
 * a bounded worker; this isolated probe exposes no production API route. */
static MdoSessionBackup* g_DecodeFixtureBackup;

static void BackupDecodeFixtureUnit(void)
{
    MdoSessionBackupRelease(g_DecodeFixtureBackup); g_DecodeFixtureBackup = NULL;
}

static bool BackupDecodeFixturePreviewSizeSafe(void)
{
    uint32 Words[2] = {sizeof(uint32), UINT32_C(0x12345678)};
    void* Small = xrtMalloc(sizeof(Words));
    bool Ok;
    if ( Small == NULL ) return false;
    memcpy(Small, Words, sizeof(Words));
    Ok = !MdoSessionBackupPreviewGet(g_DecodeFixtureBackup, (MdoSessionBackupPreview*)Small) &&
        memcmp(Small, Words, sizeof(Words)) == 0;
    xrtFree(Small);
    return Ok;
}

static bool BackupDecodeFixtureSeed(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/backup-decode/seed/";
    xstrview Target = Request->head->Target;
    MdoSessionRuntimeOptions Options;
    MdoSession* Session;
    xwork_event Event = {0};
    xwork_error Error;
    MdoApiContext Context = {0};
    char Id[33], Relative[256];
    str Artifact;
    bool Ok;
    if ( Target.Size != sizeof(Prefix) - 1u + 32u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    memcpy(Id, Target.Data + sizeof(Prefix) - 1u, 32u); Id[32] = '\0';
    MdoSessionRuntimeOptionsInit(&Options);
    Session = MdoSessionOpen("default", Id, &Options, &Error);
    snprintf(Relative, sizeof(Relative), "sessions/default/%s/artifacts/run-00000000000000000001/00000000000000000001-decode.txt", Id);
    Artifact = MdoHomeExternalPath(Relative);
    Event.eKind = XWORK_EVENT_TOOL_DONE; Event.bSuccess = true; Event.uRunId = 1u;
    Event.sToolName = "mdo.todo"; Event.sText = "{\"items\":[{\"text\":\"Offline schema fixture\",\"done\":false}]}";
    Event.iTextLength = strlen(Event.sText); Event.uArtifactId = 1u; Event.sArtifactPath = Artifact;
    Ok = Session != NULL && Artifact != NULL && MdoSessionEventBridgeOnEvent(Session->Bridge, &Event);
    Event.sText = "{\"items\":[{\"text\":\"Offline schema fixture\",\"done\":true}]}";
    Event.iTextLength = strlen(Event.sText);
    Ok = Ok && MdoSessionEventBridgeOnEvent(Session->Bridge, &Event);
    xrtFree(Artifact); MdoSessionRelease(Session);
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "decode-seed");
    (void)MdoApiReplySuccessTake(&Context, 200u, xrtValueBool(Ok), NULL);
    return true;
}

static bool BackupDecodeFixtureControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/backup-decode/";
    xstrview Target = Request->head->Target;
    MdoApiContext Context = {0};
    MdoSessionBackupLimits Limits;
    MdoSessionBackupPreview Preview = {0};
    xwork_error Error;
    xvalue *Value, *Files;
    xcancel* Cancel = NULL;
    size_t i;
    bool Encodable = false;
    if ( BackupDecodeFixtureSeed(Request) ) return true;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "decode-fixture");
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/kinds") ) {
        Value = xrtValueObject();
        (void)MdoApiValueSetUInt(Value, "start", XWORK_EVENT_AGENT_START);
        (void)MdoApiValueSetUInt(Value, "model", XWORK_EVENT_MODEL_DONE);
        (void)MdoApiValueSetUInt(Value, "removed", MDO_SESSION_EVENT_HISTORY_TRUNCATED);
        (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
        return true;
    }
    xworkErrorInit(&Error); MdoSessionBackupLimitsInit(&Limits);
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/release") ) BackupDecodeFixtureUnit();
    else if ( !MdoApiViewEqualText(Target, "/__fixture/backup-decode/state") ) {
        BackupDecodeFixtureUnit();
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/files") ) Limits.Files = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/file") ) Limits.FileBytes = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/total") ) Limits.TotalBytes = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/document") ) Limits.DocumentBytes = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/deadline") ) Limits.Deadline = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/budget") ) Limits.Files += 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/cancel") ||
             MdoApiViewEqualText(Target, "/__fixture/backup-decode/history-cancel") ) {
            Cancel = xrtCancelCreate();
            if ( Cancel == NULL ) return false;
            if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/cancel") )
                (void)xrtCancelRequest(Cancel);
            else g_BackupHistoryProbeCancel = Cancel;
        }
        g_DecodeFixtureBackup = MdoSessionBackupDecode(MdoApiBackupUploadData(g_UploadFixturePin),
            MdoApiBackupUploadBytes(g_UploadFixturePin), &Limits, Cancel,
            MdoApiViewEqualText(Target, "/__fixture/backup-decode/null-error") ? NULL : &Error);
        g_BackupHistoryProbeCancel = NULL;
        xrtCancelDestroy(Cancel);
    }
    Preview.Size = sizeof(Preview);
    (void)MdoSessionBackupPreviewGet(g_DecodeFixtureBackup, &Preview);
    if ( g_DecodeFixtureBackup ) {
        size_t Size = 999u;
        str Encoded = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &Size, NULL);
        Encodable = Encoded != NULL; xrtFree(Encoded);
        if ( Preview.ExportSchema == 1u && Size != 0u ) return false;
    }
    Value = xrtValueObject(); Files = xrtValueArray();
    (void)MdoApiValueSetBool(Value, "ok", g_DecodeFixtureBackup != NULL);
    (void)MdoApiValueSetUInt(Value, "code", Error.eCode);
    (void)MdoApiValueSetString(Value, "error", Error.sMessage);
    (void)MdoApiValueSetBool(Value, "encodable", Encodable);
    (void)MdoApiValueSetBool(Value, "restore_ready", false);
    (void)MdoApiValueSetBool(Value, "preview_size_safe", BackupDecodeFixturePreviewSizeSafe());
    (void)MdoApiValueSetUInt(Value, "schema", Preview.ExportSchema);
    (void)MdoApiValueSetUInt(Value, "bytes", Preview.Bytes);
    (void)MdoApiValueSetUInt(Value, "ui_first", Preview.UiFirstEventId);
    (void)MdoApiValueSetUInt(Value, "ui_last", Preview.UiLastEventId);
    (void)MdoApiValueSetUInt(Value, "ui_records", Preview.UiRecords);
    (void)MdoApiValueSetUInt(Value, "unverified_refs", Preview.UnverifiedHistoryReferences);
    (void)MdoApiValueSetUInt(Value, "removed_refs", Preview.RemovedHistoryReferences);
    (void)MdoApiValueSetString(Value, "project_id", Preview.Info.ProjectId);
    (void)MdoApiValueSetString(Value, "session_id", Preview.Info.Id);
    (void)MdoApiValueSetString(Value, "model_id", Preview.Info.ModelId);
    (void)MdoApiValueSetString(Value, "title", Preview.Info.Title);
    for ( i = 0u; i < MdoSessionBackupFileCount(g_DecodeFixtureBackup); ++i ) {
        MdoSessionBackupFile File;
        uint8 Digest[XRT_SHA256_SIZE]; char Hash[65];
        xvalue* Entry = xrtValueObject();
        if ( !MdoSessionBackupFileGet(g_DecodeFixtureBackup, i, &File) ||
             !xrtSha256(File.Data, File.Bytes, Digest) ) { xrtValueRelease(Entry); break; }
        MdoUploadHash(Digest, Hash);
        (void)MdoApiValueSetString(Entry, "path", File.Path);
        (void)MdoApiValueSetUInt(Entry, "bytes", File.Bytes);
        (void)MdoApiValueSetString(Entry, "sha256", Hash);
        (void)xrtValueArrayAppendNew(Files, Entry);
    }
    (void)MdoApiValueSetTake(Value, "files", &Files);
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
    return true;
}
