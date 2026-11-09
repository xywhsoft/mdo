"""Context rejection, summary budgets and retry visibility on the native app.

Only disposable Home/workspace and a synthetic loopback model are used.
"""
from __future__ import annotations
import argparse,hashlib,json,os,shutil,subprocess,tempfile,threading,time,uuid
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from model_fixture import conversation_catalog
from test_api_runtime import ROOT,request,free_port,wait_ready,session_events
from test_packed_compaction_recovery import CODING_SUMMARY

class Model(BaseHTTPRequestHandler):
    mode='overflow';normal=0;records=[];summaries=0
    def log_message(self,*_):pass
    def handle(self):
        try:super().handle()
        except (ConnectionError,OSError):pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        summary=body['messages'][0]['content'].startswith('Create a precise continuation summary')
        record={'summary':summary,'bytes':len(json.dumps(body['messages'])),'correction':len(body['messages'])>2}
        type(self).records.append(record);status=200
        if summary:
            type(self).summaries+=1
            if self.mode=='wait':status=429;code='daily_token_limit'
            elif self.mode=='shrink' and record['bytes']>7000:status=400;code='context_length_exceeded'
            text='x'*100000 if self.mode=='quality' and self.summaries==1 else CODING_SUMMARY
        else:
            type(self).normal+=1;text='Completed fixture work.'
            if self.mode.startswith('overflow') and self.normal==3:
                status=413 if self.mode=='overflow413' else 400
                code='1261' if self.mode=='overflow1261' else 'context_length_exceeded'
        if status!=200:value={'error':{'code':code,'message':'Synthetic rejection.'}}
        elif not summary and self.mode.startswith('overflow') and self.normal==2:
            value={'choices':[{'index':0,'message':{'role':'assistant','content':'','tool_calls':[{'id':'write-once','type':'function','function':{'name':'write','arguments':json.dumps({'path':'completed.txt','content':'written once'})}}]},'finish_reason':'tool_calls'}]}
        else:value={'choices':[{'index':0,'message':{'role':'assistant','content':text},'finish_reason':'stop'}]}
        if status==200:
            value.update(id='fixture',model=body['model'],usage={'prompt_tokens':19000 if self.mode in ('shrink','quality','wait') and not summary else 2000,'completion_tokens':50})
        raw=json.dumps(value).encode();self.send_response(status)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)));self.send_header('Connection','close');self.end_headers();self.wfile.write(raw)

def main():
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group(required=True)
    g.add_argument('--host',type=Path);g.add_argument('--packed',type=Path);p.add_argument('--record',type=Path);p.add_argument('--case',action='append');a=p.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',0),Model);threading.Thread(target=server.serve_forever,daemon=True).start();records=[]
    try:
      with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='compaction-governance-') as raw:
        base=Path(raw);home=base/'home';workspace=base/'workspace';workspace.mkdir();port=free_port()
        config=base/'xs.json' if a.packed else ROOT/'app'/('.context-qa-'+uuid.uuid4().hex+'.json')
        config.write_text(json.dumps({'engine':{'workers':2},'services':[{'enabled':True,'class':'http','name':'context-qa','ip':'127.0.0.1','port':port,'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}),encoding='utf-8')
        if a.packed:
            exe=base/'mdo.exe';shutil.copyfile(a.packed.resolve(),exe);command=[str(exe)];directory=base
            if os.name!='nt':exe.chmod(0o700)
        else:command=[str(a.host.resolve()),str(config),'--','--home',str(home)];directory=ROOT/'app'
        with (base/'runtime.log').open('wb') as log:
          child=subprocess.Popen(command,cwd=directory,env=dict(os.environ,MDO_HOME=str(home),USE_WEBVIEW='0',MDO_CONTEXT_FIXTURE_KEY='fixture-only'),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
          try:
            wait_ready(port,child)
            def call(method,path,body=None,headers=None,status=200):
                actual,fields,data=request(port,method,'/api/v1'+path,body=json.dumps(body).encode() if body is not None else None,headers={'Content-Type':'application/json',**(headers or {})})
                assert actual==status,(path,actual,data[:300]);return json.loads(data)['data'],fields
            def wait(run):
                until=time.monotonic()+20
                while time.monotonic()<until:
                    data,_=call('GET','/runs/'+run['id'])
                    if data['terminal']:return data
                    time.sleep(.03)
                raise AssertionError(('timeout',Model.mode,Model.records,[(e['kind'],e.get('model_error_kind'),e.get('text'),e.get('model_http_status')) for e in session_events(port,'/api/v1'+path) if e['kind'] in ('error','model_retry','compaction_start','compaction_done','tool_end')]))
            settings,fields=call('GET','/models/config')
            provider=json.loads(json.dumps(conversation_catalog()['providers'][0]));provider.update(id='context-qa',name='fixture',builtin=False,editable=True,removable=True,endpoints={'chat_completions':f'http://127.0.0.1:{server.server_port}/v1'},credential={'secret_ref':'env:MDO_CONTEXT_FIXTURE_KEY'})
            model=json.loads(json.dumps(conversation_catalog()['items'][0]));model.update(id='context-qa',name='fixture',provider='context-qa',builtin=False,free=False,editable=True,removable=True,protocols=['openai-chat-completions'],default_protocol='openai-chat-completions')
            model['window'].update(context_tokens=32768,max_input_tokens=32768,max_output_tokens=1024,output_reserve_tokens=1024,summary_tokens=1024)
            settings['providers'].append(provider);settings['items'].append(model);settings.pop('runtime_override',None)
            call('PUT','/settings/models',{'schema_version':1,'patch':settings},{'If-Match':fields['etag']})
            call('POST','/projects',{'id':'context-qa','name':'Context QA','workspace_root':str(workspace),'default_model_id':'context-qa'},status=201)
            for mode in (a.case or ('overflow','overflow413','overflow1261','shrink','quality','wait')):
                Model.mode=mode;Model.normal=Model.summaries=0;Model.records=[]
                session,_=call('POST','/sessions',{'project_id':'context-qa','title':mode,'model_id':'context-qa','reasoning_effort':'none','permission_profile':'full-access','max_output_tokens':1024},status=201)
                path='/projects/context-qa/sessions/'+session['id']
                for i in range(8 if mode=='shrink' else 1):
                    run,_=call('POST',path+'/runs',{'prompt':'Seed completed history. '+('history-note '*180 if mode=='shrink' else '')},status=202)
                    assert wait(run)['state']=='succeeded'
                prompt='Continue with exactly one write.' if mode.startswith('overflow') else 'Continue. '+'history-note '*1300
                run,_=call('POST',path+'/runs',{'prompt':prompt},status=202)
                if mode=='wait':
                    until=time.monotonic()+12
                    while not any(e['kind']=='model_retry' for e in session_events(port,'/api/v1'+path)):
                        assert time.monotonic()<until,(mode,Model.records)
                        time.sleep(.04)
                    state,_=call('GET','/runs/'+run['id']);assert not state['terminal'],state
                    started=time.monotonic();call('DELETE','/runs/'+run['id']);assert wait(run)['state']=='cancelled';assert time.monotonic()-started<2
                else:
                    result=wait(run);assert result['state']=='succeeded',(mode,result,Model.records)
                events=session_events(port,'/api/v1'+path)
                assert not any(e['kind']=='error' for e in events),(mode,events)
                assert Model.summaries>0,(mode,Model.records)
                if mode.startswith('overflow'):
                    writes=[e for e in events if e['kind']=='tool_done' and e.get('tool_name')=='write']
                    assert len(writes)==1,(mode,[(e['kind'],e.get('tool')) for e in events])
                    assert (workspace/'completed.txt').read_text()=='written once'
                    assert Model.normal==4,(mode,Model.records)
                if mode=='shrink':
                    sizes=[r['bytes'] for r in Model.records if r['summary']]
                    assert len(sizes)>1 and all(b<a for a,b in zip(sizes,sizes[1:])),sizes
                if mode=='quality':assert any(r['correction'] and r['bytes']<30000 for r in Model.records if r['summary']),Model.records
                records.append({'mode':mode,'normal_calls':Model.normal,'summary_calls':Model.summaries,'retry_notices':sum(e['kind']=='model_retry' for e in events),'passed':True})
            result={'passed':True,'cases':records,'runtime_sha256':hashlib.sha256((a.packed or a.host).read_bytes()).hexdigest(),
                'sdk_overlay_sha256':json.loads((ROOT/'deps.lock').read_text())['sdk_overlay']['sha256']}
            if a.record:a.record.parent.mkdir(parents=True,exist_ok=True);a.record.write_text(json.dumps(result,indent=2)+'\n')
            print(json.dumps(result))
          except Exception:
            print((base/'runtime.log').read_text(errors='replace')[-6000:]);raise
          finally:
            child.terminate()
            try:child.wait(8)
            except subprocess.TimeoutExpired:child.kill();child.wait(5)
            if not a.packed:config.unlink(missing_ok=True)
    finally:server.shutdown();server.server_close()
if __name__=='__main__':main()
