/* One held writer per iteration, coordinated by semaphores, no load test. */
typedef struct MdoCaptureApiWriter {
    xmutex* Lock;
    xsem* Ready;
    xsem* Release;
    bool Held;
} MdoCaptureApiWriter;

static int32 MdoCaptureApiWriterThread(ptr data)
{
    MdoCaptureApiWriter* writer = (MdoCaptureApiWriter*)data;
    writer->Held = xrtMutexLock(writer->Lock);
    xrtSemPost(writer->Ready);
    if (writer->Held) {
        (void)xrtSemWaitFor(writer->Release, UINT64_C(5000000));
        xrtMutexUnlock(writer->Lock);
    }
    return 0;
}

static bool MdoApiCaptureProbe(XS_HttpReq* request)
{
    xmutex* locks[4] = {g_MdoAttachmentLock, g_MdoDraftLock,
        g_MdoQueueLock, g_MdoFeedbackLock};
    MdoApiSessionCaptureGuard guard, nested;
    MdoApiContext context = {0};
    xvalue* data;
    size_t i, j;
    bool ok = true;
    if (request == NULL || request->head == NULL ||
        !MdoApiViewEqualText(request->head->Target,
            "/__fixture/session-capture")) return false;
    context.Request = request;
    snprintf(context.RequestId, sizeof(context.RequestId), "%s", "capture-probe");
    for (i = 0u; i < 4u && ok; ++i) {
        MdoCaptureApiWriter writer = {0};
        xthread* thread = NULL;
        writer.Lock = locks[i];
        writer.Ready = xrtSemCreate(0u, 1u);
        writer.Release = xrtSemCreate(0u, 1u);
        if (!writer.Ready || !writer.Release) ok = false;
        if (ok) thread = xrtThreadCreate(MdoCaptureApiWriterThread, &writer, 0u);
        if (!thread || xrtSemWaitFor(writer.Ready, UINT64_C(5000000)) != XWAIT_OK)
            ok = false;
        if (ok) {
            ok = writer.Held && !MdoApiSessionCaptureAcquire(&guard) &&
                !guard.Attachment && !guard.Draft && !guard.Queue && !guard.Feedback;
            for (j = 0u; j < 4u; ++j) if (j != i) {
                bool free_lock = xrtMutexTryLock(locks[j]);
                if (free_lock) xrtMutexUnlock(locks[j]);
                ok = ok && free_lock;
            }
        }
        if (writer.Release) xrtSemPost(writer.Release);
        if (thread) { (void)xrtThreadWait(thread); xrtThreadDestroy(thread); }
        xrtSemDestroy(writer.Ready); xrtSemDestroy(writer.Release);
    }
    if (ok) {
        ok = MdoApiSessionCaptureAcquire(&guard);
        if (ok) {
            ok = !MdoApiSessionCaptureAcquire(&nested);
            for (j = 0u; j < 4u; ++j) {
                bool escaped = xrtMutexTryLock(locks[j]);
                if (escaped) xrtMutexUnlock(locks[j]);
                ok = ok && !escaped;
            }
            MdoApiSessionCaptureRelease(&guard);
            MdoApiSessionCaptureRelease(&guard); /* harmless repeated cleanup */
        }
    }
    if (ok) {
        ok = MdoApiSessionCaptureAcquire(&guard);
        MdoApiSessionCaptureRelease(&guard);
    }
    data = xrtValueObject();
    if (!data || !MdoApiValueSetBool(data, "ok", ok)) {
        xrtValueRelease(data);
        (void)MdoApiReplyError(&context, 500u, "capture_probe_failed", "allocation", NULL);
    } else (void)MdoApiReplySuccessTake(&context, ok ? 200u : 500u, data, NULL);
    return true;
}
