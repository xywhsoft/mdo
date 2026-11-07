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
#define MDO_RECOVERY_WAIT_MS 200u
#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
static unsigned Calls,Waiting,Mode,Changes;
static MdoModelLoginState ReadyState(void){return MDO_MODEL_LOGIN_READY;}
static uint64 Revision;static uint64 ReadRevision(void){return Revision;}
static int32 StopCondition(void* D){xrtSleep(70);xrtCancelRequest((xcancel*)D);return 0;}
static xcancel* Cancel;
static bool StopWaiting;
static xllm_result Attempt(void* D,xllm_client** C,const xllm_request* R,
 const xllm_stream_callbacks* CB,xllm_response** Out,xllm_error* E){
 (void)D;(void)C;(void)R;(void)CB;(void)Out;++Calls;xllmErrorInit(E);
 if(Mode){
  if(Mode<4&&Calls==2)return XLLM_RESULT_OK;
  if(Mode==5){E->eCode=XLLM_ERROR_AUTH;E->iHttpStatus=401;E->tDiagnostics.uAttemptCount=1;snprintf(E->sProviderCode,sizeof(E->sProviderCode),"login_required");}
  else if(Mode==4){E->eCode=XLLM_ERROR_PROTOCOL;snprintf(E->sProviderCode,sizeof(E->sProviderCode),"empty_response");}
  else if(Mode==2){E->eCode=XLLM_ERROR_AUTH;snprintf(E->sProviderCode,sizeof(E->sProviderCode),"insufficient_balance");}
  else MdoModelProfileErrorInit(E,"profile_credential_unavailable","Fixture missing credential");
  return XLLM_RESULT_ERROR;
 }
 if(Calls==4&&!StopWaiting)return XLLM_RESULT_OK;
 E->eCode=XLLM_ERROR_NETWORK;return XLLM_RESULT_ERROR;
}
static bool Notify(void* D,const xllm_error* E,uint32 Next,uint32 Delay){
 (void)D;(void)Next;
 if(Mode&&Delay&&(!strcmp(MdoModelErrorKind(E),"waiting_model")||!strcmp(MdoModelErrorKind(E),"waiting_account"))){
  ++Changes;
  if(Mode==1)(void)MdoConfigImport(MDO_CONFIG_SETTINGS,xrtStrView("{\"schema_version\":1,\"patch\":{\"agent\":{\"recovery_retries\":3}}}"));
  if(Mode==2||Mode==4||Mode==5)++Revision;
 }
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
 R.uDeadline=MDO_DEADLINE_NEVER;StopWaiting=false;Observer.WaitForRecovery=true;
 for(Mode=1;Mode<=5;Mode++){
  Calls=Changes=0;R.pCancel=Cancel=xrtCancelCreate();
  g_MdoModelsOnline.RecoveryRevision=ReadRevision;if(Mode==5)g_MdoModelsOnline.State=ReadyState;
  xthread* Stopper=Mode>=3?xrtThreadCreate(StopCondition,Cancel,0):NULL;
  Result=MdoModelsComplete(NULL,&R,NULL,&Out,&E,Mode==4,Attempt,NULL,&Observer);
  if(Stopper){xrtThreadWait(Stopper);xrtThreadDestroy(Stopper);}
  printf("condition_case=%u result=%d calls=%u changes=%u base_ok=%d\n",Mode,Result,Calls,Changes,Ok);
  Ok=Ok&&Changes==1&&(Mode>=3?(Result==XLLM_RESULT_CANCELLED&&Calls==(Mode>=4?MdoRecoveryRetryLimit()+1:1)):(Result==XLLM_RESULT_OK&&Calls==2));
  xrtCancelDestroy(Cancel);
 }
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
