"""Public SDK through real TCC workers and generation-owned native providers."""
import argparse,base64,json,os,subprocess,sys,tempfile,time,uuid
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready

COMMAND=r'''
#include "mdo/command.h"
#include "mdo/sdk.h"
mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 const mdo_sdk_context_v1* S=C->Application;const mdo_values_v1* V=S->Api->Values;
 const char* Method=V->Text(V->At(A,0));mdo_error_v1 E={0};
 mdo_result Code=mdoCall(S,Method,V->At(A,1),R,&E);
 const char* Mode=V->Text(V->At(A,2));
 if(Mode&&!strcmp(Mode,"ids")&&Code==MDO_RESULT_OK){
  xvalue* Ids=V->Array();for(size_t i=0;i<V->Count(*R);i++)V->Append(Ids,V->Get(V->At(*R,i),"id"));V->Release(*R);*R=Ids;
 }
 if(Mode&&!strcmp(Mode,"catch")&&Code!=MDO_RESULT_OK){
  V->Release(*R);*R=V->Object();xvalue* S=V->String(E.Code);V->Set(*R,"error",S);V->Release(S);
  S=V->UInt(E.Status);V->Set(*R,"status",S);V->Release(S);Code=MDO_RESULT_OK;
 }
 return Code;
}'''
MODULE=r'''
#include "mdo/sdk.h"
#include <string.h>
static mdo_result Echo(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 (void)U;(void)E;*R=C->Api->Values->Clone(P);return MDO_RESULT_OK;
}
static mdo_result Hook(void* U,const mdo_sdk_context_v1* C,const char* T,const xvalue* P,xvalue** R,mdo_error_v1* E){
 (void)U;(void)T;(void)E;if(!strcmp(C->Method,"qa.echo")){
 const mdo_values_v1* V=C->Api->Values;*R=V->Clone(P);xvalue* B=V->Bool(true);V->Set(*R,"hooked",B);V->Release(B);}
 return MDO_RESULT_OK;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* Error,size_t N){
 (void)D;xvalue* Catalog=NULL;mdo_error_v1 E={0};
 /* Register callbacks may query the old catalog without deadlocking reload. */
 if(mdoServices(H->Application,&Catalog,&E)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 H->Application->Api->Values->Release(Catalog);
 mdo_service_v1 S={sizeof(S),1,"qa.echo","SDK echo","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Echo};
 mdo_hook_v1 HookSpec={sizeof(HookSpec),1,"qa.before","service.before",0,true,NULL,Hook};
 if(R->AddService(R->Context,&S,Error,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 return R->AddHook(R->Context,&HookSpec,Error,N);
}
static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),"qa.sdk","SDK fixture","SDK fixture","1",MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};
const mdo_module_v1* mdoModuleEntry(void){return &Module;}
'''

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);args=p.parse_args()
 with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='application-sdk-') as tmp:
  base=Path(tmp);port=free_port();config=write_site(base,port);home=base/'home';(home/'commands').mkdir(parents=True)
  (home/'commands/sdk.c').write_text('/* mdo-command {"id":"sdk","description":"SDK fixture"} */\n#include <string.h>\n'+COMMAND,encoding='utf-8')
  modules=home/'modules/tools';modules.mkdir(parents=True);(modules/'sdk.c').write_text(MODULE,encoding='utf-8')
  with (base/'host.log').open('wb') as log:
   child=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
   try:
    wait_ready(port,child)
    def invoke(method,params=None,mode=None):
     identity='cmd-'+uuid.uuid4().hex;raw=[method,params]
     if mode:raw.append(mode)
     status,_,body=request(port,'POST','/api/v1/command-invocations?project=default',body=json.dumps({'invocation_id':identity,'command_id':'sdk','raw_arguments':json.dumps(raw)}).encode(),headers={'Content-Type':'application/json'})
     assert status==202,(status,body)
     end=time.monotonic()+20
     while time.monotonic()<end:
      status,_,body=request(port,'GET','/api/v1/command-invocations/'+identity);value=json.loads(body)['data']
      if value['terminal']:
       assert value['state']=='succeeded',value
       return value['result']
      time.sleep(.025)
     raise AssertionError((method,'deadline',(base/'host.log').read_text(encoding='utf-8',errors='replace')))
    ids=invoke('sdk.services',{},'ids');assert len(ids)>200 and 'qa.echo' in ids,ids
    assert {'account.logout','session.create','product.update.install.post','product.device_transfers.post'}<=set(ids)
    assert invoke('sdk.context')['project']=='default'
    native=base/'outside-workspace'/'中文.bin'
    assert invoke('fs.write',{'path':str(native),'base64':'AAEC/w=='})['bytes']==4
    read=invoke('fs.read',{'path':str(native),'offset':1,'limit':2})
    assert base64.b64decode(read['base64'])==b'\x01\x02' and read['size']==4,read
    assert invoke('fs.list',{'path':str(native.parent)})['items'][0]['name']=='中文.bin'
    assert invoke('storage.write',{'path':'settings/state.json','text':'{"test":true}'})['bytes']==13
    assert invoke('storage.read',{'path':'settings/state.json'})['text']=='{"test":true}'
    assert invoke('storage.read',{'path':'../escape'},'catch')['status']==400
    process=invoke('process.run',{'program':sys.executable,'cwd':str(base),'args':['-c','import sys; print(repr(sys.argv[1])); sys.exit(7)','']})
    assert process['exit_code']==7 and process['stdout']['text'].strip()=="''",process
    assert invoke('process.run',{'program':'mdo-sdk-missing-program'},'catch')['error']=='sdk_native_failed'
    assert invoke('runtime.info')['version']=='0.2.4'
    invoke('runtime.env.set',{'name':'MDO_SDK_TEST_VALUE','value':'hello'})
    assert invoke('runtime.env.get',{'name':'MDO_SDK_TEST_VALUE'})['value']=='hello'
    stream=invoke('stream.open',{'capacity':4})['handle']
    assert invoke('stream.write',{'handle':stream,'text':'abcd'})['written']==4
    assert invoke('stream.write',{'handle':stream,'text':'x'},'catch')['error']=='sdk_stream_would_block'
    assert invoke('stream.read',{'handle':stream,'limit':2})['text']=='ab'
    assert invoke('stream.write',{'handle':stream,'text':'ef'})['written']==2
    invoke('stream.close',{'handle':stream})
    end=invoke('stream.read',{'handle':stream});assert end['text']=='cdef' and end['eof'],end
    invoke('stream.release',{'handle':stream})
    assert invoke('stream.stat',{'handle':stream},'catch')['status']==410
    opid='sdk-'+uuid.uuid4().hex;start={'id':opid,'method':'fs.append','params':{'path':str(base/'once.txt'),'text':'once'}}
    invoke('operation.start',start)
    assert invoke('operation.wait',{'id':opid,'timeout_ms':2000})['state']=='succeeded'
    invoke('operation.start',start);assert (base/'once.txt').read_text()=='once'
    assert invoke('operation.result',{'id':opid})['bytes']==4
    assert invoke('operation.start',{**start,'method':'fs.write'},'catch')['status']==409
    invoke('operation.release',{'id':opid})
    assert invoke('operation.start',start)['state']=='succeeded' and (base/'once.txt').read_text()=='once'
    sleeping=invoke('operation.start',{'method':'runtime.sleep','params':{'milliseconds':10000}})['id']
    invoke('operation.cancel',{'id':sleeping})
    assert invoke('operation.wait',{'id':sleeping,'timeout_ms':2000})['state']=='cancelled'
    invoke('operation.release',{'id':sleeping})
    assert invoke('qa.echo',{'nested':['',True,3,{'windows':'C:\\work'}]})=={'nested':['',True,3,{'windows':'C:\\work'}],'hooked':True}
    created=invoke('session.create',{'body':{'project_id':'default','title':'Created using public SDK'}})
    assert created['status']==201 and created['transport']=='native',created
    session=created['data']['id'];scope={'project':'default','session':session}
    detail=invoke('session.get',{'path':scope});assert detail['data']['title']=='Created using public SDK',detail
    assert invoke('sdk.does_not_exist',{},'catch')=={'error':'sdk_method_missing','status':404}
    assert invoke('session.get',{'path':{'session':'missing'}},'catch')['status']==404
    events=invoke('events.read',{'after':0});assert events['cursor']>0 and events['items'],events
    assert 'nested' not in json.dumps(events),'Service event journal leaked input payloads'
    assert invoke('events.read',{'epoch':'old-generation'})['resync_required']
    assert child.poll() is None
    status,_,body=request(port,'GET','/api/v1/application/services');catalog=json.loads(body)['data'];assert status==200 and len(catalog)>200,(status,body)
    status,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':'qa.echo','params':{'hello':'front-end'}}).encode(),headers={'Content-Type':'application/json'})
    assert status==200 and json.loads(body)['data']=={'hello':'front-end','hooked':True},(status,body)
   finally:
    child.terminate()
    try:child.wait(timeout=10)
    except subprocess.TimeoutExpired:child.kill();child.wait()
 print('Application SDK: full product directory, real CRUD, typed values, providers, hooks, errors and reentrant registration PASS')
if __name__=='__main__':main()
