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
#include "mdo/agent.h"
#include "mdo/work.h"
#include "mdo/mailbox.h"
#include <stdio.h>
static MdoApprovalScope* Scope;
static mdo_result Worker(void* U,const mdo_agent_context_v1* C,char* Error,size_t N){
 const mdo_sdk_context_v1* S=C->Application;const mdo_values_v1* V=S->Api->Values;xvalue* P=V->Parse("{\"id\":\"from-c-worker\",\"title\":\"Created from the C member\"}"),*R=NULL;mdo_error_v1 E={0};
 mdo_result Code=mdoWorkCreate(S,P,&R,&E);V->Release(P);V->Release(R);if(Code!=MDO_RESULT_OK)return Code;
 xwork_error X;Code=mdoAgentRunDefault(C,&X);if(Code!=MDO_RESULT_OK)snprintf(Error,N,"%s",X.sMessage);return Code;
}
static const mdo_agent_program_v1 Program={sizeof(Program),1,NULL,Worker};
static const mdo_agent_v1 Role={.Size=sizeof(mdo_agent_v1),.AbiVersion=1,.Id="subagent.c-worker",.Name="C worker",.Description="Member implemented with public C SDK",.Model="qa-local-model",.SystemPrompt="C worker reply briefly.",.AllowedEffects=MDO_TOOL_EFFECT_ALL,.Flags=MDO_AGENT_SUBAGENT|MDO_AGENT_READ_ONLY,.Program=&Program};
static mdo_result Tool(void* U,const mdo_tool_context_v1* C,const char* Args,mdo_result_writer_v1* W,char* Error,size_t N){
 const mdo_sdk_context_v1* S=C->Application;const mdo_values_v1* V=S->Api->Values;xvalue* R=NULL;mdo_error_v1 E={0};
 xvalue* P=V->Parse("{\"mailbox\":\"team\",\"message_id\":\"member-active\",\"body\":\"ready\"}");mdo_result Code=mdoMailboxSend(S,P,&R,&E);V->Release(P);V->Release(R);R=NULL;if(Code!=MDO_RESULT_OK)return Code;
 Code=mdoCall(S,"sdk.context",NULL,&R,&E);char* Text=Code==MDO_RESULT_OK?V->Json(R):NULL;bool Ok=Text&&W->WriteText(W->Context,Text)&&W->SetSuccess(W->Context,true);V->Free(Text);V->Release(R);return Ok?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
static mdo_result Call(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 const mdo_values_v1* V=C->Api->Values;const char* S=V->Text(V->Get(P,"session"));const char* A=V->Text(V->Get(P,"action"));xwork_error X;
 if(!strcmp(A,"acquire"))Scope=MdoSessionPermissionScopeAcquire("default",S,&X);
 else if(!strcmp(A,"release")){MdoSessionPermissionScopeRelease("default",S,Scope);Scope=NULL;}
 else if(!strcmp(A,"bounce")){MdoSessionRuntimeOptions O;MdoSessionRuntimeOptionsInit(&O);MdoSession* H=MdoSessionOpen("default",S,&O,&X);if(!H)return MDO_RESULT_ERROR;MdoSessionRelease(H);}
 else {MdoSession* H=MdoSessionLoad("default",S,&X);bool Ok=H&&MdoSessionSetPermission(H,A,&X);MdoSessionRelease(H);if(!Ok)return MDO_RESULT_ERROR;}
 *R=V->Bool(Scope&&xworkFileScopeExternal(Scope->Files));return MDO_RESULT_OK;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
#ifdef MEMBER_ROLE_ONLY
 return R->AddAgent(R->Context,&Role,E,N);
#else
 mdo_service_v1 S={sizeof(S),1,"qa.scope","Scope test","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Call};if(R->AddService(R->Context,&S,E,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 mdo_tool_v1 T={0};T.Size=sizeof(T);T.AbiVersion=1;T.Id="qa.context";T.Name="Member context";T.Description="Read current context";T.ParametersJson="{\"type\":\"object\"}";T.Effects=MDO_TOOL_EFFECT_READ;T.MaxResultBytes=8192;T.Execute=Tool;
 return R->AddTool(R->Context,&T,E,N);
#endif
}
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){static const mdo_module_v1 M={sizeof(M),1,
#ifdef MEMBER_ROLE_ONLY
 "qa.worker",
#else
 "qa.scope",
#endif
 "Scope QA","QA","1",MDO_CAPABILITY_ALL,NULL,0,Register,NULL};return &M;}
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
            (f.home/'modules/tools/collaboration.c').write_text((ROOT/'examples/collaboration/tools.c').read_text(encoding='utf-8'),encoding='utf-8')
            native=f.home/'modules/subagents/worker.c';native.parent.mkdir(parents=True);native.write_text('#define MEMBER_ROLE_ONLY\n'+SCOPE_MODULE,encoding='utf-8')
            (roles/'member.md').write_text('---\nschema: mdo.agent.v1\nname: Member QA\nmodel: qa-local-model\ntools: [qa.context]\nread_only: true\n---\nReply briefly.\n',encoding='utf-8')
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
                demo=f.call('session.create',{'body':{'project_id':'default','model_id':'qa-local-model','permission_profile':'full-access'}})['data']['id']
                def example(tool,action,args):
                    value=f.call('tool.call',{'project':'default','session':demo,'tool':tool,'arguments':{'action':action,'args':args}})
                    assert value['success'],value
                    text=value['text'];return json.loads(text[text.index('{'):text.rindex('}')+1])
                assert example('members','create',{'id':'example-worker','agent_id':'subagent.member'})['state']=='idle'
                example('members','send',{'id':'example-worker','message_id':'example-turn','prompt':'Run using the optional example tools'})
                assert example('members','run',{'id':'example-worker'})['last_outcome']['outcome']=='succeeded'
                assert example('shared_work','create',{'id':'example-task','title':'A shared task'})['ready']
                assert f.call('work.get',{'namespace':'session.'+demo,'id':'example-task'})['title']=='A shared task'
                base={'namespace':'qa.members','id':'researcher'}
                def call(action,**params):return f.call('instance.'+action,{**base,**params})
                first=call('create',agent_id='subagent.member');assert first['state']=='idle',first
                assert call('create',agent_id='subagent.member')==first
                f.call('instance.create',{**base,'agent_id':'mdo.default'},409)
                assert call('step')['no_work']
                sent=call('send',message_id='turn1',prompt='Remember the first turn');assert call('send',message_id='turn1',prompt='Remember the first turn')==sent
                result=call('step');assert result['state']=='idle' and result['last_outcome']['outcome']=='succeeded',result
                assert result['last_outcome']['text']=='sdk-model-ok',result
                assert call('result',message_id='turn1')['result']==result['last_outcome']
                assert call('wait',message_id='turn1',timeout_ms=100)['completed']
                assert call('step')['no_work']
                call('send',message_id='turn2',prompt='Second turn')
                assert not call('wait',message_id='turn2',timeout_ms=20)['completed']
                pending=f.call('operation.start',{'method':'instance.wait','params':{**base,'message_id':'turn2','timeout_ms':3000}})
                result=call('step');assert result['last_outcome']['outcome']=='succeeded',result
                assert f.call('operation.wait',{'id':pending['id'],'timeout_ms':1000})['outcome']=='succeeded'
                assert f.call('operation.result',{'id':pending['id']})['result']==result['last_outcome'];f.call('operation.release',{'id':pending['id']})
                users=[m['content'] for m in mock.last_request['messages'] if m['role']=='user'];assert 'Remember the first turn' in users and 'Second turn' in users,users
                f.stop();f.start();assert call('get')['last_outcome']==result['last_outcome']
                call('send',message_id='turn3',prompt='After restart');result=call('step');assert result['last_outcome']['outcome']=='succeeded',result
                users=[m['content'] for m in mock.last_request['messages'] if m['role']=='user'];assert len(users)>=3,users
                (roles/'member.md').write_text((roles/'member.md').read_text(encoding='utf-8')+'Updated.\n',encoding='utf-8');f.call('runtime.reload',{'domain':'modules'})
                call('send',message_id='updated',prompt='Use the updated role');assert call('step')['last_outcome']['outcome']=='succeeded'
                assert any('Updated.' in m.get('content','') for m in mock.last_request['messages'] if m['role']=='system')
                mock.child_scope=True;call('send',message_id='member-context',prompt='Inspect current member');assert call('step')['last_outcome']['outcome']=='succeeded';mock.child_scope=False
                reply=next(m['content'] for m in mock.last_request['messages'] if m['role']=='tool');context=json.loads(reply[reply.index('{'):reply.rindex('}')+1]);assert context['namespace']=='qa.members' and context['member']=='researcher',context
                assert f.call('mailbox.read',{'namespace':'qa.members','mailbox':'team'})['items'][0]['sender']=='researcher'
                assert len(f.call('instance.list',{'namespace':'qa.members'}))==1
                summary=f.call('instance.list',{'namespace':'qa.members'})[0];assert 'last_outcome' not in summary and summary['outcome']=='succeeded',summary
                c_worker={**base,'id':'native-worker'};f.call('instance.create',{**c_worker,'agent_id':'subagent.c-worker'});f.call('instance.send',{**c_worker,'message_id':'c-turn','prompt':'Run C worker'})
                assert f.call('instance.step',c_worker)['last_outcome']['outcome']=='succeeded'
                assert f.call('work.get',{'namespace':'qa.members','id':'from-c-worker'})['ready']
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
                interrupted=f.call('instance.get',crashed)
                assert f.call('instance.wait',{**crashed,'message_id':'lost','timeout_ms':3000})['result']['outcome']=='requires_review'
                resumed=f.call('instance.resume',{**crashed,'message_id':'lost','expected_revision':interrupted['revision']})
                assert resumed['state']=='idle' and resumed['last_outcome']['outcome']=='succeeded',resumed
                f.call('instance.resume',{**crashed,'message_id':'lost','expected_revision':interrupted['revision']},409)
                users=[m['content'] for m in mock.last_request['messages'] if m['role']=='user'];assert users.count('Do not replay after crash')==1,users
                # Explicitly abandoning a second interrupted turn sends no model request.
                f.call('instance.send',{**crashed,'message_id':'abandoned','prompt':'Finish this interrupted response'})
                mock.blocked=True;mock.entered.clear();mock.release.clear();f.call('operation.start',{'method':'instance.step','params':crashed});assert mock.entered.wait(3)
                f.stop();calls=mock.calls;mock.blocked=False;mock.release.set();f.start();interrupted=f.call('instance.get',crashed)
                abandoned=f.call('instance.abandon',{**crashed,'message_id':'abandoned','expected_revision':interrupted['revision']})
                assert abandoned['state']=='idle' and abandoned['last_outcome']['outcome']=='cancelled',abandoned
                assert mock.calls==calls,'Abandon unexpectedly invoked a model'
                f.call('instance.send',{**crashed,'message_id':'next','prompt':'A new turn after abandon'});assert f.call('instance.step',crashed)['last_outcome']['outcome']=='succeeded'
                f.call('instance.send',{**crashed,'message_id':'not-started','prompt':'Close with pending work'})
                assert f.call('instance.close',crashed)['state']=='closed'
                assert f.call('instance.wait',{**crashed,'message_id':'not-started','timeout_ms':100})['result']['outcome']=='cancelled'
            finally:f.stop()
    finally:mock.shutdown();mock.server_close();worker.join()
    print('PASS native members: multi-turn ledger, durable results/waits, restart without replay, explicit resume/abandon, live permissions/reload and close')
if __name__=='__main__':main()
