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

/* A decoded object is retained after its upload pin is released. This adapter
 * exercises only the model replay gate; it cannot publish a product session. */
static bool BackupDecodeFixtureReplay(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/backup-decode/replay";
    xstrview Target = Request->head->Target;
    MdoApiContext Context = {0};
    MdoSessionBackupLimits Limits;
    xllm_session* Session;
    xllm_request ModelRequest;
    xllm_session_config Config;
    xllm_file_ledger Ledger = {0};
    xwork_error Error;
    xllm_error ModelError;
    xcancel* Cancel = NULL;
    xvalue *Value, *Messages;
    bool Rendered, Released;
    size_t i, j;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    MdoSessionBackupLimitsInit(&Limits);
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-deadline") ) Limits.Deadline = 1u;
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-file") ) Limits.FileBytes = 1u;
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-files") ) Limits.Files = 1u;
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-cancel") ) {
        Cancel = xrtCancelCreate();
        if ( Cancel == NULL ) return false;
        (void)xrtCancelRequest(Cancel);
    }
    Session = MdoSessionBackupReplayModel(g_DecodeFixtureBackup, &Limits, Cancel,
        MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-null-error") ? NULL : &Error);
    xrtCancelDestroy(Cancel);
    Released = MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-release");
    if ( Released ) BackupDecodeFixtureUnit();
    xllmRequestInit(&ModelRequest);
    Rendered = Session != NULL && xllmSessionBuildRequest(Session, &ModelRequest, &ModelError);
    Value = xrtValueObject(); Messages = xrtValueArray();
    (void)MdoApiValueSetBool(Value, "ok", Session != NULL);
    (void)MdoApiValueSetBool(Value, "rendered", Rendered);
    (void)MdoApiValueSetBool(Value, "released", Released);
    if ( !MdoApiViewEqualText(Target, "/__fixture/backup-decode/replay-null-error") ) {
        (void)MdoApiValueSetUInt(Value, "code", Error.eCode);
        (void)MdoApiValueSetString(Value, "error", Error.sMessage);
    }
    (void)MdoApiValueSetBool(Value, "restore_ready", false);
    if ( Session != NULL ) {
        (void)MdoApiValueSetBool(Value, "unbound", !xllmSessionHasDriver(Session) &&
            !xllmSessionJournalPath(Session) && xllmSessionGetConfig(Session, &Config) && !Config.sSnapshotPath);
        (void)MdoApiValueSetUInt(Value, "turn", xllmSessionCurrentTurn(Session));
        (void)MdoApiValueSetUInt(Value, "last_sequence", xllmSessionLastSequence(Session));
        (void)xllmSessionGetFileLedger(Session, &Ledger);
        (void)MdoApiValueSetUInt(Value, "read_files", Ledger.iReadFileCount);
        (void)MdoApiValueSetUInt(Value, "modified_files", Ledger.iModifiedFileCount);
    }
    if ( Rendered ) for ( i = 0u; i < ModelRequest.iMessageCount; ++i ) {
        const xllm_message* Message = &ModelRequest.pMessages[i];
        xvalue *Entry = xrtValueObject(), *Calls = xrtValueArray();
        (void)MdoApiValueSetUInt(Entry, "role", Message->eRole);
        (void)MdoApiValueSetString(Entry, "content", Message->sContent != NULL ? Message->sContent : "");
        (void)MdoApiValueSetString(Entry, "reasoning", Message->sReasoningContent != NULL ? Message->sReasoningContent : "");
        (void)MdoApiValueSetString(Entry, "tool_call_id", Message->sToolCallId != NULL ? Message->sToolCallId : "");
        for ( j = 0u; j < Message->iToolCallCount; ++j ) {
            xvalue* Call = xrtValueObject();
            (void)MdoApiValueSetString(Call, "id", Message->pToolCalls[j].sId);
            (void)MdoApiValueSetString(Call, "name", Message->pToolCalls[j].sName);
            (void)MdoApiValueSetString(Call, "arguments", Message->pToolCalls[j].sArgumentsJson);
            (void)xrtValueArrayAppendNew(Calls, Call);
        }
        (void)MdoApiValueSetTake(Entry, "tool_calls", &Calls);
        (void)xrtValueArrayAppendNew(Messages, Entry);
    }
    (void)MdoApiValueSetTake(Value, "messages", &Messages);
    xllmRequestUnit(&ModelRequest); xllmSessionDestroy(Session);
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "replay-fixture");
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
    return true;
}

/* Write a few records through the locked library, using only a test-owned
 * file in the isolated Home. This checks writer/reader compatibility without
 * invoking a model or borrowing the product session's ledger. */
static bool BackupDecodeFixtureJournal(XS_HttpReq* Request)
{
    static const char Relative[] = "data/backup-journal-probe.jsonl";
    MdoApiContext Context = {0};
    xllm_session* Session = NULL;
    xllm_error Error;
    xvalue* Value;
    xfile File = NULL;
    char Buffer[16384], Extra;
    size_t Bytes = 0u, Read = 0u;
    str Native = NULL;
    bool Ok = false;
    if ( !MdoApiViewEqualText(Request->head->Target, "/__fixture/backup-decode/journal") ) return false;
    if ( !MdoHomeAtomicWrite(Relative, "", 0u, false) ) goto done;
    Native = MdoHomeExternalPath(Relative);
    Session = xllmSessionCreate(NULL, &Error);
    if ( Session == NULL || Native == NULL || !xllmSessionEnableJournal(Session, Native, &Error) ||
         xllmSessionBeginTurn(Session) != 1u ||
         !xllmSessionAddText(Session, 1u, XLLM_ROLE_USER, "Original question", 0u) ||
         !xllmSessionAddText(Session, 1u, XLLM_ROLE_ASSISTANT, "Original answer", 0u) ||
         !xllmSessionNoteFileRead(Session, "../source.c") ||
         !xllmSessionNoteFileModified(Session, "C:\\source\\file.c") ||
         xllmSessionBeginTurn(Session) != 2u ||
         !xllmSessionAddText(Session, 2u, XLLM_ROLE_USER, "Second question", 0u) ||
         !xllmSessionAddText(Session, 2u, XLLM_ROLE_ASSISTANT, "Second answer", 0u) ||
         !xllmSessionTruncateAfter(Session, 2u, &Error) || !xllmSessionClear(Session, &Error) ||
         !xllmSessionFlushJournal(Session, &Error) ) goto done;
    File = MdoHomeOpenRead(Relative);
    if ( File == NULL ) goto done;
    while ( Bytes < sizeof(Buffer) - 1u ) {
        if ( !xrtRead(File, Buffer + Bytes, sizeof(Buffer) - 1u - Bytes, &Read) ) goto done;
        if ( Read == 0u ) break;
        Bytes += Read;
    }
    Ok = Bytes > 0u && xrtRead(File, &Extra, 1u, &Read) && Read == 0u;
done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    xllmSessionDestroy(Session); xrtFree(Native);
    if ( !MdoHomeRemove(Relative, false) ) Ok = false;
    Value = xrtValueObject();
    (void)MdoApiValueSetBool(Value, "ok", Ok);
    if ( Ok ) { Buffer[Bytes] = '\0'; (void)MdoApiValueSetString(Value, "journal", Buffer); }
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "journal-writer");
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
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
    if ( BackupDecodeFixtureReplay(Request) ) return true;
    if ( BackupDecodeFixtureJournal(Request) ) return true;
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
             MdoApiViewEqualText(Target, "/__fixture/backup-decode/history-cancel") ||
             MdoApiViewEqualText(Target, "/__fixture/backup-decode/journal-cancel") ||
             MdoApiViewEqualText(Target, "/__fixture/backup-decode/snapshot-cancel") ) {
            Cancel = xrtCancelCreate();
            if ( Cancel == NULL ) return false;
            if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/cancel") )
                (void)xrtCancelRequest(Cancel);
            else if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/history-cancel") )
                g_BackupHistoryProbeCancel = Cancel;
            else if ( MdoApiViewEqualText(Target, "/__fixture/backup-decode/journal-cancel") )
                g_BackupJournalProbeCancel = Cancel;
            else g_BackupSnapshotProbeCancel = Cancel;
        }
        g_DecodeFixtureBackup = MdoSessionBackupDecode(MdoApiBackupUploadData(g_UploadFixturePin),
            MdoApiBackupUploadBytes(g_UploadFixturePin), &Limits, Cancel,
            MdoApiViewEqualText(Target, "/__fixture/backup-decode/null-error") ? NULL : &Error);
        g_BackupHistoryProbeCancel = NULL;
        g_BackupSnapshotProbeCancel = NULL;
        g_BackupJournalProbeCancel = NULL;
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
