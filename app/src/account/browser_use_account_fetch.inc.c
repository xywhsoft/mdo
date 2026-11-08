/* Recovery is limited to one replay-safe device-list read and one monotonic
 * 15s budget. Effectful account/device calls are always attempted once. */
static cstr MdoAccountErrorDomain(cstr Domain)
{
    static const char* const known[] = {"xs.fetch","xrt.net","xrt.tls","xrt.future","xrt.io",
        "xrt.http1","xrt.http.decode","xrt.http.client","xrt.http.client.response","xrt.http.call","xrt.http.exchange"};
    for (size_t i = 0u; Domain && i < sizeof(known)/sizeof(known[0]); i++)
        if (!strcmp(Domain,known[i])) return known[i];
    return "native";
}
static void MdoAccountTransportFailure(MdoAccountTransport* Info)
{
    const xerror* first = xrtGetError();
    xerrkind kind = first ? xrtErrorKind(first) : XERR_INTERNAL;
    bool specific = false, transient = false;
    Info->Phase = "transport"; Info->Failures++; Info->CauseCount = 0u;
    for (const xerror* cause = first; cause && Info->CauseCount < 8u; cause = xrtErrorCause(cause)) {
        uint32 i = Info->CauseCount++; xerrkind nested = xrtErrorKind(cause);
        Info->Causes[i].Kind = nested; Info->Causes[i].Code = xrtErrorCode(cause);
        Info->Causes[i].SystemCode = xrtErrorSystemCode(cause);
        cstr domain = xrtErrorDomain(cause);
        Info->Causes[i].Domain = MdoAccountErrorDomain(domain);
        Info->Causes[i].Fetch = domain && !strcmp(domain,"xs.fetch");
        if (nested == XERR_IO || nested == XERR_AGAIN || nested == XERR_CLOSED) transient = true;
        /* Keep the first specific failure, before transport teardown's cancel. */
        if (!specific && (nested == XERR_PERMISSION || nested == XERR_MEMORY || nested == XERR_RANGE ||
            nested == XERR_ARGUMENT || nested == XERR_TIMEOUT || nested == XERR_CANCELLED ||
            nested == XERR_UNSUPPORTED || nested == XERR_PROTOCOL)) { kind = nested; specific = true; }
    }
    bool incomplete = first && kind == XERR_PROTOCOL && Info->Causes[0].Fetch &&
        Info->Causes[0].Code == XS_FETCH_ERROR_INCOMPLETE;
    /* HTTP wrappers can inherit a teardown STATE kind. An actual IO cause
     * still permits replay-safe reads, unless a specific denial intervenes. */
    Info->Retryable = incomplete || kind == XERR_TIMEOUT || (!specific && transient);
}
static bool MdoAccountFetch(XS_FetchRequest* Request, XS_FetchResponse* Response,
    bool ReadOnly, MdoAccountTransport* Info)
{
    MdoAccountTransport local = {0}; if (!Info) Info = &local;
    memset(Info,0,sizeof(*Info)); Info->Phase = "request";
    uint64 start = MdoMonotonicUs(), end = MdoDeadlineAfterUs(15000000u);
    bool ok = false;
    for (uint32 attempt = 0u; attempt < (ReadOnly ? 3u : 1u); attempt++) {
        if (Request->Cancel && xrtCancelRequested(Request->Cancel)) { Info->Phase = "cancelled"; break; }
        uint64 remaining = MdoDeadlineRemainingUs(end);
        if (!remaining) break;
        if (attempt) { xsFetchResponseUnit(Response); memset(Response,0,sizeof(*Response)); }
        Request->Timeout = ReadOnly && remaining > 4500000u ? 4500000u : remaining;
        Request->IdleTimeout = Request->Timeout < 10000000u ? Request->Timeout : 10000000u;
        xrtClearError(); Info->Attempts++;
        ok = xsFetch(Request,Response);
        if (ok) { Info->Phase = "response"; break; }
        /* Capture before response cleanup/sleep can replace the thread error. */
        MdoAccountTransportFailure(Info);
        if (!ReadOnly || !Info->Retryable || attempt == 2u) break;
        uint64 delay = (250000u << attempt) + MdoMonotonicUs() % 100001u;
        if (delay >= MdoDeadlineRemainingUs(end)) break;
        uint64 wait = MdoDeadlineAfterUs(delay);
        while (!MdoDeadlineExpired(wait) && !MdoDeadlineExpired(end) &&
            !(Request->Cancel && xrtCancelRequested(Request->Cancel))) xrtSleep(20u);
    }
    if (Request->Cancel && xrtCancelRequested(Request->Cancel)) {
        Info->Phase = "cancelled"; Info->Retryable = false; ok = false;
    }
    uint64 now = MdoMonotonicUs(); Info->ElapsedMs = now >= start ? (now-start)/1000u : 0u;
    return ok;
}
