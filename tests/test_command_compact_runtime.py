"""Manual compaction, failure atomicity and cancellation with a loopback model."""
import argparse,json,os,subprocess,tempfile,threading,time,uuid
from pathlib import Path
from collections import Counter
from http.server import ThreadingHTTPServer
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready,session_events
from test_packed_compaction_failure import Model

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);a=p.parse_args()
    Model.calls=Counter();Model.mode='valid';Model.requests=[]
    server=ThreadingHTTPServer(('127.0.0.1',0),Model);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
      with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='command-compact-') as raw:
        base=Path(raw);port=free_port();config=write_site(base,port);home=base/'home'
        with (base/'host.log').open('wb') as output:
          child=subprocess.Popen([str(a.host.resolve()),str(config),'--','--home',str(home)],cwd=base,
            env=dict(os.environ,MDO_CONTEXT_FIXTURE_KEY='fixture-only'),stdout=output,stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
          try:
            wait_ready(port,child)
            def call(method,path,body=None,headers=None,status=200):
              actual,fields,data=request(port,method,'/api/v1'+path,body=json.dumps(body).encode() if body is not None else None,
                headers={'Content-Type':'application/json',**(headers or {})})
              assert actual==status,(path,actual,data)
              return json.loads(data)['data'],fields
            def wait(path):
              until=time.monotonic()+20
              while time.monotonic()<until:
                data,_=call('GET',path)
                if data['terminal']:return data
                time.sleep(.03)
              raise AssertionError(('timeout',path))
            settings,fields=call('GET','/models/config')
            provider=json.loads(json.dumps(settings['providers'][0]));provider.update(id='command-fixture',name='fixture',builtin=False,editable=True,removable=True,
              endpoints={'chat_completions':f'http://127.0.0.1:{server.server_port}/v1'},credential={'secret_ref':'env:MDO_CONTEXT_FIXTURE_KEY'})
            model=json.loads(json.dumps(settings['items'][0]));model.update(id='command-fixture',name='fixture',provider='command-fixture',builtin=False,free=False,
              editable=True,removable=True,protocols=['openai-chat-completions'],default_protocol='openai-chat-completions')
            model['window'].update(context_tokens=131072,max_input_tokens=131072,max_output_tokens=1024,output_reserve_tokens=1024,summary_tokens=1024)
            settings['providers'].append(provider);settings['items'].append(model);settings.pop('runtime_override',None)
            call('PUT','/settings/models',{'schema_version':1,'patch':settings},{'If-Match':fields['etag']})
            (home/'commands').mkdir(exist_ok=True)
            (home/'commands/sessioncreate.c').write_text('/* mdo-command {"id":"sessioncreate","description":"Create fixture session"} */\n#include "mdo/command.h"\nmdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){return mdoCommandCall(C,"session.create",C->Host->At(A,0),R);}',encoding='utf-8')
            create,_=call('POST','/command-invocations?project=default',{'invocation_id':'cmd-'+uuid.uuid4().hex,'command_id':'sessioncreate',
              'raw_arguments':json.dumps([{'title':'Manual compact','model_id':'command-fixture','reasoning_effort':'none','permission_profile':'read-only','max_output_tokens':1024}])},status=202)
            created=wait('/command-invocations/'+create['id']);assert created['state']=='succeeded',created
            session=created['result']
            path='/projects/default/sessions/'+session['id']
            for i in range(5):
              run,_=call('POST',path+'/runs',{'prompt':f'Turn {i} PIXEL_KEEP: dimensions32 palette8 output pixels.png. '+'history-note '*2500},status=202)
              assert wait('/runs/'+run['id'])['state']=='succeeded'
            assert Model.calls['summary']==0,Model.calls
            before=session_events(port,'/api/v1'+path)
            def invoke():
              result,_=call('POST','/command-invocations?project=default',{'invocation_id':'cmd-'+uuid.uuid4().hex,'command_id':'compact','session_id':session['id']},status=202)
              return '/command-invocations/'+result['id']
            Model.mode='quota';failed=wait(invoke());assert failed['state']=='failed' and Model.calls['summary']>0,(failed,Model.calls)
            assert Model.calls['normal']==5
            Model.mode='wait';count=Model.calls['summary'];pending=invoke();until=time.monotonic()+8
            while Model.calls['summary']==count and time.monotonic()<until:time.sleep(.02)
            assert Model.calls['summary']>count,(wait(pending),Model.calls)
            call('DELETE',pending);assert wait(pending)['state']=='cancelled'
            Model.mode='valid';result=wait(invoke())
            assert result['state']=='succeeded' and result['actions'][0]['type']=='ui.refresh',result
            events=session_events(port,'/api/v1'+path)
            assert any(e['kind']=='compaction_done' for e in events),[e['kind'] for e in events]
            history=lambda events:[(e['kind'],e.get('text')) for e in events if e['kind'] in ('user_message','assistant_message','final')]
            assert history(events)==history(before),'Compaction removed original transcript'
            run,_=call('POST',path+'/runs',{'prompt':'Continue PIXEL_KEEP.'},status=202)
            assert wait('/runs/'+run['id'])['state']=='succeeded'
          finally:
            child.terminate()
            try:child.wait(timeout=10)
            except subprocess.TimeoutExpired:child.kill();child.wait()
    finally:server.shutdown();server.server_close();thread.join(timeout=5)
    print('Manual compact: forced summary / quota failure / cancellation / original transcript / continuation: PASS')
if __name__=='__main__':main()
