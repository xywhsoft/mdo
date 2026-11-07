"""Repair a provider credential during a retained call; bypass the stale client."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
from test_model_runtime import ROOT, run_probe, write_site, personal_catalog

PROBE=r'''
#include <stdio.h>
#include <xsbase.h>
#include "model-plugin-free.h"
#define MDO_RECOVERY_WAIT_MS 4u
#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
static unsigned Changes;
static bool Notify(void* D,const xllm_error* E,uint32 N,uint32 Delay){
 (void)D;(void)N;
 if(Delay&&!strcmp(MdoModelErrorKind(E),"waiting_model")){
  ++Changes;char* Json=xrtEnvGet("MDO_FIXED_FIXTURE_CONFIG");
  bool Ok=Json&&MdoConfigImport(MDO_CONFIG_MODELS,xrtStrView(Json))&&MdoModelManagerReload();
  xrtFree(Json);return Ok;
 }return true;
}
void ServiceInit(XS_HostInfo* H){
 (void)H;bool Ok=MdoHomeInit()&&MdoConfigInit()&&MdoModelManagerInit();
 MdoModelCatalog* Pinned=MdoModelCatalogSnapshot();
 MdoModelClientOptions O;MdoModelClientOptionsInit(&O);
 O.ModelId="fixture-model";O.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;O.MaxOutputTokens=64;
 MdoModelRetryObserver Observer={Notify,NULL,true};O.RetryObserver=&Observer;
 xllm_error E;xllm_client* Old=MdoModelClientCreate(Pinned,&O,NULL,&E);
 xllm_request R;xllmRequestInit(&R);R.uMaxOutputTokens=64;R.bStream=false;
 xllmRequestAddTextMessage(&R,XLLM_ROLE_USER,"Fixture greeting");
 for(unsigned I=0;I<2;I++){
  xllm_response* Out=NULL;
  xllm_result Code=MdoModelConfiguredComplete(Pinned,Old,&O,&R,NULL,&Out,&E);
  Ok=Ok&&Code==XLLM_RESULT_OK&&Out&&Out->sContent&&!strcmp(Out->sContent,"fixture ok");
  xllmResponseDestroy(Out);
 }
 Ok=Ok&&Changes==1;
 xllmRequestUnit(&R);xllmClientDestroy(Old);MdoModelCatalogRelease(Pinned);
 MdoModelManagerUnit();MdoConfigUnit();MdoHomeUnit();
 printf("configured_recovery_passed=%d\nprobe_done=1\n",Ok);
}
'''

class Service(BaseHTTPRequestHandler):
    old=0
    fixed=0
    def log_message(self,*args): pass
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length',0)))
        fixed=self.headers.get('Authorization')=='Bearer fixed-fixture-only'
        if fixed:type(self).fixed+=1
        else:type(self).old+=1
        data=({'id':'fixture','model':'fixture-model','choices':[{'index':0,'message':{'role':'assistant','content':'fixture ok'},'finish_reason':'stop'}]}
              if fixed else {'error':{'code':'invalid_api_key','message':'Fixture rejection'}})
        payload=json.dumps(data).encode();self.send_response(200 if fixed else 401)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)))
        self.end_headers();self.wfile.write(payload)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');args=parser.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',0),Service);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix='configured-recovery-',dir=ROOT/'.build') as raw:
            base=Path(raw);site=base/'site';write_site(site)
            catalog=personal_catalog();provider=catalog['providers'][0]
            provider['endpoints']['chat_completions']=f'http://127.0.0.1:{server.server_port}/v1/chat/completions'
            defaults=site/'default-home/config/defaults.json';data=json.loads(defaults.read_text(encoding='utf-8'));data['models']=catalog
            defaults.write_text(json.dumps(data),encoding='utf-8')
            provider['credential']['secret_ref']='env:MDO_FIXED_FIXTURE_KEY'
            (site/'probe.c').write_text(PROBE,encoding='utf-8')
            output=run_probe(args.host.resolve(),site,base/'home',{'MDO_FIXED_FIXTURE_KEY':'fixed-fixture-only','MDO_FIXED_FIXTURE_CONFIG':json.dumps({'schema_version':1,'patch':catalog})})
            assert 'configured_recovery_passed=1' in output,output
            assert Service.old==1 and Service.fixed==2,(Service.old,Service.fixed)
            print('PASS credential repair resumes the retained call and later calls avoid the stale cached client')
    finally:server.shutdown();server.server_close();worker.join(timeout=3)

if __name__=='__main__':main()
