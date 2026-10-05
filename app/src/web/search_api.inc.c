/* xadmin is the only search protocol. Provider credentials stay on its server;
 * this client sends a member access token only to its account service.
 * No retry or redirect can duplicate billed searches or forward credentials. */
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
    case 403u: return "Search account access was denied. Complete the phone/email verification required by the service.";
    case 404u: return "Search service is unavailable. Its administrator must enable the search plugin.";
    case 405u: return "Search service does not accept this request. Contact its administrator.";
    case 429u: return "Search quota or concurrency limit reached. Try later; do not repeatedly retry.";
    case 502u: return "Search provider is unavailable or returned an invalid response. Try later.";
    case 503u: return "Search service is not ready. Its administrator must enable a provider and configure its API key.";
    case 504u: return "Search service timed out waiting for its provider. Try later.";
    default: return "Search API returned an unsuccessful response. Check the service; no search results were established.";
    }
}

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
    xjsonreadconfig JsonConfig;
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
acquire_account:
    if (!MdoAccountAcquire(Query.Data, pContext, &Lease)) {
        Result = MdoWebToolFail(pWriter, pError,
            "Search needs an account login. The login wait was skipped, cancelled or expired; no search was sent. Do not retry until the user signs in.");
        goto done;
    }
    Token = Lease.AccessToken;
    FetchContext = *pContext;
    if (Lease.Cancel) FetchContext.pCancel = Lease.Cancel;
    Authorization = (char*)xrtMalloc(strlen(Token) + 8u);
    if (!Body) Body = xrtJsonStringify(Arguments, false, &BodySize);
    if ( Authorization == NULL || Body == NULL ) goto memory_failed;
    snprintf(Authorization, strlen(Token) + 8u, "Bearer %s", Token);
    Headers[0] = (XS_FetchHeader){ "Accept", "application/json" };
    Headers[1] = (XS_FetchHeader){ "Content-Type", "application/json" };
    Headers[2] = (XS_FetchHeader){ "User-Agent", "mdo/1 web_search" };
    Headers[3] = (XS_FetchHeader){ "Authorization", Authorization };
    Attempted = true;
    if ( !MdoWebFetchRequest(State, &FetchContext, MdoAccountSearchEndpoint(),
            Headers, 4u, true, Body, BodySize, &Response) ) {
        /* Transport diagnostics may contain headers. Never reflect them into
         * a model-visible result containing an account credential. */
        const xerror* Cause = xrtGetError();
        xerrkind Kind = Cause != NULL ? xrtErrorKind(Cause) : XERR_IO;
        if ( (pContext->pCancel != NULL && xrtCancelRequested(pContext->pCancel)) || Kind == XERR_CANCELLED )
            Result = MdoWebFail(pError, XWORK_ERROR_CANCELLED, "Search request was cancelled");
        else if ( Kind == XERR_TIMEOUT || (pContext->uDeadline != XRT_DEADLINE_NEVER &&
                    xrtDeadlineExpired(pContext->uDeadline)) )
            Result = MdoWebFail(pError, XWORK_ERROR_TIMEOUT, "Search request timed out");
        else Result = MdoWebToolFail(pWriter, pError, "Cannot reach the search service. Check your network connection.");
        goto done;
    }
    if (Response.Status == 401 && !Renewed && MdoAccountRejectAccess(&Lease)) {
        Renewed = true;
        State->Transport.ResponseUnit(State->Transport.Context, &Response);
        memset(&Response, 0, sizeof(Response));
        MdoSecretRelease(&Authorization); MdoAccountRelease(&Lease); Token = NULL;
        goto acquire_account;
    }
    if (Lease.Managed) MdoAccountSearchStatus(Response.Status);
    if ( Response.Status != 200u ) {
        Result = MdoWebToolFail(pWriter, pError, MdoWebSearchServiceError(Response.Status));
        goto done;
    }
    xrtJsonReadConfigInit(&JsonConfig);
    JsonConfig.MaxInputBytes = State->Settings.MaxResponseBytes;
    JsonConfig.MaxDepth = 8u;
    JsonConfig.MaxValues = 512u;
    JsonConfig.MaxContainerItems = 64u;
    if ( Response.Body == NULL || !xrtUtf8Valid(
            (xstrview){ (const char*)Response.Body, Response.BodySize }, NULL) ) goto invalid_response;
    Envelope = xrtJsonRead((xstrview){ (const char*)Response.Body, Response.BodySize }, &JsonConfig);
    if ( Envelope == NULL || xrtValueType(Envelope) != XVALUE_OBJECT ||
         !MdoWebUnsigned(xrtValueObjectGet(Envelope, xrtStrView("code")), &Code) ) goto invalid_response;
    if ( Code != 0u ) {
        Result = MdoWebToolFail(pWriter, pError, MdoWebSearchServiceError(Code));
        goto done;
    }
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
    Output = xrtValueObject();
    Results = xrtValueArray();
    if ( Output == NULL || Results == NULL ) goto memory_failed;
    for ( i = 0u; i < xrtValueCount(Items); ++i )
        if ( !MdoWebSearchApiItem(Results, xrtValueArrayGet(Items, i), Provider, Response.FetchedAt) )
            goto invalid_response;
    if ( !MdoWebObjectString(Output, "type", "web_search_results", 18u) ||
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
    if ( !MdoWebWriteValue(pWriter, Output, pError) ) { Result = XWORK_RESULT_LIMIT; goto done; }
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
