"""Offline packed UI: native search -> fault proxy -> unified website receipts.

Send search-lost, search-retry, search-quota or search-restart, then continue.
The restart case holds a saved result until the owned host is restarted via the
printed restart marker. Only the disposable pack overrides the account origin
and injects a local test JWT. Product HTTP/model/tool/UI code is unchanged.
The fixture lasts at most ten minutes, within the disposable JWT's lifetime.
"""
from __future__ import annotations
import argparse
from collections import Counter
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import threading
import time

from test_search_xadmin_integration import run, web, WEBSITE_HOST
from test_api_runtime import free_port, request, session_events, wait_ready
from test_packed_queue_start_recovery import configure_model

SEED = r'''
void ServiceInit(XS_HostInfo* Host)
{
    SearchUiProductInit(Host);
    const char* Token=getenv("MDO_SEARCH_UI_TOKEN");
    const char* Member=getenv("MDO_SEARCH_UI_MEMBER");
    if (!Token || !Member || strlen(Token)>=sizeof(g_MdoAccount.Tokens.Access)) return;
    xrtMutexLock(g_MdoAccount.Lock);
    MdoAccountNewSessionLocked();
    strcpy(g_MdoAccount.Tokens.Access,Token);
    g_MdoAccount.Tokens.Expires=xrtDeadlineAfter(900000000u);
    g_MdoAccount.Tokens.MemberId=(uint64)atoi(Member);
    (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_PROFILE);
    xrtMutexUnlock(g_MdoAccount.Lock);
    (void)MdoWebManagerReload();
}
'''


def scenario(ctx,args):
    ctx['policy'](default_provider='bocha',max_concurrent=4)
    base=args.directory.resolve()
    if base.exists():raise RuntimeError('Choose a new owned fixture directory; existing data is retained')
    base.mkdir(parents=True);requests=[];model_calls=Counter();model_messages=[]
    search_calls=Counter();holding=threading.Event();release=threading.Event();shutdown=threading.Event()

    class Model(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_POST(self):
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            messages=body['messages'];last=max(i for i,m in enumerate(messages) if m['role']=='user')
            prompt=messages[last]['content']
            if isinstance(prompt,list):prompt=''.join(p.get('text','') for p in prompt if p.get('type')=='text')
            model_calls[prompt]+=1;tail=[m for m in messages[last+1:] if m['role']=='tool']
            model_messages.append({'prompt':prompt,'user_messages':sum(m['role']=='user' for m in messages),
                'tool_results':len(tail),'tools':[t['function']['name'] for t in body.get('tools',[])]})
            message={'role':'assistant','content':'SEARCH_UI_OK: '+prompt};finish='stop'
            if prompt.startswith('search-') and not tail:
                assert model_calls[prompt]==1,'model must not replay a searched prompt'
                assert 'web_search' in model_messages[-1]['tools'],model_messages[-1]
                message={'role':'assistant','content':'','tool_calls':[{'id':'ui-'+prompt,'type':'function',
                    'function':{'name':'web_search','arguments':json.dumps({'query':prompt,'count':2})}}]}
                finish='tool_calls'
            elif prompt=='search-quota':message['content']='今日搜索额度不足，请在额度重置后再试。可以继续其他任务。'
            raw=json.dumps({'id':'search-ui-fixture','model':body['model'],'choices':[{'index':0,
                'message':message,'finish_reason':finish}],'usage':{'prompt_tokens':20,'completion_tokens':10}},ensure_ascii=False).encode()
            self.send_response(200);self.send_header('Content-Type','application/json')
            self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)

    class Search(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def handle(self):
            try:super().handle()
            except (ConnectionResetError,BrokenPipeError):pass
        def reply(self,status,body,headers=None):
            self.send_response(status)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)))
            if headers and 'Retry-After' in headers:self.send_header('Retry-After',headers['Retry-After'])
            self.end_headers();self.wfile.write(body)
        def forward(self,body=None):
            conn=http.client.HTTPConnection('127.0.0.1',ctx['port'],timeout=10)
            try:
                conn.request(self.command,self.path,body,{'Authorization':'Bearer '+ctx['token'],'Content-Type':'application/json'})
                response=conn.getresponse();return response.status,response.read(),dict(response.getheaders())
            finally:conn.close()
        def do_GET(self):self.reply(*self.forward())
        def do_POST(self):
            assert self.path=='/api/v1/search/requests',self.path
            raw=self.rfile.read(int(self.headers['Content-Length']));body=json.loads(raw);query=body['query']
            search_calls[query]+=1;attempt=search_calls[query]
            requests.append({'query':query,'id':body['request_id'],'attempt':attempt,'body':raw.decode()})
            if query=='search-quota' or (query=='search-retry' and attempt<=2):
                reason='daily_limit' if query=='search-quota' else 'server_busy'
                reply={'code':429,'data':{'error':{'code':reason,'retry_safe':reason!='daily_limit',
                    'dispatched':False,'retry_after_ms':86400000 if reason=='daily_limit' else 1000}}}
                self.reply(429,json.dumps(reply).encode());return
            status,result,headers=self.forward(raw)
            if query=='search-restart' and attempt==1:
                holding.set();deadline=time.monotonic()+120
                while not release.wait(.05) and not shutdown.is_set() and time.monotonic()<deadline:pass
                self.close_connection=True;return
            if query=='search-lost' and attempt==1:
                self.connection.shutdown(socket.SHUT_RDWR);self.close_connection=True;return
            self.reply(status,result,headers)

    model=ThreadingHTTPServer(('127.0.0.1',0),Model);search=ThreadingHTTPServer(('127.0.0.1',0),Search)
    threads=[]
    for server in (model,search):
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();threads.append(thread)
    process=None;log=None;sessions=[];lifecycle=[];provider_start=ctx['state']()['calls'];quota_start=ctx['used']()
    try:
        app=base/'app';shutil.copytree(web.ROOT/'app',app)
        entry=app/'generated/mdo_unity.c';source=entry.read_text(encoding='utf-8')
        entry.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(f'http://127.0.0.1:{search.server_port}')+'\n'
            '#define ServiceInit SearchUiProductInit\n'+source+'\n#undef ServiceInit\n'+SEED,encoding='utf-8')
        exe=base/'mdo.exe';subprocess.run([str(ctx['host'].resolve()),'pack',str(app),'-o',str(exe)],check=True,capture_output=True)
        port=free_port();config=json.loads((app/'xs.json').read_text(encoding='utf-8'))
        config['services'][0].update(**{'class':'http','ip':'127.0.0.1','port':port})
        config['services'][0]['host_default']['devfile']='generated/mdo_unity.c'
        (base/'xs.json').write_text(json.dumps(config),encoding='utf-8')
        env=dict(os.environ,USE_WEBVIEW='0',MDO_HOME=str(base/'home'),MDO_QUEUE_FIXTURE_KEY='fixture-only',
            MDO_SEARCH_UI_TOKEN=ctx['token'],MDO_SEARCH_UI_MEMBER=str(ctx['member_id']))
        log=(base/'host.log').open('wb')
        def launch():
            return subprocess.Popen([str(exe)],cwd=base,env=env,stdout=log,stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        def stop():
            if process and process.poll() is None:
                process.terminate()
                try:process.wait(5)
                except subprocess.TimeoutExpired:process.kill();process.wait(5)
        process=launch();wait_ready(port,process)
        configure_model(port,f'http://127.0.0.1:{model.server_port}/v1')
        for mode in ('automatic','restart'):
            status,_,raw=request(port,'POST','/api/v1/sessions',body=json.dumps({'project_id':'default',
                'title':'搜索恢复界面 '+mode,'model_id':'queue-fixture','reasoning_effort':'none',
                'permission_profile':'full-access'}).encode(),headers={'Content-Type':'application/json'})
            assert status==201,raw;sessions.append(json.loads(raw)['data']['id'])
        state={'url':f'http://127.0.0.1:{port}/#/projects/default/sessions/{sessions[0]}',
            'restart_url':f'http://127.0.0.1:{port}/#/projects/default/sessions/{sessions[1]}',
            'pid':process.pid,'port':port,'sessions':sessions,'restart_marker':str(base/'restart'),'stop_marker':str(base/'stop')}
        (base/'state.json').write_text(json.dumps(state,indent=2),encoding='utf-8');print(json.dumps(state),flush=True)
        end=time.monotonic()+args.duration
        while not (base/'stop').exists() and time.monotonic()<end and process.poll() is None:
            proof={'model_calls':model_calls,'model_requests':model_messages,
                'search_calls':search_calls,'search_requests':requests,'holding':holding.is_set(),
                'provider_dispatches':ctx['state']()['calls']-provider_start,'quota':ctx['used']()-quota_start,
                'lifecycle':lifecycle}
            temporary=base/'proof.tmp'
            temporary.write_text(json.dumps(proof,indent=2),encoding='utf-8')
            temporary.replace(base/'proof.json')
            if (base/'restart').exists():
                (base/'restart').unlink();stop();release.set();lifecycle.append({'event':'stopped','pid':process.pid})
                time.sleep(2);process=launch();wait_ready(port,process);lifecycle.append({'event':'restarted','pid':process.pid})
                state['pid']=process.pid;(base/'state.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
            time.sleep(1)
        if process.poll() is None:
            for index,session in enumerate(sessions):
                events=session_events(port,'/api/v1/projects/default/sessions/'+session)
                (base/f'events-{index}.json').write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
        return {'model_calls':model_calls,'search_calls':search_calls,'provider_dispatches':ctx['state']()['calls']-provider_start,
            'quota':ctx['used']()-quota_start,'sessions':sessions,'lifecycle':lifecycle}
    finally:
        shutdown.set();release.set()
        if process and process.poll() is None:
            process.terminate()
            try:process.wait(5)
            except subprocess.TimeoutExpired:process.kill();process.wait(5)
        if log:log.close()
        for server in (model,search):server.shutdown();server.server_close()
        for thread in threads:thread.join(2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--duration',type=int,default=600)
    args=parser.parse_args()
    if not 30<=args.duration<=600:parser.error('--duration must be between 30 and 600 seconds')
    print(run(args.host,args.website_host,lambda ctx:scenario(ctx,args)),flush=True)
