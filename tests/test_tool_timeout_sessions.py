"""Actual agent loops: web/exec/MCP timeouts, failure streaks, task deadline/stop."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_interrupt_runtime import free_port
from test_api_runtime import MCP_MOCK_SERVER
from test_mcp_runtime import server_document

ROOT=Path(__file__).resolve().parents[1]


class Handler(BaseHTTPRequestHandler):
    calls={}
    def log_message(self,*_): pass
    def do_GET(self):
        count=self.calls.get(self.path,0)+1; self.calls[self.path]=count
        if self.path!='/recover' or count==1: time.sleep(1.3)
        try:
            self.send_response(200); self.send_header('Content-Length','2'); self.end_headers(); self.wfile.write(b'ok')
        except (BrokenPipeError,ConnectionResetError): pass
    def do_POST(self):
        request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        method=request.get('method');key='POST '+str(method)
        self.calls[key]=self.calls.get(key,0)+1
        if 'id' not in request:
            self.send_response(202);self.send_header('Content-Length','0');self.end_headers();return
        if method=='server/discover':
            result={'resultType':'complete','supportedVersions':['2026-07-28'],'capabilities':{'tools':{}},'ttlMs':60000,'cacheScope':'private'}
        elif method=='tools/list':
            result={'resultType':'complete','ttlMs':60000,'cacheScope':'private','tools':[{'name':'echo','description':'Timeout fixture',
                'inputSchema':{'type':'object','properties':{'text':{'type':'string'}},'required':['text'],'additionalProperties':False}}]}
        else:
            time.sleep(.6)
            result={'resultType':'complete','content':[{'type':'text','text':'late'}],'isError':False}
        body=json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}).encode()
        try:
            self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass


def run(host,report=None,http_only=False):
    Handler.calls={}
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True
    worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix='tool-timeout-sessions-',dir=ROOT/'.build') as raw:
            base=Path(raw); site=base/'site'; home=base/'home'; workspace=base/'workspace'
            workspace.mkdir();(workspace/'input.txt').write_text('reset failure streak',encoding='utf-8')
            shutil.copytree(ROOT/'app',site)
            script=base/'mcp.py'
            mock=MCP_MOCK_SERVER.replace('    else:\n        continue',
                '    elif method == "tools/call":\n        import time; time.sleep(.6)\n        result = {"resultType":"complete","content":[{"type":"text","text":"late"}],"isError":False}\n    else:\n        continue')
            script.write_text(mock,encoding='utf-8')
            mcp=json.loads(server_document(sys.executable,str(script),str(base/'marker'),server_id='timeout'))
            mcp['request_timeout_ms']=100;mcp['transport']['environment']=[]
            (site/'default-home/mcp/timeout.json').write_text(json.dumps(mcp),encoding='utf-8')
            http={**mcp,'id':'httptimeout','transport':{'type':'streamable-http','endpoint':f'http://127.0.0.1:{server.server_port}/mcp','headers':[]}}
            (site/'default-home/mcp/httptimeout.json').write_text(json.dumps(http),encoding='utf-8')
            defaults=site/'default-home/config/defaults.json'; config=json.loads(defaults.read_text(encoding='utf-8'))
            config['settings']['web'].update(allow_http=True,allow_private_networks=True,timeout_ms=1000,idle_timeout_ms=1000)
            defaults.write_text(json.dumps(config),encoding='utf-8')
            (site/'probe.c').write_text(
                '#define PROBE_FIRST_MODE '+('9' if http_only else '0')+'\n'
                '#define PROBE_WORKSPACE '+json.dumps(str(workspace))+'\n'
                '#define PROBE_PYTHON_JSON '+json.dumps(json.dumps(sys.executable))+'\n'
                '#define PROBE_PAGE_ORIGIN '+json.dumps(f'http://127.0.0.1:{server.server_port}')+'\n'
                '#define ServiceInit ProbeProductInit\n#include "generated/mdo_unity.c"\n#undef ServiceInit\n'
                '#include "tool-timeout-session.c"\n',encoding='utf-8')
            shutil.copy2(ROOT/'tests/fixtures/tool-timeout-session.c',site/'tool-timeout-session.c')
            (site/'xs.json').write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'probe',
                'ip':'127.0.0.1','port':free_port(),'host_default':{'enabled':True,'name':'mdo','path':'web',
                'devlang':'c','devfile':'probe.c'}}]}),encoding='utf-8')
            result=subprocess.run([str(host.resolve()),'xs.json','--','--home',str(home)],cwd=site,
                env=dict(os.environ,USERPROFILE=str(base),HOME=str(base)),capture_output=True,
                text=True,encoding='utf-8',errors='replace',timeout=55,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            output=result.stdout+result.stderr
            if report: report.write_text(json.dumps({'exit_code':result.returncode,'output':output,'http_calls':Handler.calls},indent=2)+'\n',encoding='utf-8')
            assert result.returncode==0 and 'timeout_sessions_ok=1' in output,(result.returncode,output)
            for name in (('mcp-http-timeout',) if http_only else ('page-recover','page-exhausted','exec-timeout','mcp-timeout','mcp-http-timeout','nine-failures','failure-reset')):
                path=home/'sessions/default'/name
                events=[json.loads(line) for line in (path/'ui-events.jsonl').read_text(encoding='utf-8').splitlines()]
                assert any(e['kind']==10 and e['success'] for e in events),name
                assert not any(e['kind']==11 for e in events),name
                snap=json.loads((path/'snapshot.json').read_text(encoding='utf-8'))
                pending={c['id'] for e in snap['entries'] for c in e.get('tool_calls',[])}-{e['tool_call_id'] for e in snap['entries'] if e.get('role')==3}
                assert not pending,(name,pending)
            if not http_only: assert Handler.calls['/recover']==2 and Handler.calls['/slow']>=8,Handler.calls
            assert Handler.calls['POST tools/call']==1,Handler.calls
            print('\n'.join(line for line in output.splitlines() if line.startswith('session_case=')))
            print('PASS HTTP MCP timeout continuation' if http_only else 'PASS 10 real agent sessions, persistent tool results, streak reset, task deadline and explicit stop')
    finally:
        server.shutdown();server.server_close();worker.join(timeout=3)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--report',type=Path);parser.add_argument('--http-only',action='store_true')
    args=parser.parse_args();run(args.host,args.report,args.http_only)
