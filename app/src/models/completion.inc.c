#include "../../include/mdo/time.h"
/* Product retry policy. The transport performs one attempt; only this layer
 * decides whether another model generation is appropriate. No tool is replayed
 * here and no failed attempt is published as an Agent error. */
#ifndef MDO_MODEL_RETRY_BASE_MS
#define MDO_MODEL_RETRY_BASE_MS 1000u
#endif
/* Initial attempt plus ten recoveries for a completed but unusable draft. */
#define MDO_MODEL_REPAIR_ATTEMPTS 11u
#ifndef MDO_MODEL_RETRY_MAX_MS
#define MDO_MODEL_RETRY_MAX_MS 300000u
#endif

static bool MdoModelsCode(const xllm_error* Error, const char* Code)
{
    return Error && (strcmp(Error->sProviderCode, Code) == 0 ||
        strcmp(Error->sProviderType, Code) == 0);
}

static const struct {
    const char* Kind;
    const char* Message;
} MdoModelsProfileErrors[] = {
    { "profile_login_pending", "Restoring account login and loading online models. Please wait before continuing." },
    { "profile_login_required", "Sign in to use the selected online model, or choose another model." },
    { "profile_login_unavailable", "The online model list could not be loaded. Check the connection and retry account refresh." },
    { "profile_image_unsupported", "The selected model cannot view images in this conversation. Choose a model with image input to continue." },
    { "profile_model_missing", "The selected model is no longer in the model list. Refresh the list or choose another model." },
    { "profile_protocol_unsupported", "The selected model does not support this API protocol. Check the model protocol or choose another model." },
    { "profile_reasoning_unsupported", "The selected model does not support this reasoning level. Choose a supported level." },
    { "profile_output_limit", "The configured output limit exceeds this model's limit. Lower the output limit in the Agent or model settings." },
    { "profile_context_limit", "The Agent's context or input limit exceeds this model's limit. Adjust the Agent limits or choose a model with a larger context window." },
    { "profile_credential_unavailable", "The configured model credential is unavailable locally. Check the provider key or its secret reference in model settings." },
    { "profile_proxy_unavailable", "The configured proxy credential is unavailable locally. Check the proxy settings and its secret reference." },
    { "profile_proxy_invalid", "The selected proxy configuration is invalid. Check the model, provider or global proxy settings." },
    { "profile_system_proxy_unavailable", "System proxy resolution failed. Check the system network proxy or PAC settings, or select a manual proxy. The request was not silently switched to a direct connection." },
    { "profile_ca_unavailable", "The configured CA certificate could not be loaded. Check the certificate file in network settings." },
    { "profile_endpoint_invalid", "The configured model endpoint is invalid. Check the provider API address in model settings." },
};

void MdoModelProfileErrorInit(xllm_error* Error, const char* Kind,
    const char* Diagnostic)
{
    if (!Error) return;
    xllmErrorInit(Error);
    Error->eCode = XLLM_ERROR_INVALID_ARGUMENT;
    snprintf(Error->sProviderCode, sizeof(Error->sProviderCode), "%s", Kind ? Kind : "");
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Diagnostic ? Diagnostic : "");
}

static size_t MdoModelsProfileErrorIndex(const xllm_error* Error)
{
    size_t i;
    /* xwork's error events retain the code, HTTP status and model error enum.
     * Reserved local codes must survive that replay without an ABI change;
     * actual HTTP responses and resource/cancellation failures keep theirs. */
    if (Error && Error->iHttpStatus == 0 &&
        (Error->eCode == XLLM_ERROR_INVALID_ARGUMENT || Error->eCode == XLLM_ERROR_AUTH))
        for (i = 0u; i < sizeof(MdoModelsProfileErrors) /
                sizeof(MdoModelsProfileErrors[0]); ++i)
            if (!strcmp(Error->sProviderCode, MdoModelsProfileErrors[i].Kind))
                return i;
    return SIZE_MAX;
}

/* Stable product classifications; provider prose is not a user-facing API.
 * Specific business codes take precedence over ambiguous HTTP statuses. */
const char* MdoModelErrorKind(const xllm_error* Error)
{
    if (!Error || Error->eCode == XLLM_ERROR_NONE) return "unknown";
    /* Scope termination supersedes the last failed transport response. */
    if (Error->eCode == XLLM_ERROR_CANCELLED) return "cancelled";
    if (Error->eCode == XLLM_ERROR_TIMEOUT) return "timeout";
    if (Error->eCode == XLLM_ERROR_OUTPUT_LIMIT) return "output_limit";
    if (MdoModelsCode(Error, "empty_response")) return "empty_response";
    size_t Profile = MdoModelsProfileErrorIndex(Error);
    if (Profile != SIZE_MAX) return MdoModelsProfileErrors[Profile].Kind;
    if (MdoModelsCode(Error, "upstream_quota_exceeded")) return "service_quota_exceeded";
    if (MdoModelsCode(Error, "upstream_configuration_error")) return "service_configuration";
    if (MdoModelsCode(Error, "daily_token_limit")) return "daily_token_limit";
    if (MdoModelsCode(Error, "insufficient_balance")) return "insufficient_balance";
    if (MdoModelsCode(Error, "insufficient_quota") ||
        MdoModelsCode(Error, "quota_exceeded")) return "quota_exceeded";
    if (MdoModelsCode(Error, "membership_required")) return "membership_required";
    if (MdoModelsCode(Error, "login_required")) return "login_required";
    if (MdoModelsCode(Error, "request_already_exists")) return "request_pending";
    if (MdoModelsCode(Error, "price_changed")) return "model_changed";
    if (MdoModelsCode(Error, "1313")) return "account_restricted";
    if (MdoModelsCode(Error, "context_length_exceeded") ||
        MdoModelsCode(Error, "max_tokens_exceeded")) return "context_limit";
    if (MdoModelsCode(Error, "model_unavailable") ||
        Error->eCode == XLLM_ERROR_MODEL_NOT_FOUND || Error->iHttpStatus == 404)
        return "model_unavailable";
    if (Error->eCode == XLLM_ERROR_AUTH || Error->iHttpStatus == 401)
        return "authentication_failed";
    if (Error->iHttpStatus == 403) return "permission_denied";
    if (Error->iHttpStatus == 402) return "insufficient_balance";
    if (Error->iHttpStatus == 413) return "request_too_large";
    if (Error->eCode == XLLM_ERROR_RATE_LIMIT || Error->iHttpStatus == 429)
        return "rate_limited";
    if (Error->iHttpStatus == 408 ||
        Error->iHttpStatus == 504) return "timeout";
    if (Error->eCode == XLLM_ERROR_NETWORK &&
        Error->tCause.eDomain != XLLM_ERROR_DOMAIN_RESOURCE) return "network";
    if (Error->iHttpStatus >= 500 && Error->iHttpStatus <= 599)
        return "service_unavailable";
    if (Error->eCode == XLLM_ERROR_PARSE || Error->eCode == XLLM_ERROR_PROTOCOL ||
        Error->eCode == XLLM_ERROR_INCOMPLETE_RESPONSE)
        return "invalid_response";
    /* Local validation has no upstream status. Do not imply that the model
     * service rejected a request which could not be prepared in this client. */
    if (Error->eCode == XLLM_ERROR_INVALID_ARGUMENT && Error->iHttpStatus == 0)
        return "request_configuration";
    if (Error->iHttpStatus == 400 || Error->iHttpStatus == 422 ||
        Error->eCode == XLLM_ERROR_INVALID_ARGUMENT) return "invalid_request";
    return "unknown";
}

const char* MdoModelErrorMessage(const xllm_error* Error)
{
    const char* Kind = MdoModelErrorKind(Error);
    size_t Profile = MdoModelsProfileErrorIndex(Error);
    if (Profile != SIZE_MAX && !strcmp(Kind, MdoModelsProfileErrors[Profile].Kind))
        return MdoModelsProfileErrors[Profile].Message;
    if (!strcmp(Kind, "daily_token_limit")) return
        "Today's model token allowance is exhausted or reserved by unfinished requests. Check account usage; it resets at 00:00 Asia/Shanghai.";
    if (!strcmp(Kind, "insufficient_balance")) return
        "The account has insufficient available balance for this request. Check the account balance or choose another model.";
    if (!strcmp(Kind, "quota_exceeded")) return
        "The model service's request or spending allowance has been reached. Check account usage or choose another model.";
    if (!strcmp(Kind, "service_quota_exceeded")) return
        "The upstream model provider's allowance is exhausted. Choose another model or wait for its allowance to reset.";
    if (!strcmp(Kind, "service_configuration")) return
        "The model service's credentials, permissions or subscription are unavailable. Choose another model or contact the service administrator.";
    if (!strcmp(Kind, "membership_required")) return
        "Your membership does not include this model. Choose an available model or update your membership.";
    if (!strcmp(Kind, "login_required")) return
        "Sign in to use online models, or wait for login renewal.";
    if (!strcmp(Kind, "authentication_failed")) return
        "The model service rejected the credentials. Check the provider key and its API permissions.";
    if (!strcmp(Kind, "permission_denied")) return
        "The model service denied access to this model. Check account permissions or choose another model.";
    if (!strcmp(Kind, "account_restricted")) return
        "The model provider has restricted this account. Review the account with the provider before continuing.";
    if (!strcmp(Kind, "request_pending")) return
        "The service has already accepted this request. Check its existing receipt before submitting it again.";
    if (!strcmp(Kind, "model_changed")) return
        "The model price or configuration changed. Refresh the model list before continuing.";
    if (!strcmp(Kind, "model_unavailable")) return
        "This model is unavailable. Refresh the model list or choose another model.";
    if (!strcmp(Kind, "context_limit")) return
        "The request exceeds this model's context limit. Reduce the context or choose a model with a larger context window.";
    if (!strcmp(Kind, "request_too_large")) return
        "The model request exceeds the service's size limit. Reduce the context or attachments before continuing.";
    if (!strcmp(Kind, "rate_limited")) return
        "The model service is still rate limiting requests after automatic retries. Please try again later.";
    if (!strcmp(Kind, "timeout")) return
        "The model did not finish within the allowed time. Your conversation is saved; you can continue when the service recovers.";
    if (!strcmp(Kind, "network")) return
        "The connection to the model service could not be restored. Check the network; your conversation is saved and can be continued.";
    if (!strcmp(Kind, "service_unavailable")) return
        "The model service is temporarily unavailable and automatic recovery did not succeed. Your conversation is saved; try again later.";
    if (!strcmp(Kind, "invalid_response")) return
        "The model service returned an incomplete or invalid response. Your conversation is saved; you can continue or choose another model.";
    if (!strcmp(Kind, "empty_response")) return
        "The model repeatedly returned only reasoning or no usable reply after automatic recovery. Your conversation is saved; you can continue or choose another model.";
    if (!strcmp(Kind, "output_limit")) return
        "The model reached its output limit before completing a tool call. Automatic recovery did not succeed; increase the output limit or ask for smaller steps. Your conversation is saved.";
    if (!strcmp(Kind, "invalid_request")) return
        "The model service rejected the request settings. Check the model, API protocol and supported options.";
    if (!strcmp(Kind, "request_configuration")) return
        "The saved model settings could not produce a valid request. Check the API endpoint, model capabilities, reasoning options and output limit.";
    if (!strcmp(Kind, "cancelled")) return "The model request was cancelled.";
    return Error && Error->sMessage[0] ? Error->sMessage :
        "The model request could not be completed. Your conversation is saved.";
}

typedef struct MdoModelStreamGuard {
    const xllm_stream_callbacks* Callbacks;
    xatomic32 Delivered;
    xatomic32 Stopped;
} MdoModelStreamGuard;

static bool MdoModelsStream(void* Data, const xllm_event* Event)
{
    MdoModelStreamGuard* Guard = Data;
    if (Event->eKind == XLLM_EVENT_TEXT_DELTA ||
        Event->eKind == XLLM_EVENT_REASONING_DELTA ||
        Event->eKind == XLLM_EVENT_TOOL_CALL_DELTA)
        xrtAtomic32Store(&Guard->Delivered, 1u, XMEMORY_RELEASE);
    if (Guard->Callbacks && Guard->Callbacks->OnEvent &&
        !Guard->Callbacks->OnEvent(Guard->Callbacks->pUserData, Event)) {
        xrtAtomic32Store(&Guard->Stopped, 1u, XMEMORY_RELEASE);
        return false;
    }
    return true;
}

static bool MdoModelsRetryable(const xllm_error* Error)
{
    const char* Kind = MdoModelErrorKind(Error);
    return !strcmp(Kind, "rate_limited") || !strcmp(Kind, "network") ||
        !strcmp(Kind, "service_unavailable") || !strcmp(Kind, "timeout") ||
        (!strcmp(Kind, "unknown") && Error->eCode == XLLM_ERROR_UPSTREAM) ||
        Error->eCode == XLLM_ERROR_INCOMPLETE_RESPONSE;
}

static bool MdoModelsUsableResponse(const xllm_response* Response)
{
    const unsigned char* Text;
    if (!Response) return false;
    if (Response->iToolCallCount) return true;
    Text = (const unsigned char*)Response->sContent;
    for (; Text && *Text; ++Text)
        if (*Text != ' ' && *Text != '\t' && *Text != '\r' && *Text != '\n') return true;
    return false;
}

static xllm_result MdoModelsScope(const xllm_request* Request, xllm_error* Error)
{
    if (Request->pCancel && xrtCancelRequested(Request->pCancel)) {
        Error->eCode = XLLM_ERROR_CANCELLED;
        return XLLM_RESULT_CANCELLED;
    }
    if (Request->uDeadline != MDO_DEADLINE_NEVER &&
        MdoDeadlineExpired(Request->uDeadline)) {
        Error->eCode = XLLM_ERROR_TIMEOUT;
        return XLLM_RESULT_TIMEOUT;
    }
    return XLLM_RESULT_OK;
}

static uint32 MdoModelsRetryDelay(uint32 Attempt, uint32 RetryAfter,
    uint32 PreviousDelay, uint64* Jitter)
{
    uint64 Delay = MDO_MODEL_RETRY_BASE_MS;
    for (uint32 i = 1u; i < Attempt && Delay < MDO_MODEL_RETRY_MAX_MS; ++i)
        Delay *= 2u;
    if (Delay > MDO_MODEL_RETRY_MAX_MS) Delay = MDO_MODEL_RETRY_MAX_MS;
    if (Delay <= PreviousDelay && PreviousDelay < MDO_MODEL_RETRY_MAX_MS)
        Delay = (uint64)PreviousDelay + MDO_MODEL_RETRY_BASE_MS;
    if (Delay > MDO_MODEL_RETRY_MAX_MS) Delay = MDO_MODEL_RETRY_MAX_MS;
    /* Positive jitter never retries earlier than the exponential base or the
     * provider's requested minimum. Retry-After is not clipped to our cap. */
    /* Local xorshift state is only scheduling jitter, not secret material. It
     * needs no global RNG state or additional optional runtime module. */
    *Jitter ^= *Jitter << 13u;
    *Jitter ^= *Jitter >> 7u;
    *Jitter ^= *Jitter << 17u;
    Delay += *Jitter % (Delay / 4u + 1u);
    if (Delay > MDO_MODEL_RETRY_MAX_MS) Delay = MDO_MODEL_RETRY_MAX_MS;
    /* A provider's larger minimum must not be followed by a shorter wait.
     * A successful completion starts a new local failure streak. */
    if (Delay < PreviousDelay) Delay = PreviousDelay;
    if (Delay < RetryAfter) Delay = RetryAfter;
    return Delay > UINT32_MAX ? UINT32_MAX : (uint32)Delay;
}

/* Online attempts can replace credentials while sharing the same recovery
 * policy. The adapter retains its client until the next attempt, so
 * retry hooks never receive an already destroyed client. */
typedef xllm_result (*MdoModelsAttemptFn)(void*, xllm_client**,
    const xllm_request*, const xllm_stream_callbacks*, xllm_response**, xllm_error*);

/* SDK/Agent request defaults are broader than a conservatively discovered
 * model. Borrow a view of the request and use only declared optional controls.
 * Messages, tool schemas, cancellation, hooks and the caller's request survive. */
static const xllm_request* MdoModelsClientRequest(xllm_client* Client,
    const xllm_request* Request, xllm_request* View)
{
    xllm_model_profile Profile;
    if (!Client || !Request || !xllmClientGetModelProfile(Client, &Profile)) return Request;
    *View = *Request;
    if (View->sReasoningEffort && !strcmp(View->sReasoningEffort, "default")) View->sReasoningEffort = NULL;
    if (!(Profile.uCapabilities & XLLM_CAP_PARALLEL_TOOL_CALL))
        View->bParallelToolCalls = false;
    if (!(Profile.uCapabilities & XLLM_CAP_REASONING_CONTROL) &&
        View->sReasoningEffort && !strcmp(View->sReasoningEffort, "none"))
        View->sReasoningEffort = NULL;
    return View;
}

#include "tool_schema.inc.c"

static xllm_result MdoModelsClientComplete(xllm_client* Client, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    xllm_request View; MdoModelHooks Context; xllm_hooks Hooks;
    const xllm_request* Prepared = MdoModelsClientRequest(Client, Request, &View);
    if (!Client || !Request || Prepared != &View)
        return xllmClientComplete(Client, Prepared, Callbacks, Response, Error);
    MdoModelsRequestHooks(&View, &Context, &Hooks, Request->sReasoningEffort && !strcmp(Request->sReasoningEffort, "default"));
    return xllmClientComplete(Client, &View, Callbacks, Response, Error);
}

static xllm_result MdoModelsComplete(xllm_client* Client, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response,
    xllm_error* OutError, bool Restartable, MdoModelsAttemptFn Complete, void* Data,
    const MdoModelRetryObserver* Observer)
{
    xllm_error Error;
    xllm_result Result = XLLM_RESULT_ERROR;
    uint64 Jitter = MdoMonotonicUs() ^ (uint64)(uintptr_t)Request ^
        (uint64)(uintptr_t)Client;
    if (!Jitter) Jitter = 1u;
    uint32 Attempt = 1u;
    uint32 AttemptsMade = 0u;
    uint32 RepairAttempts = 0u;
    uint32 PreviousDelay = 0u;
    bool Retryable = false;
    bool AuthenticationRetried = false;
    xllm_request RepairRequest = {0};
    xllm_message* RepairMessages = NULL;
    const xllm_request* ActiveRequest = Request;
    if (Response) *Response = NULL;
    xllmErrorInit(&Error);
    if ((!Client && !Complete) || !Request || !Response) {
        Error.eCode = XLLM_ERROR_INVALID_ARGUMENT;
        goto done;
    }
    for (;;) {
        MdoModelStreamGuard Guard = {0};
        xllm_stream_callbacks Stream = {0};
        Guard.Callbacks = Callbacks;
        xrtAtomic32Init(&Guard.Delivered, 0u);
        xrtAtomic32Init(&Guard.Stopped, 0u);
        Stream.pUserData = &Guard;
        Stream.OnEvent = MdoModelsStream;
        xllm_request AttemptRequest = *ActiveRequest;
        Result = MdoModelsScope(Request, &Error);
        if (Result != XLLM_RESULT_OK) { Retryable = false; break; }
        if (AttemptsMade != UINT32_MAX) ++AttemptsMade;
        Result = Complete ? Complete(Data, &Client, &AttemptRequest,
            Callbacks ? &Stream : NULL, Response, &Error) :
            MdoModelsClientComplete(Client, &AttemptRequest, Callbacks ? &Stream : NULL,
                Response, &Error);
        if (Result == XLLM_RESULT_OK && Restartable &&
            !MdoModelsUsableResponse(*Response)) {
            xllmErrorInit(&Error);
            Error.eCode = XLLM_ERROR_PROTOCOL;
            snprintf(Error.sProviderCode, sizeof(Error.sProviderCode), "empty_response");
            snprintf(Error.sMessage, sizeof(Error.sMessage),
                "Model returned only reasoning or no usable text/tool calls");
            if (*Response) {
                Error.iHttpStatus = (int32)(*Response)->uHttpStatus;
                Error.tDiagnostics = (*Response)->tDiagnostics;
            }
            xllmResponseDestroy(*Response);
            *Response = NULL;
            Result = XLLM_RESULT_ERROR;
        }
        /* Waiting for credentials can end before transport starts. Do not
         * report that wait as another model request. */
        if (Complete && Result != XLLM_RESULT_OK && !Error.tDiagnostics.uAttemptCount)
            --AttemptsMade;
        if (Result == XLLM_RESULT_OK) break;
        if (MdoModelsScope(Request, &Error) != XLLM_RESULT_OK) {
            Retryable = false;
            Result = Error.eCode == XLLM_ERROR_CANCELLED ?
                XLLM_RESULT_CANCELLED : XLLM_RESULT_TIMEOUT;
            break;
        }
        /* Invalid streamed drafts are also replaceable at the Agent boundary:
         * no tools execute until a valid complete response is returned. */
        /* Only a definite online authentication rejection before delivery may
         * trigger one renewal. Business errors and delivered content remain
         * protected, including for replaceable Agent streams. */
        bool RenewAuthentication = Complete && !AuthenticationRetried &&
            Error.iHttpStatus == 401 &&
            !strcmp(MdoModelErrorKind(&Error), "login_required") && !*Response &&
            !Error.tDiagnostics.bModelDataDelivered &&
            !xrtAtomic32Load(&Guard.Delivered, XMEMORY_ACQUIRE) &&
            !xrtAtomic32Load(&Guard.Stopped, XMEMORY_ACQUIRE);
        Retryable = RenewAuthentication || ((MdoModelsRetryable(&Error) || (Restartable &&
            (!strcmp(MdoModelErrorKind(&Error), "invalid_response") ||
             !strcmp(MdoModelErrorKind(&Error), "empty_response") ||
             Error.eCode == XLLM_ERROR_OUTPUT_LIMIT))) &&
            !*Response && (Restartable ||
            (!Error.tDiagnostics.bModelDataDelivered &&
             !xrtAtomic32Load(&Guard.Delivered, XMEMORY_ACQUIRE))) &&
            !xrtAtomic32Load(&Guard.Stopped, XMEMORY_ACQUIRE));
        if (!Retryable) break;
        /* Transient/opaque connection failures remain recoverable until a
         * successful model reply or the caller's cancellation/deadline.
         * Known malformed/output-limited drafts still have bounded repair. */
        if (!RenewAuthentication && !MdoModelsRetryable(&Error) &&
            ++RepairAttempts >= MDO_MODEL_REPAIR_ATTEMPTS) break;
        if (RenewAuthentication) AuthenticationRetried = true;
        if ((Error.eCode == XLLM_ERROR_OUTPUT_LIMIT ||
             MdoModelsCode(&Error, "empty_response"))) {
            /* Add one ephemeral instruction to a borrowed message overlay.
             * Keep the user's token limit and persisted history unchanged;
             * the failed draft has never committed or dispatched tools. */
            if (!RepairMessages && (Request->iMessageCount >= SIZE_MAX / sizeof(*RepairMessages) - 1u ||
                !(RepairMessages = calloc(Request->iMessageCount + 1u,
                    sizeof(*RepairMessages))))) {
                Error.eCode = XLLM_ERROR_OUT_OF_MEMORY;
                snprintf(Error.sMessage, sizeof(Error.sMessage),
                    "Cannot prepare model output-limit recovery");
                Retryable = false;
                break;
            }
            if (Request->iMessageCount) memcpy(RepairMessages, Request->pMessages,
                Request->iMessageCount * sizeof(*RepairMessages));
            RepairMessages[Request->iMessageCount].eRole = XLLM_ROLE_SYSTEM;
            RepairMessages[Request->iMessageCount].sContent =
                MdoModelsCode(&Error, "empty_response") ?
                "The previous generation returned only reasoning or no usable reply. "
                "None of its draft tool calls executed. Continue from the existing "
                "tool results. Keep reasoning brief and return either a visible "
                "answer or one small complete tool call. Do not repeat completed "
                "tools or invent another user message." :
                "The previous generation hit the output token limit before completing "
                "its tool calls. None of those draft calls executed. Keep reasoning "
                "brief and emit only one small, complete tool call at a time. Split "
                "large file writes or edits into smaller steps. Do not repeat already "
                "completed work. The original output limit still applies.";
            RepairRequest = *Request;
            RepairRequest.pMessages = RepairMessages;
            RepairRequest.iMessageCount = Request->iMessageCount + 1u;
            RepairRequest.iMessageCap = RepairRequest.iMessageCount;
            RepairRequest.pbMessageBorrowed = NULL;
            RepairRequest.pStablePrefixOwner = NULL;
            RepairRequest.uStablePrefixStamp = 0u;
            RepairRequest.iStableMessages = 0u;
            ActiveRequest = &RepairRequest;
        }
        uint64 Now = MdoMonotonicUs();
        uint32 Delay = MdoModelsRetryDelay(Attempt,
            Error.tDiagnostics.uRetryAfterMs, PreviousDelay, &Jitter);
        PreviousDelay = Delay;
        Error.tDiagnostics.uAttemptCount = Attempt;
        /* Zero means this product policy has no fixed connection-attempt cap. */
        Error.tDiagnostics.uMaxAttempts = 0u;
        Error.tDiagnostics.uRetryAfterMs = Delay;
        Error.tDiagnostics.bRetryable = true;
        if (Request->pHooks && Request->pHooks->pOnRetry &&
            !Request->pHooks->pOnRetry(Client, &Error.tDiagnostics,
                Attempt == UINT32_MAX ? Attempt : Attempt + 1u,
                Request->pHooks->pUserData)) {
            Retryable = false;
            break;
        }
        uint64 End = Now + (uint64)Delay * 1000u;
        if (Observer && Observer->Notify && !Observer->Notify(Observer->UserData,
                &Error, Attempt == UINT32_MAX ? Attempt : Attempt + 1u, Delay)) {
            Error.eCode = XLLM_ERROR_HOOK;
            Retryable = false;
            break;
        }
        for (;;) {
            Result = MdoModelsScope(Request, &Error);
            if (Result != XLLM_RESULT_OK) { Retryable = false; goto done; }
            uint64 Current = MdoMonotonicUs();
            if (Current >= End) break;
            uint64 Remaining = End - Current;
            xrtSleep(Remaining > 20000u ? 20u :
                (uint32)((Remaining + 999u) / 1000u));
        }
        if (Observer && Observer->Notify && !Observer->Notify(Observer->UserData,
                &Error, Attempt == UINT32_MAX ? Attempt : Attempt + 1u, 0u)) {
            Error.eCode = XLLM_ERROR_HOOK;
            Retryable = false;
            break;
        }
        if (Attempt != UINT32_MAX) ++Attempt;
    }
done:
    /* The overlay owns only its array; every string is borrowed. */
    free(RepairMessages);
    Error.tDiagnostics.uAttemptCount = AttemptsMade;
    Error.tDiagnostics.uMaxAttempts = 0u;
    Error.tDiagnostics.bRetryable = false;
    Error.tDiagnostics.bRetryExhausted = Retryable &&
        Result != XLLM_RESULT_OK && Result != XLLM_RESULT_CANCELLED;
    if (Response && *Response && Result == XLLM_RESULT_OK) {
        (*Response)->tDiagnostics.uAttemptCount = AttemptsMade;
        (*Response)->tDiagnostics.uMaxAttempts = 0u;
        (*Response)->tDiagnostics.bRetryable = false;
        (*Response)->tDiagnostics.bRetryExhausted = false;
    }
    if (Result != XLLM_RESULT_OK) {
        const char* Message = MdoModelErrorMessage(&Error);
        /* Parser/protocol prose is generated locally and explains the exact
         * defect to diagnostics. The timeline translates the structured kind;
         * provider prose/credentials are still replaced by a safe summary. */
        if (Message != Error.sMessage && Error.eCode != XLLM_ERROR_PARSE &&
            Error.eCode != XLLM_ERROR_PROTOCOL &&
            Error.eCode != XLLM_ERROR_INCOMPLETE_RESPONSE)
            snprintf(Error.sMessage, sizeof(Error.sMessage), "%s", Message);
    }
    if (OutError) *OutError = Error;
    return Result;
}

xllm_result MdoModelComplete(xllm_client* Client, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    return MdoModelsComplete(Client, Request, Callbacks, Response, Error, false, NULL, NULL, NULL);
}

xllm_result MdoModelCompleteRestartable(xllm_client* Client, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error)
{
    return MdoModelsComplete(Client, Request, Callbacks, Response, Error, true, NULL, NULL, NULL);
}

xllm_result MdoModelCompleteRestartableObserved(xllm_client* Client, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response, xllm_error* Error,
    const MdoModelRetryObserver* Observer)
{
    return MdoModelsComplete(Client, Request, Callbacks, Response, Error, true, NULL, NULL, Observer);
}
