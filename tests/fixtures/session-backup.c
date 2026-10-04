/* Small deterministic file-format probe. No model, shell or queue is run.
 * Some sidecars deliberately use only the fields understood by the capture
 * validator; this is not a successful restore/schema-replay test. */
static bool BackupProbeWrite(const MdoSessionInfo* info, const char* name,
    const void* bytes, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "sessions/%s/%s/%s", info->ProjectId, info->Id, name);
    return MdoHomeAtomicWrite(path, bytes, size, false);
}

static bool BackupProbeRemove(const MdoSessionInfo* info, const char* name)
{
    char path[512];
    snprintf(path, sizeof(path), "sessions/%s/%s/%s", info->ProjectId, info->Id, name);
    return MdoHomeRemove(path, false);
}

static bool SessionBackupProbe(MdoSession* unused)
{
    static const char* const files[] = {
        "draft.json", "queue.json", "feedback.json",
        "attachments/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bin",
        "attachments/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.json",
        "attachments/events/1.json", "attachments/runs/1.json",
        "queue-receipts/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.json",
        "artifacts/run-00000000000000000001/00000000000000000001-probe.txt",
        "meta.json.bak", "draft.json.tmp"
    };
    static const unsigned char image[] = {0x89, 'P', 'N', 'G', 0, 0xff, 1, 2};
    static const char* const documents[] = {
        "{\"schema_version\":6,\"revision\":1,\"text\":\"draft\",\"attachments\":[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"],\"submissions\":[]}",
        "{\"schema_version\":7,\"revision\":1,\"items\":[{\"id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"text\":\"wait for confirmation\",\"state\":\"pending\",\"attachments\":[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]}],\"discard_images\":[\"cccccccccccccccccccccccccccccccc\"]}",
        "retired opaque data",
        NULL,
        "{\"schema_version\":2,\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"mime_type\":\"image/png\",\"size\":8,\"created_at\":1,\"file_name\":\"original \\u56fe\\u7247.png\"}",
        "{\"schema_version\":1,\"run_id\":1,\"attachments\":[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]}",
        "{\"schema_version\":1,\"run_id\":1,\"attachments\":[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]}",
        "{\"schema_version\":1,\"item_id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"run_id\":\"receipt-run\"}",
        "full artifact\n", "old metadata", "unfinished temporary"
    };
    MdoSessionCreateOptions create;
    MdoSessionInfo info = {0};
    MdoSession* session = NULL;
    MdoSessionBackup *backup = NULL, *attempt = NULL;
    MdoSessionBackupLimits limits;
    xwork_error error;
    xwork_event event = {0};
    char path[512];
    str json = NULL, link = NULL, target = NULL;
    size_t size = 0u, i;
    bool ok = false, immutable = false, bounded = false, missing = false;
    bool unknown = false, deadline = false, partial = false, link_checked = false;
    (void)unused;
    MdoSessionCreateOptionsInit(&create);
    create.ProjectId = "backup-fixture"; create.RequestedId = "backup-fixture";
    create.Title = "backup file-format fixture"; create.Agent.WorkspaceRoot = ".";
    session = MdoSessionCreate(&create, &error);
    info.Size = sizeof(info);
    if (!session || !MdoSessionGetInfo(session, &info)) goto done;
    for (i = 0u; i < sizeof(files) / sizeof(files[0]); ++i) {
        const void* data = documents[i] != NULL ? (const void*)documents[i] : (const void*)image;
        size_t bytes = documents[i] != NULL ? strlen(documents[i]) : sizeof(image);
        if (!BackupProbeWrite(&info, files[i], data, bytes)) goto done;
    }
    event.eKind = XWORK_EVENT_TOOL_DONE; event.bSuccess = true;
    event.sToolName = "mdo.todo"; event.sText = "{\"items\":[{\"text\":\"Saved task\",\"done\":false}]}";
    event.iTextLength = strlen(event.sText); event.uRunId = 1u; event.uArtifactId = 1u;
    event.sArtifactPath = "C:\\old-home\\sessions\\backup-fixture\\backup-fixture\\artifacts\\run-00000000000000000001\\00000000000000000001-probe.txt";
    if (!MdoSessionEventBridgeOnEvent(session->Bridge, &event)) goto done;
    backup = MdoSessionBackupCapture(session, NULL, &error);
    if (!backup) goto done;
    json = MdoSessionBackupEncode(backup, NULL, &size, &error);
    if (!json || !MdoHomeAtomicWrite("data/session-backup-probe.json", json, size, false)) goto done;
    xrtFree(json); json = NULL;
    /* Serialize solely from owned bytes, while live writers are allowed again. */
    if (!BackupProbeWrite(&info, "draft.json", "{}", 2u)) goto done;
    json = MdoSessionBackupEncode(backup, NULL, &size, &error);
    immutable = json != NULL && MdoBackupFind(backup, "draft.json")->Bytes == strlen(documents[0]);
    if (!immutable || !MdoHomeAtomicWrite("data/session-backup-after-write.json", json, size, false)) goto done;
    xrtFree(json); json = NULL;
    if (!BackupProbeWrite(&info, "draft.json", documents[0], strlen(documents[0]))) goto done;
    MdoSessionBackupLimitsInit(&limits); limits.TotalBytes = 1u;
    attempt = MdoSessionBackupCapture(session, &limits, &error);
    bounded = attempt == NULL && error.eCode == XWORK_ERROR_LIMIT;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    MdoSessionBackupLimitsInit(&limits); limits.Files = 1u;
    attempt = MdoSessionBackupCapture(session, &limits, &error);
    bounded = bounded && attempt == NULL && error.eCode == XWORK_ERROR_LIMIT;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    MdoSessionBackupLimitsInit(&limits); limits.FileBytes = 1u;
    attempt = MdoSessionBackupCapture(session, &limits, &error);
    bounded = bounded && attempt == NULL && error.eCode == XWORK_ERROR_LIMIT;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    MdoSessionBackupLimitsInit(&limits); limits.DocumentBytes = 16u; size = 999u;
    json = MdoSessionBackupEncode(backup, &limits, &size, &error);
    bounded = bounded && json == NULL && size == 0u && error.eCode == XWORK_ERROR_LIMIT;
    xrtFree(json); json = NULL;
    MdoSessionBackupLimitsInit(&limits); limits.Deadline = 1u;
    attempt = MdoSessionBackupCapture(session, &limits, &error);
    deadline = attempt == NULL && error.eCode == XWORK_ERROR_LIMIT;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    json = MdoSessionBackupEncode(backup, &limits, &size, &error);
    deadline = deadline && json == NULL && size == 0u && error.eCode == XWORK_ERROR_LIMIT;
    xrtFree(json); json = NULL;
    if (!BackupProbeWrite(&info, "not-a-session-secret.json", "{}", 2u)) goto done;
    attempt = MdoSessionBackupCapture(session, NULL, &error);
    unknown = attempt == NULL && error.eCode == XWORK_ERROR_IO;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    if (!BackupProbeRemove(&info, "not-a-session-secret.json")) goto done;
    if (!BackupProbeRemove(&info, files[3])) goto done;
    attempt = MdoSessionBackupCapture(session, NULL, &error);
    if (!attempt) goto done;
    json = MdoSessionBackupEncode(attempt, NULL, &size, &error);
    missing = json == NULL && size == 0u && error.eCode == XWORK_ERROR_IO;
    xrtFree(json); json = NULL;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    if (!BackupProbeWrite(&info, files[3], image, sizeof(image))) goto done;
    if (!BackupProbeRemove(&info, files[8])) goto done;
    attempt = MdoSessionBackupCapture(session, NULL, &error);
    if (!attempt) goto done;
    json = MdoSessionBackupEncode(attempt, NULL, &size, &error);
    missing = missing && json == NULL && size == 0u && error.eCode == XWORK_ERROR_IO;
    xrtFree(json); json = NULL;
    MdoSessionBackupRelease(attempt); attempt = NULL;
    if (!BackupProbeWrite(&info, files[8], documents[8], strlen(documents[8]))) goto done;
    {
        const MdoBackupOwnedFile* original = MdoBackupFind(backup, "ui-events.jsonl");
        xfile file;
        snprintf(path, sizeof(path), "sessions/%s/%s/ui-events.jsonl", info.ProjectId, info.Id);
        file = MdoHomeOpenWrite(path, XFILE_APPEND | XFILE_SYNC);
        if (!file || !xrtWriteFull(file, "{", 1u, NULL) || !xrtClose(file)) goto done;
        attempt = MdoSessionBackupCapture(session, NULL, &error);
        if (!attempt) goto done;
        json = MdoSessionBackupEncode(attempt, NULL, &size, &error);
        partial = json == NULL && size == 0u && error.eCode == XWORK_ERROR_IO;
        xrtFree(json); json = NULL;
        MdoSessionBackupRelease(attempt); attempt = NULL;
        if (!BackupProbeWrite(&info, "ui-events.jsonl", original->Data, original->Bytes)) goto done;
    }
    snprintf(path, sizeof(path), "sessions/%s/%s/link.json", info.ProjectId, info.Id);
    link = MdoHomeExternalPath(path);
    snprintf(path, sizeof(path), "sessions/%s/%s/meta.json", info.ProjectId, info.Id);
    target = MdoHomeExternalPath(path);
    if (link && target && xrtLinkCreate(target, link, false)) {
        attempt = MdoSessionBackupCapture(session, NULL, &error);
        link_checked = attempt == NULL && error.eCode == XWORK_ERROR_IO;
        MdoSessionBackupRelease(attempt); attempt = NULL;
        if (!xrtLinkDelete(link) || !link_checked) goto done;
    } else {
        xrtClearError(); printf("backup_link=unavailable\n");
    }
    attempt = MdoSessionBackupCapture(session, NULL, &error);
    json = attempt != NULL ? MdoSessionBackupEncode(attempt, NULL, &size, &error) : NULL;
    ok = json != NULL && immutable && bounded && missing && unknown && deadline && partial;
    printf("backup_format=immutable:%d bounded:%d missing:%d unknown:%d deadline:%d partial:%d retry:%d\n",
        immutable, bounded, missing, unknown, deadline, partial, ok);
    if (link_checked) printf("backup_link=rejected\n");
done:
    if (!ok) printf("backup_format_error=%s\n", error.sMessage);
    xrtFree(json); xrtFree(link); xrtFree(target);
    MdoSessionBackupRelease(attempt);
    MdoSessionRelease(session);
    /* Remove only this fixture's known files and empty directories. */
    if (info.Id[0]) {
        static const char* const dirs[] = {"attachments/events", "attachments/runs", "attachments",
            "queue-receipts", "artifacts/run-00000000000000000001", "artifacts", ""};
        static const char* const extra[] = {"meta.json", "snapshot.json", "journal.jsonl",
            "ui-events.jsonl", "todo.json", ".runtime.lock", "not-a-session-secret.json"};
        for (i = 0u; i < sizeof(files) / sizeof(files[0]); ++i) (void)BackupProbeRemove(&info, files[i]);
        for (i = 0u; i < sizeof(extra) / sizeof(extra[0]); ++i) (void)BackupProbeRemove(&info, extra[i]);
        for (i = 0u; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
            snprintf(path, sizeof(path), "sessions/%s/%s%s%s", info.ProjectId, info.Id,
                dirs[i][0] ? "/" : "", dirs[i]);
            if (!MdoHomeRemoveEmptyDirectory(path)) ok = false;
        }
        snprintf(path, sizeof(path), "sessions/%s", info.ProjectId);
        if (!MdoHomeRemoveEmptyDirectory(path)) ok = false;
    }
    MdoSessionBackupRelease(backup);
    return ok;
}
