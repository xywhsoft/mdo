"""Real native service leases: refresh, deadline, cancellation and authority fences."""
import argparse
from pathlib import Path
import time

from test_remote_manager import run
from remote_website_fixture import WEBSITE_HOST


HOOK=r'''
static xatomic32 ServiceProbeGate;
static xthread* ServiceProbeThread;
static xcancel* ServiceProbeCancel;
static uint32 ServiceProbeTimeout;
static bool ServiceProbeTaken,ServiceProbeEmpty;
static uint64 ServiceProbeElapsed,ServiceProbeMember;
static int32 ServiceProbeWork(void* Data) {
    (void)Data; MdoAccountLease lease={0}; uint64 start=MdoMonotonicUs();
    ServiceProbeTaken=MdoAccountAcquireServiceWait(ServiceProbeCancel,ServiceProbeTimeout,&lease);
    ServiceProbeElapsed=MdoMonotonicUs()-start; ServiceProbeMember=lease.MemberId;
    ServiceProbeEmpty=!lease.AccessToken && !lease.Cancel && !lease.Watch &&
        !lease.Managed && !lease.Generation && !lease.MemberId;
    MdoAccountRelease(&lease); return 0;
}
static bool ServiceProbeAction(MdoApiContext* C,cstr Name) {
    size_t size=strlen(Name); return C->Target.Query.Size==size && !memcmp(C->Target.Query.Data,Name,size);
}
static bool ServiceProbeRoute(MdoApiContext* C) {
    if (xrtAtomic32Exchange(&ServiceProbeGate,1,XMEMORY_ACQ_REL))
        return MdoApiReplyError(C,409,"probe_busy","Probe already in progress",NULL);
    xvalue* out=xrtValueObject(); bool ok=out!=NULL;
    if (ServiceProbeAction(C,"cancel")) xrtCancelRequest(ServiceProbeCancel);
    else if (ServiceProbeAction(C,"epoch"))
        ok=MdoAccountLoginPassword("lease_epoch_probe_missing","not-a-password",false);
    else if (ServiceProbeAction(C,"refresh")) ok=MdoAccountRefresh();
    else if (ServiceProbeAction(C,"invalid")) {
        MdoAccountLease lease={0};
        ok=!MdoAccountAcquireServiceWait(NULL,0,&lease) &&
            !MdoAccountAcquireServiceWait(NULL,30001,&lease) && !lease.AccessToken && !lease.Cancel && !lease.Watch;
        MdoAccountRelease(&lease);
        MdoAccountSetBool(out,"invalid_rejected",ok);
    } else if (!ServiceProbeAction(C,"poll")) {
        if (ServiceProbeThread) ok=false;
        else {
            if (!ServiceProbeAction(C,"fresh")) {
                xrtMutexLock(g_MdoAccount.Lock); g_MdoAccount.Tokens.Expires=0; xrtMutexUnlock(g_MdoAccount.Lock);
            }
            ServiceProbeTimeout=ServiceProbeAction(C,"short") ? 40:ServiceProbeAction(C,"wait") ? 5000:2000;
            ServiceProbeTaken=false; ServiceProbeEmpty=true; ServiceProbeElapsed=ServiceProbeMember=0;
            ServiceProbeCancel=xrtCancelCreate();
            ServiceProbeThread=ServiceProbeCancel ? xrtThreadCreate(ServiceProbeWork,NULL,0):NULL;
            ok=ServiceProbeThread!=NULL; MdoAccountSetBool(out,"started",ok);
        }
    }
    bool done=!ServiceProbeThread || xrtThreadState(ServiceProbeThread)==XTHREAD_FINISHED;
    if (done && ServiceProbeThread) {
        xrtThreadWait(ServiceProbeThread); xrtThreadDestroy(ServiceProbeThread); ServiceProbeThread=NULL;
        xrtCancelDestroy(ServiceProbeCancel); ServiceProbeCancel=NULL;
    }
    MdoAccountSetBool(out,"done",done); MdoAccountSetBool(out,"taken",done && ServiceProbeTaken);
    MdoAccountSetBool(out,"empty",done && ServiceProbeEmpty);
    MdoAccountSetUInt(out,"elapsed_us",done ? ServiceProbeElapsed:0);
    MdoAccountSetUInt(out,"member_id",done ? ServiceProbeMember:0);
    xrtMutexLock(g_MdoAccount.Lock); unsigned waiters=0;
    for (size_t i=0;i<MDO_ACCOUNT_WAIT_MAX;i++) if (g_MdoAccount.Waiters[i].Id) ++waiters;
    MdoAccountSetUInt(out,"waiters",waiters); MdoAccountSetUInt(out,"acquirers",g_MdoAccount.Acquirers);
    MdoAccountSetBool(out,"busy",g_MdoAccount.Busy);
    MdoAccountSetBool(out,"pending_work",g_MdoAccount.Work.Kind!=0);
    MdoAccountSetBool(out,"refresh_available",g_MdoAccount.Tokens.Refresh[0]!=0);
    xrtMutexUnlock(g_MdoAccount.Lock);
    xrtAtomic32Store(&ServiceProbeGate,0,XMEMORY_RELEASE);
    if (!ok) { xrtValueRelease(out); return MdoApiReplyError(C,500,"probe_failed","Probe failed",NULL); }
    return MdoApiReplySuccessTake(C,200,out,NULL);
}'''
ROUTE=' {"/api/v1/test-service-lease",XHTTP_METHOD_GET,"GET",ServiceProbeRoute,false},'


def setup(site,home):
    client=site/'src/account/client.c'
    needle='    return MdoAccountJsonClient(Origin,Path,Method,Body,Access,Cancel,Status,false);'
    source=client.read_text(encoding='utf-8'); assert source.count(needle)==1
    client.write_text(source.replace(needle,
        '    if (!strcmp(Path,"/api/v1/token/refresh")) xrtSleep(1000);\n'+needle),encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True,type=Path)
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    args=parser.parse_args(); logout_armed=False
    def probe(app,action): return app(path='/test-service-lease?'+action)
    def completed(app):
        end=time.monotonic()+4
        while True:
            result=probe(app,'poll')
            assert result['waiters']==0,'service acquisition created an interactive login prompt'
            if result['done']:
                assert result['acquirers']==0
                return result
            assert time.monotonic()<end,'service acquisition did not terminate'
            time.sleep(.025)
    def signed_in(app):
        end=time.monotonic()+5
        while app(path='/account')['state']!='signed_in':
            assert time.monotonic()<end,'native refresh did not recover'; time.sleep(.025)
    def exercise(**values):
        app=values['app']
        assert probe(app,'invalid')['invalid_rejected']
        assert probe(app,'fresh')['started']; result=completed(app)
        assert result['taken'] and result['member_id'] and result['elapsed_us']<1000000
        assert probe(app,'expire')['started']; result=completed(app)
        assert result['taken'] and 700000<=result['elapsed_us']<2500000
        assert probe(app,'short')['started']; result=completed(app)
        assert not result['taken'] and result['empty'] and 20000<=result['elapsed_us']<500000,result
        assert result['refresh_available']; signed_in(app)
        assert probe(app,'wait')['started']; time.sleep(.04); probe(app,'cancel'); result=completed(app)
        assert not result['taken'] and result['empty'] and result['elapsed_us']<750000,result
        signed_in(app)
        assert probe(app,'wait')['started']; time.sleep(.04); probe(app,'epoch'); result=completed(app)
        assert not result['taken'] and result['empty'] and result['elapsed_us']<750000,result
        end=time.monotonic()+3
        while True:
            status=probe(app,'poll')
            if not status['busy'] and not status['pending_work']: break
            assert time.monotonic()<end; time.sleep(.025)
        probe(app,'refresh'); signed_in(app)
        print('PASS native service lease: fresh, delayed refresh, bounded timeout, cancellation, epoch fence, no interactive waiters',flush=True)
        return values['client']
    def before_logout(app):
        nonlocal logout_armed
        assert probe(app,'wait')['started']; time.sleep(.04); logout_armed=True
    def observe(stage,app):
        if stage=='disabled' and logout_armed:
            result=completed(app)
            assert not result['taken'] and result['empty'] and not result['refresh_available']
            assert result['elapsed_us']<750000
            print('PASS logout interrupts pending service lease and clears acquisition references',flush=True)
    run(args.host.resolve(),args.website_host.resolve(),exercise,native_hook=HOOK,native_routes=ROUTE,
        site_setup=setup,observe=observe,before_logout=before_logout)
    return 0


if __name__=='__main__': raise SystemExit(main())
