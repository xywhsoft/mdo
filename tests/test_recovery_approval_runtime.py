"""Unbounded default approval waits, explicit deadlines and cancellation."""
import argparse
from pathlib import Path
import tempfile
from test_model_runtime import ROOT, run_probe, write_site
from runtime_sources import copy_app_source

PROBE = r'''
#include <stdio.h>
#include <xsbase.h>
#include "model-plugin-free.h"
#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
#include "src/approvals/manager.c"
#include "src/asks/manager.c"
static xwork_permission_request Request;
static xwork_permission_decision Decision;
static int32 Await(void* D){(void)D;Decision=MdoApprovalOnPermission(NULL,&Request);return 0;}
static bool Pending(MdoApprovalInfo* Info){
 uint64 Until=MdoDeadlineAfterUs(1000000u);xwork_error E;
 while(!MdoDeadlineExpired(Until)){
  MdoApprovalSnapshot* S=MdoApprovalSnapshotCreate(&E);
  bool Ok=S&&MdoApprovalSnapshotAt(S,0,Info);MdoApprovalSnapshotRelease(S);
  if(Ok)return true;xrtSleep(2);
 }return false;
}
void ServiceInit(XS_HostInfo* H){
 (void)H;bool Ok=MdoHomeInit()&&MdoConfigInit()&&MdoApprovalManagerInit();
 Ok=Ok&&MDO_ASK_TIMEOUT_US==UINT64_C(14400000000);
 memset(&Request,0,sizeof(Request));Request.uRequestId=71;Request.sToolName="write";
 Request.sToolCallId="wait";Request.sArgumentsJson="{}";Request.sWorkspaceRoot=".";
 Request.uEffects=XWORK_TOOL_EFFECT_WORKSPACE_WRITE;
 xcancel* Cancel=xrtCancelCreate();Request.pCancel=Cancel;
 xthread* T=xrtThreadCreate(Await,NULL,0);MdoApprovalInfo Info={0};Info.Size=sizeof(Info);
 Ok=Ok&&T&&Pending(&Info)&&Info.ExpiresAt==MDO_DEADLINE_NEVER;
 xrtCancelRequest(Cancel);if(T){xrtThreadWait(T);xrtThreadDestroy(T);}xrtCancelDestroy(Cancel);
 Ok=Ok&&Decision==XWORK_PERMISSION_DENY;
 Request.pCancel=NULL;Request.uRequestId=72;Request.uDeadline=MdoDeadlineAfterUs(80000);
 uint64 Start=MdoMonotonicUs();T=xrtThreadCreate(Await,NULL,0);
 memset(&Info,0,sizeof(Info));Info.Size=sizeof(Info);
 Ok=Ok&&T&&Pending(&Info)&&Info.ExpiresAt==Request.uDeadline;
 if(T){xrtThreadWait(T);xrtThreadDestroy(T);}
 Ok=Ok&&Decision==XWORK_PERMISSION_DENY&&MdoMonotonicUs()-Start<1000000u;
 MdoApprovalManagerUnit();MdoConfigUnit();MdoHomeUnit();
 printf("approval_recovery_passed=%d\nprobe_done=1\n",Ok);
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='approval-recovery-',dir=ROOT/'.build') as raw:
        base=Path(raw);site=base/'site';write_site(site)
        for source in ('src/approvals/manager.c','src/asks/manager.c'):
            copy_app_source(source,site)
        (site/'probe.c').write_text(PROBE,encoding='utf-8')
        output=run_probe(args.host.resolve(),site,base/'home')
        assert 'approval_recovery_passed=1' in output,output
        print('PASS default permission wait, caller deadline, cancellation and four-hour ask timeout')

if __name__=='__main__':main()
