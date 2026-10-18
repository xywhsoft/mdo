"""Execute the actual lease functions with native clocks, threads and cancel refs.

The surrounding account HTTP worker is a controlled state fixture; the lease,
wait, release and session-watch functions are extracted from production source.
Real website integration is covered by test_remote_service_lease.py.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def function(source,name):
    match=re.search(r'^[^\n;{}]*\b'+name+r'\([^;{}]*\)\s*\{',source,re.M)
    assert match,name
    opening=source.index('{',match.start()); depth=1; end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[match.start():end]


BODY=r'''
#include <stdio.h>
static struct {
    xmutex* Lock; xcond* Changed; xcancel* SessionCancel;
    bool Initialized,Stopping;
    uint64 Epoch; size_t Acquirers;
    struct { char Access[64],Refresh[64]; uint64 Expires,MemberId; } Tokens;
} g_MdoAccount;
static bool Queued,Result; static uint64 Elapsed; static xcancel* Caller;
enum { MDO_ACCOUNT_WORK_REFRESH=1 };
static bool MdoAccountQueueLocked(unsigned Kind) { Queued=Kind==MDO_ACCOUNT_WORK_REFRESH; return Queued; }
static void MdoSecretRelease(char** Value) {
    if (*Value) { xrtSecureZero(*Value,strlen(*Value)+1); xrtFree(*Value); *Value=NULL; }
}
@FUNCTIONS@
#define CHECK(c) do { if (!(c)) { printf("lease_fail=%u:%s\n",__LINE__,#c); fflush(stdout); xsAppRequestStop(); return; } } while (0)
static void reset(bool Fresh) {
    xrtMutexLock(g_MdoAccount.Lock); Queued=false; g_MdoAccount.Stopping=false; ++g_MdoAccount.Epoch;
    g_MdoAccount.Tokens.MemberId=1; strcpy(g_MdoAccount.Tokens.Access,"unit-access");
    strcpy(g_MdoAccount.Tokens.Refresh,"unit-refresh");
    g_MdoAccount.Tokens.Expires=Fresh ? MdoDeadlineAfterUs(60000000):0;
    xrtMutexUnlock(g_MdoAccount.Lock);
}
static int32 worker(void* Data) {
    (void)Data; MdoAccountLease lease={0}; uint64 start=MdoMonotonicUs();
    Result=MdoAccountAcquireServiceWait(Caller,1000,&lease); Elapsed=MdoMonotonicUs()-start;
    if (!Result && (lease.AccessToken || lease.Cancel || lease.Watch || lease.Managed || lease.MemberId || lease.Generation))
        printf("partial_lease=1\n");
    MdoAccountRelease(&lease); return 0;
}
static bool waiting(void) {
    uint64 until=MdoDeadlineAfterUs(1000000);
    while (!MdoDeadlineExpired(until)) {
        xrtMutexLock(g_MdoAccount.Lock); bool ready=g_MdoAccount.Acquirers==1 && Queued;
        xrtMutexUnlock(g_MdoAccount.Lock); if (ready) return true; xrtSleep(1);
    }
    return false;
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host; xmutex lock; xcond changed; MdoAccountLease lease={0}; xthread* thread;
    CHECK(xrtMutexInit(&lock) && xrtCondInit(&changed));
    g_MdoAccount.Lock=&lock; g_MdoAccount.Changed=&changed; g_MdoAccount.Initialized=true;
    g_MdoAccount.SessionCancel=xrtCancelCreate(); CHECK(g_MdoAccount.SessionCancel);
    reset(true); CHECK(!MdoAccountAcquireServiceWait(NULL,0,&lease));
    CHECK(!MdoAccountAcquireServiceWait(NULL,30001,&lease));
    CHECK(MdoAccountAcquireServiceWait(NULL,100,&lease) && lease.MemberId==1);
    MdoAccountRelease(&lease); CHECK(g_MdoAccount.Acquirers==0);
    reset(false); Result=false; Caller=NULL; thread=xrtThreadCreate(worker,NULL,0); CHECK(thread && waiting());
    xrtMutexLock(&lock); g_MdoAccount.Tokens.Expires=MdoDeadlineAfterUs(60000000); xrtCondBroadcast(&changed); xrtMutexUnlock(&lock);
    xrtThreadWait(thread); xrtThreadDestroy(thread); CHECK(Result && g_MdoAccount.Acquirers==0);
    reset(false); uint64 start=MdoMonotonicUs(); CHECK(!MdoAccountAcquireServiceWait(NULL,40,&lease));
    CHECK(MdoMonotonicUs()-start>=20000 && MdoMonotonicUs()-start<500000 && g_MdoAccount.Acquirers==0);
    reset(false); Caller=xrtCancelCreate(); thread=xrtThreadCreate(worker,NULL,0); CHECK(Caller && thread && waiting());
    xrtCancelRequest(Caller); xrtThreadWait(thread); xrtThreadDestroy(thread);
    CHECK(!Result && Elapsed<500000 && g_MdoAccount.Acquirers==0); xrtCancelDestroy(Caller); Caller=NULL;
    reset(false); thread=xrtThreadCreate(worker,NULL,0); CHECK(thread && waiting());
    xrtMutexLock(&lock); ++g_MdoAccount.Epoch; g_MdoAccount.Tokens.Expires=MdoDeadlineAfterUs(60000000);
    xrtCondBroadcast(&changed); xrtMutexUnlock(&lock);
    xrtThreadWait(thread); xrtThreadDestroy(thread); CHECK(!Result && Elapsed<500000 && g_MdoAccount.Acquirers==0);
    reset(false); thread=xrtThreadCreate(worker,NULL,0); CHECK(thread && waiting());
    xrtMutexLock(&lock); xcancel* old=g_MdoAccount.SessionCancel; g_MdoAccount.SessionCancel=xrtCancelCreate();
    xrtCancelRequest(old); xrtCancelDestroy(old); g_MdoAccount.Tokens.Expires=MdoDeadlineAfterUs(60000000);
    xrtCondBroadcast(&changed); xrtMutexUnlock(&lock);
    xrtThreadWait(thread); xrtThreadDestroy(thread); CHECK(!Result && Elapsed<500000 && g_MdoAccount.Acquirers==0);
    reset(false); thread=xrtThreadCreate(worker,NULL,0); CHECK(thread && waiting());
    xrtMutexLock(&lock); g_MdoAccount.Stopping=true; xrtCondBroadcast(&changed); xrtMutexUnlock(&lock);
    xrtThreadWait(thread); xrtThreadDestroy(thread); CHECK(!Result && Elapsed<500000 && g_MdoAccount.Acquirers==0);
    g_MdoAccount.Initialized=false; xrtCancelDestroy(g_MdoAccount.SessionCancel);
    CHECK(xrtCondDestroy(&changed) && xrtMutexDestroy(&lock));
    puts("service_lease_native_ok=1"); fflush(stdout); xsAppRequestStop();
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True,type=Path)
    args=parser.parse_args()
    session=(ROOT/'app/src/account/session.c').read_text(encoding='utf-8')
    names=('MdoAccountCancelLease','MdoAccountLeaseLocked','MdoAccountAcquireServiceWait','MdoAccountRelease')
    functions='\n'.join(function(session,n) for n in names)
    source=(ROOT/'app/include/mdo/time.h').read_text(encoding='utf-8')+'\n'+(ROOT/'app/include/mdo/account.h').read_text(encoding='utf-8')+'\n'
    source+=BODY.replace('@FUNCTIONS@',functions)
    with tempfile.TemporaryDirectory(prefix='service-lease-native-',dir=ROOT/'.build') as directory:
        site=Path(directory); (site/'web').mkdir(); (site/'main.c').write_text(source,encoding='utf-8')
        with socket.socket() as sock: sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
        (site/'xs.json').write_text(json.dumps({'services':[{'name':'lease-native','class':'http','ip':'127.0.0.1','port':port,'host_default':{'name':'lease-native','path':'web','devlang':'c','devfile':'main.c'}}]}))
        result=subprocess.run([str(args.host.resolve()),str(site/'xs.json')],cwd=site,capture_output=True,text=True,errors='replace',timeout=15)
        output=result.stdout+result.stderr
        assert result.returncode==0 and 'service_lease_native_ok=1' in output and 'partial_lease' not in output,output
    print('PASS actual service-lease functions: refresh wake, real timeout/cancel, epoch/session replacement/Unit fences and reference drain; source_sha256='+hashlib.sha256(functions.encode()).hexdigest())
    return 0


if __name__=='__main__': raise SystemExit(main())
