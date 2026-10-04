#include "internal.h"

MdoAccountManager g_MdoAccount;

/* All session transitions and the single pending job belong to Lock. The
 * worker alone performs identity HTTP requests, outside Lock. Epoch prevents
 * a late response from resurrecting an account after logout or a new login. */
static void MdoAccountChangedLocked(void)
{
    ++g_MdoAccount.Revision;
    xrtCondBroadcast(g_MdoAccount.Changed);
}
static void MdoAccountClearTokensLocked(void)
{
    xrtSecureZero(&g_MdoAccount.Tokens, sizeof(g_MdoAccount.Tokens));
    g_MdoAccount.Saved = false;
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
static void MdoAccountSyncOriginLocked(void)
{
    MdoConfigWebSettings settings; char origin[MDO_ACCOUNT_ORIGIN_LIMIT] = "";
    memset(&settings, 0, sizeof(settings)); settings.Size = sizeof(settings);
    if (MdoConfigGetWebSettings(&settings)) (void)MdoAccountOrigin(settings.Endpoint, origin);
    if (!strcmp(origin, g_MdoAccount.Origin)) return;
    ++g_MdoAccount.Epoch;
    xrtCancelRequest(g_MdoAccount.WorkCancel);
    xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
    MdoAccountNewSessionLocked();
    MdoAccountClearTokensLocked(); MdoAccountClearAuthorizationLocked();
    xrtValueRelease(g_MdoAccount.Profile); g_MdoAccount.Profile = NULL;
    xrtValueRelease(g_MdoAccount.Usage); g_MdoAccount.Usage = NULL;
    strcpy(g_MdoAccount.Origin, origin); g_MdoAccount.SearchStatus = 0;
    g_MdoAccount.Message[0] = 0; MdoAccountChangedLocked();
}
static bool MdoAccountQueueLocked(unsigned Kind)
{
    if (g_MdoAccount.Stopping || !g_MdoAccount.Origin[0]) return false;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH && g_MdoAccount.Authorization.State[0]) return true;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH &&
        ((g_MdoAccount.Busy && (g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_REFRESH ||
          g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_EXCHANGE)) ||
         g_MdoAccount.Work.Kind == MDO_ACCOUNT_WORK_REFRESH ||
         g_MdoAccount.Work.Kind == MDO_ACCOUNT_WORK_EXCHANGE)) return true;
    if (Kind == MDO_ACCOUNT_WORK_PROFILE && (g_MdoAccount.Busy || g_MdoAccount.Work.Kind)) return true;
    if (Kind == MDO_ACCOUNT_WORK_REFRESH && !g_MdoAccount.Tokens.Refresh[0]) return false;
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
    cstr access = MdoAccountText(Data, "access_token", MDO_ACCOUNT_TOKEN_LIMIT - 1);
    cstr refresh = MdoAccountText(Data, "refresh_token", 64);
    cstr verifier = MdoAccountText(Data, "code_verifier", 128);
    cstr code = MdoAccountText(Data, "code", 64);
    if (access) xrtSecureZero((void*)access, strlen(access));
    if (refresh) xrtSecureZero((void*)refresh, strlen(refresh));
    if (verifier) xrtSecureZero((void*)verifier, strlen(verifier));
    if (code) xrtSecureZero((void*)code, strlen(code));
    xrtValueRelease(Data);
}
static int32 MdoAccountWorker(void* Unused)
{
    (void)Unused;
    for (;;) {
        MdoAccountWork work; xcancel* cancel; uint16 status = 0, usage_status = 0;
        xvalue *body = NULL, *data = NULL, *profile = NULL, *usage = NULL;
        MdoAccountTokens tokens; bool ok = false;
        memset(&work, 0, sizeof(work)); memset(&tokens, 0, sizeof(tokens));
        xrtMutexLock(g_MdoAccount.Lock);
        while (!g_MdoAccount.Stopping && !g_MdoAccount.Work.Kind) {
            if (g_MdoAccount.Authorization.Expires && g_MdoAccount.Authorization.Expires <= xrtNow()/1000000) {
                MdoAccountClearAuthorizationLocked(); strcpy(g_MdoAccount.Message, "authorization_expired");
                MdoAccountChangedLocked();
            }
            xrtCondWaitFor(g_MdoAccount.Changed, g_MdoAccount.Lock, 1000000);
        }
        if (g_MdoAccount.Stopping) { xrtMutexUnlock(g_MdoAccount.Lock); break; }
        work = g_MdoAccount.Work; xrtSecureZero(&g_MdoAccount.Work, sizeof(g_MdoAccount.Work));
        g_MdoAccount.Busy = true; g_MdoAccount.BusyKind = work.Kind;
        cancel = xrtCancelCreate(); g_MdoAccount.WorkCancel = cancel;
        if (work.Kind == MDO_ACCOUNT_WORK_REFRESH) {
            /* Remove the old refresh before sending it. A crash or lost reply
             * must cause re-login, never replay a possibly rotated token. */
            (void)MdoHomeRemove(MDO_ACCOUNT_SESSION_PATH, false); g_MdoAccount.Saved = false;
        }
        MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock);
        if (cancel && (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE || work.Kind == MDO_ACCOUNT_WORK_REFRESH)) {
            body = xrtValueObject();
            if (body && work.Kind == MDO_ACCOUNT_WORK_EXCHANGE) {
                ok = MdoAccountSetString(body, "grant_type", "authorization_code") &&
                    MdoAccountSetString(body, "client_id", work.Authorization.Client) &&
                    MdoAccountSetString(body, "redirect_uri", work.Authorization.Redirect) &&
                    MdoAccountSetString(body, "code", work.Code) &&
                    MdoAccountSetString(body, "code_verifier", work.Authorization.Verifier);
            } else if (body) ok = MdoAccountSetString(body, "refresh_token", work.Tokens.Refresh);
            if (ok) data = MdoAccountClient(work.Origin, work.Kind == MDO_ACCOUNT_WORK_EXCHANGE ?
                "/api/v1/auth/token" : "/api/v1/token/refresh", "POST", body, NULL, cancel, &status);
            ok = MdoAccountClientTokens(data, &tokens) &&
                (work.Kind != MDO_ACCOUNT_WORK_REFRESH || tokens.MemberId == work.Tokens.MemberId);
            MdoAccountSecretValueUnit(data); data = NULL;
        } else if (cancel && work.Kind == MDO_ACCOUNT_WORK_PROFILE) {
            data = MdoAccountClient(work.Origin, "/api/v1/profile", "GET", NULL, work.Tokens.Access, cancel, &status);
            profile = MdoAccountProfile(data, work.Tokens.MemberId); xrtValueRelease(data); data = NULL;
            data = MdoAccountClient(work.Origin, "/api/v1/search/usage", "GET", NULL, work.Tokens.Access, cancel, &usage_status);
            usage = MdoAccountUsage(data); xrtValueRelease(data); data = NULL;
        } else if (cancel && work.Kind == MDO_ACCOUNT_WORK_LOGOUT) {
            data = MdoAccountClient(work.Origin, "/api/v1/logout", "POST", NULL, work.Tokens.Access, cancel, &status);
            xrtValueRelease(data); data = NULL;
        }
        MdoAccountSecretValueUnit(body);
        xrtMutexLock(g_MdoAccount.Lock);
        g_MdoAccount.WorkCancel = NULL; g_MdoAccount.Busy = false; g_MdoAccount.BusyKind = 0;
        if (!g_MdoAccount.Stopping && work.Epoch == g_MdoAccount.Epoch) {
            if (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE || work.Kind == MDO_ACCOUNT_WORK_REFRESH) {
                if (ok) {
                    if (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE) {
                        MdoAccountNewSessionLocked();
                        xrtValueRelease(g_MdoAccount.Profile); g_MdoAccount.Profile = NULL;
                        xrtValueRelease(g_MdoAccount.Usage); g_MdoAccount.Usage = NULL;
                        g_MdoAccount.Remember = work.Authorization.Remember;
                    }
                    g_MdoAccount.Tokens = tokens; g_MdoAccount.Message[0] = 0;
                    (void)MdoAccountCredentialSaveLocked();
                    (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_PROFILE);
                } else {
                    if (work.Kind == MDO_ACCOUNT_WORK_REFRESH) MdoAccountClearTokensLocked();
                    strcpy(g_MdoAccount.Message, status == 401 || status == 410 ? "login_expired" :
                        status == 404 ? "service_update_required" : "account_connection_failed");
                }
                ++g_MdoAccount.RefreshSerial;
                if (work.Kind == MDO_ACCOUNT_WORK_EXCHANGE) MdoAccountClearAuthorizationLocked();
            } else if (work.Kind == MDO_ACCOUNT_WORK_PROFILE) {
                if (profile) { xrtValueRelease(g_MdoAccount.Profile); g_MdoAccount.Profile = profile; profile = NULL; }
                if (usage) { xrtValueRelease(g_MdoAccount.Usage); g_MdoAccount.Usage = usage; usage = NULL; }
                if (usage_status) g_MdoAccount.SearchStatus = usage_status;
                /* Profile reads never invalidate a working token on transient
                 * network failures. Search handles a real 401 with one refresh. */
            } else if (work.Kind == MDO_ACCOUNT_WORK_LOGOUT && status != 200) {
                strcpy(g_MdoAccount.Message, "local_logout_only");
            }
            MdoAccountChangedLocked();
        }
        xrtMutexUnlock(g_MdoAccount.Lock);
        xrtCancelDestroy(cancel); xrtValueRelease(profile); xrtValueRelease(usage);
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
    xrtMutexLock(g_MdoAccount.Lock);
    MdoConfigWebSettings settings; memset(&settings, 0, sizeof(settings)); settings.Size = sizeof(settings);
    if (MdoConfigGetWebSettings(&settings)) (void)MdoAccountOrigin(settings.Endpoint, g_MdoAccount.Origin);
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
        if (g_MdoAccount.Changed) xrtCondBroadcast(g_MdoAccount.Changed);
        while (g_MdoAccount.Acquirers) xrtCondWaitFor(g_MdoAccount.Changed, g_MdoAccount.Lock, 100000);
        xrtMutexUnlock(g_MdoAccount.Lock);
    }
    if (g_MdoAccount.Worker) { xrtThreadWait(g_MdoAccount.Worker); xrtThreadDestroy(g_MdoAccount.Worker); }
    xrtValueRelease(g_MdoAccount.Profile); xrtValueRelease(g_MdoAccount.Usage);
    xrtCancelDestroy(g_MdoAccount.SessionCancel); xrtCondDestroy(g_MdoAccount.Changed);
    xrtMutexDestroy(g_MdoAccount.Lock); xrtSecureZero(&g_MdoAccount, sizeof(g_MdoAccount));
}
xvalue* MdoAccountSnapshot(void)
{
    if (!g_MdoAccount.Initialized) return NULL;
    xrtMutexLock(g_MdoAccount.Lock); MdoAccountSyncOriginLocked();
    bool live = g_MdoAccount.Tokens.Access[0] && !xrtDeadlineExpired(g_MdoAccount.Tokens.Expires);
    cstr state = g_MdoAccount.Authorization.State[0] ? "authorizing" :
        g_MdoAccount.Tokens.Refresh[0] && !live ? "refreshing" : live ? "signed_in" :
        g_MdoAccount.Profile ? "expired" : "signed_out";
    xvalue *out = xrtValueObject(), *pending = xrtValueArray(); size_t i;
    bool ok = out && pending && MdoAccountSetString(out, "state", state) &&
        MdoAccountSetString(out, "origin", g_MdoAccount.Origin) &&
        MdoAccountSetString(out, "message", g_MdoAccount.Message) &&
        MdoAccountSetBool(out, "persistence_available", xsCredentialProtectionAvailable()) &&
        MdoAccountSetBool(out, "remembered", g_MdoAccount.Saved) &&
        MdoAccountSetBool(out, "busy", g_MdoAccount.Busy || g_MdoAccount.Work.Kind) &&
        MdoAccountSetUInt(out, "revision", g_MdoAccount.Revision) &&
        MdoAccountSetUInt(out, "search_status", g_MdoAccount.SearchStatus);
    if (ok && g_MdoAccount.Profile) ok = xrtValueObjectSetNew(out, xrtStrView("profile"), xrtValueClone(g_MdoAccount.Profile));
    if (ok && g_MdoAccount.Usage) ok = xrtValueObjectSetNew(out, xrtStrView("usage"), xrtValueClone(g_MdoAccount.Usage));
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
    xrtMutexLock(g_MdoAccount.Lock); MdoAccountSyncOriginLocked();
    ok = MdoAccountAuthorizationStart(g_MdoAccount.Origin, LocalOrigin, Remember, &authorization) &&
        MdoAccountAuthorizationSave(&authorization);
    if (ok) {
        if (g_MdoAccount.Busy && g_MdoAccount.BusyKind == MDO_ACCOUNT_WORK_REFRESH) {
            /* The previous rotation may already have reached the server.
             * Discard its old refresh rather than replay it after switching. */
            xrtSecureZero(g_MdoAccount.Tokens.Refresh, sizeof(g_MdoAccount.Tokens.Refresh));
            g_MdoAccount.Saved = false;
        }
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
bool MdoAccountLoginCancel(void)
{
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock);
    if (!g_MdoAccount.Authorization.State[0]) { xrtMutexUnlock(g_MdoAccount.Lock); return true; }
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
    bool ok = g_MdoAccount.Authorization.Expires > xrtNow()/1000000 && !different &&
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
    if (ok) MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock); return ok;
}
bool MdoAccountRefresh(void)
{
    if (!g_MdoAccount.Initialized) return false;
    xrtMutexLock(g_MdoAccount.Lock); MdoAccountSyncOriginLocked();
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
    if (ok) MdoAccountChangedLocked(); xrtMutexUnlock(g_MdoAccount.Lock); return ok;
}
static void MdoAccountCancelLease(void* Data)
{
    xrtCancelRequest((xcancel*)Data);
}
bool MdoAccountAcquire(cstr Endpoint, cstr Query, const xwork_tool_context* Context, MdoAccountLease* Lease)
{
    char origin[MDO_ACCOUNT_ORIGIN_LIMIT]; size_t slot = MDO_ACCOUNT_WAIT_MAX, i; bool ok = false;
    if (!Lease || !Context) return false;
    memset(Lease, 0, sizeof(*Lease));
    /* Developer integration override is never exposed through the UI or saved. */
    if (MdoSecretResolve(xrtStrView("env:MDO_SEARCH_ACCESS_TOKEN"), MDO_ACCOUNT_TOKEN_LIMIT - 1, &Lease->AccessToken)) {
        if (MdoAccountTokenValid(Lease->AccessToken)) return true;
        MdoSecretRelease(&Lease->AccessToken); return false;
    }
    if (!g_MdoAccount.Initialized || !MdoAccountOrigin(Endpoint, origin)) return false;
    uint64 until = xrtDeadlineAfter(300000000);
    xrtMutexLock(g_MdoAccount.Lock); ++g_MdoAccount.Acquirers; MdoAccountSyncOriginLocked();
    if (strcmp(origin, g_MdoAccount.Origin)) goto done;
    for (;;) {
        if (g_MdoAccount.Stopping || strcmp(origin, g_MdoAccount.Origin) ||
            (Context->pCancel && xrtCancelRequested(Context->pCancel)) || xrtDeadlineExpired(until) ||
            (Context->uDeadline != XRT_DEADLINE_NEVER && xrtDeadlineExpired(Context->uDeadline)) ||
            (slot < MDO_ACCOUNT_WAIT_MAX && g_MdoAccount.Waiters[slot].Skipped)) break;
        if (g_MdoAccount.Tokens.Access[0] && g_MdoAccount.Tokens.Expires > xrtDeadlineAfter(30000000)) {
            Lease->AccessToken = xrtMalloc(strlen(g_MdoAccount.Tokens.Access) + 1);
            Lease->Cancel = xrtCancelChild(Context->pCancel);
            if (!Lease->AccessToken || !Lease->Cancel || !g_MdoAccount.SessionCancel) break;
            strcpy(Lease->AccessToken, g_MdoAccount.Tokens.Access);
            Lease->Watch = xrtCancelWatch(g_MdoAccount.SessionCancel, MdoAccountCancelLease, Lease->Cancel);
            if (!Lease->Watch) break;
            Lease->Generation = g_MdoAccount.Epoch; Lease->Managed = true; ok = true; break;
        }
        if (g_MdoAccount.Tokens.Refresh[0]) (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH);
        if (slot == MDO_ACCOUNT_WAIT_MAX) {
            for (i = 0; i < MDO_ACCOUNT_WAIT_MAX; i++) if (!g_MdoAccount.Waiters[i].Id) { slot = i; break; }
            if (slot == MDO_ACCOUNT_WAIT_MAX) break;
            MdoAccountWaiter* waiter = &g_MdoAccount.Waiters[slot]; waiter->Id = ++g_MdoAccount.NextWaiter;
            waiter->RunId = Context->uRunId; snprintf(waiter->Query, sizeof(waiter->Query), "%s", Query ? Query : "");
            MdoAccountChangedLocked();
        }
        xrtCondWaitFor(g_MdoAccount.Changed, g_MdoAccount.Lock, 100000);
    }
done:
    if (slot < MDO_ACCOUNT_WAIT_MAX) { memset(&g_MdoAccount.Waiters[slot], 0, sizeof(g_MdoAccount.Waiters[slot])); MdoAccountChangedLocked(); }
    --g_MdoAccount.Acquirers; xrtCondBroadcast(g_MdoAccount.Changed); xrtMutexUnlock(g_MdoAccount.Lock);
    if (!ok) MdoAccountRelease(Lease); return ok;
}
void MdoAccountRelease(MdoAccountLease* Lease)
{
    if (!Lease) return;
    xrtCancelUnwatch(Lease->Watch); xrtCancelDestroy(Lease->Cancel);
    MdoSecretRelease(&Lease->AccessToken); memset(Lease, 0, sizeof(*Lease));
}
bool MdoAccountRejectAccess(const MdoAccountLease* Lease)
{
    if (!Lease || !Lease->Managed || !g_MdoAccount.Initialized) return false;
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
    if (Status == 200) (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_PROFILE);
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
