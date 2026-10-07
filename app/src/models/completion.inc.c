/* Product retry policy. The transport performs one attempt; only this layer
 * decides whether another model generation is appropriate. No tool is replayed
 * here and no failed attempt is published as an Agent error. */
#ifndef MDO_MODEL_RETRY_BASE_MS
#define MDO_MODEL_RETRY_BASE_MS 1000u
#endif
#define MDO_MODEL_MAX_ATTEMPTS 6u
#define MDO_MODEL_RETRY_MAX_MS 30000u
#define MDO_MODEL_RECOVERY_MS 120000u

static bool MdoModelsCode(const xllm_error* Error, const char* Code)
{
    return Error && (strcmp(Error->sProviderCode, Code) == 0 ||
        strcmp(Error->sProviderType, Code) == 0);
}

/* Stable product classifications; provider prose is not a user-facing API.
 * Specific business codes take precedence over ambiguous HTTP statuses. */
const char* MdoModelErrorKind(const xllm_error* Error)
{
    if (!Error || Error->eCode == XLLM_ERROR_NONE) return "unknown";
    /* Scope termination supersedes the last failed transport response. */
    if (Error->eCode == XLLM_ERROR_CANCELLED) return "cancelled";
    if (Error->eCode == XLLM_ERROR_TIMEOUT) return "timeout";
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
    if (Error->eCode == XLLM_ERROR_PARSE || Error->eCode == XLLM_ERROR_PROTOCOL)
        return "invalid_response";
    if (Error->iHttpStatus == 400 || Error->iHttpStatus == 422 ||
        Error->eCode == XLLM_ERROR_INVALID_ARGUMENT) return "invalid_request";
    return "unknown";
}

const char* MdoModelErrorMessage(const xllm_error* Error)
{
    const char* Kind = MdoModelErrorKind(Error);
    if (!strcmp(Kind, "daily_token_limit")) return
        "Today's model token allowance is exhausted or reserved by unfinished requests. Check account usage; it resets at 00:00 Asia/Shanghai.";
    if (!strcmp(Kind, "insufficient_balance")) return
        "The account has insufficient available balance for this request. Check the account balance or choose another model.";
    if (!strcmp(Kind, "quota_exceeded")) return
        "The model service's request or spending allowance has been reached. Check account usage or choose another model.";
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
    if (!strcmp(Kind, "invalid_request")) return
        "The model service rejected the request settings. Check the model, API protocol and supported options.";
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
        !strcmp(Kind, "service_unavailable") || !strcmp(Kind, "timeout");
}

static xllm_result MdoModelsScope(const xllm_request* Request, xllm_error* Error)
{
    if (Request->pCancel && xrtCancelRequested(Request->pCancel)) {
        Error->eCode = XLLM_ERROR_CANCELLED;
        return XLLM_RESULT_CANCELLED;
    }
    if (Request->uDeadline != XRT_DEADLINE_NEVER &&
        xrtDeadlineExpired(Request->uDeadline)) {
        Error->eCode = XLLM_ERROR_TIMEOUT;
        return XLLM_RESULT_TIMEOUT;
    }
    return XLLM_RESULT_OK;
}

static uint32 MdoModelsRetryDelay(uint32 Attempt, uint32 RetryAfter,
    uint64* Jitter)
{
    uint64 Delay = MDO_MODEL_RETRY_BASE_MS;
    for (uint32 i = 1u; i < Attempt && Delay < MDO_MODEL_RETRY_MAX_MS; ++i)
        Delay *= 2u;
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
    if (Delay < RetryAfter) Delay = RetryAfter;
    return Delay > UINT32_MAX ? UINT32_MAX : (uint32)Delay;
}

xllm_result MdoModelComplete(xllm_client* Client, const xllm_request* Request,
    const xllm_stream_callbacks* Callbacks, xllm_response** Response,
    xllm_error* OutError)
{
    xllm_error Error;
    xllm_result Result = XLLM_RESULT_ERROR;
    uint64 RecoveryStarted = 0u;
    uint64 Jitter = xrtClock() ^ (uint64)(uintptr_t)Request ^
        (uint64)(uintptr_t)Client;
    if (!Jitter) Jitter = 1u;
    uint32 Attempt = 0u;
    uint32 AttemptsMade = 0u;
    bool Retryable = false;
    if (Response) *Response = NULL;
    xllmErrorInit(&Error);
    if (!Client || !Request || !Response) {
        Error.eCode = XLLM_ERROR_INVALID_ARGUMENT;
        goto done;
    }
    for (Attempt = 1u; Attempt <= MDO_MODEL_MAX_ATTEMPTS; ++Attempt) {
        MdoModelStreamGuard Guard = {0};
        xllm_stream_callbacks Stream = {0};
        Guard.Callbacks = Callbacks;
        xrtAtomic32Init(&Guard.Delivered, 0u);
        xrtAtomic32Init(&Guard.Stopped, 0u);
        Stream.pUserData = &Guard;
        Stream.OnEvent = MdoModelsStream;
        Result = MdoModelsScope(Request, &Error);
        if (Result != XLLM_RESULT_OK) { Retryable = false; break; }
        ++AttemptsMade;
        Result = xllmClientComplete(Client, Request, Callbacks ? &Stream : NULL,
            Response, &Error);
        if (Result == XLLM_RESULT_OK) break;
        if (MdoModelsScope(Request, &Error) != XLLM_RESULT_OK) {
            Retryable = false;
            Result = Error.eCode == XLLM_ERROR_CANCELLED ?
                XLLM_RESULT_CANCELLED : XLLM_RESULT_TIMEOUT;
            break;
        }
        Retryable = MdoModelsRetryable(&Error) &&
            !Error.tDiagnostics.bModelDataDelivered && !*Response &&
            !xrtAtomic32Load(&Guard.Delivered, XMEMORY_ACQUIRE) &&
            !xrtAtomic32Load(&Guard.Stopped, XMEMORY_ACQUIRE);
        if (!Retryable || Attempt == MDO_MODEL_MAX_ATTEMPTS) break;
        uint64 Now = xrtClock();
        if (!RecoveryStarted) RecoveryStarted = Now;
        uint64 RecoveryEnd = RecoveryStarted + (uint64)MDO_MODEL_RECOVERY_MS * 1000u;
        uint32 Delay = MdoModelsRetryDelay(Attempt,
            Error.tDiagnostics.uRetryAfterMs, &Jitter);
        if (Now >= RecoveryEnd || (uint64)Delay * 1000u > RecoveryEnd - Now) break;
        Error.tDiagnostics.uAttemptCount = Attempt;
        Error.tDiagnostics.uMaxAttempts = MDO_MODEL_MAX_ATTEMPTS;
        Error.tDiagnostics.bRetryable = true;
        if (Request->pHooks && Request->pHooks->pOnRetry &&
            !Request->pHooks->pOnRetry(Client, &Error.tDiagnostics,
                Attempt + 1u, Request->pHooks->pUserData)) {
            Retryable = false;
            break;
        }
        uint64 End = Now + (uint64)Delay * 1000u;
        for (;;) {
            Result = MdoModelsScope(Request, &Error);
            if (Result != XLLM_RESULT_OK) { Retryable = false; goto done; }
            uint64 Current = xrtClock();
            if (Current >= End) break;
            uint64 Remaining = End - Current;
            xrtSleep(Remaining > 20000u ? 20u :
                (uint32)((Remaining + 999u) / 1000u));
        }
    }
done:
    Error.tDiagnostics.uAttemptCount = AttemptsMade;
    Error.tDiagnostics.uMaxAttempts = MDO_MODEL_MAX_ATTEMPTS;
    Error.tDiagnostics.bRetryable = false;
    Error.tDiagnostics.bRetryExhausted = Retryable &&
        Result != XLLM_RESULT_OK && Result != XLLM_RESULT_CANCELLED;
    if (Response && *Response && Result == XLLM_RESULT_OK) {
        (*Response)->tDiagnostics.uAttemptCount = AttemptsMade;
        (*Response)->tDiagnostics.uMaxAttempts = MDO_MODEL_MAX_ATTEMPTS;
        (*Response)->tDiagnostics.bRetryable = false;
        (*Response)->tDiagnostics.bRetryExhausted = false;
    }
    if (Result != XLLM_RESULT_OK) {
        const char* Message = MdoModelErrorMessage(&Error);
        if (Message != Error.sMessage)
            snprintf(Error.sMessage, sizeof(Error.sMessage), "%s", Message);
    }
    if (OutError) *OutError = Error;
    return Result;
}
