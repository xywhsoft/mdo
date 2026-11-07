"""Configurable model recovery and retained waiting, without external requests."""
import argparse
from pathlib import Path
import tempfile
from test_model_runtime import ROOT, run_probe, write_site

PROBE=r'''
#include <stdio.h>
#include <xsbase.h>
#include "model-plugin-free.h"
#define MDO_MODEL_RETRY_BASE_MS 1u
#define MDO_MODEL_RETRY_MAX_MS 2u
#define MDO_RECOVERY_WAIT_MS 4u
#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
static unsigned Calls,Waiting;
static xcancel* Cancel;
static bool StopWaiting;
static xllm_result Attempt(void* D,xllm_client** C,const xllm_request* R,
 const xllm_stream_callbacks* CB,xllm_response** Out,xllm_error* E){
 (void)D;(void)C;(void)R;(void)CB;(void)Out;++Calls;xllmErrorInit(E);
 if(Calls==4&&!StopWaiting)return XLLM_RESULT_OK;
 E->eCode=XLLM_ERROR_NETWORK;return XLLM_RESULT_ERROR;
}
static bool Notify(void* D,const xllm_error* E,uint32 Next,uint32 Delay){
 (void)D;(void)Next;(void)Delay;
 if(!strcmp(MdoModelErrorKind(E),"waiting_network")){++Waiting;if(StopWaiting)xrtCancelRequest(Cancel);}
 return true;
}
void ServiceInit(XS_HostInfo* H){
 (void)H;bool Ok=MdoHomeInit()&&MdoConfigInit()&&MdoModelManagerInit();
 Ok=Ok&&MdoRecoveryRetryLimit()==10;
 const char* Patch="{\"schema_version\":1,\"patch\":{\"agent\":{\"recovery_retries\":2}}}";
 Ok=Ok&&MdoConfigImport(MDO_CONFIG_SETTINGS,xrtStrView(Patch))&&MdoRecoveryRetryLimit()==2;
 MdoModelRetryObserver Observer={Notify,NULL,true};
 xllm_request R;xllmRequestInit(&R);xllm_error E;xllm_response* Out=NULL;
 R.pCancel=Cancel=xrtCancelCreate();Calls=Waiting=0;
 xllm_result Result=MdoModelsComplete(NULL,&R,NULL,&Out,&E,false,Attempt,NULL,&Observer);
 Ok=Ok&&Result==XLLM_RESULT_OK&&Calls==4&&Waiting>=1;
 xrtCancelDestroy(Cancel);Calls=Waiting=0;StopWaiting=true;R.pCancel=Cancel=xrtCancelCreate();
 Result=MdoModelsComplete(NULL,&R,NULL,&Out,&E,false,Attempt,NULL,&Observer);
 Ok=Ok&&Result==XLLM_RESULT_CANCELLED&&Calls==3&&Waiting==1;
 xrtCancelDestroy(Cancel);Cancel=NULL;R.pCancel=NULL;
 Observer.WaitForRecovery=false;Calls=Waiting=0;
 Result=MdoModelsComplete(NULL,&R,NULL,&Out,&E,false,Attempt,NULL,&Observer);
 Ok=Ok&&Result==XLLM_RESULT_ERROR&&Calls==3&&Waiting==0;
 const char* Bad="{\"schema_version\":1,\"patch\":{\"agent\":{\"recovery_retries\":101}}}";
 Ok=Ok&&!MdoConfigImport(MDO_CONFIG_SETTINGS,xrtStrView(Bad))&&MdoRecoveryRetryLimit()==2;
 R.uDeadline=MdoMonotonicUs()-1;Calls=0;Observer.WaitForRecovery=true;
 Result=MdoModelsComplete(NULL,&R,NULL,&Out,&E,false,Attempt,NULL,&Observer);
 Ok=Ok&&Result==XLLM_RESULT_TIMEOUT&&Calls==0;
 xllmRequestUnit(&R);MdoModelManagerUnit();MdoConfigUnit();MdoHomeUnit();
 printf("policy_passed=%d\nprobe_done=1\n",Ok);
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='recovery-policy-',dir=ROOT/'.build') as raw:
        base=Path(raw);site=base/'site';write_site(site)
        (site/'probe.c').write_text(PROBE,encoding='utf-8')
        output=run_probe(args.host.resolve(),site,base/'home',timeout_seconds=20)
    assert 'policy_passed=1' in output,output
    print('PASS configurable retries, retained recovery, cancellation, explicit deadline and bounded probes')

if __name__=='__main__':main()
