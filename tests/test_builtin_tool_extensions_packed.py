"""All bundled tools use directory activation; the host never revives a disabled tool."""
from __future__ import annotations
import argparse
from collections import defaultdict
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import json,os,tempfile,threading,time
from pathlib import Path
from test_packed_home_lease import ROOT,request,site,start,stop,wait_bootstrap,release_packed_copies
from test_packed_queue_start_recovery import configure_model

NATIVE='read ls glob grep write edit exec spawn status wait stdin stop ask skill agent search open find tools load fetch'.split()
class Model(BaseHTTPRequestHandler):
    scenario='enabled';calls=defaultdict(int);errors=[];child_checked=False
    def log_message(self,*_):pass
    def do_POST(self):
        try:
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            text=json.dumps(body['messages']);child='You are the general-purpose assistant working on a task delegated' in text
            names={t['function']['name'] for t in body.get('tools',[])}
            mode=type(self).scenario;key=(mode,child);type(self).calls[key]+=1
            assert ('read' in names)==(mode!='disabled'),(mode,child,names)
            message={'role':'assistant','content':'TOOL_SET_VERIFIED'};reason='stop'
            if child:
                assert 'CHECK_CHILD_TOOL_SET' in text
                type(self).child_checked=True
            elif self.calls[key]==1:
                name='agent' if mode=='disabled' else 'read'
                args={'name':'subagent.general','prompt':'CHECK_CHILD_TOOL_SET','background':False} if mode=='disabled' else {'path':'tool-sentinel.txt'}
                message={'role':'assistant','content':None,'tool_calls':[{'id':'tool-call-'+mode,'type':'function','function':{'name':name,'arguments':json.dumps(args)}}]};reason='tool_calls'
            elif mode!='disabled':assert 'BUILTIN_READ_EXECUTED' in text,'native read did not return the fixture sentinel'
            data=json.dumps({'id':'builtin-fixture','model':body['model'],'choices':[{'index':0,'message':message,'finish_reason':reason}],'usage':{'prompt_tokens':20,'completion_tokens':10}}).encode()
            self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        except Exception as error:
            type(self).errors.append(str(error));self.send_error(500)

def check(packed:Path,previous:Path|None,report:Path|None):
    evidence={}
    with tempfile.TemporaryDirectory(prefix='builtin-tools-',dir=ROOT/'.build') as raw:
        base=Path(raw);home=base/'mdo-home';directory,port=site(base,'current',packed)
        env={**os.environ,'USE_WEBVIEW':'0','MDO_QUEUE_FIXTURE_KEY':'fixture-only'}
        old=None
        if previous:
            old,old_port=site(base,'previous',previous);old_process=start(old,previous,home,env)
            try:
                wait_bootstrap(old_process,old_port,old/'packed.log')
                _,plan=request(old_port,'POST','/api/v1/ecosystem',dict(action='dependency_plan',operation='disable',ref='tool:todo',cascade=True));plan=plan['data']
                status,result=request(old_port,'POST','/api/v1/ecosystem',dict(action='apply_plan',plan_id=plan['id'],revision=plan['revision'],trust_code=True));assert status==200,result
            finally:stop(old_process)
        process=start(directory,packed,home,env);model=None
        try:
            _,boot=wait_bootstrap(process,port,directory/'packed.log');assert boot['data']['ready'],boot
            status,result=request(port,'GET','/api/v1/extensions/tools?limit=100');items=result['data']['items'];assert status==200 and len(items)==22,result
            byid={x['id']:x for x in items};assert set(byid)=={'tool:'+name for name in NATIVE+['todo']}
            assert all(x['source']=='builtin' and x['name']==x['alias'] for x in items),items
            assert all((home/'tools'/name/'extension.json').is_file() and (home/'tools'/name/'tool.c').is_file() for name in NATIVE+['todo'])
            evidence['all_22_tools_are_bundled_directories']=True
            print('PASS all 22 tools share the bundled directory model',flush=True)
            def change(name,operation):
                status,p=request(port,'POST','/api/v1/ecosystem',dict(action='dependency_plan',operation=operation,ref='tool:'+name,cascade=True));assert status==200,p;p=p['data'];assert p['ready'],p
                status,r=request(port,'POST','/api/v1/ecosystem',dict(action='apply_plan',plan_id=p['id'],revision=p['revision'],trust_code=True));assert status==200 and r['data'].get('runtime_ready') is not False,r
            def registered():
                status,r=request(port,'GET','/api/v1/tools');assert status==200,r;return {x['id']:x for x in r['data']['items']}
            if previous:
                assert not byid['tool:todo']['enabled'] and byid['tool:todo']['name']=='todo',byid['tool:todo']
                evidence['upgrade_adds_native_directories_and_preserves_disabled_todo']=True;change('todo','enable')
            assert set(registered())==set(NATIVE+['todo'])
            for name in NATIVE:
                change(name,'disable');assert name not in registered(),name
                status,row=request(port,'GET','/api/v1/extensions/tools/'+name);assert status==200 and not row['data']['enabled'],row
                change(name,'enable');assert name in registered(),name
            evidence['all_21_host_tools_disable_and_enable_through_dependency_plans']=True
            print('PASS all 21 host-tool enable/disable cycles',flush=True)
            model=ThreadingHTTPServer(('127.0.0.1',0),Model);thread=threading.Thread(target=model.serve_forever,daemon=True);thread.start()
            configure_model(port,f'http://127.0.0.1:{model.server_port}/v1/chat/completions')
            status,r=request(port,'POST','/api/v1/sessions',dict(project_id='default',title='Builtin extensions QA',agent_id='mdo.default',model_id='queue-fixture',protocol='openai-chat-completions',reasoning_effort='none',permission_profile='full-access',max_output_tokens=1024));assert status==201,r
            sid=r['data']['id'];path='/api/v1/projects/default/sessions/'+sid
            workspace=home/'workspace';workspace.mkdir(parents=True,exist_ok=True);(workspace/'tool-sentinel.txt').write_text('BUILTIN_READ_EXECUTED',encoding='utf-8')
            def run(mode):
                Model.scenario=mode;status,r=request(port,'POST',path+'/runs',dict(prompt='Check '+mode,timeout_ms=15000));assert status==202,r;job=r['data'];deadline=time.monotonic()+20
                while not job['terminal'] and time.monotonic()<deadline:
                    time.sleep(.05);status,r=request(port,'GET','/api/v1/runs/'+job['id']);job=r['data']
                assert job['state']=='succeeded' and not Model.errors,(job,Model.errors)
            run('enabled');evidence['enabled_binding_uses_real_workspace_read']=True
            change('read','disable');stop(process);process=start(directory,packed,home,env);wait_bootstrap(process,port,directory/'packed.log')
            assert 'read' not in registered();run('disabled');assert Model.child_checked,Model.errors
            evidence['disabled_tool_stays_hidden_after_restart_in_main_and_child']=True
            change('read','enable');run('reenabled');evidence['reenable_restores_execution_in_same_conversation']=True
            change('exec','remove');stop(process);process=start(directory,packed,home,env);wait_bootstrap(process,port,directory/'packed.log')
            assert 'exec' not in registered() and not (home/'tools/exec').exists()
            evidence['removed_native_extension_has_no_hidden_fallback']=True
        finally:
            stop(process)
            if model:model.shutdown();model.server_close();thread.join(timeout=5)
            copies=[directory/packed.name]+([old/previous.name] if old else []);release_packed_copies(*copies)
    if report:report.write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    print('PASS',len(evidence),'bundled tool extension contracts')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--packed',type=Path,required=True);p.add_argument('--previous',type=Path);p.add_argument('--report',type=Path);a=p.parse_args()
    check(a.packed.resolve(),a.previous.resolve() if a.previous else None,a.report)
