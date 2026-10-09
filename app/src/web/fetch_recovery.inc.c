/* Recovery stays inside one read-only tool call: attempts never write a tool
 * result, publish a document or replay the billed search POST. Wall-clock
 * Retry-After dates become delays once; all waiting uses the monotonic clock. */
#define MDO_WEB_OPEN_MAX_ATTEMPTS 6u
#define MDO_WEB_OPEN_RECOVERY_US UINT64_C(60000000)

static bool MdoWebFetchRequest(MdoWebState*, const xwork_tool_context*,
    const char*, const XS_FetchHeader*, size_t, bool, const char*, size_t,
    XS_FetchResponse*, uint64*);

static void MdoWebRetryAfterHeader(void* Data, uint16 Status,
    xstrview Name, xstrview Value)
{
    static const char Key[] = "retry-after";
    uint64 Delay = 0u;
    bool Digits = true;
    (void)Status;
    if ( Data == NULL || Name.Size != sizeof(Key) - 1u ) return;
    for ( size_t i = 0u; i < Name.Size; ++i )
        if ( tolower((unsigned char)Name.Data[i]) != Key[i] ) return;
    while ( Value.Size && (Value.Data[0] == ' ' || Value.Data[0] == '\t') ) {
        ++Value.Data; --Value.Size;
    }
    while ( Value.Size && (Value.Data[Value.Size - 1u] == ' ' ||
                           Value.Data[Value.Size - 1u] == '\t') ) --Value.Size;
    if ( !Value.Size ) return;
    for ( size_t i = 0u; i < Value.Size; ++i ) {
        unsigned char Ch = (unsigned char)Value.Data[i];
        if ( Ch < '0' || Ch > '9' ) { Digits = false; break; }
        uint64 Digit = (uint64)(Ch - '0');
        /* An overflowing minimum is effectively infinite, never zero. */
        if ( Delay > (UINT64_MAX - Digit) / 10u ) Delay = UINT64_MAX;
        else Delay = Delay * 10u + Digit;
    }
    if ( Digits ) {
        Delay = Delay > UINT64_MAX / 1000000u ? UINT64_MAX : Delay * 1000000u;
    } else {
        xtime Date = 0;
        if ( !xrtTimeTryParseHTTPDate(Value, &Date) ) return;
        xtime Now = xrtNow();
        Delay = Date > Now ? (uint64)Date - (uint64)Now : 0u;
    }
    /* Duplicate fields are handled conservatively: never retry sooner than
     * any valid advertised minimum. Malformed fields leave backoff intact. */
    if ( Delay > *(uint64*)Data ) *(uint64*)Data = Delay;
}

static bool MdoWebTransientStatus(uint16 Status)
{
    return Status == 408u || Status == 425u || Status == 429u ||
        Status == 500u || Status == 502u || Status == 503u || Status == 504u;
}

static xerrkind MdoWebTransportKind(void)
{
    const xerror* Error = xrtGetError();
    if ( Error == NULL ) return XERR_INTERNAL;
    xerrkind Kind = xrtErrorKind(Error);
    for ( const xerror* Cause = Error; Cause != NULL; Cause = xrtErrorCause(Cause) ) {
        xerrkind Nested = xrtErrorKind(Cause);
        if ( Nested == XERR_PERMISSION || Nested == XERR_MEMORY || Nested == XERR_RANGE ||
             Nested == XERR_ARGUMENT || Nested == XERR_CANCELLED || Nested == XERR_UNSUPPORTED ||
             Nested == XERR_PROTOCOL )
            return Nested;
    }
    return Kind;
}

static bool MdoWebTransientTransport(void)
{
    const xerror* Error = xrtGetError();
    if ( Error == NULL ) return false;
    xerrkind Kind = MdoWebTransportKind();
    /* Only this explicit EOF code permits recovery of a protocol failure.
     * Certificate/policy errors and malformed HTTP are not network outages. */
    bool Incomplete = Kind == XERR_PROTOCOL && strcmp(xrtErrorDomain(Error), "xs.fetch") == 0 &&
        xrtErrorCode(Error) == XS_FETCH_ERROR_INCOMPLETE;
    return Incomplete || Kind == XERR_IO || Kind == XERR_AGAIN || Kind == XERR_TIMEOUT ||
        Kind == XERR_CLOSED;
}

typedef struct MdoWebRecovery {
    uint32 Attempts;
    bool Exhausted;
    bool WaitExceedsBudget;
    bool Cancelled;
} MdoWebRecovery;

static uint64 MdoWebRecoveryDelay(uint32 Attempt, uint64* Jitter, uint64 Minimum)
{
    uint64 Delay = UINT64_C(500000) << (Attempt - 1u);
    if ( Delay > 8000000u ) Delay = 8000000u;
    *Jitter ^= *Jitter << 13u; *Jitter ^= *Jitter >> 7u; *Jitter ^= *Jitter << 17u;
    Delay += *Jitter % (Delay / 4u + 1u);
    if ( Delay > 8000000u ) Delay = 8000000u;
    return Delay < Minimum ? Minimum : Delay;
}

static bool MdoWebRecoveryWait(const xwork_tool_context* Context, uint64 End,
    uint64 Delay)
{
    uint64 WaitEnd = xrtClock() + Delay;
    xerror* Saved = xrtTakeError();
    while ( !xrtDeadlineExpired(WaitEnd) && !xrtDeadlineExpired(End) ) {
        if ( Context->pCancel && xrtCancelRequested(Context->pCancel) ) break;
        uint64 Left = xrtDeadlineRemaining(WaitEnd);
        xrtSleep(Left > 20000u ? 20u : (uint32)((Left + 999u) / 1000u));
    }
    if ( Saved ) xrtSetErrorTake(Saved);
    return !(Context->pCancel && xrtCancelRequested(Context->pCancel)) && !xrtDeadlineExpired(End);
}

static bool MdoWebFetchPage(MdoWebState* State, const xwork_tool_context* Context,
    const char* Url, const XS_FetchHeader* Headers, size_t HeaderCount,
    XS_FetchResponse* Response, MdoWebRecovery* Recovery)
{
    uint64 End = xrtDeadlineAfter(MDO_WEB_OPEN_RECOVERY_US);
    uint64 Jitter = xrtClock() ^ (uint64)(uintptr_t)Context;
    if ( !Jitter ) Jitter = 1u;
    if ( Context->uDeadline != XRT_DEADLINE_NEVER && Context->uDeadline < End )
        End = Context->uDeadline;
    xwork_tool_context Attempt = *Context;
    Attempt.uDeadline = End;
    bool Ok = false;
    memset(Recovery, 0, sizeof(*Recovery));
    for ( uint32 Count = 1u; Count <= MDO_WEB_OPEN_MAX_ATTEMPTS; ++Count ) {
        if ( Context->pCancel && xrtCancelRequested(Context->pCancel) ) {
            Recovery->Cancelled = true; break;
        }
        if ( xrtDeadlineExpired(End) ) break;
        if ( Count > 1u ) {
            State->Transport.ResponseUnit(State->Transport.Context, Response);
            memset(Response, 0, sizeof(*Response));
            xrtClearError();
        }
        uint64 RetryAfter = 0u;
        ++Recovery->Attempts;
        Ok = MdoWebFetchRequest(State, &Attempt, Url, Headers, HeaderCount,
            false, NULL, 0u, Response, &RetryAfter);
        Recovery->Exhausted = false;
        if ( Context->pCancel && xrtCancelRequested(Context->pCancel) ) {
            Recovery->Cancelled = true; break;
        }
        if ( Ok ? !MdoWebTransientStatus(Response->Status) :
                  !MdoWebTransientTransport() ) break;
        Recovery->Exhausted = true;
        if ( Count == MDO_WEB_OPEN_MAX_ATTEMPTS ) break;
        uint64 Delay = MdoWebRecoveryDelay(Count, &Jitter, RetryAfter);
        uint64 Remaining = xrtDeadlineRemaining(End);
        if ( Delay >= Remaining ) {
            Recovery->WaitExceedsBudget = true; break;
        }
        /* Preserve the final failure while cleanup/sleep may replace the
         * thread-local error. Every abandoned response is released once. */
        if ( !MdoWebRecoveryWait(Context, End, Delay) ) {
            Recovery->Cancelled = Context->pCancel && xrtCancelRequested(Context->pCancel);
            break;
        }
    }
    return Ok;
}

static xwork_result MdoWebPageFailure(xwork_tool_result_writer* Writer,
    xwork_error* Error, const xwork_tool_context* Context,
    bool FetchOk, const XS_FetchResponse* Response, const MdoWebRecovery* Recovery)
{
    if ( Recovery->Cancelled )
        return MdoWebFail(Error, XWORK_ERROR_CANCELLED, "Web page reading cancelled");
    if ( Context->uDeadline != XRT_DEADLINE_NEVER && xrtDeadlineExpired(Context->uDeadline) )
        return MdoWebFail(Error, XWORK_ERROR_TIMEOUT, "Web page reading exceeded the task deadline");
    if ( !FetchOk ) {
        xerrkind Kind = MdoWebTransportKind();
        if ( Kind == XERR_CANCELLED )
            return MdoWebFail(Error, XWORK_ERROR_CANCELLED, "Web page reading cancelled");
        if ( Kind == XERR_MEMORY )
            return MdoWebFail(Error, XWORK_ERROR_OUT_OF_MEMORY, "Cannot allocate Web page response");
        if ( Recovery->Exhausted ) {
            char Message[480];
            snprintf(Message, sizeof(Message), "Web page could not be reached (%u attempt%s). %sThe connection or remote server is still unavailable; try another source or retry later.",
                (unsigned)Recovery->Attempts, Recovery->Attempts == 1u ? "" : "s",
                Recovery->WaitExceedsBudget ? "The next retry would exceed the remaining time. " :
                "Automatic recovery limits were reached. ");
            return MdoWebToolFail(Writer, Error, Message);
        }
        /* These are routine tool failures the model can work around, not a
         * reason to abort the whole conversation. Keep raw transport prose
         * (which can contain URL credentials) out of model-visible results. */
        const char* Reason = "Web page reading could not be completed; check the URL or use another source.";
        if ( Kind == XERR_RANGE )
            Reason = "Web page exceeds the configured response size limit; use another source.";
        else if ( Kind == XERR_PERMISSION )
            Reason = "Network or certificate policy denied this page; a permitted public address and valid certificate are required.";
        else if ( Kind == XERR_PROTOCOL )
            Reason = "The website response or secure connection failed protocol validation; use another source.";
        else if ( Kind == XERR_UNSUPPORTED || Kind == XERR_STATE )
            Reason = "Web page reading is unavailable in this runtime; check the application configuration.";
        return MdoWebToolFail(Writer, Error, Reason);
    }
    const char* Reason = "The server rejected the page request.";
    switch ( Response->Status ) {
        case 401: Reason = "This page requires its own website login; mdo account login does not grant access."; break;
        case 403: Reason = "The website denied access, possibly requiring login or browser verification."; break;
        case 404: case 410: Reason = "The page was not found or has been removed; check the URL or use another source."; break;
        case 429: Reason = "The website rate-limited page reading; wait before trying this source again."; break;
        case 408: case 504: Reason = "The remote website timed out."; break;
        case 425: case 500: case 502: case 503: Reason = "The remote website is temporarily unavailable."; break;
    }
    char Message[640];
    snprintf(Message, sizeof(Message), "web_open HTTP %u: %s%s", (unsigned)Response->Status,
        Reason, Recovery->WaitExceedsBudget ?
        " The required retry delay exceeds this call's remaining time; no early retry was sent." :
        Recovery->Exhausted ? " Automatic retries were exhausted; try another source or retry later." : "");
    /* Never echo an arbitrary error body, URL query or website credentials. */
    return MdoWebToolFail(Writer, Error, Message);
}
