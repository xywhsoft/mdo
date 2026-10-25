"""Durable members with real native loops against an offline HTTP model."""
import argparse,json,tempfile,threading,time
from http.server import ThreadingHTTPServer
from pathlib import Path
from test_api_runtime import ROOT
from test_application_sdk_runtime import ModelHandler
from test_orchestration_sdk_runtime import Fixture

SCOPE_MODULE=r'''
#include "mdo/sdk.h"
#include "mdo/native/sessions.h"
#include "mdo/native/approvals.h"
static MdoApprovalScope* Scope;
static mdo_result Call(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 const mdo_values_v1* V=C->Api->Values;const char* S=V->Text(V->Get(P,"session"));const char* A=V->Text(V->Get(P,"action"));xwork_error X;
 if(!strcmp(A,"acquire"))Scope=MdoSessionPermissionScopeAcquire("default",S,&X);
 else if(!strcmp(A,"release")){MdoSessionPermissionScopeRelease("default",S,Scope);Scope=NULL;}
 else if(!strcmp(A,"bounce")){MdoSessionRuntimeOptions O;MdoSessionRuntimeOptionsInit(&O);MdoSession* H=MdoSessionOpen("default",S,&O,&X);if(!H)return MDO_RESULT_ERROR;MdoSessionRelease(H);}
 else {MdoSession* H=MdoSessionLoad("default",S,&X);bool Ok=H&&MdoSessionSetPermission(H,A,&X);MdoSessionRelease(H);if(!Ok)return MDO_RESULT_ERROR;}
 *R=V->Bool(Scope&&xworkFileScopeExternal(Scope->Files));return MDO_RESULT_OK;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
 mdo_service_v1 S={sizeof(S),1,"qa.scope","Scope test","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Call};return R->AddService(R->Context,&S,E,N);
}
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){static const mdo_module_v1 M={sizeof(M),1,"qa.scope","Scope QA","QA","1",MDO_CAPABILITY_ALL,NULL,0,Register,NULL};return &M;}
'''

class DelayedModel(ModelHandler):
    def do_POST(self):
        self.server.calls=getattr(self.server,'calls',0)+1
        if getattr(self.server,'blocked',False):
            self.server.entered.set();self.server.release.wait(10)
        try:super().do_POST()
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);a=p.parse_args()
    mock=ThreadingHTTPServer(('127.0.0.1',0),DelayedModel);mock.entered=threading.Event();mock.release=threading.Event();worker=threading.Thread(target=mock.serve_forever,daemon=True);worker.start()
    try:
        with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='member-sdk-') as tmp:
            f=Fixture(a.host.resolve(),Path(tmp));roles=f.home/'subagents';roles.mkdir(parents=True)
            (f.home/'modules/tools/scope.c').write_text(SCOPE_MODULE,encoding='utf-8')
            (roles/'member.md').write_text('---\nschema: mdo.agent.v1\nname: Member QA\nmodel: qa-local-model\ntools: []\nread_only: true\n---\nReply briefly.\n',encoding='utf-8')
            f.start()
            try:
                cfg=f.call('product.models.config.get');patch={k:v for k,v in cfg['data'].items() if k!='runtime_override'}
                patch['providers'].append({'id':'qa-local','name':'QA Local','builtin':False,'editable':True,'removable':True,'verify_peer':True,'timeout_ms':5000,'endpoints':{'chat_completions':f'http://127.0.0.1:{mock.server_port}/v1/chat/completions'}})
                model=next(m for m in patch['items'] if m['id']=='ornith-1.5-35b').copy();model.update({'id':'qa-local-model','name':'QA Local','provider':'qa-local','wire_model':'qa-local-model','builtin':False,'free':False,'editable':True,'removable':True,'protocols':['openai-chat-completions'],'default_protocol':'openai-chat-completions'});patch['items'].append(model)
                f.call('product.settings.by_domain.put',{'path':{'domain':'models'},'revision':cfg['revision'],'body':{'schema_version':1,'patch':patch}})
                parent=f.call('session.create',{'body':{'project_id':'default','model_id':'qa-local-model','permission_profile':'balanced'}})['data']['id']
                def scope(action):return f.call('qa.scope',{'session':parent,'action':action})
                assert scope('acquire') is False;assert scope('full-access') is True
                assert scope('bounce') is True;assert scope('balanced') is False;scope('release')
                base={'namespace':'qa.members','id':'researcher'}
                def call(action,**params):return f.call('instance.'+action,{**base,**params})
                first=call('create',agent_id='subagent.member');assert first['state']=='idle',first
                assert call('create',agent_id='subagent.member')==first
                f.call('instance.create',{**base,'agent_id':'mdo.default'},409)
                assert call('step')['no_work']
                sent=call('send',message_id='turn1',prompt='Remember the first turn');assert call('send',message_id='turn1',prompt='Remember the first turn')==sent
                result=call('step');assert result['state']=='idle' and result['last_outcome']['outcome']=='succeeded',result
                assert result['last_outcome']['text']=='sdk-model-ok',result
                assert call('step')['no_work']
                call('send',message_id='turn2',prompt='Second turn');result=call('step');assert result['last_outcome']['outcome']=='succeeded',result
                users=[m['content'] for m in mock.last_request['messages'] if m['role']=='user'];assert 'Remember the first turn' in users and 'Second turn' in users,users
                f.stop();f.start();assert call('get')['last_outcome']==result['last_outcome']
                call('send',message_id='turn3',prompt='After restart');result=call('step');assert result['last_outcome']['outcome']=='succeeded',result
                users=[m['content'] for m in mock.last_request['messages'] if m['role']=='user'];assert len(users)>=3,users
                (roles/'member.md').write_text((roles/'member.md').read_text(encoding='utf-8')+'Updated.\n',encoding='utf-8');f.call('runtime.reload',{'domain':'modules'})
                call('send',message_id='updated',prompt='Use the updated role');assert call('step')['last_outcome']['outcome']=='succeeded'
                assert any('Updated.' in m.get('content','') for m in mock.last_request['messages'] if m['role']=='system')
                assert len(f.call('instance.list',{'namespace':'qa.members'}))==1
                assert call('close')['state']=='closed';f.call('instance.send',{**base,'message_id':'turn4','prompt':'closed'},409)
                crashed={**base,'id':'interrupted'}
                f.call('instance.create',{**crashed,'agent_id':'subagent.member'});f.call('instance.send',{**crashed,'message_id':'lost','prompt':'Do not replay after crash'})
                mock.blocked=True;f.call('operation.start',{'method':'instance.step','params':crashed});assert mock.entered.wait(3)
                assert f.call('instance.get',crashed)['state']=='running'
                f.call('instance.step',crashed,409)
                f.stop();calls=mock.calls;mock.blocked=False;mock.release.set();f.start()
                assert f.call('instance.get',crashed)['state']=='requires_review'
                f.call('instance.step',crashed,409);f.call('instance.send',{**crashed,'message_id':'another','prompt':'must not queue'},409)
                time.sleep(.1);assert mock.calls==calls,'Restart replayed a model effect'
                assert f.call('instance.close',crashed)['state']=='closed'
            finally:f.stop()
    finally:mock.shutdown();mock.server_close();worker.join()
    print('PASS native SubAgent member identity, inbox dedup, multi-turn ledger, restart, live role reload and close')
if __name__=='__main__':main()
