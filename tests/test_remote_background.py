"""Real relay lifecycle with a deterministic CPU-lease platform adapter."""
import argparse
from pathlib import Path
import time
from test_remote_manager import run, ROOT, XADMIN
from remote_website_fixture import WEBSITE_HOST

FAKE = r'''
#include <xsbase.h>
static xatomic32 TestCpuActive, TestCpuAcquired, TestCpuReleased, TestCpuRenewed;
static uint64 TestCpuAcquire(uint32 ms) {
    if (ms != 60000u) return 0u;
    uint32 token = xrtAtomic32FetchAdd(&TestCpuAcquired,1u,XMEMORY_ACQ_REL)+1u;
    xrtAtomic32Store(&TestCpuActive,token,XMEMORY_RELEASE); return token;
}
static bool TestCpuRenew(uint64 token, uint32 ms) {
    if (!token || ms != 60000u || token != xrtAtomic32Load(&TestCpuActive,XMEMORY_ACQUIRE)) return false;
    xrtAtomic32FetchAdd(&TestCpuRenewed,1u,XMEMORY_ACQ_REL); return true;
}
static void TestCpuRelease(uint64 token) {
    if (token && token == xrtAtomic32Load(&TestCpuActive,XMEMORY_ACQUIRE)) {
        xrtAtomic32Store(&TestCpuActive,0u,XMEMORY_RELEASE);
        xrtAtomic32FetchAdd(&TestCpuReleased,1u,XMEMORY_ACQ_REL);
    }
}
#define xsBackgroundLeaseAcquire TestCpuAcquire
#define xsBackgroundLeaseRenew TestCpuRenew
#define xsBackgroundLeaseRelease TestCpuRelease
'''
HOOK = r'''
static bool TestCpuState(MdoApiContext* c) {
    xvalue* value = xrtValueObject();
    MdoAccountSetUInt(value,"active",xrtAtomic32Load(&TestCpuActive,XMEMORY_ACQUIRE));
    MdoAccountSetUInt(value,"acquired",xrtAtomic32Load(&TestCpuAcquired,XMEMORY_ACQUIRE));
    MdoAccountSetUInt(value,"released",xrtAtomic32Load(&TestCpuReleased,XMEMORY_ACQUIRE));
    MdoAccountSetUInt(value,"renewed",xrtAtomic32Load(&TestCpuRenewed,XMEMORY_ACQUIRE));
    return MdoApiReplySuccessTake(c,200u,value,NULL);
}
'''

def setup(site, home):
    source = site/'generated/mdo_unity.c'
    source.write_text(FAKE+source.read_text(encoding='utf-8'),encoding='utf-8')

last_release = 0
def observe(stage, app):
    global last_release
    end = time.monotonic()+13
    while True:
        state = app(path='/test-background-state')
        if stage == 'disabled' and not state['active']: break
        if stage == 'online' and state['active'] and state['renewed'] > 0: break
        if stage == 'reconnect' and state['active']: break
        if stage == 'unit' and state['released'] > last_release: break
        if time.monotonic() >= end: raise AssertionError((stage,state))
        time.sleep(.1)
    last_release = state['released']

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    args=parser.parse_args()
    run(args.host.resolve(),args.website_host.resolve(),native_hook=HOOK,
        native_routes=' {"/api/v1/test-background-state",XHTTP_METHOD_GET,"GET",TestCpuState,false},',
        site_setup=setup,observe=observe)
    print('PASS CPU lease default-off, renewable while available, Unit, disable, account switch and logout release')
