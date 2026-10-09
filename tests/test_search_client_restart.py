"""Finite OS process restart tests; local website, provider fixture and JWT only.

Kill one owned client after search acceptance but before response delivery.
Resume its actual session journal in a second process; count provider/quota,
HTTP bodies and final tool/UI results independently. No public service or load.
"""
from __future__ import annotations
import argparse
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

from test_search_xadmin_integration import run, web, WEBSITE_HOST
from test_api_runtime import free_port
from test_interrupt_runtime import stop_host


def scenario(ctx, baseline=False):
    ctx['policy'](default_provider='bocha',max_concurrent=4)
    records={}; accepted=threading.Event(); release=threading.Event()

    class Handler(BaseHTTPRequestHandler):
        mode=''; requests=[]; initial=True
        def log_message(self,*_):pass
        def handle(self):
            try:super().handle()
            except ConnectionResetError:pass  # Owned client exits with an idle connection.
        def reply(self,status,body):
            self.send_response(status);self.send_header('Content-Type','application/json')
            self.send_header('Content-Length',str(len(body)));self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError):pass
        def do_POST(self):
            assert self.path in ('/api/v1/search/requests','/api/v1/search'),self.path
            body=self.rfile.read(int(self.headers['Content-Length']))
            self.requests.append((self.path,body))
            if self.mode=='legacy' and self.path.endswith('/requests'):
                self.reply(404,b'{"code":404}');return
            first=Handler.initial;Handler.initial=False
            if not (first and self.mode=='before_submission'):
                conn=http.client.HTTPConnection('127.0.0.1',ctx['port'],timeout=10)
                try:
                    conn.request('POST',self.path,body,{'Authorization':'Bearer '+ctx['token'],
                        'Content-Type':'application/json'})
                    response=conn.getresponse();status=response.status;raw=response.read()
                    assert status==200,(status,raw)
                finally:conn.close()
            else:status=0;raw=b''
            if first:
                accepted.set();assert release.wait(10),'fixture did not release accepted request'
                # Drop only after the parent has terminated its owned client.
                self.close_connection=True;return
            self.reply(status,raw)

    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        # Long managed checkout names can exceed Python's legacy Windows path
        # limit when the 64-byte record key is appended. The owned temporary
        # fixture is disposable and must also work from those clean checkouts.
        with tempfile.TemporaryDirectory(prefix='mdo-search-restart-') as raw:
            base=Path(raw);site=base/'site';workspace=base/'project';workspace.mkdir()
            shutil.copytree(web.ROOT/'app',site)
            if baseline:
                old=subprocess.check_output(['git','show','66d65df5e8eb94a6ef616161eb75bc14fc8eab5f:app/src/web/search_api.inc.c'],cwd=web.ROOT)
                (site/'src/web/search_api.inc.c').write_bytes(old)
            shutil.copy2(web.ROOT/'tests/fixtures/search-restart-session.c',site/'restart.c')
            (site/'probe.c').write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(f'http://127.0.0.1:{server.server_port}')+'\n'
                '#define RESTART_FIXTURE_WORKSPACE '+json.dumps(str(workspace.resolve()))+'\n'
                '#define ServiceInit SearchRestartProductInit\n#include "generated/mdo_unity.c"\n#undef ServiceInit\n'
                '#include "restart.c"\n',encoding='utf-8')
            for mode in (('lost',) if baseline else ('lost','before_submission','legacy','account_changed','expired','future_clock','corrupt')):
                Handler.mode=mode;Handler.requests=[];Handler.initial=True;accepted.clear();release.clear()
                before=ctx['state']()['calls'];quota=ctx['used']();home=base/mode
                port=free_port();config=site/'xs.json'
                config.write_text(json.dumps({'engine':{'workers':1},'services':[{'enabled':True,
                    'class':'http','name':'restart','ip':'127.0.0.1','port':port,'host_default':{
                    'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'probe.c'}}]}))
                env=os.environ.copy();env['RESTART_FIXTURE_TOKEN']=ctx['token'];env['RESTART_FIXTURE_MEMBER']=str(ctx['member_id'])
                env.pop('RESTART_FIXTURE_RESUME',None)
                command=[str(ctx['host'].resolve()),str(config),'--','--home',str(home)]
                log=base/(mode+'.log');process=None
                def launch():
                    with log.open('ab') as output:
                        return subprocess.Popen(command,cwd=site,env=env,stdout=output,stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                try:
                    process=launch();end=time.monotonic()+30
                    while not accepted.wait(.1):
                        assert process.poll() is None and time.monotonic()<end,log.read_text(encoding='utf-8',errors='replace')
                    session=home/'sessions/default/search-restart-session'
                    tokens=list((session/'artifacts/requests').glob('*.json'))
                    assert len(tokens)==int(not baseline),tokens
                    token=json.loads(tokens[0].read_text(encoding='utf-8')) if tokens else None
                    original=json.loads(Handler.requests[0][1])['request_id']
                    if token:assert token['id']==original,token
                    stop_host(process);process=None;release.set()
                    assert not any(e['kind']==6 for e in map(json.loads,(session/'ui-events.jsonl').read_text(encoding='utf-8').splitlines()))
                    assert not list((session/'artifacts/completed').glob('*.json'))
                    if mode=='account_changed':env['RESTART_FIXTURE_MEMBER']=str(ctx['member_id']+1000)
                    elif mode=='expired':
                        token['created_at']=int(time.time())-86401;tokens[0].write_text(json.dumps(token),encoding='utf-8')
                    elif mode=='future_clock':
                        token['created_at']=int(time.time())+600;tokens[0].write_text(json.dumps(token),encoding='utf-8')
                    elif mode=='corrupt':tokens[0].write_text('damaged',encoding='utf-8')
                    env['RESTART_FIXTURE_RESUME']='1';process=launch();end=time.monotonic()+30
                    while 'restart_fixture_done=1' not in log.read_text(encoding='utf-8',errors='replace'):
                        assert process.poll() is None and time.monotonic()<end,log.read_text(encoding='utf-8',errors='replace')
                        time.sleep(.1)
                    output=log.read_text(encoding='utf-8',errors='replace')
                    success=mode in ('lost','before_submission')
                    events=[json.loads(line) for line in (session/'ui-events.jsonl').read_text(encoding='utf-8').splitlines()]
                    assert f'tool_seen:1 tool_success:{int(success)} model_calls:1 error:0:' in output,(output,Handler.requests,events[-5:])
                    assert output.count('restart_user_messages=1')==2 and 'restart_user_messages=2' not in output,output
                    assert 'test-bocha-secret' not in output and ctx['token'] not in output,output
                    if mode=='legacy':assert 'Search API was not found' in output,output
                    if mode=='account_changed':assert 'another service or account' in output,output
                    if mode in ('expired','future_clock'):assert 'safe recovery period' in output,output
                    if mode=='corrupt':assert 'damaged or unavailable' in output,output
                    completed=[e for e in events if e['kind']==6]
                    assert len(completed)==1 and completed[0]['success'] is success,completed
                    assert any(e['kind']==10 and e['success'] for e in events),events[-5:]
                    artifacts=list((session/'artifacts').rglob('*.md'))
                    assert len(artifacts)==int(success),artifacts
                    assert len(list((session/'artifacts/completed').glob('*.json')))==1
                    posts=Handler.requests
                    assert len(posts)==(3 if mode=='legacy' else 2 if success else 1),posts
                    if mode=='legacy':
                        assert [p for p,_ in posts]==['/api/v1/search/requests','/api/v1/search','/api/v1/search/requests']
                        assert posts[0][1]==posts[2][1],posts
                    elif baseline:assert json.loads(posts[1][1])['request_id']!=original,posts
                    else:assert all(json.loads(b)['request_id']==original for _,b in posts)
                    if success and not baseline:assert posts[0][1]==posts[1][1],posts
                    dispatches=2 if baseline else 1
                    assert ctx['state']()['calls']==before+dispatches and ctx['used']()==quota+dispatches,(mode,ctx['state'](),ctx['used'](),quota)
                    records[mode]={'actual_client_processes':2,'http_requests':len(posts),'provider_dispatches':dispatches,
                        'quota':dispatches,'tool_success':success,'tool_completions':1,'agent_completed':True,
                        'same_id':not baseline,'user_prompt_preserved_once':True,
                        'artifacts':len(artifacts),'body_sha256':hashlib.sha256(posts[0][1]).hexdigest()}
                finally:
                    stop_host(process);release.set()
        return records
    finally:
        release.set();server.shutdown();server.server_close();thread.join(2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--baseline',action='store_true',help='Reproduce duplicate debit with the prior search callback, in the disposable source only')
    args=parser.parse_args();records=run(args.host,args.website_host,lambda ctx:scenario(ctx,args.baseline))
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    print('PASS baseline reproduced duplicate debit after actual client restart' if args.baseline else
        'PASS actual client restart: retained search ID, single debit, guarded legacy/account/clock/expiry/corruption',records)
