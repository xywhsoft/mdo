/* Included by the real xs/TCC session probe, after production sources. */
static bool CaptureDataGateProbe(void)
{
    MdoSessionDataLease *writer = NULL, *nested = NULL, *capture = NULL;
    MdoSessionDataLease *other = NULL, *late = NULL;
    xwork_error error;
    bool ok = false;
    writer = MdoSessionDataAcquire("Capture-Project.", "Capture-Session.",
        MDO_SESSION_DATA_WRITE, &error);
    nested = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_WRITE, &error);
    capture = MdoSessionDataAcquire("CAPTURE-PROJECT", "CAPTURE-SESSION",
        MDO_SESSION_DATA_CAPTURE, &error);
    if (!writer || !nested || capture || error.eCode != XWORK_ERROR_CONTEXT) goto done;
    other = MdoSessionDataAcquire("capture-project", "other-session",
        MDO_SESSION_DATA_CAPTURE, &error);
    if (!other) goto done;
    MdoSessionDataRelease(writer); writer = NULL;
    capture = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_CAPTURE, &error);
    if (capture || error.eCode != XWORK_ERROR_CONTEXT) goto done;
    MdoSessionDataRelease(nested); nested = NULL;
    late = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_CAPTURE, &error);
    if (!late) goto done;
    writer = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_WRITE, &error);
    if (writer || error.eCode != XWORK_ERROR_CONTEXT) goto done;
    capture = MdoSessionDataAcquire("../escape", "session", MDO_SESSION_DATA_CAPTURE, &error);
    if (capture || error.eCode != XWORK_ERROR_INVALID_ARGUMENT) goto done;
    MdoSessionDataRelease(other); other = NULL;
    MdoSessionDataUnit();
    capture = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_CAPTURE, &error);
    if (capture || error.eCode != XWORK_ERROR_CONTEXT || !MdoSessionDataInit()) goto done;
    capture = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_CAPTURE, &error);
    if (!capture) goto done;
    MdoSessionDataRelease(late); late = NULL;
    /* Releasing an old registry cannot unlock the new capture. */
    writer = MdoSessionDataAcquire("capture-project", "capture-session",
        MDO_SESSION_DATA_WRITE, &error);
    ok = !writer && error.eCode == XWORK_ERROR_CONTEXT;
done:
    MdoSessionDataRelease(writer);
    MdoSessionDataRelease(nested);
    MdoSessionDataRelease(capture);
    MdoSessionDataRelease(other);
    MdoSessionDataRelease(late);
    printf("capture_data_gate=%d\n", ok);
    return ok;
}

typedef struct CaptureSessionProbe {
    MdoSession* Session;
    MdoSession* Other;
    unsigned Calls;
    bool Fail;
    bool Silent;
    bool WritersBlocked;
    bool OtherBlocked;
    bool LocksBlocked;
} CaptureSessionProbe;

static bool CaptureNoop(const MdoSessionInfo* info, void* data, xwork_error* error)
{
    (void)info; (void)data; (void)error;
    return true;
}

static int32 CaptureOtherThread(ptr data)
{
    CaptureSessionProbe* probe = (CaptureSessionProbe*)data;
    xwork_error error;
    bool manager = xrtMutexTryLock(g_MdoSessions.Lock);
    if (manager) xrtMutexUnlock(g_MdoSessions.Lock);
    probe->LocksBlocked = !manager;
    probe->OtherBlocked = !MdoSessionWithCapture(probe->Other,
        CaptureNoop, NULL, &error) && error.eCode == XWORK_ERROR_CONTEXT;
    return 0;
}

static bool CaptureSessionReader(const MdoSessionInfo* info,
    void* data, xwork_error* error)
{
    CaptureSessionProbe* probe = (CaptureSessionProbe*)data;
    const char ids[4][33] = {{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}};
    xwork_event todo = {0};
    xthread* thread;
    ++probe->Calls;
    todo.eKind = XWORK_EVENT_TOOL_DONE; todo.bSuccess = true;
    todo.sToolName = "mdo.todo"; todo.sText = "{\"items\":[]}";
    todo.iTextLength = strlen(todo.sText);
    probe->WritersBlocked =
        !MdoSessionTodoProject(info->ProjectId, info->Id, 99u, &todo) &&
        !MdoSessionTodoReset(info->ProjectId, info->Id) &&
        !MdoSessionAttachmentEventWrite(info->ProjectId, info->Id, 99u, 1u, ids, 1u) &&
        !MdoSessionAttachmentPruneRemoved(info->ProjectId, info->Id) &&
        !MdoSessionAttachmentEventClone(info->ProjectId, info->Id, 1u,
            info->ProjectId, "must-not-create", 1u, 1u);
    /* Rollback also honors exclusion rather than deleting existing files. */
    MdoSessionAttachmentForkRollback(info->ProjectId, info->Id);
    thread = xrtThreadCreate(CaptureOtherThread, probe, 0u);
    if (!thread || xrtThreadWait(thread) != XWAIT_OK) {
        if (thread) xrtThreadDestroy(thread);
        return false;
    }
    xrtThreadDestroy(thread);
    if (probe->Fail && !probe->Silent) {
        error->eCode = XWORK_ERROR_LIMIT;
        snprintf(error->sMessage, sizeof(error->sMessage), "%s", "capture reader failure");
    }
    return !probe->Fail && probe->WritersBlocked && probe->OtherBlocked && probe->LocksBlocked;
}

static bool CaptureSessionBoundaryProbe(MdoSession* session)
{
    MdoSessionInfo info;
    CaptureSessionProbe probe = {0};
    MdoSessionDataLease* writer = NULL;
    xwork_error error;
    uint64 sequence;
    bool ok = false, failed, released;
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoSessionGetInfo(session, &info)) goto done;
    probe.Session = session;
    probe.Other = MdoSessionLoad(info.ProjectId, info.Id, &error);
    if (!probe.Other) goto done;
    writer = MdoSessionDataAcquire(info.ProjectId, info.Id, MDO_SESSION_DATA_WRITE, &error);
    if (!writer) goto done;
    failed = !MdoSessionWithCapture(session, CaptureSessionReader, &probe, &error) &&
        error.eCode == XWORK_ERROR_CONTEXT && probe.Calls == 0u;
    MdoSessionDataRelease(writer); writer = NULL;
    if (!failed || !MdoSessionWithCapture(session, CaptureSessionReader, &probe, &error)) goto done;
    printf("capture_boundary=writers:%d other:%d locks:%d calls:%u\n",
        probe.WritersBlocked, probe.OtherBlocked, probe.LocksBlocked, probe.Calls);
    probe.Fail = true;
    failed = !MdoSessionWithCapture(session, CaptureSessionReader, &probe, &error) &&
        error.eCode == XWORK_ERROR_LIMIT && strcmp(error.sMessage, "capture reader failure") == 0;
    released = MdoSessionLastSequence(session, &sequence, &error);
    probe.Silent = true;
    failed = failed && !MdoSessionWithCapture(session, CaptureSessionReader, &probe, &error) &&
        error.eCode == XWORK_ERROR_IO;
    probe.Fail = false; probe.Silent = false;
    ok = failed && released && MdoSessionWithCapture(session, CaptureSessionReader, &probe, NULL);
    printf("capture_failure=preserved:%d released:%d retry:%d\n", failed, released, ok);
done:
    MdoSessionDataRelease(writer);
    MdoSessionRelease(probe.Other);
    return ok;
}
