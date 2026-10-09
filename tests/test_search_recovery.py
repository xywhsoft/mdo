"""Finite native search rejection/recovery cases. Local HTTP, no billing.

One xwork invocation per case, only its final result is visible to the model.
Assertions count actual POSTs, permission checks and monotonic retry intervals.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import socket
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from test_search_api_runtime import envelope
import test_web_runtime as web

# name, reason, HTTP status, failures before recovery, final success, POST count
CASES = [
    ('busy','member_busy',429,2,True,3),
    ('workers','workers_busy',503,1,True,2),
    ('budget','budget_busy',503,1,True,2),
    ('business','server_busy',200,1,True,2),
    ('retry_header','server_busy',429,1,True,2),
    ('minute_long','minute_limit',429,99,False,1),
    ('daily','daily_limit',429,99,False,1),
    ('global','global_daily_limit',429,99,False,1),
    ('provider_auth','provider_auth_failed',502,99,False,1),
    ('provider_limit','provider_rate_limited',502,99,False,1),
    ('database','budget_unavailable',503,99,False,1),
    ('unknown','future_error',429,99,False,1),
    ('sent','member_busy',429,99,False,1),
    ('unsafe','member_busy',429,99,False,1),
    ('bad_bool','member_busy',429,99,False,1),
    ('missing','member_busy',429,99,False,1),
    ('negative','member_busy',429,99,False,1),
    ('overflow','member_busy',429,99,False,1),
    ('mismatch','member_busy',503,99,False,1),
    ('auth_mismatch','server_busy',401,99,False,1),
    ('disconnect','server_busy',503,99,False,1),
    ('malformed_http','server_busy',503,99,False,1),
    ('short','server_busy',429,99,False,1),
    ('cancel','server_busy',429,99,False,1),
    ('exhausted','server_busy',429,99,False,6),
]
class Handler(BaseHTTPRequestHandler):
    calls: dict[str,list[float]] = {}
    def log_message(self,*_):pass
    def do_POST(self):
        assert self.path=='/api/v1/search'
        assert self.headers['Authorization']=='Bearer probe-secret'
        args=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        name=args['query']; case=next(c for c in CASES if c[0]==name)
        calls=self.calls.setdefault(name,[]);calls.append(time.monotonic())
        if name=='disconnect':
            self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();self.close_connection=True;return
        if name=='malformed_http':
            self.connection.sendall(b'HTTP/1.1 invalid\r\n\r\n');self.close_connection=True;return
        _,reason,status,failures,_,_=case
        result=envelope(name)
        if len(calls)<=failures:
            code=429 if reason in ('member_busy','server_busy','minute_limit','daily_limit','global_daily_limit','future_error') else status
            error={'code':reason,'retry_safe':True,'dispatched':False,'retry_after_ms':100}
            if name=='minute_long':error['retry_after_ms']=61000
            if name in ('daily','global'):error['retry_after_ms']=86400000
            if name=='sent':error['dispatched']=True
            if name=='unsafe':error['retry_safe']=False
            if name=='bad_bool':error['retry_safe']=1
            if name=='missing':error.pop('dispatched')
            if name=='negative':error['retry_after_ms']=-1
            if name=='overflow':error['retry_after_ms']=18446744073709551615
            result={'code':code,'message':'HOSTILE_SECRET_BODY','data':{'error':error}}
        else:status=200
        raw=json.dumps(result).encode()
        self.send_response(status)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)))
        if name=='retry_header' and len(calls)==1:self.send_header('Retry-After','2')
        self.end_headers();self.wfile.write(raw)


def source(origin):
    text=web.PROBE_SOURCE
    a=text.index('static unsigned char *Copy(');b=text.index('static void ResponseUnit(',a)
    text=text[:a]+r'''
static xcancel* ProbeCancel;
static uint64 ProbeDeadline;
static xthread* CancelThread;
static int32 CancelLater(void* cancel) { xrtSleep(120u); xrtCancelRequest(cancel); return 0; }
static bool Fetch(void* data,const XS_FetchRequest* request,XS_FetchResponse* response) {
    Probe* probe=data; ++probe->Fetches;
    bool ok=xsFetch(request,response);
    if (ProbeCancel && !CancelThread) CancelThread=xrtThreadCreate(CancelLater,ProbeCancel,0u);
    return ok;
}
'''+text[b:]
    text=text.replace('context.uDeadline = xrtDeadlineAfter(5000000u);',
                      'context.uDeadline=ProbeDeadline; context.pCancel=ProbeCancel;')
    a=text.index('    if (!Execute(agent, "web_search"');b=text.index('done:\n',a)
    calls=[]
    for name,_,_,_,_,_ in CASES:
        args=json.dumps(json.dumps({'query':name}))
        calls.append(f'''
    probe.Fetches=0u; ProbeDeadline=xrtDeadlineAfter({200000 if name=='short' else 25000000}u);
    ProbeCancel={'xrtCancelCreate()' if name=='cancel' else 'NULL'};
    uint64 start_{name}=xrtClock(); bool ok_{name}=Execute(agent,"web_search",{args},&search);
    printf("case={name} success:%d fetches:%u elapsed_ms:%llu\\n",ok_{name},probe.Fetches,
        (unsigned long long)((xrtClock()-start_{name})/1000u));
    xrtFree(search);search=NULL;
    if (CancelThread) {{ xrtThreadWait(CancelThread);xrtThreadDestroy(CancelThread);CancelThread=NULL; }}
    xrtCancelDestroy(ProbeCancel);ProbeCancel=NULL;
''')
    calls.append(r'''
    memset(&snapshot,0,sizeof(snapshot));snapshot.Size=sizeof(snapshot);
    MdoWebManagerGetSnapshot(&snapshot);
    printf("summary=permissions:%u completed:%llu failed:%llu\n",probe.Permissions,
        (unsigned long long)snapshot.RequestsCompleted,(unsigned long long)snapshot.RequestsFailed);
    printf("probe_done=1\n");
''')
    return '#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+text[:a]+''.join(calls)+text[b:]


def run(host):
    Handler.calls={};server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='search-recovery-',dir=web.ROOT/'.build') as raw:
            base=Path(raw);web.write_site(base/'site')
            (base/'site/probe.c').write_text(source(f'http://127.0.0.1:{server.server_port}'),encoding='utf-8')
            output=web.run_probe(host.resolve(),base/'site',base/'home',wait_timeout=55)
        assert 'probe_done=1' in output and 'init_error=' not in output,output
        assert 'HOSTILE_SECRET_BODY' not in output and 'probe-secret' not in output,output
        rows=re.findall(r'case=(\w+) success:(\d) fetches:(\d+) elapsed_ms:(\d+)',output)
        found={name:{'success':bool(int(ok)),'posts':int(posts),'elapsed_ms':int(ms)} for name,ok,posts,ms in rows}
        assert len(found)==len(CASES),output
        for name,_,_,_,success,posts in CASES:
            row=found[name];actual=Handler.calls.get(name,[])
            assert row['success']==success and row['posts']==posts and len(actual)==posts,(name,row,output)
            for i in range(1,len(actual)):
                assert actual[i]-actual[i-1] >= min(.5*2**(i-1),8)-.03,(name,actual)
        assert Handler.calls['retry_header'][1]-Handler.calls['retry_header'][0]>=1.98
        assert found['cancel']['elapsed_ms']<800 and found['short']['elapsed_ms']<500
        assert output.count('execute_web_search=')==len(CASES),output
        assert f'summary=permissions:{len(CASES)} completed:5 failed:{len(CASES)-5}' in output,output
        for marker in ('daily search quota is exhausted','rejected the service credentials',
                       'required wait exceeds', 'Automatic recovery limits were reached',
                       'failed protocol validation'):
            assert marker in output,output
        return found
    finally:server.shutdown();server.server_close();thread.join(2)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=web.ROOT/'.build/host/xs.exe')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args(); rows=run(args.host)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(rows,indent=2)+'\n')
    print('PASS finite native search recovery/explicit reasons/retry safety/cancel/deadline',len(rows),'cases')


if __name__=='__main__':main()
