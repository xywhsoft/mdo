#include "../../include/mdo/time.h"
/* Settings probes never persist drafts or follow redirects with credentials.
 * Discovery verifies the catalog endpoint; only Test makes a model request. */
#include "../../include/mdo/http_url.h"
#include "../../include/mdo/model_http_sdk.h"
#include <ctype.h>
#include "../../include/mdo/account.h"
#include "model_parameters.inc.c"

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

static xhttpclient* MdoApiModelDiscoveryClient(MdoApiContext* Context, cstr Url, bool Verify,
    const xvalue* ProviderProxy, uint64 Timeout, xllm_error* Error)
{
    MdoConfigTransportSettings Transport = {0}; xhttpclientconfig Config;
    xtlsverifierconfig Tls; xtlsverifier* Verifier = NULL; xx509store* Store = NULL;
    xnetproxyconfig ProxyConfig; xnetproxy* Proxy = NULL; char* Password = NULL;
    xhttpclient* Client = NULL; XS_SystemProxyInfo SystemProxy = {0};
    Transport.Size = sizeof(Transport);
    if (!Context->Request->server || !Context->Request->server->Engine ||
        !MdoModelProxySettings(ProviderProxy, &Transport, Error)) return NULL;
    xrtHttpClientConfigInit(&Config); Config.Timeout = Timeout; Config.IdleTimeout = Timeout < 10000000u ? Timeout : 10000000u;
    Config.Decompress.MaxBody = MDO_API_RESPONSE_MAX_BYTES;
    Config.Redirect.MaxHops = 0u; Config.Retry.MaxRetries = 0u;
    if (!Verify || Transport.CaPemPath[0]) {
        xrtTlsVerifierConfigInit(&Tls);
        if (!Verify) Tls.Verify = MdoApiModelAcceptTls;
        else { Store = MdoModelsLoadCaStore(Transport.CaPemPath, Error); if (!Store) goto done; Tls.Store = Store; }
        Verifier = xrtTlsVerifierCreate(&Tls); if (!Verifier) goto done;
        Config.TlsVerifier = Verifier; Config.SystemTrust = false;
    }
    bool System = !strcmp(Transport.ProxyKind, "system");
    if (System) {
        SystemProxy.Size = sizeof(SystemProxy); SystemProxy.Version = XS_SYSTEM_PROXY_VERSION;
        if (!xsSystemProxyResolve(Url, 5000u, &SystemProxy)) {
            MdoModelProfileErrorInit(Error, "profile_system_proxy_unavailable", SystemProxy.Reason);
            goto done;
        }
        if (SystemProxy.Kind != XS_SYSTEM_PROXY_DIRECT) {
            xrtNetProxyConfigInit(&ProxyConfig);
            ProxyConfig.Type = SystemProxy.Kind == XS_SYSTEM_PROXY_SOCKS5 ? XNET_PROXY_SOCKS5 : XNET_PROXY_HTTP_CONNECT;
            ProxyConfig.Host = xrtStrView(SystemProxy.Host); ProxyConfig.Port = SystemProxy.Port;
            ProxyConfig.Username = (xbytesview){(const unsigned char*)SystemProxy.User, strlen(SystemProxy.User)};
            ProxyConfig.Password = (xbytesview){(const unsigned char*)SystemProxy.Password, strlen(SystemProxy.Password)};
            Proxy = xrtNetProxyCreate(&ProxyConfig); if (!Proxy) goto done; Config.Proxy = Proxy;
        }
    } else if (strcmp(Transport.ProxyKind, "none") && !MdoApiModelProxyBypassed(Transport.ProxyBypass, Url)) {
        xrtNetProxyConfigInit(&ProxyConfig);
        ProxyConfig.Type = strcmp(Transport.ProxyKind, "socks5") == 0 ? XNET_PROXY_SOCKS5 : XNET_PROXY_HTTP_CONNECT;
        ProxyConfig.Host = xrtStrView(Transport.ProxyHost); ProxyConfig.Port = Transport.ProxyPort;
        if (Transport.ProxySecretRef[0] && !MdoSecretResolve(xrtStrView(Transport.ProxySecretRef), 4096u, &Password)) {
            MdoModelProfileErrorInit(Error, "profile_proxy_unavailable", "model proxy password reference is unavailable");
            xrtClearError(); goto done;
        }
        ProxyConfig.Username = (xbytesview){(const unsigned char*)Transport.ProxyUser, strlen(Transport.ProxyUser)};
        ProxyConfig.Password = (xbytesview){(const unsigned char*)(Password ? Password : ""), Password ? strlen(Password) : 0u};
        Proxy = xrtNetProxyCreate(&ProxyConfig); if (!Proxy) goto done; Config.Proxy = Proxy;
    }
    Client = xrtHttpClientCreate(Context->Request->server->Engine, &Config);
    if (Client && Proxy && xrtHttpClientProxy(Client) != Proxy) {
        /* A host built without HTTP proxy support must not silently dial the
         * origin directly. This symbol is also required by host ABI checks. */
        xrtHttpClientDestroy(Client); Client = NULL;
        MdoModelProfileErrorInit(Error, "profile_proxy_invalid", "the HTTP transport did not retain the selected proxy");
    }
done:
    xrtSecureZero(&SystemProxy, sizeof(SystemProxy));
    MdoSecretRelease(&Password); xrtNetProxyRelease(Proxy); xrtTlsVerifierRelease(Verifier); xrtX509StoreFree(Store);
    return Client;
}

#include "model_catalog_client.inc.c"

static bool MdoApiModelDiscoverRun(MdoApiContext* Context, MdoApiJsonBody Body, bool Parameters)
{
    xvalue* Provider; xvalue* Endpoints; xvalue* Data = NULL; xvalue* Items = NULL; xvalue* Parsed = NULL; xvalue* List = NULL;
    xhttprequest* Request = NULL; xhttpclient* Client = NULL; xhttpresult* Result = NULL;
    cstr Endpoint = NULL, Key = NULL, Ref = NULL; char Url[2056], Authorization[4104]; char* Secret = NULL;
    cstr Code = "connection_failed", Message = "Could not connect; check the address, network, proxy and TLS settings";
    uint16 Reply = 502u, HttpStatus = 0u; bool Verify = true, Anthropic = false, Ok = false; size_t i;
    bool RefreshCatalog = true;
    const xvalue* RefreshValue = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("refresh_catalog"));
    if (Parameters && RefreshValue && !xrtValueGetBool(RefreshValue, &RefreshCatalog))
        return MdoApiReplyError(Context, 422u, "parameters_invalid", "refresh_catalog must be a boolean", NULL);
    Provider = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("provider"));
    Endpoints = xrtValueObjectGet(Provider, XRT_STR_LITERAL("endpoints"));
    const xvalue* Selected = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("models"));
    cstr Protocol = MdoApiModelText(Body.Value, "protocol", 64);
    if (Parameters) {
        if (xrtValueType(Selected) != XVALUE_ARRAY || !xrtValueCount(Selected) || xrtValueCount(Selected) > 32u ||
            !Protocol || (strcmp(Protocol, "openai-chat-completions") && strcmp(Protocol, "openai-responses") && strcmp(Protocol, "anthropic-messages")))
            return MdoApiReplyError(Context, 422u, "parameters_invalid", "Select 1 to 32 model IDs and an available protocol", NULL);
        for (size_t n = 0; n < xrtValueCount(Selected); ++n) {
            xstrview Id = {0};
            if (!xrtValueGetString(xrtValueArrayGet(Selected, n), &Id) || !Id.Size || Id.Size > 256 || memchr(Id.Data, 0, Id.Size))
                return MdoApiReplyError(Context, 422u, "parameters_invalid", "Model IDs must contain 1 to 256 characters", NULL);
        }
        Anthropic = !strcmp(Protocol, "anthropic-messages");
        Endpoint = MdoApiModelText(Endpoints, Anthropic ? "anthropic_messages" : !strcmp(Protocol, "openai-responses") ? "responses" : "chat_completions", 2048u);
    } else Endpoint = MdoApiModelText(Endpoints, "chat_completions", 2048u);
    if (!Parameters && !Endpoint) Endpoint = MdoApiModelText(Endpoints, "responses", 2048u);
    if (!Parameters && !Endpoint) { Endpoint = MdoApiModelText(Endpoints, "anthropic_messages", 2048u); Anthropic = true; }
    if (!Endpoint || !MdoHttpUrlValid(xrtStrView(Endpoint), true) || strchr(Endpoint, '?')) {
        Reply = 422u; Code = "endpoint_invalid"; Message = "Enter a full model API endpoint URL"; goto done;
    }
    snprintf(Url, sizeof(Url), "%s", Endpoint);
    const char* Suffix = Anthropic ? "/messages" : ((Parameters ? !strcmp(Protocol, "openai-chat-completions") : MdoApiModelText(Endpoints, "chat_completions", 2048u) != NULL) ? "/chat/completions" : "/responses");
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
    /* OpenAI's public catalog has IDs only. Do not send another network
     * request to learn that it has no parameter metadata. */
    if (Parameters && !strncmp(Endpoint, "https://api.openai.com/v1/", 26u)) { Code = "not_exposed"; goto done; }
    xllm_error DiscoveryError = {0};
    Client = MdoApiModelDiscoveryClient(Context, Url, Verify,
        xrtValueObjectGet(Provider, XRT_STR_LITERAL("proxy")), 15000000u, &DiscoveryError);
    char ProxyFailureCode[64];
    if (!Client && (!strcmp(MdoModelErrorKind(&DiscoveryError), "profile_system_proxy_unavailable") ||
                   !strcmp(MdoModelErrorKind(&DiscoveryError), "profile_proxy_invalid") ||
                   !strcmp(MdoModelErrorKind(&DiscoveryError), "profile_proxy_unavailable"))) {
        snprintf(ProxyFailureCode, sizeof(ProxyFailureCode), "model_%s", MdoModelErrorKind(&DiscoveryError));
        Code = ProxyFailureCode; Reply = 422u;
        Message = MdoModelErrorMessage(&DiscoveryError);
        goto done;
    }
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
    List = xrtValueObjectGet(Parsed, XRT_STR_LITERAL("data"));
    if (xrtValueType(List) != XVALUE_ARRAY) { Code = "catalog_invalid"; Message = "The supplier returned an unreadable model list"; goto done; }
    if (Parameters) { Code = "ok"; goto done; }
    Data = xrtValueObject(); Items = xrtValueArray(); if (!Data || !Items) goto done;
    for (i = 0u; i < xrtValueCount(List) && i < 2048u; i++) {
        cstr Id = MdoApiModelText(xrtValueArrayGet(List, i), "id", 256u);
        if (Id && !MdoApiValueAppendString(Items, Id)) goto done;
    }
    Ok = MdoApiValueSetTake(Data, "items", &Items) && MdoApiValueSetUInt(Data, "http_status", HttpStatus) &&
        MdoApiValueSetBool(Data, "truncated", xrtValueCount(List) > 2048u);
done:
    if (Parameters && Endpoint && strcmp(Code, "endpoint_invalid") && strcmp(Code, "key_invalid") &&
        strcmp(Code, "credential_unavailable") && strcmp(Code, "authentication_failed")) {
        bool NeedCatalog = false;
        for (size_t n = 0; n < xrtValueCount(Selected); ++n) {
            const xvalue* Row = NULL;
            for (size_t k = 0; k < xrtValueCount(List); ++k) {
                const xvalue* Candidate = xrtValueArrayGet(List, k);
                if (xrtValueEqual(xrtValueObjectGet(Candidate, XRT_STR_LITERAL("id")), xrtValueArrayGet(Selected, n))) { Row = Candidate; break; }
            }
            xvalue* Fields = MdoParameterApiFields(Row);
            NeedCatalog |= !xrtValueObjectHas(Fields, XRT_STR_LITERAL("context_tokens")) ||
                !xrtValueObjectHas(Fields, XRT_STR_LITERAL("max_output_tokens")) || !xrtValueObjectHas(Fields, XRT_STR_LITERAL("reasoning"));
            xrtValueRelease(Fields);
        }
        bool Stale = false;
        xvalue* Registry = NeedCatalog ? MdoParameterRegistry(Context, RefreshCatalog, &Stale) : NULL;
        Data = xrtValueObject(); Items = xrtValueArray();
        Ok = Data && Items;
        for (size_t n = 0; Ok && n < xrtValueCount(Selected); ++n) {
            xstrview Id; char Name[257]; xrtValueGetString(xrtValueArrayGet(Selected, n), &Id);
            memcpy(Name, Id.Data, Id.Size); Name[Id.Size] = 0; const xvalue* Row = NULL;
            for (size_t k = 0; k < xrtValueCount(List); ++k) {
                const xvalue* Candidate = xrtValueArrayGet(List, k);
                if (MdoParameterTextEqual(xrtValueObjectGet(Candidate, XRT_STR_LITERAL("id")), Name)) { Row = Candidate; break; }
            }
            xvalue* Resolved = MdoParameterResolve(Registry, Endpoint, Protocol, Name, Row, Code);
            Ok = Resolved && MdoApiValueSetBool(Resolved, "catalog_stale", Stale) &&
                MdoApiValueSetBool(Resolved, "catalog_unavailable", NeedCatalog && !Registry) && MdoApiValueAppendTake(Items, &Resolved); xrtValueRelease(Resolved);
        }
        Ok = Ok && MdoApiValueSetTake(Data, "items", &Items); xrtValueRelease(Registry);
    }
    xrtSecureZero(Authorization, sizeof(Authorization));
    if (Key && Key != Secret) xrtSecureZero((void*)Key, strlen(Key));
    MdoSecretRelease(&Secret); xrtValueRelease(Parsed); xrtValueRelease(Items);
    xrtHttpResultDestroy(Result); xrtHttpRequestDestroy(Request); xrtHttpClientDestroy(Client);
    xrtClearError();
    if (Ok) return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
    xrtValueRelease(Data); return MdoApiReplyError(Context, Reply, Code, Message, NULL);
}

static bool MdoApiModelProbeVisibleReply(const xllm_response* Response)
{
    if (!Response || !Response->sContent) return false;
    for (const unsigned char* p = (const unsigned char*)Response->sContent; *p; ++p)
        if (*p > 32u) return true;
    return false;
}

static bool MdoApiModelProbeToolReply(const xllm_response* Response)
{
    if (!Response || Response->iToolCallCount != 1u || !Response->pToolCalls[0].sId ||
        !Response->pToolCalls[0].sId[0] || !Response->pToolCalls[0].sName ||
        strcmp(Response->pToolCalls[0].sName, "mdo_connection_probe") || !Response->pToolCalls[0].sArgumentsJson) return false;
    xvalue* Args = xrtJsonParse(xrtStrView(Response->pToolCalls[0].sArgumentsJson));
    bool Ok = Args && xrtValueType(Args) == XVALUE_OBJECT && xrtValueCount(Args) == 0u;
    xrtValueRelease(Args); return Ok;
}

static bool MdoApiModelProbeAddTools(xllm_request* Request)
{
    /* Snapshot metadata only. No user Agent lifecycle, history, tool
     * discovery, execution or permission callback runs during verification. */
    xwork_tool_catalog* Catalog = MdoModelVerificationTools(); bool Ok = Catalog != NULL;
    for (size_t i = 0u; Ok && i < xworkToolCatalogCount(Catalog); ++i) {
        xwork_tool_info Tool = {0};
        if (!xworkToolCatalogToolAt(Catalog, i, &Tool)) { Ok = false; break; }
        if (MdoBuiltinToolFind(Tool.sName))
            Ok = xllmRequestAddTool(Request, Tool.sName, Tool.sDescription, Tool.sParametersJson, Tool.bStrict);
    }
    xworkToolCatalogRelease(Catalog);
    return Ok && xllmRequestAddTool(Request, "mdo_connection_probe", "Return a connection check. This tool is never executed.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}", false) &&
        xllmRequestSetToolChoice(Request, XLLM_TOOL_CHOICE_NAMED, "mdo_connection_probe");
}

static bool MdoApiModelTestRun(MdoApiContext* Context, MdoApiJsonBody Body)
{
    MdoModelCatalog* Catalog = NULL; xllm_client* Client = NULL; xllm_response* Response = NULL;
    xllm_request Request; xllm_error Error = {0}; MdoModelClientOptions Options;
    MdoModelInfo Info = {0}; xvalue* Data = NULL; bool Ok = false, Tools = false; uint64 Started = MdoMonotonicUs();
    char Fingerprint[65], Current[65]; xvalue* Verification = NULL;
    cstr Id = MdoApiModelText(Body.Value, "model_id", 128u);
    xvalue* ToolMode = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("tools"));
    if (ToolMode && !xrtValueGetBool(ToolMode, &Tools))
        return MdoApiReplyError(Context, 422u, "probe_invalid", "tools must be a boolean", NULL);
    /* Wait for a settings transaction to finish before pairing its revision
     * with the runtime catalog. Otherwise a new JSON profile could be paired
     * with an old catalog while a reload is still being published. */
    MdoSettingsServiceSnapshot Settings = {0}; MdoConfigSnapshot Revision = {0};
    Settings.Size = sizeof(Settings); Revision.Size = sizeof(Revision);
    if (!MdoSettingsServiceGetSnapshot(&Settings) || !Settings.Initialized || Settings.Degraded)
        return MdoApiReplyError(Context, 503u, "model_verification_unavailable", "Model configuration is not ready for verification", NULL);
    Catalog = MdoModelCatalogSnapshot(); Info.Size = sizeof(Info);
    xllmRequestInit(&Request); MdoModelClientOptionsInit(&Options); Options.ModelId = Id;
    if (!Id || !MdoModelCatalogModelFind(Catalog, Id, &Info)) {
        MdoModelCatalogRelease(Catalog);
        return MdoApiReplyError(Context, 404u, "model_not_found", "Save the model before testing it", NULL);
    }
    if (Tools && !(Info.Capabilities & XLLM_CAP_TOOL_CALL_OUT)) {
        MdoModelCatalogRelease(Catalog);
        return MdoApiReplyError(Context, 422u, "model_tools_unsupported", "This model profile does not declare tool calling", NULL);
    }
    bool NeedsTools = (Info.Capabilities & XLLM_CAP_TOOL_CALL_OUT) != 0u;
    bool Persist = MdoApiModelVerificationSnapshot(Id, Fingerprint);
    if ((!Persist && !Info.Builtin) || !MdoConfigGetSnapshot(&Revision) || Revision.Revision != Settings.Config.Revision) {
        MdoModelCatalogRelease(Catalog);
        return MdoApiReplyError(Context, 409u, "model_config_changed", "Read the current model configuration before testing", NULL);
    }
    if (Persist) {
        Verification = MdoApiModelVerificationUpdate(Id, Fingerprint, Tools, NeedsTools, "pending", 0u, "");
        if (!Verification) {
            MdoModelCatalogRelease(Catalog);
            return MdoApiReplyError(Context, 503u, "model_verification_unavailable", "The model verification result could not be saved", NULL);
        }
        xrtValueRelease(Verification); Verification = NULL;
    }
    Options.MaxOutputTokens = Info.MaxOutputTokens; /* Validate the saved runtime budget and reasoning selection together. */
    bool Online = MdoModelIsOnline(Catalog, Id);
    if (!Online) Client = MdoModelClientCreate(Catalog, &Options, NULL, &Error);
    Request.bStream = true; Request.bParallelToolCalls = false;
    Request.uDeadline = Context->SendDeadline;
    xllmRequestSetCancel(&Request, Context->SendCancel);
    bool Prepared = !Tools || MdoApiModelProbeAddTools(&Request);
    if (!Prepared) { Error.eCode = XLLM_ERROR_OUT_OF_MEMORY;
        snprintf(Error.sMessage, sizeof(Error.sMessage), "Model verification tool descriptors are unavailable"); }
    if (Prepared && (Client || Online) && xllmRequestAddTextMessage(&Request, XLLM_ROLE_USER, Tools ? "Call mdo_connection_probe once." : "Reply briefly with OK."))
        Ok = (Online ? MdoModelOnlineComplete(Catalog, &Options, &Request, NULL, &Response, &Error) :
            MdoModelComplete(Client, &Request, NULL, &Response, &Error)) == XLLM_RESULT_OK;
    Ok = Ok && (Tools ? MdoApiModelProbeToolReply(Response) : MdoApiModelProbeVisibleReply(Response));
    uint64 Latency = (MdoMonotonicUs() - Started) / 1000u;
    xllmResponseDestroy(Response); xllmRequestUnit(&Request); xllmClientDestroy(Client);
    MdoModelCatalogRelease(Catalog); xrtClearError();
    char Code[64];
    snprintf(Code, sizeof(Code), "model_%s", Error.eCode == XLLM_ERROR_NONE ?
        (Tools ? "tools_test_failed" : "test_failed") : MdoModelErrorKind(&Error));
    if (Persist) {
        if (!MdoConfigGetSnapshot(&Revision) || Revision.Revision != Settings.Config.Revision ||
            !MdoApiModelVerificationSnapshot(Id, Current) || strcmp(Fingerprint, Current))
            return MdoApiReplyError(Context, 409u, "model_config_changed", "Model configuration changed during the test; run verification again", NULL);
        Verification = MdoApiModelVerificationUpdate(Id, Fingerprint, Tools, NeedsTools,
            Ok ? "passed" : "failed", Latency, Ok ? "" : Code);
        if (!Verification)
            return MdoApiReplyError(Context, 503u, "model_verification_unavailable", "The model verification result could not be saved", NULL);
    }
    if (Ok) {
        Data = xrtValueObject(); Ok = Data && MdoApiValueSetBool(Data, "verified", true) &&
            MdoApiValueSetUInt(Data, "latency_ms", Latency) &&
            (!Verification || MdoApiValueSetTake(Data, "verification", &Verification));
        xrtValueRelease(Verification);
        if (Ok) return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 503u, "model_verification_unavailable", "The model verification response is unavailable", NULL);
    }
    xrtValueRelease(Verification);
    return MdoApiReplyError(Context, 422u, Error.eCode == XLLM_ERROR_NONE ?
        (Tools ? "model_tools_test_failed" : "model_test_failed") : Code,
        Error.eCode == XLLM_ERROR_NONE ? "The model response did not satisfy the connection test" :
        MdoModelErrorMessage(&Error), NULL);
}

typedef struct MdoModelProbeJob {
    MdoApiContext Context;
    XS_HttpReq Request;
    xhttp1head Head;
    MdoApiJsonBody Body;
    bool Test, Parameters, Catalog, Sent;
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
    Job->Sent = Job->Catalog ? MdoApiModelProvidersRun(&Job->Context) : Job->Test ? MdoApiModelTestRun(&Job->Context, Job->Body) : MdoApiModelDiscoverRun(&Job->Context, Job->Body, Job->Parameters);
    return Job->Sent ? XTASK_SUCCESS : XTASK_FAILED;
}

static bool MdoApiModelProbeStart(MdoApiContext* Context, bool Test, bool Parameters, bool Catalog)
{
    MdoModelProbeJob* Job = xrtCalloc(1u, sizeof(*Job)); xfuture* Future = NULL; xtaskargs Args = {0};
    if (!Job) return MdoApiReplyError(Context, 503u, "probe_unavailable", "Model test resources are unavailable", NULL);
    MdoApiBodyStatus Status = Catalog ? MDO_API_BODY_OK : MdoApiJsonBodyRead(Context, &Job->Body);
    if (Status != MDO_API_BODY_OK) { xrtFree(Job); return MdoApiReplyBodyError(Context, Status); }
    Job->Head.MethodCode = Context->Request->head->MethodCode; Job->Request.head = &Job->Head;
    Job->Request.server = xsServerRetain(Context->Request->server);
    if (Context->Request->tls) Job->Request.tls = xrtTlsStreamRef(Context->Request->tls);
    else Job->Request.tcp = xrtNetStreamRef(Context->Request->tcp);
    Job->Context.Request = &Job->Request; Job->Test = Test; Job->Parameters = Parameters; Job->Catalog = Catalog;
    memcpy(Job->Context.RequestId, Context->RequestId, sizeof(Job->Context.RequestId));
    Job->Context.SendDeadline = MdoDeadlineAfterUs((Test || Parameters) ? 30000000u : 15000000u); Job->Context.CloseResponse = true;
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

bool MdoApiModelDiscoverRoute(MdoApiContext* Context) { return MdoApiModelProbeStart(Context, false, false, false); }
bool MdoApiModelTestRoute(MdoApiContext* Context) { return MdoApiModelProbeStart(Context, true, false, false); }

bool MdoApiModelParametersRoute(MdoApiContext* Context) { return MdoApiModelProbeStart(Context, false, true, false); }

bool MdoApiModelProvidersRoute(MdoApiContext* Context) { return MdoApiModelProbeStart(Context, false, false, true); }
