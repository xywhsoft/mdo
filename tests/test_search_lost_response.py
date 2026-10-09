"""Finite native mdo -> loopback fault proxy -> real unified-plugin receipts.

Each case is one tool execution. Dropped/truncated responses and one in-flight
duplicate must recover the same paid search, not submit a second provider call.
No public network, production credentials or load testing is involved.
"""
from __future__ import annotations
import argparse
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import socket
import threading
import time

from test_search_xadmin_integration import run, invoke, web, WEBSITE_HOST


def scenario(ctx):
    ctx['policy'](default_provider='bocha',max_concurrent=4)
    state,used=ctx['state'],ctx['used']
    records={}; workers=[]; lock=threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        mode=''; requests=[]; restarted_dispatches=0
        def log_message(self,*_):pass
        def reply(self,status,body):
            self.send_response(status)
            self.send_header('Content-Type','application/json')
            self.send_header('Content-Length',str(len(body)))
            self.end_headers();self.wfile.write(body)
        def drop(self):
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close();self.close_connection=True
        def forward(self,body,path):
            conn=http.client.HTTPConnection('127.0.0.1',ctx['port'],timeout=10)
            try:
                conn.request('POST',path,body,{'Authorization':'Bearer '+ctx['token'],
                    'Content-Type':'application/json','Accept':'application/json'})
                response=conn.getresponse();return response.status,response.read()
            finally:conn.close()
        def do_POST(self):
            assert self.path in ('/api/v1/search/requests','/api/v1/search'),self.path
            assert self.headers['Authorization']=='Bearer '+ctx['token']
            body=self.rfile.read(int(self.headers['Content-Length']))
            args=json.loads(body)
            with lock:
                self.requests.append((self.path,body));attempt=len(self.requests)
            if self.path.endswith('/requests'):
                assert re.fullmatch('[0-9a-f]{32}',args['request_id'])
            else:assert 'request_id' not in args
            if self.mode=='pending_mismatch':
                self.reply(429,json.dumps({'code':429,'data':{'request_id':'0'*32,'error':{
                    'code':'request_pending','retry_safe':True,'dispatched':True,'retry_after_ms':100}}}).encode());return
            if self.mode=='fallback_limit':
                self.reply(404 if attempt==6 else 503,json.dumps({'code':404 if attempt==6 else 503,
                    'data':{'error':{'code':'workers_busy','retry_safe':True,'dispatched':False,'retry_after_ms':100}}}).encode());return
            if ((self.mode=='legacy_disconnect' and attempt==1) or
                (self.mode=='no_fallback_after_lost' and attempt==2)):
                self.reply(404,b'{"code":404,"message":"route unavailable"}');return
            if self.mode=='gateway' and attempt==1:
                self.reply(503,b'<html>temporary gateway failure</html>');return
            if self.mode=='pending' and attempt==1:
                entered=state()['entered_count']
                worker=threading.Thread(target=self.forward,args=(body,self.path))
                workers.append(worker);worker.start()
                end=time.monotonic()+2
                while state()['entered_count']==entered and time.monotonic()<end:time.sleep(.02)
                assert state()['entered_count']>entered
                self.drop();return
            status,response=self.forward(body,self.path)
            if self.mode=='result_mismatch':
                changed=json.loads(response);changed['data']['request_id']='0'*32
                self.reply(status,json.dumps(changed).encode());return
            if attempt==1 and self.mode=='truncated':
                self.send_response(status);self.send_header('Content-Length',str(len(response)))
                self.end_headers();self.wfile.write(response[:len(response)//2]);self.wfile.flush()
                self.drop();return
            if ((attempt==1 and self.mode in ('lost','restart','no_fallback_after_lost')) or
                self.mode=='legacy_disconnect'):
                if self.mode=='restart':
                    Handler.restarted_dispatches=state()['calls'];ctx['restart']()
                self.drop();return
            self.reply(status,response)

    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        cases=[('lost',True,2,1),('truncated',True,2,1),('gateway',True,2,1),('pending',True,3,1),
            ('restart',True,2,1),('no_fallback_after_lost',False,2,1),('legacy_disconnect',False,2,1),
            ('pending_mismatch',False,1,0),('result_mismatch',False,1,1),('fallback_limit',False,6,0)]
        for mode,success,expected_posts,expected_dispatches in cases:
            Handler.mode=mode;Handler.requests=[];Handler.restarted_dispatches=0
            before=state()['calls'];quota=used()
            output=invoke(ctx['host'],f'http://127.0.0.1:{server.server_port}/api/v1/search',
                [(json.dumps({'query':'slow' if mode=='pending' else 'hello','count':2}),success)],
                ctx['token'],external=True,deadline_us=25_000_000 if mode=='fallback_limit' else 5_000_000)
            for worker in workers:
                worker.join(3);assert not worker.is_alive()
            workers.clear()
            posts=int(re.search(r'case_fetches=0:(\d+)',output).group(1))
            actual=Handler.requests
            assert posts==len(actual)==expected_posts,(mode,posts,actual,output)
            dispatches=(Handler.restarted_dispatches if mode=='restart' else 0)+state()['calls']-before
            assert dispatches==expected_dispatches and used()==quota+expected_dispatches,(mode,dispatches,used(),quota,output)
            assert output.count('execute_web_search=')==1,output
            assert f'summary=permissions:1 completed:{int(success)} failed:{int(not success)}' in output,output
            assert 'test-bocha-secret' not in output and 'test-zai-secret' not in output,output
            if mode=='legacy_disconnect':
                assert [p for p,_ in actual]==['/api/v1/search/requests','/api/v1/search']
                assert 'Submission could not be confirmed' in output
            else:
                assert all(p=='/api/v1/search/requests' for p,_ in actual)
                assert len({b for _,b in actual})==1
            if success:assert '"type":"web_search_results"' in output,output
            records[mode]={'success':success,'http_requests':posts,'provider_dispatches':dispatches,
                'quota':expected_dispatches,'one_final_tool_result':True,'same_body':mode!='legacy_disconnect'}
        return records
    finally:
        for worker in workers:worker.join(3)
        server.shutdown();server.server_close();thread.join(2)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=Path,default=web.ROOT/'.build/host/xs.exe')
    p.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    p.add_argument('--output',type=Path)
    args=p.parse_args();records=run(args.host,args.website_host,scenario)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(records,indent=2)+'\n')
    print('PASS native lost/truncated/gateway/pending/restart recovery, one dispatch/quota, guarded fallback',records)


if __name__=='__main__':main()
