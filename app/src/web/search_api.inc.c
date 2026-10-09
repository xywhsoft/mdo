/* xadmin is the only search protocol. Provider credentials stay on its server;
 * this client sends a member access token only to its account service.
 * One opaque ID is reused on the dedicated durable endpoint. A legacy service
 * is used only after a definitive route 404 and never blindly replayed. */
#define MDO_WEB_SEARCH_MAX_RESULTS 10u
#define MDO_WEB_SEARCH_TOKEN_REF "account:search-access-token"

static bool MdoWebSearchQueryValid(xstrview Query)
{
    size_t i;
    bool NonSpace = false;
    if ( !xrtUtf8Valid(Query, NULL) ) return false;
    for ( i = 0u; i < Query.Size; ++i ) {
        unsigned char c = (unsigned char)Query.Data[i];
        if ( c < 0x20u || c == 0x7fu ) return false;
        if ( c != ' ' ) NonSpace = true;
    }
    return NonSpace;
}

static const char* MdoWebSearchServiceError(uint64 Status)
{
    switch ( Status ) {
    case 400u: return "Search API rejected the request; check its query and result limit. Do not repeat the same request.";
    case 401u: return "Search requires a valid account login. Sign in again; repeating this query cannot renew the login.";
    case 403u: return "Search account access was denied. Check the account's permissions or verification requirements.";
    case 404u: return "Search API was not found at the account service. Contact its administrator.";
    case 405u: return "Search service does not accept this request. Contact its administrator.";
    case 429u: return "Search service limited this request without specifying the reason. Wait before searching again; no automatic retry was sent.";
    case 502u: return "Search provider is unavailable or returned an invalid response. Try later.";
    case 503u: return "Search service is temporarily unavailable. Try later or contact its administrator.";
    case 504u: return "Search service timed out waiting for its provider. Try later.";
    default: return "Search API returned an unsuccessful response. Check the service; no search results were established.";
    }
}

#include "search_errors.inc.c"

static bool MdoWebSearchOptionalText(const xvalue* Item, const char* Key,
    size_t Limit, xstrview* Text)
{
    const xvalue* Value = xrtValueObjectGet(Item, xrtStrView(Key));
    *Text = xrtStrView("");
    return Value == NULL || MdoWebString(Value, 0u, Limit, Text);
}

static bool MdoWebSearchApiItem(xvalue* Results, const xvalue* Item,
    xstrview Provider, int64 FetchedAt)
{
    xstrview Title, Url, Snippet, Site, Published;
    xvalue* Output = NULL;
    size_t i;
    if ( Item == NULL || xrtValueType(Item) != XVALUE_OBJECT ||
         !MdoWebString(xrtValueObjectGet(Item, xrtStrView("title")), 1u, 512u, &Title) ||
         !MdoWebString(xrtValueObjectGet(Item, xrtStrView("url")), 1u, 2048u, &Url) ||
         !MdoWebPageUrl(Url, true, NULL) ||
         !MdoWebSearchOptionalText(Item, "snippet", 2048u, &Snippet) ||
         !MdoWebSearchOptionalText(Item, "site", 256u, &Site) ||
         !MdoWebSearchOptionalText(Item, "published_at", 64u, &Published) ) return false;
    for ( i = 0u; i < xrtValueCount(Results); ++i ) {
        xstrview Existing;
        if ( MdoWebString(xrtValueObjectGet(xrtValueArrayGet(Results, i),
                xrtStrView("url")), 1u, 2048u, &Existing) &&
             Existing.Size == Url.Size && memcmp(Existing.Data, Url.Data, Url.Size) == 0 )
            return true;
    }
    Output = xrtValueObject();
    if ( Output == NULL ||
         !MdoWebObjectString(Output, "title", Title.Data, Title.Size) ||
         !MdoWebObjectString(Output, "url", Url.Data, Url.Size) ||
         !MdoWebObjectString(Output, "snippet", Snippet.Data, Snippet.Size) ||
         !MdoWebObjectString(Output, "site", Site.Data, Site.Size) ||
         !MdoWebObjectString(Output, "published_at", Published.Data, Published.Size) ||
         !MdoWebObjectString(Output, "source", Provider.Data, Provider.Size) ||
         !MdoWebObjectInt(Output, "fetched_at", FetchedAt) ||
         !xrtValueArrayAppendTake(Results, &Output) ) {
        xrtValueRelease(Output);
        return false;
    }
    return true;
}

static xwork_result MdoWebSearchExecute(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_tool_result_writer* pWriter, xwork_error* pError)
{
    static const char* const Keys[] = { "query", "count" };
    MdoWebState* State = (MdoWebState*)pUserData;
    xvalue* Arguments = NULL;
    xvalue* Envelope = NULL;
    xvalue* Output = NULL;
    xvalue* Results = NULL;
    const xvalue* Data;
    const xvalue* Items;
    const xvalue* CountValue;
    xstrview Query, Provider, RequestId;
    uint64 Count = MDO_WEB_SEARCH_MAX_RESULTS;
    uint64 Code;
    uint64 ReportedCount;
    bool Truncated;
    bool Attempted = false;
    bool Success = false;
    size_t i;
    size_t BodySize = 0u;
    char* Body = NULL;
    char* Token = NULL;
    MdoAccountLease Lease = {0};
    xwork_tool_context FetchContext;
    bool Renewed = false;
    char* Authorization = NULL;
    XS_FetchHeader Headers[4];
    XS_FetchResponse Response;
    uint64 End = XRT_DEADLINE_NEVER;
    uint64 Jitter = xrtClock() ^ (uint64)(uintptr_t)pContext;
    uint32 Attempts = 0u;
    bool Registered = true;
    bool UncertainSubmission = false;
    char RequestKey[33] = {0}, Endpoint[1024];
    bool AccountAcquired = false;
    uint64 MemberId = 0u;
    xwork_tool_context AccountContext;
    if ( !Jitter ) Jitter = 1u;
    xwork_result Result = XWORK_RESULT_ERROR;
    memset(&Response, 0, sizeof(Response));
    if ( !MdoWebArguments(ArgumentsJson, &Arguments) ||
         !MdoWebAllowedKeys(Arguments, Keys, 2u) ||
         !MdoWebString(xrtValueObjectGet(Arguments, xrtStrView("query")),
            1u, MDO_WEB_QUERY_LIMIT, &Query) || !MdoWebSearchQueryValid(Query) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_search requires a nonblank UTF-8 query without control characters and optional count (1-10)");
        goto done;
    }
    CountValue = xrtValueObjectGet(Arguments, xrtStrView("count"));
    if ( CountValue != NULL && (!MdoWebUnsigned(CountValue, &Count) ||
         Count == 0u || Count > MDO_WEB_SEARCH_MAX_RESULTS) ) {
        Result = MdoWebToolFail(pWriter, pError, "web_search count must be an integer from 1 to 10");
        goto done;
    }
    unsigned char Random[16];
    if ( !xrtSecureRandom(Random,sizeof(Random)) ) {
        Result=MdoWebToolFail(pWriter,pError,"Cannot create a safe search request ID. No search was submitted.");goto done;
    }
    for (i=0u;i<sizeof(Random);++i) snprintf(RequestKey+i*2u,3u,"%02x",Random[i]);
    xrtSecureZero(Random,sizeof(Random));
    if ( !MdoWebObjectString(Arguments,"request_id",RequestKey,32u) ) goto memory_failed;
    if (snprintf(Endpoint,sizeof(Endpoint),"%s/requests",MdoAccountSearchEndpoint()) >= (int)sizeof(Endpoint)) {
        Result=MdoWebToolFail(pWriter,pError,"Search service address exceeds the supported size.");goto done;
    }
acquire_account:
    /* Initial sign-in may wait for the user. A renewal during recovery shares
     * the same remaining deadline instead of starting a new waiting budget. */
    AccountContext = *pContext;
    if (End != XRT_DEADLINE_NEVER) AccountContext.uDeadline = End;
    if (!MdoAccountAcquire(Query.Data, &AccountContext, &Lease)) {
        Result = MdoWebToolFail(pWriter, pError,
            "Search needs an account login. The login wait was skipped, cancelled or expired; no search was sent. Do not retry until the user signs in.");
        goto done;
    }
    Token = Lease.AccessToken;
    if (AccountAcquired && MemberId!=Lease.MemberId) {
        Result=MdoWebFail(pError,XWORK_ERROR_CANCELLED,"Account changed while recovering search; no request was sent for the new account.");goto done;
    }
    MemberId=Lease.MemberId;AccountAcquired=true;
    FetchContext = *pContext;
    if (Lease.Cancel) FetchContext.pCancel = Lease.Cancel;
    if ( End == XRT_DEADLINE_NEVER ) {
        End = xrtDeadlineAfter(MDO_WEB_SEARCH_RECOVERY_US);
        if ( pContext->uDeadline != XRT_DEADLINE_NEVER && pContext->uDeadline < End ) End = pContext->uDeadline;
    }
    FetchContext.uDeadline = End;
    Authorization = (char*)xrtMalloc(strlen(Token) + 8u);
    if (!Body) Body = xrtJsonStringify(Arguments, false, &BodySize);
    if ( Authorization == NULL || Body == NULL ) goto memory_failed;
    snprintf(Authorization, strlen(Token) + 8u, "Bearer %s", Token);
    Headers[0] = (XS_FetchHeader){ "Accept", "application/json" };
    Headers[1] = (XS_FetchHeader){ "Content-Type", "application/json" };
    Headers[2] = (XS_FetchHeader){ "User-Agent", "mdo/1 web_search" };
    Headers[3] = (XS_FetchHeader){ "Authorization", Authorization };
fetch_search:
    if ( (FetchContext.pCancel && xrtCancelRequested(FetchContext.pCancel)) ||
         (pContext->pCancel && xrtCancelRequested(pContext->pCancel)) ) {
        Result = MdoWebFail(pError, XWORK_ERROR_CANCELLED, "Search request was cancelled"); goto done;
    }
    uint64 RetryAfter = 0u;
    ++Attempts;
    Attempted = true;
    if ( !MdoWebFetchRequest(State, &FetchContext, Registered?Endpoint:MdoAccountSearchEndpoint(),
            Headers, 4u, true, Body, BodySize, &Response, &RetryAfter) ) {
        /* Transport diagnostics may contain headers. Never reflect them into
         * a model-visible result containing an account credential. */
        xerrkind Kind = MdoWebTransportKind();
        if (Registered) UncertainSubmission=true;
        uint64 Delay=MdoWebRecoveryDelay(Attempts,&Jitter,0u);
        if (Registered && MdoWebTransientTransport() && Attempts<MDO_WEB_SEARCH_MAX_ATTEMPTS &&
            Delay<xrtDeadlineRemaining(End) && !(FetchContext.pCancel && xrtCancelRequested(FetchContext.pCancel))) {
            if (MdoWebRecoveryWait(&FetchContext,End,Delay)) {
                State->Transport.ResponseUnit(State->Transport.Context,&Response);
                memset(&Response,0,sizeof(Response));xrtClearError();goto fetch_search;
            }
            Kind=FetchContext.pCancel && xrtCancelRequested(FetchContext.pCancel)?XERR_CANCELLED:Kind;
        }
        if ( (pContext->pCancel != NULL && xrtCancelRequested(pContext->pCancel)) || Kind == XERR_CANCELLED )
            Result = MdoWebFail(pError, XWORK_ERROR_CANCELLED, "Search request was cancelled");
        else if ( pContext->uDeadline != XRT_DEADLINE_NEVER &&
                    xrtDeadlineExpired(pContext->uDeadline) )
            Result = MdoWebFail(pError, XWORK_ERROR_TIMEOUT, "Search request timed out");
        else if ( Kind == XERR_MEMORY )
            Result = MdoWebFail(pError, XWORK_ERROR_OUT_OF_MEMORY, "Cannot allocate search request or response");
        else if ( Kind == XERR_PERMISSION )
            Result = MdoWebToolFail(pWriter, pError, "Network or certificate policy denied the search service connection. Check the service certificate or network policy.");
        else if ( Kind == XERR_RANGE )
            Result = MdoWebToolFail(pWriter, pError, "Search service response exceeded the configured size limit. Contact its administrator.");
        else if ( Kind == XERR_PROTOCOL )
            Result = MdoWebToolFail(pWriter, pError, "Search connection or response failed protocol validation. Submission could not be confirmed; no automatic replay was sent.");
        else if ( Registered )
            Result = MdoWebToolFail(pWriter,pError,"Search result could not be recovered within automatic recovery limits. The service may still hold this request's result; no new request ID was submitted. Try again later.");
        else if ( Kind == XERR_TIMEOUT )
            Result = MdoWebToolFail(pWriter, pError,
                "Search service timed out. Submission could not be confirmed; no automatic replay was sent to avoid spending the quota twice. Try again later.");
        else Result = MdoWebToolFail(pWriter, pError, "Cannot reach the search service. Submission could not be confirmed; check your connection before trying again.");
        goto done;
    }
    if (Registered && Response.Status==404u && !UncertainSubmission &&
        Attempts<MDO_WEB_SEARCH_MAX_ATTEMPTS && !xrtDeadlineExpired(End)) {
        /* A POST to this dedicated route creates a receipt. A definitive route
         * miss alone permits a legacy POST; transport failures never do. */
        Registered=false;
        if (!xrtValueObjectRemove(Arguments,xrtStrView("request_id"))) goto memory_failed;
        xrtFree(Body);Body=xrtJsonStringify(Arguments,false,&BodySize);
        if (!Body) goto memory_failed;
        State->Transport.ResponseUnit(State->Transport.Context,&Response);
        memset(&Response,0,sizeof(Response));xrtClearError();goto fetch_search;
    }
    if (Response.Status == 401 && !Renewed && Attempts < MDO_WEB_SEARCH_MAX_ATTEMPTS && MdoAccountRejectAccess(&Lease)) {
        Renewed = true;
        State->Transport.ResponseUnit(State->Transport.Context, &Response);
        memset(&Response, 0, sizeof(Response));
        MdoSecretRelease(&Authorization); MdoAccountRelease(&Lease); Token = NULL;
        goto acquire_account;
    }
    Envelope = MdoWebSearchEnvelope(State, &Response);
    bool ValidEnvelope = Envelope && xrtValueType(Envelope) == XVALUE_OBJECT &&
        MdoWebUnsigned(xrtValueObjectGet(Envelope, xrtStrView("code")), &Code);
    if ( Response.Status != 200u || (ValidEnvelope && Code != 0u) ) {
        bool RetrySafe = false;
        uint64 Minimum = 0u;
        const MdoWebSearchError* Reason = ValidEnvelope ?
            MdoWebSearchReason(Envelope,Response.Status,Code,Registered?RequestKey:NULL,&RetrySafe,&Minimum) : NULL;
        if (Registered && !ValidEnvelope && MdoWebTransientStatus(Response.Status)) {
            RetrySafe=true;UncertainSubmission=true;
        }
        if (Reason && strcmp(Reason->Code,"request_pending")==0) UncertainSubmission=true;
        uint64 Delay = MdoWebRecoveryDelay(Attempts, &Jitter, Minimum > RetryAfter ? Minimum : RetryAfter);
        bool WaitFits = Delay < xrtDeadlineRemaining(End);
        if ( RetrySafe && Attempts < MDO_WEB_SEARCH_MAX_ATTEMPTS && WaitFits ) {
            if ( !MdoWebRecoveryWait(&FetchContext,End,Delay) ) {
                if (FetchContext.pCancel && xrtCancelRequested(FetchContext.pCancel))
                    Result = MdoWebFail(pError,XWORK_ERROR_CANCELLED,"Search request was cancelled");
                else if (pContext->uDeadline != XRT_DEADLINE_NEVER && xrtDeadlineExpired(pContext->uDeadline))
                    Result = MdoWebFail(pError,XWORK_ERROR_TIMEOUT,"Search recovery exceeded the task deadline");
                else Result = MdoWebToolFail(pWriter,pError,"Search service is still busy. Automatic recovery time was exhausted; try again later.");
                goto done;
            }
            xrtValueRelease(Envelope); Envelope = NULL;
            State->Transport.ResponseUnit(State->Transport.Context,&Response);
            memset(&Response,0,sizeof(Response)); xrtClearError();
            goto fetch_search;
        }
        if (Lease.Managed) MdoAccountSearchStatus((uint16)(Response.Status == 200u && Code >= 400u && Code <= 599u ? Code : Response.Status));
        char Message[768];
        snprintf(Message,sizeof(Message),"%s%s",Reason ? Reason->Message :
            MdoWebSearchServiceError(Response.Status == 200u ? Code : Response.Status),
            RetrySafe ? (WaitFits ? " Automatic recovery limits were reached; do not repeatedly issue the same query." :
            " The required wait exceeds this call's remaining time; no early retry was sent.") : "");
        Result = MdoWebToolFail(pWriter,pError,Message);
        goto done;
    }
    if ( !ValidEnvelope ) goto invalid_response;
    if (Lease.Managed) MdoAccountSearchStatus(Response.Status);
    Data = xrtValueObjectGet(Envelope, xrtStrView("data"));
    Items = Data != NULL ? xrtValueObjectGet(Data, xrtStrView("results")) : NULL;
    if ( Data == NULL || xrtValueType(Data) != XVALUE_OBJECT ||
         Items == NULL || xrtValueType(Items) != XVALUE_ARRAY ||
         xrtValueCount(Items) > Count ||
         !MdoWebUnsigned(xrtValueObjectGet(Data, xrtStrView("count")), &ReportedCount) ||
         ReportedCount != xrtValueCount(Items) ||
         !MdoWebString(xrtValueObjectGet(Data, xrtStrView("provider")), 1u, 32u, &Provider) ||
         !MdoWebString(xrtValueObjectGet(Data, xrtStrView("request_id")), 32u, 32u, &RequestId) ||
         !xrtValueGetBool(xrtValueObjectGet(Data, xrtStrView("truncated")), &Truncated) ) goto invalid_response;
    for ( i = 0u; i < RequestId.Size; ++i )
        if ( !isxdigit((unsigned char)RequestId.Data[i]) ) goto invalid_response;
    if (Registered && memcmp(RequestId.Data,RequestKey,32u)!=0) goto invalid_response;
    Output = xrtValueObject();
    Results = xrtValueArray();
    if ( Output == NULL || Results == NULL ) goto memory_failed;
    for ( i = 0u; i < xrtValueCount(Items); ++i )
        if ( !MdoWebSearchApiItem(Results, xrtValueArrayGet(Items, i), Provider, Response.FetchedAt) )
            goto invalid_response;
    if ( !MdoWebObjectString(Output, "type", "web_search_results", 18u) ||
         !MdoWebObjectString(Output, "query", Query.Data, Query.Size) ||
         !MdoWebObjectBool(Output, "untrusted", true) ||
         !MdoWebObjectString(Output, "source", Provider.Data, Provider.Size) ||
         !MdoWebObjectString(Output, "request_id", RequestId.Data, RequestId.Size) ||
         !MdoWebObjectUInt(Output, "count", xrtValueCount(Results)) ||
         !MdoWebObjectBool(Output, "truncated", Truncated) ||
         !MdoWebObjectInt(Output, "fetched_at", Response.FetchedAt) ) goto memory_failed;
    /* MdoWebObjectTake releases on failure too; clear the local owner first. */
    {
        xvalue* Owned = Results;
        Results = NULL;
        if ( !MdoWebObjectTake(Output, "results", Owned) ) goto memory_failed;
    }
    if ( !MdoWebWriteFileResult(pContext, pWriter, Output, pError) ) { Result = XWORK_RESULT_ERROR; goto done; }
    Success = true;
    Result = XWORK_RESULT_OK;
    goto done;
invalid_response:
    Result = MdoWebToolFail(pWriter, pError,
        "Search API returned an invalid xadmin response. Do not repeat this query; report the service error to the user instead of inventing results.");
    goto done;
memory_failed:
    Result = MdoWebFail(pError, XWORK_ERROR_OUT_OF_MEMORY, "Cannot build search request or result");
done:
    if ( Attempted ) MdoWebRequestFinished(State, Success);
    MdoSecretRelease(&Authorization);
    MdoAccountRelease(&Lease);
    xrtFree(Body);
    State->Transport.ResponseUnit(State->Transport.Context, &Response);
    xrtValueRelease(Results);
    xrtValueRelease(Output);
    xrtValueRelease(Envelope);
    xrtValueRelease(Arguments);
    return Result;
}
