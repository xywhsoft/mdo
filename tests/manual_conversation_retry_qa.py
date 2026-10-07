"""Disposable packed-app fixture for browser retry, draft and stop verification.

Run this alongside a browser pointed at the printed URL. Model requests never
leave loopback; the fixture uses a normal editable model and a separate Home.
Prompts: retry (two 429s), partial (one interrupted draft), quota (daily limit),
stop (long Retry-After), continue (successful next turn). The editable fixture
model accepts images, so attachment/edit/fork checks use the same environment.
With --permission balanced, decisions updates a todo, asks a question, then
attempts a write outside its workspace (still inside the disposable directory)
and corrects the rejected path. Both writes require normal one-shot approval.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_api_runtime import ROOT, free_port, request, wait_ready


class Model(BaseHTTPRequestHandler):
    calls = {}
    markers = []
    lock = threading.Lock()
    workspace = None

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        prompt = next((row.get('content','') for row in reversed(body['messages'])
            if row['role']=='user'), '')
        if isinstance(prompt,list):
            prompt=''.join(part.get('text','') for part in prompt if part.get('type')=='text')
        images=sum(part.get('type')=='image_url' for row in body['messages']
            if isinstance(row.get('content'),list) for part in row['content'])
        with self.lock:
            attempt = self.calls[prompt] = self.calls.get(prompt,0)+1
            self.markers.append({'prompt':prompt,'image_parts':images,
                'user_messages':sum(row['role']=='user' for row in body['messages']),
                'tool_results':sum(row['role']=='tool' for row in body['messages'])})
        if prompt in ('quota','stop') or (prompt=='retry' and attempt<=2):
            self.send_response(429)
            self.send_header('Content-Type','application/json')
            self.send_header('Retry-After','10' if prompt=='stop' else '1')
            error = {'error':{'code':'daily_token_limit' if prompt=='quota'
                else 'rate_limit_exceeded','message':'fixture provider prose'}}
            raw = json.dumps(error).encode()
            self.send_header('Content-Length',str(len(raw)))
            self.end_headers(); self.wfile.write(raw)
            return
        partial = prompt=='partial' and attempt==1
        text = 'DISCARDED_FIXTURE_DRAFT' if partial else 'FIXTURE_OK: '+prompt
        self.send_response(200)
        self.send_header('Content-Type','text/event-stream')
        self.end_headers()
        delta = {'role':'assistant','content':text}
        finish = 'stop'
        if prompt == 'decisions' and attempt <= 6:
            name, arguments = [
                ('mdo.todo', {'items':[
                    {'text':'Inspect the fixture', 'done':True},
                    {'text':'Verify the question and approval', 'done':False}]}),
                ('ask_user', {'question':'Which fixture route should be checked?',
                    'options':['Inspect locally','Continue carefully']}),
                ('write', {'path':str(self.workspace/'approval-proof.txt'),
                    'content':'mdo decision fixture\n'}),
                ('write', {'path':'approval-proof.txt',
                    'content':'mdo decision fixture\n'}),
                ('exec', {'argv':[sys.executable,'-c',
                    "from pathlib import Path; "
                    "assert Path('approval-proof.txt').read_text() == 'mdo decision fixture\\n'; "
                    "print('FIXTURE_VERIFIED')"], 'timeout_ms':5000}),
                ('mdo.todo', {'items':[
                    {'text':'Inspect the fixture', 'done':True},
                    {'text':'Verify the question and approval', 'done':True}]}),
            ][attempt-1]
            delta = {'role':'assistant','tool_calls':[{'index':0,
                'id':f'fixture-decision-{attempt}','type':'function',
                'function':{'name':name,'arguments':json.dumps(arguments)}}]}
            finish = 'tool_calls'
        data = {'id':'fixture-response','model':'conversation-fixture',
            'choices':[{'index':0,'delta':delta}]}
        self.wfile.write(('data: '+json.dumps(data)+'\n\n').encode())
        self.wfile.flush()
        if partial:
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        done = {'choices':[{'index':0,'delta':{},'finish_reason':finish}],
            'usage':{'prompt_tokens':10,'completion_tokens':4,'total_tokens':14}}
        self.wfile.write(('data: '+json.dumps(done)+'\n\ndata: [DONE]\n\n').encode())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed',type=Path,required=True)
    parser.add_argument('--directory',type=Path,default=ROOT/'.build/conversation-retry-qa')
    parser.add_argument('--duration',type=int,default=1200)
    parser.add_argument('--permission',choices=['read-only','balanced'],default='read-only')
    args=parser.parse_args()
    base=args.directory.resolve()
    if base.exists():
        raise RuntimeError('Choose a new disposable directory; existing data is retained')
    base.mkdir(parents=True)
    Model.workspace = base
    exe=base/'mdo.exe';shutil.copy2(args.packed.resolve(),exe)
    model=ThreadingHTTPServer(('127.0.0.1',0),Model)
    thread=threading.Thread(target=model.serve_forever,daemon=True);thread.start()
    port=free_port()
    config={'services':[{'class':'http','enabled':True,'name':'retry-qa',
        'ip':'127.0.0.1','port':port,'host_default':{'enabled':True,'name':'mdo',
            'path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}
    (base/'xs.json').write_text(json.dumps(config),encoding='utf-8')
    stop=base/'stop'; log=(base/'host.log').open('wb')
    env=dict(os.environ,USE_WEBVIEW='0',MDO_HOME=str(base/'home'),
        MDO_CONVERSATION_FIXTURE_KEY='loopback-fixture-key')
    process=subprocess.Popen([str(exe)],cwd=base,env=env,stdout=log,stderr=log,
        creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    try:
        wait_ready(port,process)
        status,headers,raw=request(port,'GET','/api/v1/models/config')
        assert status==200,raw
        config=json.loads(raw)['data'];config.pop('runtime_override',None)
        provider=json.loads(json.dumps(config['providers'][0]))
        provider.update(id='conversation-fixture',name='Conversation fixture',
            builtin=False,editable=True,removable=True,
            endpoints={'chat_completions':f'http://127.0.0.1:{model.server_port}/v1'},
            credential={'secret_ref':'env:MDO_CONVERSATION_FIXTURE_KEY'})
        item=json.loads(json.dumps(config['items'][0]))
        item.update(id='conversation-fixture',name='Conversation fixture',
            provider='conversation-fixture',builtin=False,free=False,editable=True,
            removable=True,protocols=['openai-chat-completions'],
            default_protocol='openai-chat-completions')
        if 'media-input' not in item['capabilities']:
            item['capabilities'].append('media-input')
        item['attachments']=['image']
        config['providers'].append(provider);config['items'].append(item)
        status,_,raw=request(port,'PUT','/api/v1/settings/models',
            body=json.dumps({'schema_version':1,'patch':config}).encode(),
            headers={'Content-Type':'application/json','If-Match':headers['etag']})
        assert status==200,raw
        status,_,raw=request(port,'POST','/api/v1/sessions',
            body=json.dumps({'project_id':'default','title':'对话恢复浏览器验收',
                'model_id':'conversation-fixture','reasoning_effort':'none',
                'permission_profile':args.permission,'max_output_tokens':2048}).encode(),
            headers={'Content-Type':'application/json'})
        assert status==201,raw
        session=json.loads(raw)['data']['id']
        url=f'http://127.0.0.1:{port}/#/projects/default/sessions/{session}'
        (base/'state.json').write_text(json.dumps({'port':port,'session':session,
            'pid':process.pid,'url':url},indent=2),encoding='utf-8')
        print(url,flush=True)
        end=time.monotonic()+max(1,min(args.duration,3600))
        while not stop.exists() and time.monotonic()<end and process.poll() is None:
            time.sleep(.2)
    finally:
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill();process.wait()
        log.close();model.shutdown();model.server_close();thread.join(timeout=2)
        (base/'calls.json').write_text(json.dumps(Model.calls,indent=2),encoding='utf-8')
        (base/'requests.json').write_text(json.dumps(Model.markers,indent=2),encoding='utf-8')


if __name__=='__main__':
    main()
