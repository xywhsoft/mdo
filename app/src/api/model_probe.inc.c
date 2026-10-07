/* Settings probes never persist drafts or follow redirects with credentials.
 * Discovery verifies the catalog endpoint; only Test makes a model request. */
#include "../../include/mdo/http_url.h"
#include "../../include/mdo/model_http_sdk.h"
#include <ctype.h>

static xtlsverifydecision MdoApiModelAcceptTls(const xtlspeer* Peer, ptr Data)
{ (void)Peer; (void)Data; return XTLS_VERIFY_ACCEPT; }

/* Same endpoint patterns as the model transport: exact or edge wildcard. */
static bool MdoApiModelProxyBypassed(cstr Patterns, cstr Url)
{
    const char* Host = strstr(Url, "://"); size_t Length;
    if (!Host) return false;
    Host += 3u; Length = strcspn(Host, ":/?#");
    while (*Patterns) {
        const char* Start; size_t Size, i, Offset = 0u; bool Left, Right, Match;
        while (*Patterns == ' ' || *Patterns == ',' || *Patterns == ';') Patterns++;
        Start = Patterns; while (*Patterns && *Patterns != ',' && *Patterns != ';') Patterns++;
        Size = (size_t)(Patterns - Start); while (Size && Start[Size - 1u] == ' ') Size--;
        Left = Size && Start[0] == '*'; if (Left) { Start++; Size--; }
        Right = Size && Start[Size - 1u] == '*'; if (Right) Size--;
        if (!Size && (Left || Right)) return true;
        if (Size > Length || (!Left && !Right && Size != Length)) continue;
        if (Left && !Right) Offset = Length - Size;
        for (; Offset + Size <= Length; Offset++) {
            Match = true;
            for (i = 0u; i < Size; i++) if (tolower((unsigned char)Start[i]) != tolower((unsigned char)Host[Offset + i])) { Match = false; break; }
            if (Match) return true;
            if (!Left || !Right) break;
        }
    }
    return false;
}

static xhttpclient* MdoApiModelDiscoveryClient(MdoApiContext* Context, cstr Url, bool Verify)
{
    MdoConfigTransportSettings Transport = {0}; xhttpclientconfig Config;
    xtlsverifierconfig Tls; xtlsverifier* Verifier = NULL; xx509store* Store = NULL;
    xnetproxyconfig ProxyConfig; xnetproxy* Proxy = NULL; char* Password = NULL;
    xhttpclient* Client = NULL; xllm_error Error = {0};
    Transport.Size = sizeof(Transport);
    if (!Context->Request->server || !Context->Request->server->Engine || !MdoConfigGetTransportSettings(&Transport)) return NULL;
    xrtHttpClientConfigInit(&Config); Config.Timeout = 15000000u; Config.IdleTimeout = 10000000u;
    Config.Decompress.MaxBody = MDO_API_RESPONSE_MAX_BYTES;
    Config.Redirect.MaxHops = 0u; Config.Retry.MaxRetries = 0u;
    if (!Verify || Transport.CaPemPath[0]) {
        xrtTlsVerifierConfigInit(&Tls);
        if (!Verify) Tls.Verify = MdoApiModelAcceptTls;
        else { Store = MdoModelsLoadCaStore(Transport.CaPemPath, &Error); if (!Store) goto done; Tls.Store = Store; }
        Verifier = xrtTlsVerifierCreate(&Tls); if (!Verifier) goto done;
        Config.TlsVerifier = Verifier; Config.SystemTrust = false;
    }
    if (strcmp(Transport.ProxyKind, "none") && !MdoApiModelProxyBypassed(Transport.ProxyBypass, Url)) {
        xrtNetProxyConfigInit(&ProxyConfig);
        ProxyConfig.Type = strcmp(Transport.ProxyKind, "socks5") == 0 ? XNET_PROXY_SOCKS5 : XNET_PROXY_HTTP_CONNECT;
        ProxyConfig.Host = xrtStrView(Transport.ProxyHost); ProxyConfig.Port = Transport.ProxyPort;
        if (Transport.ProxySecretRef[0] && !MdoSecretResolve(xrtStrView(Transport.ProxySecretRef), 4096u, &Password)) goto done;
        ProxyConfig.Username = (xbytesview){(const unsigned char*)Transport.ProxyUser, strlen(Transport.ProxyUser)};
        ProxyConfig.Password = (xbytesview){(const unsigned char*)(Password ? Password : ""), Password ? strlen(Password) : 0u};
        Proxy = xrtNetProxyCreate(&ProxyConfig); if (!Proxy) goto done; Config.Proxy = Proxy;
    }
    Client = xrtHttpClientCreate(Context->Request->server->Engine, &Config);
done:
    MdoSecretRelease(&Password); xrtNetProxyRelease(Proxy); xrtTlsVerifierRelease(Verifier); xrtX509StoreFree(Store);
    return Client;
}

static bool MdoApiModelDiscoverRun(MdoApiContext* Context, MdoApiJsonBody Body)
{
    xvalue* Provider; xvalue* Endpoints; xvalue* Data = NULL; xvalue* Items = NULL; xvalue* Parsed = NULL;
    xhttprequest* Request = NULL; xhttpclient* Client = NULL; xhttpresult* Result = NULL;
    cstr Endpoint = NULL, Key = NULL, Ref = NULL; char Url[2056], Authorization[4104]; char* Secret = NULL;
    cstr Code = "connection_failed", Message = "Could not connect; check the address, network, proxy and TLS settings";
    uint16 Reply = 502u, HttpStatus = 0u; bool Verify = true, Anthropic = false, Ok = false; size_t i;
    Provider = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("provider"));
    Endpoints = xrtValueObjectGet(Provider, XRT_STR_LITERAL("endpoints"));
    Endpoint = MdoApiModelText(Endpoints, "chat_completions", 2048u);
    if (!Endpoint) Endpoint = MdoApiModelText(Endpoints, "responses", 2048u);
    if (!Endpoint) { Endpoint = MdoApiModelText(Endpoints, "anthropic_messages", 2048u); Anthropic = true; }
    if (!Endpoint || !MdoHttpUrlValid(xrtStrView(Endpoint), true) || strchr(Endpoint, '?')) {
        Reply = 422u; Code = "endpoint_invalid"; Message = "Enter a full model API endpoint URL"; goto done;
    }
    snprintf(Url, sizeof(Url), "%s", Endpoint);
    const char* Suffix = Anthropic ? "/messages" : (MdoApiModelText(Endpoints, "chat_completions", 2048u) ? "/chat/completions" : "/responses");
    size_t Length = strlen(Url), Tail = strlen(Suffix);
    if (Length <= Tail || strcmp(Url + Length - Tail, Suffix)) {
        Reply = 422u; Code = "catalog_unsupported"; Message = "This custom endpoint has no discoverable model catalog; add model IDs manually"; goto done;
    }
    strcpy(Url + Length - Tail, "/models");
    (void)xrtValueGetBool(xrtValueObjectGet(Provider, XRT_STR_LITERAL("verify_peer")), &Verify);
    Key = MdoApiModelText(Body.Value, "key", 4096u);
    if (xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("key")) && !Key) { Reply = 422u; Code = "key_invalid"; Message = "API key must be nonempty"; goto done; }
    if (Key) for (i = 0u; Key[i]; i++) if ((unsigned char)Key[i] < 33u || (unsigned char)Key[i] > 126u) { Reply = 422u; Code = "key_invalid"; Message = "API key contains invalid characters"; goto done; }
    Ref = MdoApiModelText(xrtValueObjectGet(Provider, XRT_STR_LITERAL("credential")), "secret_ref", 2048u);
    if (!Key && Ref) {
        if (!MdoSecretResolve(xrtStrView(Ref), 4096u, &Secret)) { Reply = 422u; Code = "credential_unavailable"; Message = "Saved key is unavailable; enter it again or check its reference"; goto done; }
        Key = Secret;
    }
    Client = MdoApiModelDiscoveryClient(Context, Url, Verify);
    Request = xrtHttpRequestCreate(XRT_STR_LITERAL("GET"), xrtStrView(Url));
    if (!Client || !Request) goto done;
    if (Key) {
        snprintf(Authorization, sizeof(Authorization), "%s%s", Anthropic ? "" : "Bearer ", Key);
        if (!xrtHttpRequestAddHeader(Request, xrtStrView(Anthropic ? "x-api-key" : "Authorization"), xrtStrView(Authorization))) goto done;
    }
    if (Anthropic && !xrtHttpRequestAddHeader(Request, XRT_STR_LITERAL("anthropic-version"), XRT_STR_LITERAL("2023-06-01"))) goto done;
    xhttpcalloptions Options; xrtHttpCallOptionsInit(&Options);
    Options.Cancel = Context->SendCancel;
    Options.ResponseBodyLimit = MDO_API_RESPONSE_MAX_BYTES; Options.Redirect = XHTTP_REDIRECT_MANUAL; Options.Retry.Mode = XHTTP_RETRY_DISABLED;
    Result = xrtHttpClientDoSync(Client, Request, &Options);
    const xhttpresponse* Response = Result ? xrtHttpResultResponse(Result) : NULL;
    if (!Response) goto done;
    HttpStatus = xrtHttpResponseStatus(Response);
    if (HttpStatus == 401u || HttpStatus == 403u) { Reply = 422u; Code = "authentication_failed"; Message = "The supplier rejected this key; check its API product and permissions"; goto done; }
    if (HttpStatus == 404u || HttpStatus == 405u || HttpStatus == 501u) { Reply = 422u; Code = "catalog_unsupported"; Message = "This supplier does not expose a model list; use template models or add an ID manually"; goto done; }
    if (HttpStatus == 429u) { Reply = 429u; Code = "supplier_rate_limited"; Message = "The supplier is rate limiting requests; retry later"; goto done; }
    if (HttpStatus < 200u || HttpStatus >= 300u) goto done;
    xbytesview Bytes = xrtHttpResponseBody(Response);
    Parsed = xrtJsonParse(xrtStrViewN((cstr)Bytes.Data, Bytes.Size));
    xvalue* List = xrtValueObjectGet(Parsed, XRT_STR_LITERAL("data"));
    if (xrtValueType(List) != XVALUE_ARRAY) { Code = "catalog_invalid"; Message = "The supplier returned an unreadable model list"; goto done; }
    Data = xrtValueObject(); Items = xrtValueArray(); if (!Data || !Items) goto done;
    for (i = 0u; i < xrtValueCount(List) && i < 2048u; i++) {
        cstr Id = MdoApiModelText(xrtValueArrayGet(List, i), "id", 256u);
        if (Id && !MdoApiValueAppendString(Items, Id)) goto done;
    }
    Ok = MdoApiValueSetTake(Data, "items", &Items) && MdoApiValueSetUInt(Data, "http_status", HttpStatus) &&
        MdoApiValueSetBool(Data, "truncated", xrtValueCount(List) > 2048u);
done:
    xrtSecureZero(Authorization, sizeof(Authorization));
    if (Key && Key != Secret) xrtSecureZero((void*)Key, strlen(Key));
    MdoSecretRelease(&Secret); xrtValueRelease(Parsed); xrtValueRelease(Items);
    xrtHttpResultDestroy(Result); xrtHttpRequestDestroy(Request); xrtHttpClientDestroy(Client);
    xrtClearError();
    if (Ok) return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    xrtValueRelease(Data); return MdoApiReplyError(Context, Reply, Code, Message, NULL);
}

static bool MdoApiModelTestRun(MdoApiContext* Context, MdoApiJsonBody Body)
{
    MdoModelCatalog* Catalog = NULL; xllm_client* Client = NULL; xllm_response* Response = NULL;
    xllm_request Request; xllm_error Error = {0}; MdoModelClientOptions Options;
    MdoModelInfo Info = {0}; xvalue* Data = NULL; bool Ok = false, Tools = false; uint64 Started = xrtClock();
    cstr Id = MdoApiModelText(Body.Value, "model_id", 128u);
    xvalue* ToolMode = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("tools"));
    if (ToolMode && !xrtValueGetBool(ToolMode, &Tools))
        return MdoApiReplyError(Context, 422u, "probe_invalid", "tools must be a boolean", NULL);
    Catalog = MdoModelCatalogSnapshot(); Info.Size = sizeof(Info);
    xllmRequestInit(&Request); MdoModelClientOptionsInit(&Options); Options.ModelId = Id;
    if (!Id || !MdoModelCatalogModelFind(Catalog, Id, &Info)) {
        MdoModelCatalogRelease(Catalog);
        return MdoApiReplyError(Context, 404u, "model_not_found", "Save the model before testing it", NULL);
    }
    Options.MaxOutputTokens = Info.MaxOutputTokens < 256u ? Info.MaxOutputTokens : 256u;
    bool Online = MdoModelIsOnline(Catalog, Id);
    if (!Online) Client = MdoModelClientCreate(Catalog, &Options, NULL, &Error);
    Request.bStream = false; Request.bParallelToolCalls = false;
    Request.uDeadline = Context->SendDeadline;
    xllmRequestSetCancel(&Request, Context->SendCancel);
    if (Tools) {
        Request.eToolChoice = XLLM_TOOL_CHOICE_REQUIRED;
        if (!xllmRequestAddTool(&Request, "mdo_connection_probe", "Return a connection check. This tool is never executed.",
            "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}", false)) { xllmClientDestroy(Client); Client = NULL; }
    }
    if ((Client || Online) && xllmRequestAddTextMessage(&Request, XLLM_ROLE_USER, Tools ? "Call mdo_connection_probe once." : "Reply briefly with OK."))
        Ok = (Online ? MdoModelOnlineComplete(Catalog, &Options, &Request, NULL, &Response, &Error) :
            MdoModelComplete(Client, &Request, NULL, &Response, &Error)) == XLLM_RESULT_OK;
    if (Tools) Ok = Ok && Response && Response->iToolCallCount == 1u && Response->pToolCalls[0].sName &&
        strcmp(Response->pToolCalls[0].sName, "mdo_connection_probe") == 0;
    else if (Ok && (!Response || ((!Response->sContent || !Response->sContent[0]) && (!Response->sReasoningContent || !Response->sReasoningContent[0])))) Ok = false;
    if (Ok) {
        Data = xrtValueObject(); Ok = Data && MdoApiValueSetBool(Data, "verified", true) &&
            MdoApiValueSetUInt(Data, "latency_ms", (xrtClock() - Started) / 1000u);
    }
    xllmResponseDestroy(Response); xllmRequestUnit(&Request); xllmClientDestroy(Client);
    MdoModelCatalogRelease(Catalog); xrtClearError();
    if (Ok) return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    xrtValueRelease(Data);
    return MdoApiReplyError(Context, 422u, Error.eCode == XLLM_ERROR_NONE ?
        (Tools ? "model_tools_test_failed" : "model_test_failed") : MdoModelErrorKind(&Error),
        Error.eCode == XLLM_ERROR_NONE ? "The model response did not satisfy the connection test" :
        MdoModelErrorMessage(&Error), NULL);
}

typedef struct MdoModelProbeJob {
    MdoApiContext Context;
    XS_HttpReq Request;
    xhttp1head Head;
    MdoApiJsonBody Body;
    bool Test, Sent;
} MdoModelProbeJob;
static struct { xmutex* Lock; xtaskpool* Pool; size_t Count; bool Stopping; } g_MdoModelProbes;

bool MdoApiModelProbesInit(void)
{ g_MdoModelProbes.Lock = xrtMutexCreate(); return g_MdoModelProbes.Lock != NULL; }

void MdoApiModelProbesUnit(void)
{
    if (!g_MdoModelProbes.Lock) return;
    xrtMutexLock(g_MdoModelProbes.Lock); g_MdoModelProbes.Stopping = true;
    xtaskpool* Pool = g_MdoModelProbes.Pool; xrtMutexUnlock(g_MdoModelProbes.Lock);
    if (Pool) { xrtTaskPoolCancel(Pool); xrtTaskPoolWait(Pool); xrtTaskPoolDestroy(Pool); }
    xrtMutexDestroy(g_MdoModelProbes.Lock); memset(&g_MdoModelProbes, 0, sizeof(g_MdoModelProbes));
}

static void MdoApiModelProbeDrop(ptr Value, ptr UserData)
{
    MdoModelProbeJob* Job = Value; (void)UserData;
    MdoApiModelSecretBodyUnit(&Job->Body);
    if (Job->Request.tls) {
        if (Job->Sent) xrtTlsStreamClose(Job->Request.tls); else xrtTlsStreamAbort(Job->Request.tls);
        xrtTlsStreamDestroy(Job->Request.tls);
    }
    if (Job->Request.tcp) {
        if (Job->Sent) xrtNetStreamClose(Job->Request.tcp); else xrtNetStreamAbort(Job->Request.tcp);
        xrtNetStreamDestroy(Job->Request.tcp);
    }
    xsServerRelease(Job->Request.server);
    xrtMutexLock(g_MdoModelProbes.Lock); g_MdoModelProbes.Count--; xrtMutexUnlock(g_MdoModelProbes.Lock);
    xrtFree(Job);
}

static xtaskoutcome MdoApiModelProbeRun(xcancel* Cancel, ptr Value, xtaskvalue* Result)
{
    MdoModelProbeJob* Job = Value; (void)Result; Job->Context.SendCancel = Cancel;
    if (!MdoApiDownloadLive(&Job->Context)) return XTASK_CANCELLED;
    Job->Sent = Job->Test ? MdoApiModelTestRun(&Job->Context, Job->Body) : MdoApiModelDiscoverRun(&Job->Context, Job->Body);
    return Job->Sent ? XTASK_SUCCESS : XTASK_FAILED;
}

static bool MdoApiModelProbeStart(MdoApiContext* Context, bool Test)
{
    MdoModelProbeJob* Job = xrtCalloc(1u, sizeof(*Job)); xfuture* Future = NULL; xtaskargs Args = {0};
    if (!Job) return MdoApiReplyError(Context, 503u, "probe_unavailable", "Model test resources are unavailable", NULL);
    MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Job->Body);
    if (Status != MDO_API_BODY_OK) { xrtFree(Job); return MdoApiReplyBodyError(Context, Status); }
    Job->Head.MethodCode = Context->Request->head->MethodCode; Job->Request.head = &Job->Head;
    Job->Request.server = xsServerRetain(Context->Request->server);
    if (Context->Request->tls) Job->Request.tls = xrtTlsStreamRef(Context->Request->tls);
    else Job->Request.tcp = xrtNetStreamRef(Context->Request->tcp);
    Job->Context.Request = &Job->Request; Job->Test = Test;
    memcpy(Job->Context.RequestId, Context->RequestId, sizeof(Job->Context.RequestId));
    Job->Context.SendDeadline = xrtDeadlineAfter(Test ? 30000000u : 15000000u); Job->Context.CloseResponse = true;
    xrtMutexLock(g_MdoModelProbes.Lock);
    if (!g_MdoModelProbes.Stopping && g_MdoModelProbes.Count < 2u) {
        if (!g_MdoModelProbes.Pool) { xtaskpoolconfig Config = {0}; Config.Threads = 1u; Config.QueueLimit = 1u; g_MdoModelProbes.Pool = xrtTaskPoolCreate(&Config); }
        if (g_MdoModelProbes.Pool) {
            g_MdoModelProbes.Count++; Args.Destroy = MdoApiModelProbeDrop;
            Future = xrtTaskSubmit(g_MdoModelProbes.Pool, MdoApiModelProbeRun, Job, &Args);
            if (!Future) g_MdoModelProbes.Count--;
        }
    }
    xrtMutexUnlock(g_MdoModelProbes.Lock);
    if (Future) { xrtFutureDestroy(Future); Context->Takeover = true; return true; }
    MdoApiModelSecretBodyUnit(&Job->Body); xsServerRelease(Job->Request.server);
    xrtTlsStreamDestroy(Job->Request.tls); xrtNetStreamDestroy(Job->Request.tcp); xrtFree(Job);
    return MdoApiReplyError(Context, 503u, "probe_busy", "Another model test is running; retry shortly", NULL);
}

bool MdoApiModelDiscoverRoute(MdoApiContext* Context) { return MdoApiModelProbeStart(Context, false); }
bool MdoApiModelTestRoute(MdoApiContext* Context) { return MdoApiModelProbeStart(Context, true); }
