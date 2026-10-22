"""Public SDK through real TCC workers and generation-owned native providers."""
import argparse,base64,json,os,subprocess,sys,tempfile,time,uuid,threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
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
#include "mdo/native/models.h"
#include "mdo/native/config.h"
#include <string.h>
static mdo_result Echo(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 (void)U;(void)E;
 MdoModelCatalog* Models=MdoModelCatalogSnapshot();
 if(!Models)return MDO_RESULT_ERROR;
 size_t Count=MdoModelCatalogModelCount(Models);MdoModelCatalogRelease(Models);
 if(!Count)return MDO_RESULT_ERROR;
 *R=C->Api->Values->Clone(P);return MDO_RESULT_OK;
}
static mdo_result Hook(void* U,const mdo_sdk_context_v1* C,const char* T,const xvalue* P,xvalue** R,mdo_error_v1* E){
 (void)U;(void)T;(void)E;if(!strcmp(C->Method,"qa.echo")){
 const mdo_values_v1* V=C->Api->Values;*R=V->Clone(P);xvalue* B=V->Bool(true);V->Set(*R,"hooked",B);V->Release(B);}
 return MDO_RESULT_OK;
}
static mdo_result Tool(void* U,const mdo_tool_context_v1* C,const char* P,mdo_result_writer_v1* W,char* Error,size_t N){
 (void)U;(void)P;(void)Error;(void)N;if(!C->Application||!C->Application->Api)return MDO_RESULT_ERROR;
 xvalue* R=NULL;mdo_error_v1 E={0};if(mdoCall(C->Application,"sdk.context",NULL,&R,&E)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 char* Text=C->Application->Api->Values->Json(R);bool Ok=Text&&W->WriteText(W->Context,Text)&&W->SetSuccess(W->Context,true);
 C->Application->Api->Values->Release(R);C->Application->Api->Values->Free(Text);return Ok?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* Error,size_t N){
 (void)D;xvalue* Catalog=NULL;mdo_error_v1 E={0};
 /* Register callbacks may query the old catalog without deadlocking reload. */
 if(mdoServices(H->Application,&Catalog,&E)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 H->Application->Api->Values->Release(Catalog);
 mdo_service_v1 S={sizeof(S),1,"qa.echo","SDK echo","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Echo};
 mdo_hook_v1 HookSpec={sizeof(HookSpec),1,"qa.before","service.before",0,true,NULL,Hook};
 mdo_tool_v1 T={0};T.Size=sizeof(T);T.AbiVersion=1;T.Id="qa.context";T.Name="SDK context";T.Description="SDK context test";T.ParametersJson="{\"type\":\"object\",\"additionalProperties\":false}";T.Effects=MDO_TOOL_EFFECT_READ;T.MaxResultBytes=8192;T.Execute=Tool;
 if(R->AddTool(R->Context,&T,Error,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 if(R->AddService(R->Context,&S,Error,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 return R->AddHook(R->Context,&HookSpec,Error,N);
}
static const mdo_module_v1 Module={MDO_V1_HEADER(mdo_module_v1),"qa.sdk","SDK fixture","SDK fixture","1",MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};
const mdo_module_v1* mdoModuleEntry(void){return &Module;}
'''

class ModelHandler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  self.server.last_request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  chunks=[{'id':'qa-model-response','model':'qa-local-model','choices':[{'index':0,'delta':{'role':'assistant','content':'sdk-model-ok'}}]},
          {'id':'qa-model-response','model':'qa-local-model','choices':[{'index':0,'delta':{},'finish_reason':'stop'}],'usage':{'prompt_tokens':11,'completion_tokens':4,'total_tokens':15}}]
  data=(''.join('data: '+json.dumps(c)+'\n\n' for c in chunks)+'data: [DONE]\n\n').encode()
  self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)

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
    plugin_path='web/plugins/qa-sdk/value.txt'
    first=invoke('plugin.apply',{'id':'qa-sdk','files':[{'path':plugin_path,'text':'revision one'}]})
    assert first['items'][0]['enabled'],first
    assert invoke('vfs.read',{'path':'/app/'+plugin_path})['text']=='revision one'
    invoke('plugin.apply',{'id':'qa-sdk','files':[{'path':plugin_path,'text':'revision two'}]})
    assert invoke('vfs.read',{'path':'/app/'+plugin_path})['text']=='revision two'
    invoke('plugin.rollback',{'id':'qa-sdk'})
    assert invoke('vfs.read',{'path':'/app/'+plugin_path})['text']=='revision one'
    invoke('plugin.disable',{'id':'qa-sdk'})
    assert invoke('vfs.read',{'path':'/app/'+plugin_path},'catch')['status']==404
    invoke('plugin.enable',{'id':'qa-sdk'})
    assert invoke('vfs.read',{'path':'/app/'+plugin_path})['text']=='revision one'
    assert invoke('plugin.apply',{'id':'qa-invalid','files':[{'path':'../escape','text':'bad'}]},'catch')['status']==422
    invoke('plugin.remove',{'id':'qa-sdk'})
    invoke('client.hello',{'client':'qa-window-a','context':{'project':'default'}})
    invoke('client.hello',{'client':'qa-window-b','context':{'project':'default'}})
    action=invoke('ui.invoke',{'client':'qa-window-a','action':'ui.toast','params':{'message':'SDK'}})
    assert invoke('client.poll',{'client':'qa-window-b'})==[]
    assert invoke('client.poll',{'client':'qa-window-a'})[0]['id']==action['id']
    assert invoke('client.ack',{'client':'qa-window-b','id':action['id'],'success':True},'catch')['status']==410
    invoke('client.ack',{'client':'qa-window-a','id':action['id'],'success':True})
    assert invoke('ui.result',{'id':action['id']})['state']=='succeeded'
    assert invoke('client.poll',{'client':'qa-window-a'})==[]
    assert invoke('qa.echo',{'nested':['',True,3,{'windows':'C:\\work'}]})=={'nested':['',True,3,{'windows':'C:\\work'}],'hooked':True}
    created=invoke('session.create',{'body':{'project_id':'default','title':'Created using public SDK'}})
    assert created['status']==201 and created['transport']=='native',created
    session=created['data']['id'];scope={'project':'default','session':session}
    tool=invoke('tool.call',{'project':'default','session':session,'tool':'qa.context','arguments':{}})
    assert tool['success'],tool
    tool_context=json.loads(tool['text'][tool['text'].index('{'):tool['text'].rindex('}')+1]);assert tool_context['project']=='default' and tool_context['session']==session,tool_context
    mock=ThreadingHTTPServer(('127.0.0.1',0),ModelHandler);mock_thread=threading.Thread(target=mock.serve_forever,daemon=True);mock_thread.start()
    try:
     models=invoke('product.models.config.get');patch={k:v for k,v in models['data'].items() if k!='runtime_override'}
     patch['providers'].append({'id':'qa-local','name':'QA Local','builtin':False,'editable':True,'removable':True,'verify_peer':True,'timeout_ms':5000,'endpoints':{'chat_completions':f'http://127.0.0.1:{mock.server_port}/v1/chat/completions'}})
     model=next(m for m in patch['items'] if m['id']=='ornith-1.5-35b').copy();model.update({'id':'qa-local-model','name':'QA Local','provider':'qa-local','wire_model':'qa-local-model','builtin':False,'free':False,'editable':True,'removable':True,'protocols':['openai-chat-completions'],'default_protocol':'openai-chat-completions'})
     patch['items'].append(model)
     invoke('product.settings.by_domain.put',{'path':{'domain':'models'},'revision':models['revision'],'body':{'schema_version':1,'patch':patch}})
     stream=invoke('stream.open',{'capacity':65536})['handle']
     model_result=invoke('model.complete',{'model':'qa-local-model','messages':[{'role':'user','content':'offline SDK test'}],'stream':stream})
     assert model_result['text']=='sdk-model-ok' and model_result['usage']['input_tokens']==11,model_result
     assert 'sdk-model-ok' in invoke('stream.read',{'handle':stream})['text']
     invoke('stream.release',{'handle':stream})
    finally:mock.shutdown();mock.server_close();mock_thread.join()
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
