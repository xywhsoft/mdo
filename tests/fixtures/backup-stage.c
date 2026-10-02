/* Test-only staging adapter. Product has no staging HTTP route yet. */
static MdoSessionBackupStage* g_StageFixture;
static unsigned g_StageFixtureFault;
static xcancel* g_StageFixtureCancel;
static unsigned g_ReviewFixtureFault;

void BackupReviewFixtureCandidate(str* Candidate, size_t Attempt)
{
    if ( g_ReviewFixtureFault == 1u && Attempt == 0u ) (void)xrtCancelRequest(g_StageFixtureCancel);
    if ( g_ReviewFixtureFault == 2u ) {
        xrtFree(*Candidate); *Candidate = xrtStrDup("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    }
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
    xwork_error Error;
    xvalue* Value;
    xroot Parent = NULL;
    xcancel* Cancel = NULL;
    bool Ok = true, SizeSafe, OriginalUnchanged = true;
    uint32 Words[2] = { sizeof(uint32), UINT32_C(0x87654321) };
    void* Small;
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
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
             MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-mid-cancel") ) {
            Cancel = xrtCancelCreate();
            if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-cancel") ) (void)xrtCancelRequest(Cancel);
        }
        g_StageFixtureCancel = Cancel;
        g_ReviewFixtureFault = MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-mid-cancel") ? 1u :
            (MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-collision") ? 2u : 0u);
        if ( MdoApiViewEqualText(Target, "/__fixture/backup-stage/review-inputs-deadline") ) Limits.Deadline = 1u;
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
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
    return true;
}
