#include "../../include/mdo/time.h"
#include "internal.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/recovery.h"
#ifndef MDO_ACCOUNT_REFRESH_RETRY_BASE_MS
#define MDO_ACCOUNT_REFRESH_RETRY_BASE_MS 1000u
#endif
static bool MdoAccountLeaseLocked(xcancel*,MdoAccountLease*);
static bool MdoAccountQueueLocked(unsigned);
static void MdoAccountCancelLease(void*);
static void MdoAccountRequestReadLocked(MdoAccountReadKind);
#include "model_allowance.inc.c"
#include "balance.inc.c"

MdoAccountManager g_MdoAccount;

/* Session transitions belong to Lock. Credential changes are serialized by
 * the account worker. Each background read has at most one job in flight. */
static void MdoAccountChangedLocked(void)
{
    ++g_MdoAccount.Revision;
    xrtCondBroadcast(g_MdoAccount.Changed);
}
static void MdoAccountResetReadsLocked(void)
{
    for (size_t i = 0; i < MDO_ACCOUNT_READ_COUNT; ++i) {
        MdoAccountReadSlot* slot = &g_MdoAccount.Reads[i];
        xrtCancelRequest(slot->Cancel);
        slot->Next = slot->LastStarted = 0;
    }
}
static void MdoAccountClearTokensLocked(void)
{
    MdoAccountResetReadsLocked();
    xrtSecureZero(&g_MdoAccount.Tokens, sizeof(g_MdoAccount.Tokens));
    g_MdoAccount.Saved = false;
    g_MdoAccount.ExpiredMemberId = 0;
    g_MdoAccount.NextRefresh = 0;
    g_MdoAccount.RefreshFailures = 0;
    MdoAccountClearModelsLocked();
    MdoAccountClearBalanceLocked();
    (void)MdoHomeRemove(MDO_ACCOUNT_SESSION_PATH, false);
}
static void MdoAccountClearAuthorizationLocked(void)
{
    xrtSecureZero(&g_MdoAccount.Authorization, sizeof(g_MdoAccount.Authorization));
    (void)MdoHomeRemove(MDO_ACCOUNT_PENDING_PATH, false);
}
static void MdoAccountNewSessionLocked(void)
{
    xcancel* next = xrtCancelCreate();
    xrtCancelRequest(g_MdoAccount.SessionCancel);
    xrtCancelDestroy(g_MdoAccount.SessionCancel);
    g_MdoAccount.SessionCancel = next;
}
static bool MdoAccountQueueLocked(unsigned Kind)
{
    if (g_MdoAccount.Stopping || !g_MdoAccount.Origin[0]) return false;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH && g_MdoAccount.Authorization.State[0]) return true;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH &&
        ((g_MdoAccount.Busy && (g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_REFRESH ||
          g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_EXCHANGE ||
          g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_PASSWORD)) ||
         g_MdoAccount.Work.Kind == MDO_ACCOUNT_WORK_REFRESH ||
         g_MdoAccount.Work.Kind == MDO_ACCOUNT_WORK_EXCHANGE ||
         g_MdoAccount.Work.Kind == MDO_ACCOUNT_WORK_PASSWORD)) return true;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH && !g_MdoAccount.Tokens.Refresh[0]) return false;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH && !MdoDeadlineExpired(g_MdoAccount.NextRefresh)) return true;
    xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
    g_MdoAccount.Work.Kind = Kind; g_MdoAccount.Work.Epoch = g_MdoAccount.Epoch;
    strcpy(g_MdoAccount.Work.Origin, g_MdoAccount.Origin);
    g_MdoAccount.Work.Tokens = g_MdoAccount.Tokens;
    MdoAccountChangedLocked(); return true;
}
static xvalue* MdoAccountProfile(const xvalue* Data, uint64 Id)
{
    static const char* const texts[] = { "username", "nickname", "phone", "email" };
    static const char* const flags[] = { "phone_verified", "email_verified", "security_questions_configured" };
    uint64 id = 0; size_t i; bool flag;
    if (!MdoAccountGetUInt(xrtValueObjectGet(Data, xrtStrView("id")), &id) || id != Id) return NULL;
    xvalue* profile = xrtValueObject();
    if (!profile || !MdoAccountSetUInt(profile, "id", Id)) goto failed;
    for (i = 0; i < sizeof(texts)/sizeof(texts[0]); i++) {
        cstr text = MdoAccountText(Data, texts[i], 320);
        if (text && !MdoAccountSetString(profile, texts[i], text)) goto failed;
    }
    for (i = 0; i < sizeof(flags)/sizeof(flags[0]); i++)
        if (xrtValueGetBool(xrtValueObjectGet(Data, xrtStrView(flags[i])), &flag) &&
            !MdoAccountSetBool(profile, flags[i], flag)) goto failed;
    return profile;
failed:
    xrtValueRelease(profile); return NULL;
}
static xvalue* MdoAccountUsage(const xvalue* Data)
{
    static const char* const fields[] = { "minute_used", "minute_limit", "daily_used", "daily_limit", "daily_reset_at" };
    xvalue* usage = xrtValueObject(); size_t i; uint64 value;
    /* Public snapshots accept quota facts only, never arbitrary server data. */
    for (i = 0; usage && i < sizeof(fields)/sizeof(fields[0]); i++)
        if (!MdoAccountGetUInt(xrtValueObjectGet(Data, xrtStrView(fields[i])), &value) ||
            value > 9007199254740991ULL || !MdoAccountSetUInt(usage, fields[i], value)) {
            xrtValueRelease(usage); return NULL;
        }
    return usage;
}
static void MdoAccountSecretValueUnit(xvalue* Data)
{
    MdoAccountSecretValueRelease(Data);
}

typedef struct MdoAccountReadJob {
    MdoAccountReadKind Kind;
    xcancel* Cancel;
    uint64 Epoch, CredentialRevision, Member;
    char Access[MDO_ACCOUNT_TOKEN_LIMIT];
} MdoAccountReadJob;

static const struct {
    cstr Path;
    uint64 IntervalUs;
} MdoAccountReadConfig[MDO_ACCOUNT_READ_COUNT] = {
    [MDO_ACCOUNT_READ_PROFILE] = {"/api/v1/profile", 60000000},
    [MDO_ACCOUNT_READ_SEARCH] = {"/api/v1/search/usage", 60000000},
    [MDO_ACCOUNT_READ_ALLOWANCE] = {"/api/v1/ai/allowance", 30000000},
    [MDO_ACCOUNT_READ_CATALOG] = {"/api/v1/ai/catalog", 300000000},
    [MDO_ACCOUNT_READ_BALANCE] = {"/api/v1/billing/account", 60000000}
};

static void MdoAccountRequestReadLocked(MdoAccountReadKind Kind)
{
    /* Coalesce completion bursts without losing a change during a read. */
    MdoAccountReadSlot* slot = &g_MdoAccount.Reads[Kind];
    uint64 due = slot->LastStarted + 3000000u;
    if (slot->Next > due) {
        slot->Next = due;
        xrtCondBroadcast(g_MdoAccount.Changed);
    }
}

static void MdoAccountReadDrop(ptr Data, ptr Unused)
{
    (void)Unused;
    MdoAccountReadJob* job = Data;
    xrtMutexLock(g_MdoAccount.Lock);
    g_MdoAccount.Reads[job->Kind].Cancel = NULL;
    xrtCondBroadcast(g_MdoAccount.Changed);
    xrtMutexUnlock(g_MdoAccount.Lock);
    xrtCancelDestroy(job->Cancel);
    xrtSecureZero(job, sizeof(*job));
    xrtFree(job);
}

static xtaskoutcome MdoAccountReadRun(xcancel* Cancel, ptr Data, xtaskvalue* Out)
{
    (void)Out;
    MdoAccountReadJob* job = Data;
    uint16 status = 0;
    xvalue* data = MdoAccountClient(g_MdoAccount.Origin,
        MdoAccountReadConfig[job->Kind].Path, "GET", NULL, job->Access, Cancel, &status);
    xvalue* value = NULL;
    switch (job->Kind) {
    case MDO_ACCOUNT_READ_PROFILE: value = MdoAccountProfile(data, job->Member); break;
    case MDO_ACCOUNT_READ_SEARCH: value = MdoAccountUsage(data); break;
    case MDO_ACCOUNT_READ_ALLOWANCE: value = MdoAccountAllowance(data, job->Member); break;
    case MDO_ACCOUNT_READ_BALANCE: value = MdoAccountBalance(data, job->Member); break;
    case MDO_ACCOUNT_READ_CATALOG: break;
    default: break;
    }
    xrtMutexLock(g_MdoAccount.Lock);
    if (!g_MdoAccount.Stopping && !xrtCancelRequested(Cancel) &&
        job->Epoch == g_MdoAccount.Epoch && job->Member == g_MdoAccount.Tokens.MemberId &&
        job->CredentialRevision == g_MdoAccount.ModelCredentialRevision) {
        xvalue** target = NULL;
        bool save_profile = false;
        switch (job->Kind) {
        case MDO_ACCOUNT_READ_PROFILE:
            target = &g_MdoAccount.Profile;
            save_profile = value && !*target && g_MdoAccount.Remember;
            break;
        case MDO_ACCOUNT_READ_SEARCH:
            target = &g_MdoAccount.Usage;
            if (status) g_MdoAccount.SearchStatus = status;
            break;
        case MDO_ACCOUNT_READ_ALLOWANCE:
            target = &g_MdoAccount.Allowance;
            g_MdoAccount.ModelsStatus = !value && status == 200 ? 502 : status;
            break;
        case MDO_ACCOUNT_READ_BALANCE:
            target = &g_MdoAccount.Balance;
            g_MdoAccount.BalanceStatus = !value && status == 200 ? 502 : status;
            break;
        case MDO_ACCOUNT_READ_CATALOG: {
            cstr version = MdoAccountText(data, "version", 64);
            bool ok = status == 200 && version && strlen(version) == 64;
            if (ok && strcmp(version, g_MdoAccount.OnlineVersion)) {
                ok = MdoModelManagerSetOnlineCatalog(data);
                if (ok) strcpy(g_MdoAccount.OnlineVersion, version);
            }
            g_MdoAccount.CatalogStatus = ok ? 200u : status && status != 200 ? status : 502u;
            if (!ok) g_MdoAccount.Reads[MDO_ACCOUNT_READ_CATALOG].Next = MdoDeadlineAfterUs(30000000u);
            break;
        }
        default: break;
        }
        if (target && value) {
            xrtValueRelease(*target);
            *target = value;
            value = NULL;
        }
        if (save_profile) (void)MdoAccountCredentialSaveLocked(false);
        MdoAccountChangedLocked();
    }
    xrtMutexUnlock(g_MdoAccount.Lock);
    xrtValueRelease(value);
    xrtValueRelease(data);
    return XTASK_SUCCESS;
}

static void MdoAccountDispatchReadsLocked(void)
{
    if (!g_MdoAccount.ReadPool || g_MdoAccount.Stopping || !g_MdoAccount.Tokens.Access[0] ||
        MdoDeadlineExpired(g_MdoAccount.Tokens.Expires) || g_MdoAccount.Authorization.State[0]) return;
    for (size_t i = 0; i < MDO_ACCOUNT_READ_COUNT; ++i) {
        MdoAccountReadSlot* slot = &g_MdoAccount.Reads[i];
        if (slot->Cancel || !MdoDeadlineExpired(slot->Next)) continue;
        slot->Next = MdoDeadlineAfterUs(1000000u);
        MdoAccountReadJob* job = xrtCalloc(1, sizeof(*job));
        if (job) job->Cancel = xrtCancelCreate();
        if (!job || !job->Cancel) { xrtFree(job); continue; }
        job->Kind = (MdoAccountReadKind)i;
        job->Epoch = g_MdoAccount.Epoch;
        job->CredentialRevision = g_MdoAccount.ModelCredentialRevision;
        job->Member = g_MdoAccount.Tokens.MemberId;
        strcpy(job->Access, g_MdoAccount.Tokens.Access);
        xtaskargs args = {.Cancel = job->Cancel, .Destroy = MdoAccountReadDrop};
        xfuture* future = xrtTaskSubmit(g_MdoAccount.ReadPool, MdoAccountReadRun, job, &args);
        if (!future) {
            xrtCancelDestroy(job->Cancel);
            xrtSecureZero(job, sizeof(*job));
            xrtFree(job);
            continue;
        }
        slot->Cancel = job->Cancel;
        slot->LastStarted = MdoMonotonicUs();
        slot->Next = MdoDeadlineAfterUs(MdoAccountReadConfig[i].IntervalUs);
        xrtFutureDestroy(future);
    }
}

static int32 MdoAccountWorker(void* Unused)
{
    (void)Unused;
    for (;;) {
        MdoAccountWork work; xcancel* cancel; uint16 status = 0;
        xvalue *body=NULL,*data=NULL;
        MdoAccountTokens tokens; bool ok = false;
        memset(&work, 0, sizeof(work)); memset(&tokens, 0, sizeof(tokens));
        xrtMutexLock(g_MdoAccount.Lock);
        while (!g_MdoAccount.Stopping && !g_MdoAccount.Work.Kind) {
            if (g_MdoAccount.NextRefresh && g_MdoAccount.Tokens.Refresh[0] &&
                MdoDeadlineExpired(g_MdoAccount.NextRefresh) && !g_MdoAccount.Authorization.State[0]) {
                (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH); break;
            }
            if (g_MdoAccount.Authorization.Expires && g_MdoAccount.Authorization.Expires <= MdoUtcUs()/1000000) {
                MdoAccountClearAuthorizationLocked(); strcpy(g_MdoAccount.Message, "authorization_expired");
                MdoAccountChangedLocked();
            }
            MdoAccountDispatchReadsLocked();
            xrtCondWaitFor(g_MdoAccount.Changed, g_MdoAccount.Lock, MdoDurationMs(1000000));
        }
        if (g_MdoAccount.Stopping) { xrtMutexUnlock(g_MdoAccount.Lock); break; }
        work = g_MdoAccount.Work; xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
        g_MdoAccount.Busy = true; g_MdoAccount.BusyKind = work.Kind;
        cancel = xrtCancelCreate(); g_MdoAccount.WorkCancel = cancel;
        MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock);
        MdoAccountTransport transport = {0};
        if (cancel && (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE || work.Kind == MDO_ACCOUNT_WORK_REFRESH ||
            work.Kind == MDO_ACCOUNT_WORK_PASSWORD)) {
            body = xrtValueObject();
            if (body && work.Kind == MDO_ACCOUNT_WORK_EXCHANGE) {
                ok = MdoAccountSetString(body, "grant_type", "authorization_code") &&
                    MdoAccountSetString(body, "client_id", work.Authorization.Client) &&
                    MdoAccountSetString(body, "redirect_uri", work.Authorization.Redirect) &&
                    MdoAccountSetString(body, "code", work.Code) &&
                    MdoAccountSetString(body, "code_verifier", work.Authorization.Verifier);
            } else if (body && work.Kind == MDO_ACCOUNT_WORK_PASSWORD) {
                ok = MdoAccountSetString(body, "identifier", work.Identifier) &&
                    MdoAccountSetString(body, "password", work.Password);
            } else if (body) ok = MdoAccountSetString(body, "refresh_token", work.Tokens.Refresh) &&
                MdoAccountSetBool(body, "persistent", true);
            if (ok) data = MdoAccountClientDetailed(work.Origin, work.Kind == MDO_ACCOUNT_WORK_EXCHANGE ?
                "/api/v1/auth/token" : work.Kind == MDO_ACCOUNT_WORK_PASSWORD ? "/api/v1/login" :
                "/api/v1/token/refresh", "POST", body, NULL, cancel, &status, &transport);
            xrtSecureZero(work.Password, sizeof(work.Password));
            ok = MdoAccountClientTokens(data, &tokens) &&
                (work.Kind != MDO_ACCOUNT_WORK_REFRESH || tokens.MemberId == work.Tokens.MemberId);
            MdoAccountSecretValueUnit(data); data = NULL;
        } else if (cancel && work.Kind == MDO_ACCOUNT_WORK_LOGOUT) {
            data = MdoAccountClient(work.Origin, "/api/v1/logout", "POST", NULL, work.Tokens.Access, cancel, &status);
            xrtValueRelease(data); data = NULL;
        }
        MdoAccountSecretValueUnit(body);
        uint32 RecoveryLimit=MdoRecoveryRetryLimit();
        xrtMutexLock(g_MdoAccount.Lock);
        g_MdoAccount.WorkCancel = NULL; g_MdoAccount.Busy = false; g_MdoAccount.BusyKind = 0;
        if (!g_MdoAccount.Stopping && work.Epoch == g_MdoAccount.Epoch) {
            if (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE || work.Kind == MDO_ACCOUNT_WORK_REFRESH ||
                work.Kind == MDO_ACCOUNT_WORK_PASSWORD) {
                if (ok) {
                    bool replaced = work.Kind != MDO_ACCOUNT_WORK_REFRESH ||
                        strcmp(tokens.Refresh, work.Tokens.Refresh) != 0;
                    if (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE || work.Kind == MDO_ACCOUNT_WORK_PASSWORD) {
                        uint64 Previous=g_MdoAccount.Tokens.MemberId ? g_MdoAccount.Tokens.MemberId : g_MdoAccount.ExpiredMemberId;
                        if(!Previous)(void)MdoAccountGetUInt(xrtValueObjectGet(g_MdoAccount.Profile,XRT_STR_LITERAL("id")),&Previous);
                        if(Previous!=tokens.MemberId)MdoAccountNewSessionLocked();
                        xrtValueRelease(g_MdoAccount.Profile); g_MdoAccount.Profile = NULL;
                        xrtValueRelease(g_MdoAccount.Usage); g_MdoAccount.Usage = NULL;
                        MdoAccountClearModelsLocked();
                        MdoAccountClearBalanceLocked();
                        g_MdoAccount.Remember = work.Kind == MDO_ACCOUNT_WORK_PASSWORD ? work.Remember : work.Authorization.Remember;
                    }
                    g_MdoAccount.Tokens = tokens; g_MdoAccount.ExpiredMemberId = 0;
                    g_MdoAccount.Message[0] = 0;
                    ++g_MdoAccount.ModelCredentialRevision;
                    g_MdoAccount.NextRefresh = 0; g_MdoAccount.RefreshFailures = 0;
                    (void)MdoAccountCredentialSaveLocked(replaced);
                    MdoAccountResetReadsLocked();
                    if (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE) MdoAccountClearAuthorizationLocked();
                    MdoAccountDispatchReadsLocked();
                } else {
                    bool revoked = work.Kind == MDO_ACCOUNT_WORK_REFRESH && (status == 410 || transport.RefreshRejected);
                    if (work.Kind == MDO_ACCOUNT_WORK_REFRESH) {
                        /* Keep the account binding so a paused run can resume
                         * when the same member signs in again. */
                        if (revoked) {
                            MdoAccountClearTokensLocked();
                            g_MdoAccount.ExpiredMemberId = work.Tokens.MemberId;
                        }
                        else {
                            if (status == 401) g_MdoAccount.Tokens.Expires = 0;
                            if (g_MdoAccount.RefreshFailures < UINT32_MAX) ++g_MdoAccount.RefreshFailures;
                            unsigned shift = g_MdoAccount.RefreshFailures < 6u ? g_MdoAccount.RefreshFailures : 6u;
                            uint64 delay = g_MdoAccount.RefreshFailures>RecoveryLimit ? MDO_RECOVERY_WAIT_MS : (uint64)MDO_ACCOUNT_REFRESH_RETRY_BASE_MS << shift;
                            g_MdoAccount.NextRefresh = MdoDeadlineAfterUs(delay * 1000u);
                        }
                    }
                    strcpy(g_MdoAccount.Message, revoked ? "login_expired" : !status && transport.SystemTrustFailed ? "system_trust_failed" :
                        work.Kind == MDO_ACCOUNT_WORK_PASSWORD && (status == 400 || status == 401) ? "invalid_credentials" :
                        status == 429 ? "login_rate_limited" : (status == 401 || status == 410) && !g_MdoAccount.Tokens.Refresh[0] ? "login_expired" :
                        status == 404 ? "service_update_required" : work.Kind == MDO_ACCOUNT_WORK_REFRESH ?
                        "account_renewal_pending" : "account_connection_failed");
                }
                ++g_MdoAccount.RefreshSerial;
                if (!ok && work.Kind == MDO_ACCOUNT_WORK_EXCHANGE) MdoAccountClearAuthorizationLocked();
            } else if (work.Kind == MDO_ACCOUNT_WORK_LOGOUT && status != 200) {
                strcpy(g_MdoAccount.Message, "local_logout_only");
            }
            MdoAccountChangedLocked();
        }
        xrtMutexUnlock(g_MdoAccount.Lock);
        xrtCancelDestroy(cancel);
        xrtSecureZero(&work, sizeof(work)); xrtSecureZero(&tokens, sizeof(tokens));
    }
    return 0;
}
bool MdoAccountInit(void)
{
    if (g_MdoAccount.Initialized) return true;
    memset(&g_MdoAccount, 0, sizeof(g_MdoAccount));
    g_MdoAccount.Lock = xrtMutexCreate(); g_MdoAccount.Changed = xrtCondCreate();
    g_MdoAccount.SessionCancel = xrtCancelCreate();
    if (!g_MdoAccount.Lock || !g_MdoAccount.Changed || !g_MdoAccount.SessionCancel) goto failed;
    g_MdoAccount.Initialized = true; g_MdoAccount.Epoch = 1;
    if (!MdoAccountOrigin(MDO_ACCOUNT_SERVICE_ORIGIN, g_MdoAccount.Origin)) goto failed;
    xrtMutexLock(g_MdoAccount.Lock);
    char origin[MDO_ACCOUNT_ORIGIN_LIMIT];
    xvalue* stored = MdoAccountCredentialRead(MDO_ACCOUNT_SESSION_PATH, origin);
    cstr refresh = MdoAccountText(stored, "refresh_token", 64); uint64 id = 0, schema = 0;
    if (stored && !strcmp(origin, g_MdoAccount.Origin) && MdoAccountHex(refresh, 64) &&
        MdoAccountGetUInt(xrtValueObjectGet(stored, xrtStrView("schema_version")), &schema) && schema == 1 &&
        MdoAccountGetUInt(xrtValueObjectGet(stored, xrtStrView("member_id")), &id) && id) {
        strcpy(g_MdoAccount.Tokens.Refresh, refresh); g_MdoAccount.Tokens.MemberId = id;
        g_MdoAccount.Remember = true; g_MdoAccount.Saved = true;
        g_MdoAccount.Profile = MdoAccountProfile(xrtValueObjectGet(stored, xrtStrView("profile")), id);
        (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
    } else if (stored) (void)MdoHomeRemove(MDO_ACCOUNT_SESSION_PATH, false);
    MdoAccountSecretValueUnit(stored);
    (void)MdoAccountAuthorizationLoad(&g_MdoAccount.Authorization);
    if (g_MdoAccount.Authorization.State[0]) xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
    xrtMutexUnlock(g_MdoAccount.Lock);
    MdoModelOnlineAuthority authority={MDO_ACCOUNT_SERVICE_ORIGIN,MdoAccountModelAccess,MdoAccountModelRelease,MdoAccountModelState,MdoAccountModelPin,MdoAccountModelRecoveryRevision};
    MdoModelManagerSetOnlineAuthority(&authority);
    xtaskpoolconfig pool = {.Threads = MDO_ACCOUNT_READ_COUNT, .QueueLimit = MDO_ACCOUNT_READ_COUNT};
    g_MdoAccount.ReadPool = xrtTaskPoolCreate(&pool);
    if (!g_MdoAccount.ReadPool) goto failed;
    g_MdoAccount.Worker = xrtThreadCreate(MdoAccountWorker, NULL, 0);
    if (g_MdoAccount.Worker) return true;
failed:
    MdoAccountUnit(); return false;
}
void MdoAccountUnit(void)
{
    if (g_MdoAccount.Lock) {
        xrtMutexLock(g_MdoAccount.Lock); g_MdoAccount.Stopping = true;
        xrtCancelRequest(g_MdoAccount.WorkCancel); xrtCancelRequest(g_MdoAccount.SessionCancel);
        MdoAccountResetReadsLocked();
        if (g_MdoAccount.Changed) xrtCondBroadcast(g_MdoAccount.Changed);
        while (g_MdoAccount.Acquirers) xrtCondWaitFor(g_MdoAccount.Changed, g_MdoAccount.Lock, MdoDurationMs(100000));
        xrtMutexUnlock(g_MdoAccount.Lock);
    }
    if (g_MdoAccount.Worker) { xrtThreadWait(g_MdoAccount.Worker); xrtThreadDestroy(g_MdoAccount.Worker); }
    if (g_MdoAccount.ReadPool) {
        xrtTaskPoolCancel(g_MdoAccount.ReadPool);
        xrtTaskPoolWait(g_MdoAccount.ReadPool);
        xrtTaskPoolDestroy(g_MdoAccount.ReadPool);
    }
    xrtValueRelease(g_MdoAccount.Allowance);MdoModelManagerSetOnlineAuthority(NULL);
    MdoAccountClearBalanceLocked();
    xrtValueRelease(g_MdoAccount.Profile); xrtValueRelease(g_MdoAccount.Usage);
    xrtCancelDestroy(g_MdoAccount.SessionCancel); xrtCondDestroy(g_MdoAccount.Changed);
    xrtMutexDestroy(g_MdoAccount.Lock); xrtSecureZero(&g_MdoAccount, sizeof(g_MdoAccount));
}
/* A refreshable login stays usable across short access-token expiry. No token
 * or profile fields leave the account manager through this predicate. */
bool MdoAccountHasSession(void)
{
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    bool live = !g_MdoAccount.Stopping && g_MdoAccount.Tokens.MemberId &&
        (g_MdoAccount.Tokens.Refresh[0] || (g_MdoAccount.Tokens.Access[0] &&
        !MdoDeadlineExpired(g_MdoAccount.Tokens.Expires)));
    xrtMutexUnlock(g_MdoAccount.Lock); return live;
}
xvalue* MdoAccountSnapshot(void)
{
    if (!g_MdoAccount.Initialized) return NULL;
    xrtMutexLock(g_MdoAccount.Lock);
    bool live = g_MdoAccount.Tokens.Access[0] && !MdoDeadlineExpired(g_MdoAccount.Tokens.Expires);
    cstr state = (g_MdoAccount.Work.Kind == MDO_ACCOUNT_WORK_PASSWORD ||
        (g_MdoAccount.Busy && g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_PASSWORD)) ? "signing_in" :
        g_MdoAccount.Authorization.State[0] ? "authorizing" :
        g_MdoAccount.Tokens.Refresh[0] && !live ? "refreshing" : live ? "signed_in" :
        g_MdoAccount.ExpiredMemberId || g_MdoAccount.Profile ? "expired" : "signed_out";
    xvalue *out = xrtValueObject(), *pending = xrtValueArray(); size_t i;
    bool ok = out && pending && MdoAccountSetString(out, "state", state) &&
        MdoAccountSetString(out, "origin", g_MdoAccount.Origin) &&
        MdoAccountSetString(out, "message", g_MdoAccount.Message) &&
        MdoAccountSetBool(out, "persistence_available", xsCredentialProtectionAvailable()) &&
        MdoAccountSetBool(out, "refresh_available", g_MdoAccount.Tokens.Refresh[0] != 0) &&
        MdoAccountSetBool(out, "remembered", g_MdoAccount.Saved) &&
        MdoAccountSetBool(out, "busy", g_MdoAccount.Busy || g_MdoAccount.Work.Kind ||
            (live && g_MdoAccount.Reads[MDO_ACCOUNT_READ_PROFILE].Cancel)) &&
        MdoAccountSetUInt(out, "revision", g_MdoAccount.Revision) &&
        MdoAccountSetUInt(out, "search_status", g_MdoAccount.SearchStatus);
    if (ok && g_MdoAccount.Profile) ok = xrtValueObjectSetNew(out, xrtStrView("profile"), xrtValueClone(g_MdoAccount.Profile));
    if (ok && g_MdoAccount.Usage) ok = xrtValueObjectSetNew(out, xrtStrView("usage"), xrtValueClone(g_MdoAccount.Usage));
    if(ok&&g_MdoAccount.Allowance)ok=xrtValueObjectSetNew(out,XRT_STR_LITERAL("model_allowance"),xrtValueClone(g_MdoAccount.Allowance));
    if(ok&&live&&g_MdoAccount.Balance)ok=xrtValueObjectSetNew(out,XRT_STR_LITERAL("balance"),xrtValueClone(g_MdoAccount.Balance));
    if(ok)ok=MdoAccountSetUInt(out,"balance_status",g_MdoAccount.BalanceStatus);
    if(ok)ok=MdoAccountSetBool(out,"model_allowance_pending",
        live&&!g_MdoAccount.Allowance&&!g_MdoAccount.ModelsStatus&&g_MdoAccount.Reads[MDO_ACCOUNT_READ_ALLOWANCE].Cancel);
    if(ok)ok=MdoAccountSetUInt(out,"models_status",g_MdoAccount.ModelsStatus)&&MdoAccountSetUInt(out,"model_generation",MdoModelManagerGeneration());
    if(ok)ok=MdoAccountSetString(out,"model_catalog_state",MdoModelLoginStateName(MdoAccountModelStateLocked()));
    for (i = 0; ok && i < MDO_ACCOUNT_WAIT_MAX; i++) if (g_MdoAccount.Waiters[i].Id && !g_MdoAccount.Waiters[i].Skipped) {
        xvalue* item = xrtValueObject();
        ok = item && MdoAccountSetUInt(item, "id", g_MdoAccount.Waiters[i].Id) &&
            MdoAccountSetString(item, "query", g_MdoAccount.Waiters[i].Query) &&
            MdoAccountSetUInt(item, "run_id", g_MdoAccount.Waiters[i].RunId) && xrtValueArrayAppendNew(pending, item);
    }
    if (ok) ok = xrtValueObjectSetNew(out, xrtStrView("pending_searches"), pending);
    else xrtValueRelease(pending);
    xrtMutexUnlock(g_MdoAccount.Lock);
    if (!ok) { xrtValueRelease(out); return NULL; } return out;
}
bool MdoAccountLoginStart(cstr LocalOrigin, bool Remember, xvalue** PublicResult)
{
    MdoAccountAuthorization authorization; bool ok; xvalue* out = NULL;
    if (!g_MdoAccount.Initialized || !PublicResult) return false;
    *PublicResult = NULL; memset(&authorization, 0, sizeof(authorization));
    xrtMutexLock(g_MdoAccount.Lock);
    ok = MdoAccountAuthorizationStart(g_MdoAccount.Origin, LocalOrigin, Remember, &authorization) &&
        MdoAccountAuthorizationSave(&authorization);
    if (ok) {
        ++g_MdoAccount.Epoch; xrtCancelRequest(g_MdoAccount.WorkCancel);
        xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
        g_MdoAccount.Authorization = authorization; g_MdoAccount.Message[0] = 0;
        MdoAccountChangedLocked();
    }
    xrtMutexUnlock(g_MdoAccount.Lock);
    if (ok) {
        bool opened = xsOpenExternalUrl(authorization.Url);
        out = xrtValueObject(); ok = out && MdoAccountSetString(out, "authorization_url", authorization.Url) &&
            MdoAccountSetBool(out, "opened", opened);
    }
    xrtSecureZero(&authorization, sizeof(authorization));
    if (!ok) { xrtValueRelease(out); return false; } *PublicResult = out; return true;
}
bool MdoAccountLoginPassword(cstr Identifier, cstr Password, bool Remember)
{
    if (!g_MdoAccount.Initialized || !Identifier || !Password || !Identifier[0] || !Password[0] ||
        strlen(Identifier) > 254 || strlen(Password) > 128 ||
        !xrtUtf8Valid(xrtStrView(Identifier), NULL) || !xrtUtf8Valid(xrtStrView(Password), NULL)) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    /* Keep the previous account until the new credentials succeed. Epoch
     * prevents cancelled or superseded requests from installing a late login. */
    ++g_MdoAccount.Epoch; xrtCancelRequest(g_MdoAccount.WorkCancel);
    MdoAccountClearAuthorizationLocked();
    bool ok = MdoAccountQueueLocked(MDO_ACCOUNT_WORK_PASSWORD);
    if (ok) {
        strcpy(g_MdoAccount.Work.Identifier, Identifier); strcpy(g_MdoAccount.Work.Password, Password);
        g_MdoAccount.Work.Remember = Remember && xsCredentialProtectionAvailable();
        g_MdoAccount.Message[0] = 0;
    }
    xrtMutexUnlock(g_MdoAccount.Lock); return ok;
}
bool MdoAccountLoginCancel(void)
{
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    if (!g_MdoAccount.Authorization.State[0] && g_MdoAccount.Work.Kind != MDO_ACCOUNT_WORK_PASSWORD &&
        !(g_MdoAccount.Busy && g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_PASSWORD)) {
        xrtMutexUnlock(g_MdoAccount.Lock); return true;
    }
    ++g_MdoAccount.Epoch; xrtCancelRequest(g_MdoAccount.WorkCancel);
    xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
    MdoAccountClearAuthorizationLocked(); g_MdoAccount.Message[0] = 0;
    if (g_MdoAccount.Tokens.Refresh[0] && !g_MdoAccount.Tokens.Access[0])
        (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
    MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock); return true;
}
bool MdoAccountCallback(cstr State, cstr Code, cstr Error)
{
    if (!g_MdoAccount.Initialized || !MdoAccountHex(State, 64)) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    unsigned char different = 0; size_t i;
    for (i = 0; i < 64; i++) different |= (unsigned char)(State[i] ^ g_MdoAccount.Authorization.State[i]);
    bool ok = g_MdoAccount.Authorization.Expires > MdoUtcUs()/1000000 && !different &&
        g_MdoAccount.Work.Kind != MDO_ACCOUNT_WORK_EXCHANGE &&
        !(g_MdoAccount.Busy && g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_EXCHANGE);
    if (ok && Error && !strcmp(Error, "access_denied")) {
        MdoAccountClearAuthorizationLocked(); strcpy(g_MdoAccount.Message, "authorization_cancelled");
    } else if (ok && MdoAccountHex(Code, 64) && (!Error || !Error[0])) {
        (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_EXCHANGE);
        g_MdoAccount.Work.Authorization = g_MdoAccount.Authorization; strcpy(g_MdoAccount.Work.Code, Code);
        /* Pending PKCE is no longer reusable after exchange starts. */
        (void)MdoHomeRemove(MDO_ACCOUNT_PENDING_PATH, false);
    } else ok = false;
    if (ok) MdoAccountChangedLocked();
    xrtMutexUnlock(g_MdoAccount.Lock);
    return ok;
}
bool MdoAccountRefresh(void)
{
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    bool ok = MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
    xrtMutexUnlock(g_MdoAccount.Lock); return ok;
}
bool MdoAccountLogout(void)
{
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    ++g_MdoAccount.Epoch; xrtCancelRequest(g_MdoAccount.WorkCancel);
    xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
    if (g_MdoAccount.Tokens.Access[0]) (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_LOGOUT);
    MdoAccountNewSessionLocked(); MdoAccountClearTokensLocked(); MdoAccountClearAuthorizationLocked();
    xrtValueRelease(g_MdoAccount.Profile); g_MdoAccount.Profile = NULL;
    xrtValueRelease(g_MdoAccount.Usage); g_MdoAccount.Usage = NULL;
    g_MdoAccount.Remember = false; g_MdoAccount.Message[0] = 0; g_MdoAccount.SearchStatus = 0;
    MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock); return true;
}
bool MdoAccountSkipSearch(uint64 Id)
{
    size_t i; bool ok = false;
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    for (i = 0; i < MDO_ACCOUNT_WAIT_MAX; i++) if (g_MdoAccount.Waiters[i].Id == Id && Id) {
        g_MdoAccount.Waiters[i].Skipped = true; ok = true; break;
    }
    if (ok) MdoAccountChangedLocked();
    xrtMutexUnlock(g_MdoAccount.Lock);
    return ok;
}
static void MdoAccountCancelLease(void* Data)
{
    xrtCancelRequest((xcancel*)Data);
}
cstr MdoAccountSearchEndpoint(void)
{
    return MDO_ACCOUNT_SERVICE_ORIGIN "/api/v1/search";
}
/* Lock held. The caller releases a partially created lease after unlocking,
 * since unwatch can wait for a cancellation callback to finish. */
static bool MdoAccountLeaseLocked(xcancel* Cancel, MdoAccountLease* Lease)
{
    if (!g_MdoAccount.Tokens.Access[0] || !g_MdoAccount.SessionCancel ||
        g_MdoAccount.Tokens.Expires <= MdoDeadlineAfterUs(30000000) ||
        (Cancel && xrtCancelRequested(Cancel))) return false;
    Lease->AccessToken = xrtMalloc(strlen(g_MdoAccount.Tokens.Access) + 1);
    Lease->Cancel = xrtCancelChild(Cancel);
    if (!Lease->AccessToken || !Lease->Cancel) return false;
    strcpy(Lease->AccessToken, g_MdoAccount.Tokens.Access);
    Lease->Watch = xrtCancelWatch(g_MdoAccount.SessionCancel, MdoAccountCancelLease, Lease->Cancel);
    if (!Lease->Watch) return false;
    Lease->Generation = g_MdoAccount.Epoch; Lease->MemberId = g_MdoAccount.Tokens.MemberId;
    Lease->Managed = true; return true;
}
bool MdoAccountAcquireService(xcancel* Cancel, MdoAccountLease* Lease)
{
    if (!Lease) return false;
    memset(Lease,0,sizeof(*Lease));
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    bool ok = !g_MdoAccount.Stopping && MdoAccountLeaseLocked(Cancel,Lease);
    if (!ok && !g_MdoAccount.Stopping && g_MdoAccount.Tokens.Refresh[0] &&
        (!Cancel || !xrtCancelRequested(Cancel))) (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
    xrtMutexUnlock(g_MdoAccount.Lock);
    if (!ok) MdoAccountRelease(Lease);
    return ok;
}
bool MdoAccountAcquireServiceWait(xcancel* Cancel, uint32 TimeoutMs, MdoAccountLease* Lease)
{
    if (!Lease) return false;
    memset(Lease,0,sizeof(*Lease));
    if (!g_MdoAccount.Initialized || !TimeoutMs || TimeoutMs>30000u) return false;
    bool ok=false; uint64 until=MdoDeadlineAfterUs((uint64)TimeoutMs*1000u);
    xrtMutexLock(g_MdoAccount.Lock); ++g_MdoAccount.Acquirers;
    uint64 epoch=g_MdoAccount.Epoch, member=g_MdoAccount.Tokens.MemberId;
    xcancel* session=g_MdoAccount.SessionCancel ? xrtCancelRef(g_MdoAccount.SessionCancel):NULL;
    for (;;) {
        if (!session || !member || g_MdoAccount.Stopping || g_MdoAccount.Epoch!=epoch ||
            g_MdoAccount.Tokens.MemberId!=member || xrtCancelRequested(session) ||
            (Cancel && xrtCancelRequested(Cancel)) || MdoDeadlineExpired(until)) break;
        if (g_MdoAccount.Tokens.Access[0] && g_MdoAccount.Tokens.Expires>MdoDeadlineAfterUs(30000000)) {
            ok=MdoAccountLeaseLocked(Cancel,Lease); break;
        }
        if (!g_MdoAccount.Tokens.Refresh[0] || !MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH)) break;
        uint64 left=MdoDeadlineRemainingUs(until);
        xrtCondWaitFor(g_MdoAccount.Changed,g_MdoAccount.Lock,MdoDurationMs(left<100000u ? left:100000u));
    }
    --g_MdoAccount.Acquirers; xrtCondBroadcast(g_MdoAccount.Changed); xrtMutexUnlock(g_MdoAccount.Lock);
    xrtCancelDestroy(session);
    if (!ok) MdoAccountRelease(Lease);
    return ok;
}
bool MdoAccountAcquire(cstr Query, const xwork_tool_context* Context, MdoAccountLease* Lease)
{
    size_t slot = MDO_ACCOUNT_WAIT_MAX, i; bool ok = false;
    if (!Lease || !Context) return false;
    memset(Lease, 0, sizeof(*Lease));
    if (!g_MdoAccount.Initialized) return false;
    uint64 until = MdoDeadlineAfterUs(300000000);
    xrtMutexLock(g_MdoAccount.Lock); ++g_MdoAccount.Acquirers;
    for (;;) {
        if (g_MdoAccount.Stopping ||
            (Context->pCancel && xrtCancelRequested(Context->pCancel)) || MdoDeadlineExpired(until) ||
            (Context->uDeadline != MDO_DEADLINE_NEVER && MdoDeadlineExpired(Context->uDeadline)) ||
            (slot < MDO_ACCOUNT_WAIT_MAX && g_MdoAccount.Waiters[slot].Skipped)) break;
        if (g_MdoAccount.Tokens.Access[0] && g_MdoAccount.Tokens.Expires > MdoDeadlineAfterUs(30000000)) {
            ok = MdoAccountLeaseLocked(Context->pCancel,Lease); break;
        }
        if (g_MdoAccount.Tokens.Refresh[0]) (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
        if (slot == MDO_ACCOUNT_WAIT_MAX) {
            for (i = 0; i < MDO_ACCOUNT_WAIT_MAX; i++) if (!g_MdoAccount.Waiters[i].Id) { slot = i; break; }
            if (slot == MDO_ACCOUNT_WAIT_MAX) break;
            MdoAccountWaiter* waiter = &g_MdoAccount.Waiters[slot]; waiter->Id = ++g_MdoAccount.NextWaiter;
            waiter->RunId = Context->uRunId; snprintf(waiter->Query, sizeof(waiter->Query), "%s", Query ? Query : "");
            MdoAccountChangedLocked();
        }
        xrtCondWaitFor(g_MdoAccount.Changed, g_MdoAccount.Lock, MdoDurationMs(100000));
    }
    if (slot < MDO_ACCOUNT_WAIT_MAX) { memset(&g_MdoAccount.Waiters[slot], 0, sizeof(g_MdoAccount.Waiters[slot])); MdoAccountChangedLocked(); }
    --g_MdoAccount.Acquirers; xrtCondBroadcast(g_MdoAccount.Changed); xrtMutexUnlock(g_MdoAccount.Lock);
    if (!ok) MdoAccountRelease(Lease);
    return ok;
}
void MdoAccountRelease(MdoAccountLease* Lease)
{
    if (!Lease) return;
    xrtCancelUnwatch(Lease->Watch); xrtCancelDestroy(Lease->Cancel);
    MdoSecretRelease(&Lease->AccessToken); memset(Lease, 0, sizeof(*Lease));
}
bool MdoAccountRejectAccess(const MdoAccountLease* Lease)
{
    if (!Lease || !Lease->Managed || !Lease->AccessToken || !g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    bool ok = Lease->Generation == g_MdoAccount.Epoch && g_MdoAccount.Tokens.Refresh[0];
    if (ok && !strcmp(Lease->AccessToken, g_MdoAccount.Tokens.Access)) {
        g_MdoAccount.Tokens.Expires = 0; ok = MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
    }
    xrtMutexUnlock(g_MdoAccount.Lock); return ok;
}
void MdoAccountSearchStatus(uint16 Status)
{
    if (!g_MdoAccount.Initialized) return;
    xrtMutexLock(g_MdoAccount.Lock); g_MdoAccount.SearchStatus = Status;
    if (Status == 200) MdoAccountRequestReadLocked(MDO_ACCOUNT_READ_SEARCH);
    MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock);
}
bool MdoAccountOpenWebsite(cstr Path)
{
    char url[1024]; bool ok;
    if (!g_MdoAccount.Initialized || !Path || strcmp(Path, "/account/index.html")) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    ok = g_MdoAccount.Origin[0] && snprintf(url, sizeof(url), "%s%s", g_MdoAccount.Origin, Path) < (int)sizeof(url);
    xrtMutexUnlock(g_MdoAccount.Lock); return ok && xsOpenExternalUrl(url);
}
