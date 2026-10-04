#include <stdio.h>
#include <string.h>

#include "../../include/mdo/remote.h"
#include "../../include/mdo/version.h"
#include "service_client.h"
#include "bridge.h"

/* Only the connection worker owns a socket. The service worker handles slow
 * HTTPS operations independently, so listing devices cannot starve heartbeats.
 * API callbacks enqueue jobs, never wait on the network. Both workers are lazy
 * and joined before ServiceUnit releases the routes and account authority. */
typedef struct MdoRemoteJob {
    MdoRemoteAction Action;
    uint64 Serial;
    char Id[33], Argument[97];
} MdoRemoteJob;

typedef struct MdoRemoteManager {
    xmutex* Lock;
    xcond* Changed;
    XS_ServerInfo* Server;
    xthread* Service;
    xthread* Connection;
    xcancel* Stop;
    xcancel* ServiceCancel;
    xcancel* ConnectionCancel;
    MdoRemoteConfig Config;
    MdoRemoteIdentity Identity;
    MdoRemoteJob Pending;
    xvalue* Devices;
    xvalue* Ticket;
    uint64 DevicesMember, TicketMember, TicketExpires;
    uint64 Serial, Epoch, Revision;
    char JobId[33];
    cstr JobState;
    cstr Stage;
    cstr Error;
    uint16 Status;
    bool Stopping, Busy, Started;
} MdoRemoteManager;

static MdoRemoteManager g_MdoRemote;
static int32 MdoRemoteServiceWorker(void* Data);
static int32 MdoRemoteConnectionWorker(void* Data);

static cstr MdoRemotePlatform(void)
{
#if defined(__ANDROID__)
    return "android";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}
static bool MdoRemoteServerLive(void)
{
    XS_ServerInfo* current = xsServerFind(g_MdoRemote.Server->Name);
    bool live = current == g_MdoRemote.Server;
    xsServerRelease(current);
    return live;
}
static void MdoRemoteChangedLocked(void)
{
    ++g_MdoRemote.Revision;
    (void)xrtCondBroadcast(g_MdoRemote.Changed);
}
static void MdoRemoteTicketClearLocked(void)
{
    MdoAccountSecretValueRelease(g_MdoRemote.Ticket);
    g_MdoRemote.Ticket = NULL;
    g_MdoRemote.TicketMember = g_MdoRemote.TicketExpires = 0u;
}
static bool MdoRemoteCurrentLocked(uint64 Serial, const MdoAccountLease* Lease)
{
    return !g_MdoRemote.Stopping && g_MdoRemote.Serial == Serial &&
        (!Lease || !xrtCancelRequested(Lease->Cancel));
}
static void MdoRemoteOffLocked(cstr Stage, cstr Error)
{
    g_MdoRemote.Config.AllowRemote = false;
    ++g_MdoRemote.Epoch;
    xrtCancelRequest(g_MdoRemote.ConnectionCancel);
    g_MdoRemote.Stage = Stage;
    g_MdoRemote.Error = Error;
    /* Failure never restores permission in memory. The snapshot makes a
     * failed portable config write visible instead of claiming persistence. */
    if (!MdoRemoteConfigSave(&g_MdoRemote.Config)) g_MdoRemote.Error = "remote_storage_failed";
    MdoRemoteChangedLocked();
}
static bool MdoRemoteStartLocked(void)
{
    if (g_MdoRemote.Started) return !g_MdoRemote.Stopping && !xrtCancelRequested(g_MdoRemote.Stop);
    if (g_MdoRemote.Stopping || !g_MdoRemote.Stop || xrtCancelRequested(g_MdoRemote.Stop)) return false;
    g_MdoRemote.Service = xrtThreadCreate(MdoRemoteServiceWorker,NULL,0u);
    if (!g_MdoRemote.Service) return false;
    g_MdoRemote.Connection = xrtThreadCreate(MdoRemoteConnectionWorker,NULL,0u);
    if (!g_MdoRemote.Connection) {
        /* Keep the partial worker owned for Unit; no retry can lose its handle. */
        g_MdoRemote.Stopping = true;
        xrtCancelRequest(g_MdoRemote.Stop);
        xrtCondBroadcast(g_MdoRemote.Changed);
        return false;
    }
    g_MdoRemote.Started = true;
    return true;
}
bool MdoRemoteInit(XS_ServerInfo* Server)
{
    if (g_MdoRemote.Lock) return true;
    if (!Server || !Server->Engine) return false;
    memset(&g_MdoRemote,0,sizeof(g_MdoRemote));
    g_MdoRemote.Lock = xrtMutexCreate();
    g_MdoRemote.Changed = xrtCondCreate();
    g_MdoRemote.Stop = xrtCancelCreate();
    g_MdoRemote.Server = xsServerRetain(Server);
    if (!g_MdoRemote.Lock || !g_MdoRemote.Changed || !g_MdoRemote.Stop || !g_MdoRemote.Server) {
        MdoRemoteUnit(); return false;
    }
    bool valid = MdoRemoteConfigLoad(&g_MdoRemote.Config);
    g_MdoRemote.Stage = g_MdoRemote.Config.AllowRemote ? "connecting" : "disabled";
    g_MdoRemote.JobState = "idle";
    g_MdoRemote.Error = valid ? "" : "remote_config_invalid";
    g_MdoRemote.Epoch = g_MdoRemote.Revision = 1u;
    /* Missing/off config allocates no threads, resolver, socket or files. */
    if (g_MdoRemote.Config.AllowRemote) {
        xrtMutexLock(g_MdoRemote.Lock);
        bool ok = MdoRemoteStartLocked();
        xrtMutexUnlock(g_MdoRemote.Lock);
        if (!ok) { MdoRemoteUnit(); return false; }
    }
    return true;
}
void MdoRemoteUnit(void)
{
    if (g_MdoRemote.Lock) {
        xrtMutexLock(g_MdoRemote.Lock);
        g_MdoRemote.Stopping = true;
        xrtCancelRequest(g_MdoRemote.Stop);
        xrtCancelRequest(g_MdoRemote.ServiceCancel);
        xrtCancelRequest(g_MdoRemote.ConnectionCancel);
        if (g_MdoRemote.Changed) xrtCondBroadcast(g_MdoRemote.Changed);
        xrtMutexUnlock(g_MdoRemote.Lock);
    }
    if (g_MdoRemote.Connection) {
        xrtThreadWait(g_MdoRemote.Connection); xrtThreadDestroy(g_MdoRemote.Connection);
    }
    if (g_MdoRemote.Service) {
        xrtThreadWait(g_MdoRemote.Service); xrtThreadDestroy(g_MdoRemote.Service);
    }
    MdoRemoteTicketClearLocked();
    xrtValueRelease(g_MdoRemote.Devices);
    MdoRemoteIdentityClear(&g_MdoRemote.Identity);
    xsServerRelease(g_MdoRemote.Server);
    xrtCancelDestroy(g_MdoRemote.Stop);
    xrtCondDestroy(g_MdoRemote.Changed);
    xrtMutexDestroy(g_MdoRemote.Lock);
    xrtSecureZero(&g_MdoRemote,sizeof(g_MdoRemote));
}
bool MdoRemoteRequest(MdoRemoteAction Action, cstr Argument)
{
    if (!g_MdoRemote.Lock || Action < MDO_REMOTE_ENABLE || Action > MDO_REMOTE_VIEW ||
        (Action == MDO_REMOTE_ENABLE && !MdoRemoteNameValid(Argument)) ||
        ((Action == MDO_REMOTE_DISABLE || Action == MDO_REMOTE_LIST) && Argument && Argument[0]) ||
        (Action >= MDO_REMOTE_REVOKE && !MdoAccountHex(Argument,32u))) return false;
    char random[65];
    if (!MdoAccountRandom(random)) return false;
    xrtMutexLock(g_MdoRemote.Lock);
    bool ok = !g_MdoRemote.Stopping && (Action == MDO_REMOTE_DISABLE ||
        (!g_MdoRemote.Busy && !g_MdoRemote.Pending.Action)) && MdoRemoteStartLocked();
    if (ok) {
        MdoRemoteTicketClearLocked();
        ++g_MdoRemote.Serial;
        g_MdoRemote.Pending.Action = Action;
        g_MdoRemote.Pending.Serial = g_MdoRemote.Serial;
        memcpy(g_MdoRemote.Pending.Id,random,32u); g_MdoRemote.Pending.Id[32] = 0;
        strcpy(g_MdoRemote.Pending.Argument,Argument ? Argument : "");
        strcpy(g_MdoRemote.JobId,g_MdoRemote.Pending.Id);
        g_MdoRemote.JobState = "queued";
        g_MdoRemote.Error = ""; g_MdoRemote.Status = 0u;
        if (Action == MDO_REMOTE_DISABLE) {
            xrtCancelRequest(g_MdoRemote.ServiceCancel);
            MdoRemoteOffLocked("disabled","");
        }
        MdoRemoteChangedLocked();
    }
    xrtMutexUnlock(g_MdoRemote.Lock);
    xrtSecureZero(random,sizeof(random));
    return ok;
}
xvalue* MdoRemoteSnapshot(void)
{
    if (!g_MdoRemote.Lock) return NULL;
    MdoAccountLease account = {0};
    bool signed_in = MdoAccountAcquireService(NULL,&account);
    xrtMutexLock(g_MdoRemote.Lock);
    if (g_MdoRemote.Ticket && (xrtDeadlineExpired(g_MdoRemote.TicketExpires) ||
        !signed_in || account.MemberId != g_MdoRemote.TicketMember)) MdoRemoteTicketClearLocked();
    xvalue *value = xrtValueObject(), *job = xrtValueObject();
    bool ok = value && job && MdoAccountSetBool(value,"allow_remote",g_MdoRemote.Config.AllowRemote) &&
        MdoAccountSetString(value,"name",g_MdoRemote.Config.Name) &&
        MdoAccountSetString(value,"device_id",g_MdoRemote.Identity.Id) &&
        MdoAccountSetUInt(value,"member_id",g_MdoRemote.Config.MemberId) &&
        MdoAccountSetBool(value,"persistent",g_MdoRemote.Identity.Persistent) &&
        MdoAccountSetBool(value,"persistence_available",xsCredentialProtectionAvailable()) &&
        MdoAccountSetString(value,"stage",g_MdoRemote.Stage) &&
        MdoAccountSetString(value,"error",g_MdoRemote.Error) &&
        MdoAccountSetUInt(value,"status",g_MdoRemote.Status) &&
        MdoAccountSetUInt(value,"revision",g_MdoRemote.Revision) &&
        MdoAccountSetString(job,"id",g_MdoRemote.JobId) &&
        MdoAccountSetString(job,"state",g_MdoRemote.JobState);
    if (ok) ok = xrtValueObjectSetNew(value,XRT_STR_LITERAL("job"),job);
    else xrtValueRelease(job);
    xvalue* devices = signed_in && account.MemberId == g_MdoRemote.DevicesMember && g_MdoRemote.Devices ?
        xrtValueClone(g_MdoRemote.Devices) : xrtValueObject();
    if (ok) ok = xrtValueObjectSetNew(value,XRT_STR_LITERAL("listing"),devices);
    else xrtValueRelease(devices);
    xrtMutexUnlock(g_MdoRemote.Lock);
    MdoAccountRelease(&account);
    if (!ok) { xrtValueRelease(value); return NULL; }
    return value;
}
xvalue* MdoRemoteTicketTake(cstr JobId)
{
    if (!g_MdoRemote.Lock || !MdoAccountHex(JobId,32u)) return NULL;
    MdoAccountLease lease = {0}; xvalue* ticket = NULL;
    bool live = MdoAccountAcquireService(NULL,&lease);
    xrtMutexLock(g_MdoRemote.Lock);
    if (live && !g_MdoRemote.Stopping && !strcmp(JobId,g_MdoRemote.JobId) &&
        !strcmp(g_MdoRemote.JobState,"done") && lease.MemberId == g_MdoRemote.TicketMember &&
        !xrtCancelRequested(lease.Cancel) && !xrtDeadlineExpired(g_MdoRemote.TicketExpires)) {
        ticket = g_MdoRemote.Ticket; g_MdoRemote.Ticket = NULL;
        g_MdoRemote.TicketMember = g_MdoRemote.TicketExpires = 0u;
    }
    xrtMutexUnlock(g_MdoRemote.Lock);
    MdoAccountRelease(&lease);
    return ticket;
}

static xvalue* MdoRemoteRegisterBody(const MdoRemoteIdentity* Identity, cstr Name)
{
    xvalue* body = xrtValueObject();
    bool ok = body && MdoAccountSetString(body,"device_id",Identity->Id) &&
        MdoAccountSetString(body,"device_secret",Identity->Secret) &&
        MdoAccountSetString(body,"name",Name) && MdoAccountSetString(body,"platform",MdoRemotePlatform()) &&
        MdoAccountSetString(body,"app_version",MDO_VERSION_TEXT) &&
        MdoAccountSetBool(body,"allow_remote",true) && MdoAccountSetBool(body,"reactivate",true);
    if (!ok) { MdoAccountSecretValueRelease(body); return NULL; }
    return body;
}
static void MdoRemoteRunJob(const MdoRemoteJob* Job, xcancel* Cancel)
{
    MdoAccountLease lease = {0}; MdoRemoteIdentity identity = {0};
    uint16 status = 0u; cstr error = "remote_login_required";
    xvalue *body = NULL, *result = NULL; bool ok = MdoAccountAcquireService(Cancel,&lease);
    if (!ok) {
        /* Local permission is already off; logout cannot make disabling it
         * fail merely because there is no session left to revoke remotely. */
        if (Job->Action == MDO_REMOTE_DISABLE) { ok = true; status = 200u; }
        goto done;
    }
    error = "remote_service_unavailable";
    if (Job->Action == MDO_REMOTE_LIST) {
        result = MdoRemoteServiceCall("/api/v1/devices","GET",NULL,&lease,&status);
        ok = status == 200u && result && xrtValueType(xrtValueObjectGet(result,XRT_STR_LITERAL("devices"))) == XVALUE_ARRAY;
    } else if (Job->Action == MDO_REMOTE_ENABLE) {
        xrtMutexLock(g_MdoRemote.Lock);
        if (g_MdoRemote.Identity.MemberId == lease.MemberId) identity = g_MdoRemote.Identity;
        xrtMutexUnlock(g_MdoRemote.Lock);
        if (!identity.Id[0] && !MdoRemoteIdentityLoad(lease.MemberId,true,&identity)) {
            ok = false; error = "remote_identity_unavailable"; goto done;
        }
        body = MdoRemoteRegisterBody(&identity,Job->Argument);
        if (!body) { ok = false; goto done; }
        result = MdoRemoteServiceCall("/api/v1/devices/register","POST",body,&lease,&status);
        cstr id = MdoAccountText(result,"device_id",32u);
        ok = result && id && !strcmp(id,identity.Id) && (status == 200u || status == 201u);
    } else {
        char id[33] = "";
        if (Job->Action == MDO_REMOTE_DISABLE) {
            xrtMutexLock(g_MdoRemote.Lock);
            if (g_MdoRemote.Identity.MemberId == lease.MemberId) strcpy(id,g_MdoRemote.Identity.Id);
            xrtMutexUnlock(g_MdoRemote.Lock);
            if (!id[0]) { ok = true; status = 200u; goto done; }
        } else strcpy(id,Job->Argument);
        body = xrtValueObject();
        ok = body && MdoAccountSetString(body,"device_id",id);
        cstr path = Job->Action == MDO_REMOTE_REMOVE ? "/api/v1/devices/remove" : "/api/v1/devices/revoke";
        if (Job->Action == MDO_REMOTE_CONTROL || Job->Action == MDO_REMOTE_VIEW) {
            path = "/api/v1/devices/ticket";
            ok = ok && MdoAccountSetString(body,"role","controller") &&
                MdoAccountSetString(body,"mode",Job->Action == MDO_REMOTE_VIEW ? "view" : "control");
        }
        if (!ok) goto done;
        result = MdoRemoteServiceCall(path,"POST",body,&lease,&status);
        ok = status == 200u && (Job->Action < MDO_REMOTE_CONTROL || result);
    }
done:
    xrtMutexLock(g_MdoRemote.Lock);
    if (MdoRemoteCurrentLocked(Job->Serial,lease.Cancel ? &lease : NULL) && !xrtCancelRequested(Cancel)) {
        if (ok && Job->Action == MDO_REMOTE_ENABLE) {
            MdoRemoteConfig config = {0}; strcpy(config.Name,Job->Argument); config.MemberId = lease.MemberId;
            /* A temporary identity cannot be revived after restart. Save the
             * display name/owner with the permission off in that case. */
            config.AllowRemote = identity.Persistent;
            ok = MdoRemoteConfigSave(&config);
            if (ok) {
                config.AllowRemote = true; g_MdoRemote.Config = config;
                MdoRemoteIdentityClear(&g_MdoRemote.Identity); g_MdoRemote.Identity = identity;
                ++g_MdoRemote.Epoch; xrtCancelRequest(g_MdoRemote.ConnectionCancel);
                g_MdoRemote.Stage = "connecting";
            } else error = "remote_storage_failed";
        } else if (ok && Job->Action == MDO_REMOTE_LIST) {
            xrtValueRelease(g_MdoRemote.Devices); g_MdoRemote.Devices = result; result = NULL;
            g_MdoRemote.DevicesMember = lease.MemberId;
        } else if (ok && Job->Action >= MDO_REMOTE_CONTROL) {
            MdoRemoteTicketClearLocked(); g_MdoRemote.Ticket = result; result = NULL;
            g_MdoRemote.TicketMember = lease.MemberId;
            /* Service tickets are <=60 s; this shorter local consume window
             * prevents exposing stale credentials from an abandoned tab. */
            g_MdoRemote.TicketExpires = xrtDeadlineAfter(20000000u);
        } else if (ok && (Job->Action == MDO_REMOTE_REVOKE || Job->Action == MDO_REMOTE_REMOVE) &&
            g_MdoRemote.Identity.MemberId == lease.MemberId && !strcmp(Job->Argument,g_MdoRemote.Identity.Id)) {
            MdoRemoteOffLocked("disabled","");
        }
        g_MdoRemote.JobState = ok ? "done" : "failed";
        if (!(ok && Job->Action == MDO_REMOTE_DISABLE &&
            !strcmp(g_MdoRemote.Error,"remote_storage_failed"))) g_MdoRemote.Error = ok ? "" : error;
        g_MdoRemote.Status = status;
        MdoRemoteChangedLocked();
    } else if (g_MdoRemote.Serial == Job->Serial && !g_MdoRemote.Stopping) {
        g_MdoRemote.JobState = "failed"; g_MdoRemote.Error = "remote_account_changed";
        MdoRemoteChangedLocked();
    }
    xrtMutexUnlock(g_MdoRemote.Lock);
    MdoAccountSecretValueRelease(body); MdoAccountSecretValueRelease(result);
    MdoAccountRelease(&lease); MdoRemoteIdentityClear(&identity);
}
static int32 MdoRemoteServiceWorker(void* Data)
{
    (void)Data;
    for (;;) {
        xrtMutexLock(g_MdoRemote.Lock);
        while (!g_MdoRemote.Stopping && !xrtCancelRequested(g_MdoRemote.Stop) && !g_MdoRemote.Pending.Action)
            xrtCondWaitFor(g_MdoRemote.Changed,g_MdoRemote.Lock,250000u);
        if (g_MdoRemote.Stopping || xrtCancelRequested(g_MdoRemote.Stop)) { xrtMutexUnlock(g_MdoRemote.Lock); break; }
        MdoRemoteJob job = g_MdoRemote.Pending;
        memset(&g_MdoRemote.Pending,0,sizeof(g_MdoRemote.Pending));
        xcancel* cancel = xrtCancelChild(g_MdoRemote.Stop);
        g_MdoRemote.ServiceCancel = cancel; g_MdoRemote.Busy = true;
        g_MdoRemote.JobState = "running"; MdoRemoteChangedLocked();
        xrtMutexUnlock(g_MdoRemote.Lock);
        if (cancel) MdoRemoteRunJob(&job,cancel);
        else {
            xrtMutexLock(g_MdoRemote.Lock);
            if (g_MdoRemote.Serial == job.Serial) {
                g_MdoRemote.JobState = "failed"; g_MdoRemote.Error = "remote_resources_unavailable";
            }
            xrtMutexUnlock(g_MdoRemote.Lock);
        }
        xrtMutexLock(g_MdoRemote.Lock);
        g_MdoRemote.ServiceCancel = NULL; g_MdoRemote.Busy = false;
        MdoRemoteChangedLocked(); xrtMutexUnlock(g_MdoRemote.Lock);
        xrtCancelDestroy(cancel); xrtSecureZero(&job,sizeof(job));
    }
    return 0;
}

typedef struct MdoRemoteConnectionContext {
    MdoRemoteSocket* Socket;
    MdoRemoteBridge* Bridge;
    xcancel* Cancel;
    uint64 Epoch;
    char Id[33];
} MdoRemoteConnectionContext;
static bool MdoRemoteConnectionMessage(bool Binary, xbytesview Message, void* Data)
{
    MdoRemoteConnectionContext* context = Data;
    if (Binary) {
        const uint8* bytes = Message.Data; char peer[33]; static const char hex[] = "0123456789abcdef";
        if (Message.Size < MDO_REMOTE_RELAY_HEADER || memcmp(bytes,"MDR1",4u) || (bytes[20] != 1u && bytes[20] != 2u)) return false;
        for (size_t i = 0u; i < 16u; i++) { peer[i*2u] = hex[bytes[4u+i] >> 4u]; peer[i*2u+1u] = hex[bytes[4u+i] & 15u]; }
        peer[32] = 0;
        return MdoRemoteBridgeInput(context->Bridge,peer,bytes[20] == 2u,
            (xbytesview){bytes+MDO_REMOTE_RELAY_HEADER,Message.Size-MDO_REMOTE_RELAY_HEADER});
    }
    xjsonreadconfig limits; xrtJsonReadConfigInit(&limits);
    limits.MaxInputBytes = 1024u; limits.MaxValues = 16u; limits.MaxDepth = 2u; limits.MaxStringBytes = 96u;
    xvalue* notice = xrtJsonRead(xrtStrViewN((cstr)Message.Data,Message.Size),&limits);
    cstr type = MdoAccountText(notice,"type",32u), id = MdoAccountText(notice,"device_id",32u);
    cstr mode = MdoAccountText(notice,"mode",16u), peer = MdoAccountText(notice,"peer_id",32u);
    uint64 version = 0u, payload = 0u;
    bool native_ready = type && !strcmp(type,"ready");
    bool ok = notice && type && id && !strcmp(id,context->Id) && peer &&
        (native_ready ? !peer[0] : MdoAccountHex(peer,32u)) && mode &&
        (!strcmp(mode,"control") || !strcmp(mode,"view")) &&
        MdoAccountGetUInt(xrtValueObjectGet(notice,XRT_STR_LITERAL("version")),&version) && version == 1u &&
        MdoAccountGetUInt(xrtValueObjectGet(notice,XRT_STR_LITERAL("payload_limit")),&payload) && payload == MDO_REMOTE_RELAY_PAYLOAD &&
        (!strcmp(type,"ready") || !strcmp(type,"peer_open") || !strcmp(type,"peer_close"));
    if (ok && !strcmp(type,"ready")) {
        xrtMutexLock(g_MdoRemote.Lock);
        if (g_MdoRemote.Epoch == context->Epoch && g_MdoRemote.Config.AllowRemote && !g_MdoRemote.Stopping) {
            g_MdoRemote.Stage = "online"; g_MdoRemote.Error = ""; MdoRemoteChangedLocked();
        }
        xrtMutexUnlock(g_MdoRemote.Lock);
    } else if (ok && !strcmp(type,"peer_open")) {
        ok = MdoRemoteBridgePeerOpen(context->Bridge,peer,!strcmp(mode,"view"),context->Cancel);
    } else if (ok && !strcmp(type,"peer_close")) {
        MdoRemoteBridgePeerClose(context->Bridge,peer);
    }
    xrtValueRelease(notice); return ok;
}
static bool MdoRemoteConnectionEmit(xbytesview Envelope, void* Data)
{ return MdoRemoteSocketSend((MdoRemoteSocket*)Data,true,Envelope); }
static int32 MdoRemoteConnectionWorker(void* Data)
{
    (void)Data;
    MdoRemoteNet net = {0}; MdoRemoteBridge* bridge = NULL; unsigned retry = 0u;
    char bridge_device[33] = ""; uint64 bridge_member = 0u;
    for (;;) {
        xrtMutexLock(g_MdoRemote.Lock);
        while (!g_MdoRemote.Stopping && !xrtCancelRequested(g_MdoRemote.Stop) && !g_MdoRemote.Config.AllowRemote) {
            xrtCondWaitFor(g_MdoRemote.Changed,g_MdoRemote.Lock,250000u);
            xrtMutexUnlock(g_MdoRemote.Lock);
            if (bridge) (void)MdoRemoteBridgePump(bridge,NULL,NULL);
            xrtMutexLock(g_MdoRemote.Lock);
            if (!MdoRemoteServerLive()) xrtCancelRequest(g_MdoRemote.Stop);
        }
        if (g_MdoRemote.Stopping || xrtCancelRequested(g_MdoRemote.Stop)) { xrtMutexUnlock(g_MdoRemote.Lock); break; }
        uint64 epoch = g_MdoRemote.Epoch;
        MdoRemoteConfig config = g_MdoRemote.Config;
        MdoRemoteIdentity identity = g_MdoRemote.Identity;
        xcancel* cancel = xrtCancelChild(g_MdoRemote.Stop);
        g_MdoRemote.ConnectionCancel = cancel;
        xrtMutexUnlock(g_MdoRemote.Lock);
        MdoAccountLease lease = {0}; MdoRemoteSocket* socket = NULL;
        xvalue *body = NULL, *ticket = NULL; uint16 status = 0u, close_code = 0u;
        bool available = cancel && MdoAccountAcquireService(cancel,&lease);
        cstr off = NULL;
        if (!available) { if (!MdoAccountHasSession()) off = "remote_login_required"; goto closed; }
        if (lease.MemberId != config.MemberId) { off = "remote_account_changed"; goto closed; }
        if (!identity.Id[0] && !MdoRemoteIdentityLoad(config.MemberId,false,&identity)) {
            off = "remote_identity_unavailable"; goto closed;
        }
        xrtMutexLock(g_MdoRemote.Lock);
        if (g_MdoRemote.Epoch == epoch) g_MdoRemote.Identity = identity;
        xrtMutexUnlock(g_MdoRemote.Lock);
        if (!MdoRemoteServerLive()) { xrtCancelRequest(g_MdoRemote.Stop); goto closed; }
        if (!net.Engine && !MdoRemoteNetInit(&net,g_MdoRemote.Server->Engine,NULL)) goto closed;
        body = xrtValueObject();
        if (!body || !MdoAccountSetString(body,"device_id",identity.Id) ||
            !MdoAccountSetString(body,"device_secret",identity.Secret) || !MdoAccountSetString(body,"role","device")) goto closed;
        /* Reconnect asks only for a ticket. Registration/reactivation exists
         * exclusively in the explicit enable job above. */
        ticket = MdoRemoteServiceCall("/api/v1/devices/ticket","POST",body,&lease,&status);
        MdoAccountSecretValueRelease(body); body = NULL;
        if (status == 403u || status == 404u) { off = "remote_revoked"; goto closed; }
        if (status == 401u) { (void)MdoAccountRejectAccess(&lease); goto closed; }
        /* Receipts survive transport reconnects, but never migrate between
         * account/device identities. A new binding gets a new runtime ID. */
        if (bridge && (bridge_member != lease.MemberId || strcmp(bridge_device,identity.Id))) {
            MdoRemoteBridgeDestroy(bridge); bridge = NULL;
        }
        if (!bridge) {
            bridge = MdoRemoteBridgeCreate(g_MdoRemote.Server);
            if (bridge) { bridge_member = lease.MemberId; strcpy(bridge_device,identity.Id); }
        }
        if (!bridge) goto closed;
        socket = MdoRemoteServiceConnect(&net,ticket,&lease,&status);
        MdoAccountSecretValueRelease(ticket); ticket = NULL;
        if (!socket) goto closed;
        MdoRemoteConnectionContext context = {socket,bridge,lease.Cancel,epoch,""}; strcpy(context.Id,identity.Id);
        while (!xrtCancelRequested(lease.Cancel) && MdoRemoteServerLive() &&
            MdoRemoteSocketPoll(socket,MdoRemoteConnectionMessage,&context) &&
            MdoRemoteBridgePump(bridge,MdoRemoteConnectionEmit,socket)) {
            retry = 0u; xrtSleep(5u);
        }
        close_code = MdoRemoteSocketCloseCode(socket);
        if (close_code == 1008u && MdoRemoteSocketPeerClosed(socket)) off = "remote_revoked";
closed:
        MdoRemoteBridgeDisconnect(bridge);
        if (bridge) (void)MdoRemoteBridgePump(bridge,NULL,NULL);
        xrtMutexLock(g_MdoRemote.Lock);
        bool current = !g_MdoRemote.Stopping && g_MdoRemote.Epoch == epoch && g_MdoRemote.Config.AllowRemote;
        /* Parent cancel belongs to disable/reconfigure/shutdown. A cancelled
         * lease with that parent still live belongs to logout/account switch. */
        if (current && lease.Cancel && xrtCancelRequested(lease.Cancel) && !xrtCancelRequested(cancel)) off = "remote_account_changed";
        if (current && off) MdoRemoteOffLocked(!strcmp(off,"remote_revoked") ? "revoked" : "disabled",off);
        else if (current) {
            g_MdoRemote.Stage = "reconnecting"; g_MdoRemote.Error = "remote_connection_unavailable";
            g_MdoRemote.Status = status; MdoRemoteChangedLocked();
        }
        g_MdoRemote.ConnectionCancel = NULL;
        xrtMutexUnlock(g_MdoRemote.Lock);
        MdoRemoteSocketDestroy(socket);
        MdoAccountSecretValueRelease(body); MdoAccountSecretValueRelease(ticket);
        MdoAccountRelease(&lease); MdoRemoteIdentityClear(&identity); xrtCancelDestroy(cancel);
        if (xrtCancelRequested(g_MdoRemote.Stop)) break;
        if (retry < 6u) ++retry;
        uint64 delay = (uint64)(1u << retry)*250000u;
        if (delay > 15000000u) delay = 15000000u;
        xrtMutexLock(g_MdoRemote.Lock);
        if (g_MdoRemote.Epoch == epoch && g_MdoRemote.Config.AllowRemote && !g_MdoRemote.Stopping)
            xrtCondWaitFor(g_MdoRemote.Changed,g_MdoRemote.Lock,delay);
        xrtMutexUnlock(g_MdoRemote.Lock);
    }
    MdoRemoteBridgeDestroy(bridge);
    MdoRemoteNetUnit(&net);
    return 0;
}
