/* Test-only staging adapter. Product has no staging HTTP route yet. */
static MdoSessionBackupStage* g_StageFixture;
static unsigned g_StageFixtureFault;
static xcancel* g_StageFixtureCancel;
static unsigned g_ReviewFixtureFault;
static unsigned g_RestoreFixtureFault;

void BackupRestoreFixtureAfterUi(void)
{
    if ( g_RestoreFixtureFault == 1u ) (void)xrtCancelRequest(g_StageFixtureCancel);
}

void BackupRestoreFixtureOriginHash(void)
{
    if ( g_RestoreFixtureFault == 2u ) (void)xrtCancelRequest(g_StageFixtureCancel);
}

static bool BackupRestoreFixtureArtifact(const MdoSessionEventInfo* Event, void* Data)
{
    const MdoSessionInfo* Info = (const MdoSessionInfo*)Data;
    char Actual[MDO_SESSION_PATH_CAPACITY], Relative[MDO_SESSION_BACKUP_PATH_CAPACITY], Expected[512];
    if ( Event->ArtifactPath[0] == '\0' ) return true;
    snprintf(Expected, sizeof(Expected), "sessions/%s/%s/%s", Info->ProjectId, Info->Id,
        MdoBackupArtifactRelative(xrtStrView(Event->ArtifactPath), Relative) ? Relative : "invalid");
    return MdoApiSessionArtifactPath(Actual, Info->ProjectId, Info->Id, Event) && strcmp(Actual, Expected) == 0;
}

void BackupReviewFixtureCandidate(str* Candidate, size_t Attempt)
{
    if ( g_ReviewFixtureFault == 1u && Attempt == 0u ) (void)xrtCancelRequest(g_StageFixtureCancel);
    if ( g_ReviewFixtureFault == 2u ) {
        xrtFree(*Candidate); *Candidate = xrtStrDup("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    }
}

void BackupReviewFixtureArchiveHash(void)
{
    if ( g_ReviewFixtureFault == 3u ) (void)xrtCancelRequest(g_StageFixtureCancel);
}

void BackupStageFixtureAfterWrite(const char* Path, size_t Offset)
{
    if ( g_StageFixtureFault == 3u && strncmp(Path, "artifacts/", 10u) == 0 && Offset >= 65536u )
        (void)xrtCancelRequest(g_StageFixtureCancel);
}

void BackupStageFixtureBeforeVerify(MdoSessionBackupStage* Stage)
{
    xfileoptions Options;
    xfile File;
    const char* Path = "unowned.txt";
    size_t i;
    if ( g_StageFixtureFault != 1u && g_StageFixtureFault != 2u ) return;
    if ( g_StageFixtureFault == 1u ) {
        for ( i = 0u; i < Stage->Count; ++i )
            if ( strncmp(Stage->Files[i].Path, "artifacts/", 10u) == 0 ) { Path = Stage->Files[i].Path; break; }
    }
    xrtFileOptionsInit(&Options); Options.Flags = XFILE_WRITE | XFILE_NOFOLLOW;
    if ( g_StageFixtureFault == 2u ) Options.Flags |= XFILE_CREATE | XFILE_EXCLUSIVE;
    Options.Mode = 0600u;
    File = xrtRootFileOpen(Stage->Directory, Path, &Options);
    if ( File ) { (void)xrtWriteFull(File, "!", 1u, NULL); (void)xrtFlush(File); (void)xrtClose(File); }
}

static bool BackupStageFixtureControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/backup-stage/";
    xstrview Target = Request->head->Target;
    MdoApiContext Context = {0};
    MdoSessionBackupLimits Limits;
    MdoSessionBackupStageInfo Info = {0};
    MdoSessionBackupProjectionRepair Repair = {0};
    MdoSessionBackupInputs Inputs = {0};
    MdoSessionBackupRestoreInfo Restore = {0};
    xwork_error Error;
    xvalue* Value;
    xroot Parent = NULL;
    xcancel* Cancel = NULL;
    bool Ok = true, SizeSafe, OriginalUnchanged = true, ArtifactReaderPaths = true;
    uint32 Words[2] = { sizeof(uint32), UINT32_C(0x87654321) };
    void* Small;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/export") ) {
        size_t Size = 0u;
        str Json = g_StageFixture != NULL ? MdoSessionBackupEncode(g_StageFixture->Bytes, NULL, &Size, NULL) : NULL;
        MdoSessionBackup* Decoded = Json != NULL ? MdoSessionBackupDecode(Json, Size, NULL, NULL, NULL) : NULL;
        size_t i;
        bool Equal = Decoded != NULL && MdoSessionBackupFileCount(Decoded) == MdoSessionBackupFileCount(g_StageFixture->Bytes);
        xrtFree(Json);
        for ( i = 0u; Equal && i < MdoSessionBackupFileCount(Decoded); ++i ) {
            MdoSessionBackupFile A, B;
            Equal = MdoSessionBackupFileGet(Decoded, i, &A) && MdoSessionBackupFileGet(g_StageFixture->Bytes, i, &B) &&
                strcmp(A.Path, B.Path) == 0 && A.Bytes == B.Bytes && memcmp(A.Data, B.Data, A.Bytes) == 0;
        }
        MdoSessionBackupRelease(Decoded);
        Context.Request = Request;
        snprintf(Context.RequestId, sizeof(Context.RequestId), "stage-export");
        return MdoApiReplySuccessTake(&Context, 200u, xrtValueBool(Equal), NULL);
    }
    MdoSessionBackupLimitsInit(&Limits); xworkErrorInit(&Error);
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/discard") )
        Ok = MdoSessionBackupStageDiscard(&g_StageFixture, &Error);
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/move-parent") ) {
        char Ancestor[4096];
        snprintf(Ancestor, sizeof(Ancestor), "%s/..", getenv("MDO_STAGE_FIXTURE_PARENT"));
        Parent = xrtRootOpen(Ancestor);
        Ok = Parent != NULL && xrtRootRenameNoReplace(Parent, "private-staging", "moved-staging");
        if ( Parent ) (void)xrtRootClose(Parent);
    }
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/check") )
        Ok = MdoSessionBackupStageCheck(g_StageFixture, NULL, NULL, &Error);
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/check-budget") ) {
        Limits.Files = 0u;
        Ok = MdoSessionBackupStageCheck(g_StageFixture, &Limits, NULL, &Error);
    } else if ( Target.Size >= sizeof(Prefix) - 1u + 9u &&
                memcmp(Target.Data + sizeof(Prefix) - 1u, "reconcile", 9u) == 0 ) {
        MdoSessionBackup* Repaired;
        size_t BeforeBytes = 0u, AfterBytes = 0u;
        str Before = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &BeforeBytes, NULL);
        str After;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/reconcile-growth") )
            Limits.Files = MdoSessionBackupFileCount(g_DecodeFixtureBackup);
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/reconcile-cancel") ) {
            Cancel = xrtCancelCreate(); (void)xrtCancelRequest(Cancel);
        }
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/reconcile-deadline") ) Limits.Deadline = 1u;
        Repair.Size = sizeof(Repair);
        Repaired = MdoSessionBackupReconcileHistory(g_DecodeFixtureBackup, &Limits, Cancel, &Repair,
            MdoApiViewEqualText(Target, "/__fixture/backup-stage/reconcile-null-error") ? NULL : &Error);
        xrtCancelDestroy(Cancel);
        After = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &AfterBytes, NULL);
        OriginalUnchanged = Before != NULL && After != NULL && BeforeBytes == AfterBytes &&
            memcmp(Before, After, BeforeBytes) == 0;
        xrtFree(Before); xrtFree(After);
        BackupDecodeFixtureUnit();
        Parent = xrtRootOpen(getenv("MDO_STAGE_FIXTURE_PARENT"));
        Ok = Repaired != NULL && MdoSessionBackupStagePrepare(Repaired, Parent, NULL, NULL, &g_StageFixture, &Error);
        if ( Parent ) (void)xrtRootClose(Parent);
        MdoSessionBackupRelease(Repaired);
    } else if ( Target.Size >= sizeof(Prefix) - 1u + 13u &&
                memcmp(Target.Data + sizeof(Prefix) - 1u, "review-inputs", 13u) == 0 ) {
        MdoSessionBackup* Reviewed;
        size_t BeforeBytes = 0u, AfterBytes = 0u;
        str Before = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &BeforeBytes, NULL), After;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-cancel") ||
             MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-mid-cancel") ||
             MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-origin-cancel") ) {
            Cancel = xrtCancelCreate();
            if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-cancel") ) (void)xrtCancelRequest(Cancel);
        }
        g_StageFixtureCancel = Cancel;
        g_ReviewFixtureFault = MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-mid-cancel") ? 1u :
            (MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-collision") ? 2u :
             (MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-origin-cancel") ? 3u : 0u));
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-deadline") ) Limits.Deadline = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-origin-files") ) Limits.Files = MdoSessionBackupFileCount(g_DecodeFixtureBackup);
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-growth") ) {
            MdoSessionBackupPreview Preview = {0};
            Preview.Size = sizeof(Preview);
            if ( MdoSessionBackupPreviewGet(g_DecodeFixtureBackup, &Preview) ) Limits.TotalBytes = Preview.Bytes;
        }
        Inputs.Size = sizeof(Inputs);
        Reviewed = MdoSessionBackupReviewInputs(g_DecodeFixtureBackup, &Limits, Cancel, &Inputs,
            MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-null-error") ? NULL : &Error);
        g_ReviewFixtureFault = 0u; g_StageFixtureCancel = NULL; xrtCancelDestroy(Cancel);
        After = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &AfterBytes, NULL);
        OriginalUnchanged = Before != NULL && After != NULL && BeforeBytes == AfterBytes && memcmp(Before, After, BeforeBytes) == 0;
        xrtFree(Before); xrtFree(After); BackupDecodeFixtureUnit();
        Parent = xrtRootOpen(getenv("MDO_STAGE_FIXTURE_PARENT"));
        Ok = Reviewed != NULL && MdoSessionBackupStagePrepare(Reviewed, Parent, NULL, NULL, &g_StageFixture, &Error);
        if ( Parent ) (void)xrtRootClose(Parent);
        MdoSessionBackupRelease(Reviewed);
    } else if ( Target.Size >= sizeof(Prefix) - 1u + 7u &&
                memcmp(Target.Data + sizeof(Prefix) - 1u, "restore", 7u) == 0 ) {
        MdoSessionBackup* Prepared;
        MdoSessionBackupRestoreTarget Destination = {0};
        size_t BeforeBytes = 0u, AfterBytes = 0u;
        str Before = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &BeforeBytes, NULL), After;
        Destination.Size = sizeof(Destination); Destination.ProjectId = "imported";
        Destination.SessionId = MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-second") ?
            "22222222222222222222222222222222" : "11111111111111111111111111111111";
        Destination.WorkspaceRoot = "/explicit/restore workspace 中文"; Destination.RestoredAt = INT64_C(1790900000000000);
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-project") ) Destination.ProjectId = "../foreign";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-long-project") ) Destination.ProjectId =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-id") ) Destination.SessionId = "bad";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-source-id") ) Destination.SessionId = g_DecodeFixtureBackup->Info.Id;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-missing-id") ) Destination.SessionId = NULL;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-workspace") ) Destination.WorkspaceRoot = "relative/workspace";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-control") ) Destination.WorkspaceRoot = "/root\ncontrol";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-windows") ) Destination.WorkspaceRoot = "C:\\explicit\\workspace 中文";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-unc") ) Destination.WorkspaceRoot = "\\\\host\\share\\workspace";
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-time") ) Destination.RestoredAt = 0;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-size") ) Destination.Size = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-deadline") ) Limits.Deadline = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-files") ) Limits.Files = MdoSessionBackupFileCount(g_DecodeFixtureBackup);
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-origin-files") ) Limits.Files = MdoSessionBackupFileCount(g_DecodeFixtureBackup) + 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-growth") ) Limits.TotalBytes = g_DecodeFixtureBackup->Bytes;
        g_RestoreFixtureFault = MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-ui-cancel") ? 1u :
            (MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-origin-cancel") ? 2u : 0u);
        if ( g_RestoreFixtureFault != 0u || MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-cancel") ) {
            Cancel = xrtCancelCreate();
            if ( g_RestoreFixtureFault == 0u ) (void)xrtCancelRequest(Cancel);
        }
        g_StageFixtureCancel = Cancel; Restore.Size = sizeof(Restore);
        Prepared = MdoSessionBackupPrepareRestore(g_DecodeFixtureBackup, &Destination, &Limits, Cancel, &Restore,
            MdoApiViewEqualText(Target, "/__fixture/backup-stage/restore-null-error") ? NULL : &Error);
        g_StageFixtureCancel = NULL; g_RestoreFixtureFault = 0u; xrtCancelDestroy(Cancel);
        After = MdoSessionBackupEncode(g_DecodeFixtureBackup, NULL, &AfterBytes, NULL);
        OriginalUnchanged = Before != NULL && After != NULL && BeforeBytes == AfterBytes && memcmp(Before, After, BeforeBytes) == 0;
        xrtFree(Before); xrtFree(After); BackupDecodeFixtureUnit();
        if ( Prepared != NULL ) {
            const MdoBackupOwnedFile* Ui = MdoBackupFind(Prepared, "ui-events.jsonl");
            size_t Offset = 0u;
            while ( Ui != NULL && Offset < Ui->Bytes && ArtifactReaderPaths ) {
                const char* End = (const char*)memchr(Ui->Data + Offset, '\n', Ui->Bytes - Offset);
                size_t Size = End != NULL ? (size_t)(End - (Ui->Data + Offset)) : 0u;
                ArtifactReaderPaths = End != NULL && MdoSessionsInternalEventVisit(Prepared->Info.ProjectId, Prepared->Info.Id,
                    xrtStrViewN(Ui->Data + Offset, Size), BackupRestoreFixtureArtifact, &Prepared->Info);
                Offset += Size + 1u;
            }
        }
        Parent = xrtRootOpen(getenv("MDO_STAGE_FIXTURE_PARENT"));
        Ok = Prepared != NULL && MdoSessionBackupStagePrepare(Prepared, Parent, NULL, NULL, &g_StageFixture, &Error);
        if ( Parent ) (void)xrtRootClose(Parent);
        MdoSessionBackupRelease(Prepared); Inputs = Restore.Inputs;
    } else if ( !MdoApiViewEqualText(Target, "/__fixture/backup-stage/state") ) {
        g_StageFixtureFault = MdoApiViewEqualText(Target, "/__fixture/backup-stage/corrupt") ? 1u :
            MdoApiViewEqualText(Target, "/__fixture/backup-stage/foreign") ? 2u :
            MdoApiViewEqualText(Target, "/__fixture/backup-stage/write-cancel") ? 3u : 0u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/deadline") ) Limits.Deadline = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/files") ) Limits.Files = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/file") ) Limits.FileBytes = 1u;
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/total") ) Limits.TotalBytes = 1u;
        if ( g_StageFixtureFault == 3u || MdoApiViewEqualText(Target, "/__fixture/backup-stage/cancel") ) {
            Cancel = xrtCancelCreate();
            if ( Cancel == NULL ) return false;
            if ( g_StageFixtureFault != 3u ) (void)xrtCancelRequest(Cancel);
        }
        g_StageFixtureCancel = Cancel;
        Parent = MdoApiViewEqualText(Target, "/__fixture/backup-stage/null-parent") ? NULL :
            xrtRootOpen(getenv("MDO_STAGE_FIXTURE_PARENT"));
        Ok = MdoSessionBackupStagePrepare(g_DecodeFixtureBackup, Parent, &Limits, Cancel,
            &g_StageFixture, MdoApiViewEqualText(Target, "/__fixture/backup-stage/null-error") ? NULL : &Error);
        if ( Parent ) (void)xrtRootClose(Parent);
        g_StageFixtureCancel = NULL; g_StageFixtureFault = 0u; xrtCancelDestroy(Cancel);
        /* The stage cannot depend on the original decode or upload ownership. */
        BackupDecodeFixtureUnit();
    }
    Small = xrtMalloc(sizeof(Words)); if ( Small == NULL ) return false;
    memcpy(Small, Words, sizeof(Words));
    SizeSafe = !MdoSessionBackupStageInfoGet(g_StageFixture, (MdoSessionBackupStageInfo*)Small) &&
        memcmp(Small, Words, sizeof(Words)) == 0;
    SizeSafe = SizeSafe && MdoSessionBackupReconcileHistory(g_DecodeFixtureBackup, NULL, NULL,
        (MdoSessionBackupProjectionRepair*)Small, NULL) == NULL && memcmp(Small, Words, sizeof(Words)) == 0;
    SizeSafe = SizeSafe && MdoSessionBackupReviewInputs(g_DecodeFixtureBackup, NULL, NULL,
        (MdoSessionBackupInputs*)Small, NULL) == NULL && memcmp(Small, Words, sizeof(Words)) == 0;
    SizeSafe = SizeSafe && MdoSessionBackupPrepareRestore(g_DecodeFixtureBackup, NULL, NULL, NULL,
        (MdoSessionBackupRestoreInfo*)Small, NULL) == NULL && memcmp(Small, Words, sizeof(Words)) == 0;
    xrtFree(Small);
    Info.Size = sizeof(Info); (void)MdoSessionBackupStageInfoGet(g_StageFixture, &Info);
    Context.Request = Request; snprintf(Context.RequestId, sizeof(Context.RequestId), "stage-fixture");
    Value = xrtValueObject();
    (void)MdoApiValueSetBool(Value, "ok", Ok); (void)MdoApiValueSetUInt(Value, "code", Error.eCode);
    (void)MdoApiValueSetString(Value, "error", Error.sMessage);
    (void)MdoApiValueSetBool(Value, "retained", g_StageFixture != NULL);
    (void)MdoApiValueSetBool(Value, "verified", Info.Verified);
    (void)MdoApiValueSetBool(Value, "size_safe", SizeSafe);
    (void)MdoApiValueSetString(Value, "directory", Info.DirectoryName);
    (void)MdoApiValueSetString(Value, "source_id", Info.Source.Info.Id);
    (void)MdoApiValueSetUInt(Value, "files", Info.Source.Files);
    (void)MdoApiValueSetUInt(Value, "bytes", Info.Source.Bytes);
    (void)MdoApiValueSetUInt(Value, "ui_records", Info.Source.UiRecords);
    (void)MdoApiValueSetUInt(Value, "matched", Info.ModelHistory.MatchedUiRecords);
    (void)MdoApiValueSetUInt(Value, "model_unverified", Info.ModelHistory.UnverifiedUiRecords);
    (void)MdoApiValueSetUInt(Value, "inline_images", Info.Images.InlineImages);
    (void)MdoApiValueSetBool(Value, "restore_ready", false);
    (void)MdoApiValueSetBool(Value, "original_unchanged", OriginalUnchanged);
    (void)MdoApiValueSetUInt(Value, "repaired_bindings", Repair.RemovedImageBindings);
    (void)MdoApiValueSetUInt(Value, "repaired_feedback", Repair.RemovedFeedback);
    (void)MdoApiValueSetBool(Value, "repaired_todo", Repair.TodoRebuilt);
    (void)MdoApiValueSetUInt(Value, "unverified_refs", Info.Source.UnverifiedHistoryReferences);
    (void)MdoApiValueSetUInt(Value, "removed_refs", Info.Source.RemovedHistoryReferences);
    {
        xvalue* Rows = xrtValueArray();
        size_t i;
        for ( i = 0u; i < Inputs.Count; ++i ) {
            xvalue* Row = xrtValueObject();
            (void)MdoApiValueSetString(Row, "source_id", Inputs.Items[i].SourceId);
            (void)MdoApiValueSetString(Row, "review_id", Inputs.Items[i].ReviewId);
            (void)MdoApiValueSetUInt(Row, "disposition", Inputs.Items[i].Disposition);
            (void)MdoApiValueSetBool(Row, "uncertain", Inputs.Items[i].AdmissionUncertain);
            (void)MdoApiValueAppendTake(Rows, &Row);
        }
        (void)MdoApiValueSetTake(Value, "inputs", &Rows);
    }
    (void)MdoApiValueSetUInt(Value, "accepted_queue", Inputs.AcceptedQueue);
    (void)MdoApiValueSetUInt(Value, "accepted_draft", Inputs.AcceptedDraft);
    (void)MdoApiValueSetUInt(Value, "duplicate_draft", Inputs.DuplicateDraft);
    (void)MdoApiValueSetUInt(Value, "queue_review", Inputs.QueueReview);
    (void)MdoApiValueSetUInt(Value, "draft_review", Inputs.DraftReview);
    (void)MdoApiValueSetUInt(Value, "discard_images", Inputs.ClearedDiscardImages);
    (void)MdoApiValueSetBool(Value, "direct_uncertain", Inputs.DirectRunAdmissionUncertain);
    (void)MdoApiValueSetUInt(Value, "provenance_entries", Inputs.ProvenanceEntries);
    (void)MdoApiValueSetUInt(Value, "origin_entries", Restore.ProvenanceEntries);
    (void)MdoApiValueSetUInt(Value, "restore_records", Restore.UiRecords);
    (void)MdoApiValueSetUInt(Value, "artifact_references", Restore.ArtifactReferences);
    (void)MdoApiValueSetBool(Value, "artifact_reader_paths", ArtifactReaderPaths);
    (void)MdoApiValueSetString(Value, "restore_project", Restore.Target.ProjectId);
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
    return true;
}
