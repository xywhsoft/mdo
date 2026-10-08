"""Small serial context-compaction probe against the packed native runtime.

Five conversation turns, one summary rate-limit, one deliberately rejected
summary, then a valid summary. Restart the runtime and continue once more.
Only synthetic loopback data is used.
"""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
from test_api_runtime import ROOT, free_port, request, session_events, wait_ready

SUMMARY="""## Objective
Preserve PIXEL_KEEP: dimensions32 palette8 output pixels.png. Continue the pixel editor fixture.
## Constraints
Only synthetic files and loopback services are used. Keep completed history and never replay writes.
## Architecture and decisions
The fixture uses a thirty two by thirty two canvas and eight palette colors, saved as pixels.png.
## Completed work
Previous user turns and completed replies are preserved in the portable conversation history.
## Current repository state
No files have been modified by this fixture. The editor implementation remains an open task.
## Verification evidence
The serial native conversation completed the prior replies before this compaction began.
## Open issues and risks
There are no pending tool calls. No production model, private account or external site was used.
## Exact next actions
Continue the user's newest request and retain the exact PIXEL_KEEP facts above.
"""
CODING_SUMMARY="""## Goal
Preserve PIXEL_KEEP: dimensions32 palette8 output pixels.png. Continue the pixel editor fixture.
## Constraints & Preferences
Only synthetic files and loopback services are used. Keep completed history and never replay writes.
## Progress
Prior user turns and replies completed. No editor implementation or file modifications have been made.
## Key Decisions
Use a thirty two by thirty two canvas and eight palette colors; retain the output filename pixels.png.
## Next Steps
Continue the user's latest request while retaining the original facts and completed work.
## Critical Context
PIXEL_KEEP: dimensions32 palette8 output pixels.png. No tools or production credentials are involved.
"""

class Model(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    calls=Counter()
    requests=[]
    def log_message(self,*_):pass
    def handle(self):
        try:super().handle()
        except (BrokenPipeError,ConnectionResetError):pass
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        summary=body['messages'][0]['content'].startswith('Create a precise continuation summary')
        kind='summary' if summary else 'normal';self.calls[kind]+=1
        self.requests.append({'kind':kind,'message_bytes':len(json.dumps(body['messages'])),
            'has_marker':'PIXEL_KEEP' in json.dumps(body['messages']),
            'tool_count':len(body.get('tools',[])),'max_tokens':body.get('max_tokens')})
        if summary and self.calls[kind]==1:
            value={'error':{'code':'rate_limit_exceeded','message':'Fixture summary retry'}};status=429
        else:
            # Obey the higher-priority system format when it conflicts with
            # the user-side style; this reproduces the original product bug.
            generated=SUMMARY if 'Return exactly these populated headings: Objective' in body['messages'][0]['content'] else CODING_SUMMARY
            text='too short' if summary and self.calls[kind]==2 else generated if summary else 'Reply complete; PIXEL_KEEP: dimensions32 palette8 output pixels.png.'
            value={'id':'context-fixture','model':body['model'],'choices':[{'index':0,'message':{'role':'assistant','content':text},'finish_reason':'stop'}],
                'usage':{'prompt_tokens':max(1,len(json.dumps(body['messages']))//4),'completion_tokens':max(1,len(text)//4)}}
            status=200
        raw=json.dumps(value).encode();self.send_response(status)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)))
        self.send_header('Connection','close');self.end_headers();self.wfile.write(raw)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--packed',type=Path,required=True)
    parser.add_argument('--record',type=Path)
    parser.add_argument('--serve-ui',action='store_true',help='Keep the passing fixture open until its stop file is created')
    args=parser.parse_args()
    Model.calls=Counter();Model.requests=[]
    server=ThreadingHTTPServer(('127.0.0.1',0),Model)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='packed-context-',dir=ROOT/'.build') as raw:
            base=Path(raw);exe=base/'mdo.exe';shutil.copy2(args.packed.resolve(),exe);port=free_port()
            config={'services':[{'class':'http','enabled':True,'name':'context-qa','ip':'127.0.0.1','port':port,
                'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}
            (base/'xs.json').write_text(json.dumps(config));log=(base/'native.log').open('wb')
            process=subprocess.Popen([str(exe)],cwd=base,env=dict(os.environ,USE_WEBVIEW='0',MDO_HOME=str(base/'home'),
                MDO_CONTEXT_FIXTURE_KEY='fixture-only'),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                wait_ready(port,process)
                def call(method,path,body=None,headers=None,status=200):
                    actual,head,data=request(port,method,'/api/v1'+path,body=None if body is None else json.dumps(body).encode(),
                        headers={'Content-Type':'application/json',**(headers or {})})
                    assert actual==status,(path,actual,data[:500]);return json.loads(data)['data'],head
                settings,head=call('GET','/models/config')
                provider=json.loads(json.dumps(settings['providers'][0]));provider.update(id='context-fixture',name='Context fixture',
                    builtin=False,editable=True,removable=True,endpoints={'chat_completions':f'http://127.0.0.1:{server.server_port}/v1'},
                    credential={'secret_ref':'env:MDO_CONTEXT_FIXTURE_KEY'})
                model=json.loads(json.dumps(settings['items'][0]));model.update(id='context-fixture',name='Context fixture',
                    provider='context-fixture',builtin=False,free=False,editable=True,removable=True,
                    protocols=['openai-chat-completions'],default_protocol='openai-chat-completions')
                model['window'].update(context_tokens=16384,max_input_tokens=16384,max_output_tokens=1024,
                    output_reserve_tokens=1024,summary_tokens=1024)
                settings['providers'].append(provider);settings['items'].append(model);settings.pop('runtime_override',None)
                call('PUT','/settings/models',{'schema_version':1,'patch':settings},{'If-Match':head['etag']})
                session,_=call('POST','/sessions',{'project_id':'default','title':'Context recovery fixture',
                    'model_id':'context-fixture','reasoning_effort':'none','permission_profile':'read-only','max_output_tokens':1024},status=201)
                path='/projects/default/sessions/'+session['id'];runs=[]
                for index in range(5):
                    prompt=f'Turn {index+1}. PIXEL_KEEP: dimensions32 palette8 output pixels.png. '+('history-note '*1000)
                    run,_=call('POST',path+'/runs',{'prompt':prompt},status=202)
                    until=time.monotonic()+20
                    while time.monotonic()<until:
                        result,_=call('GET','/runs/'+str(run['id']))
                        if result['terminal']:break
                        time.sleep(.05)
                    if not result['terminal'] or result['state']!='succeeded':
                        failed_events=session_events(port,'/api/v1'+path)
                        raise AssertionError((result,Model.requests,[(e['kind'],e['text'][:200]) for e in failed_events if e['kind'] in ('error','compaction_start','compaction_rejected','compaction_done')]))
                    runs.append(result)
                # The persisted checkpoint must survive a process boundary.
                process.terminate();process.wait(timeout=10)
                process=subprocess.Popen([str(exe)],cwd=base,env=dict(os.environ,USE_WEBVIEW='0',MDO_HOME=str(base/'home'),
                    MDO_CONTEXT_FIXTURE_KEY='fixture-only'),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                wait_ready(port,process)
                run,_=call('POST',path+'/runs',{'prompt':'Continue after restart. Recall the original dimensions, palette and filename.'},status=202)
                until=time.monotonic()+20
                while time.monotonic()<until:
                    result,_=call('GET','/runs/'+str(run['id']))
                    if result['terminal']:break
                    time.sleep(.05)
                assert result['terminal'] and result['state']=='succeeded',result
                assert 'PIXEL_KEEP' in result['final_text'],result
                runs.append(result)
                events=session_events(port,'/api/v1'+path)
                record={'passed':False,'restarted':True,'calls':dict(Model.calls),'requests':Model.requests,
                    'run_compactions':[r['compactions'] for r in runs],
                    'events':[{'kind':e['kind'],'text':e['text'][:160]} for e in events if e['kind'] in
                        ('error','compaction_start','compaction_rejected','compaction_done','model_done')],
                    'final_errors':sum(e['kind']=='error' for e in events)}
                assert Model.calls['summary']>=3 and record['final_errors']==0,record
                assert any(r['compactions'] for r in runs),record
                assert sum(e['kind']=='compaction_rejected' for e in events)==1,record
                assert sum(e['kind']=='model_done' and e['success'] for e in events)==6,record
                assert all(r['has_marker'] for r in Model.requests) and all(r['tool_count']==0 for r in Model.requests if r['kind']=='summary'),record
                record['passed']=True
                if args.record:args.record.parent.mkdir(parents=True,exist_ok=True);args.record.write_text(json.dumps(record,indent=2)+'\n')
                print(json.dumps(record,indent=2),flush=True)
                if args.serve_ui:
                    stop=base/'stop'
                    print(json.dumps({'url':f'http://127.0.0.1:{port}/#{path}', 'stop_file':str(stop)}),flush=True)
                    while not stop.exists():
                        assert process.poll() is None,'Packed UI fixture exited'
                        time.sleep(.2)
            except Exception:
                print((base/'native.log').read_text(errors='replace'));raise
            finally:
                if process.poll() is None:process.terminate();process.wait(timeout=10)
                log.close()
    finally:
        server.shutdown();server.server_close();thread.join(timeout=3)

if __name__=='__main__':main()
