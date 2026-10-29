"""Exercise the public SDK against current product sources, without copying
the app/runtime, contacting a model, or touching an existing Home."""
from __future__ import annotations
import argparse
import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from test_api_runtime import free_port
from current_product_host import product_http_config

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--ownership',action='store_true',help='Also exercise the real external-tool directory lease bridge')
    args=parser.parse_args();checks=0
    def check(condition,detail):
        nonlocal checks
        assert condition,detail
        checks+=1
    port=free_port()
    # Linux directory transactions require the native filesystem's no-replace
    # rename; WSL DrvFS deliberately does not emulate that guarantee.
    with tempfile.TemporaryDirectory(prefix='process-sdk-',dir=ROOT/'.build' if os.name=='nt' else None) as raw, product_http_config(port) as config:
        base=Path(raw);home=base/'home';workspace=base/'workspace';(workspace/'sub').mkdir(parents=True)
        url=f'http://127.0.0.1:{port}';log=base/'host.log'
        def request(method,path,body=None,expected=200):
            headers={}
            if body is not None:
                with urllib.request.urlopen(url+'/api/v1/bootstrap',timeout=10) as response:
                    headers['X-Mdo-Write-Token']=response.headers.get('X-Mdo-Write-Token','')
                headers['Content-Type']='application/json'
            req=urllib.request.Request(url+path,data=json.dumps(body).encode() if body is not None else None,headers=headers,method=method)
            try:
                with urllib.request.urlopen(req,timeout=15) as response:status=response.status;value=json.load(response)
            except urllib.error.HTTPError as error:status=error.code;value=json.load(error)
            check(status==expected,(path,status,value));return value
        def call(method,params=None,expected=200,project='process-qa'):
            value=request('POST','/api/v1/application/call',{'method':method,'params':params or {},'context':{'project':project}},expected)
            return value['data'] if expected==200 else value['error']
        def run(script,**options):
            return call('process.run',{'program':sys.executable,'args':['-u','-c',script],**options})
        with log.open('wb') as output:
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--port',str(port),'--','--home',str(home)],cwd=ROOT/'app',env={**os.environ,'USE_WEBVIEW':'0'},stdout=output,stderr=output,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            end=time.monotonic()+20
            while time.monotonic()<end:
                if process.poll() is not None:raise AssertionError(log.read_text(encoding='utf-8',errors='replace'))
                try:
                    bootstrap=request('GET','/api/v1/bootstrap')
                    if bootstrap['data'].get('ready'):break
                    if bootstrap['data'].get('stage')=='failed':raise AssertionError(bootstrap)
                except (urllib.error.URLError,TimeoutError):time.sleep(.1)
            else:raise AssertionError(log.read_text(encoding='utf-8',errors='replace'))
            call('project.create',{'body':{'id':'process-qa','name':'Process output QA','workspace_root':str(workspace)}},project='default')
            short=call('process.run',{'program':sys.executable,'args':['-c','import sys; print(repr(sys.argv[1])); print("error",file=sys.stderr); sys.exit(7)','']})
            check(short['exit_code']==7 and short['output_complete'] and not short['stdout']['native_path'],short)
            check(base64.b64decode(short['stdout']['base64']).strip()==b"''" and short['stderr']['text'].strip()=='error',short)
            large=run('import sys;sys.stdout.buffer.write(b"1234567890\\n"*300000);sys.stderr.buffer.write(b"error\\n"*3000)')
            check(large['output_complete'] and not large['truncated'] and 'base64' not in large['stdout'],large)
            out=Path(large['stdout']['native_path']);err=Path(large['stderr']['native_path'])
            check(out.read_bytes()==b'1234567890\n'*300000 and err.read_bytes()==b'error\n'*3000,'SDK output byte mismatch')
            check(out.is_relative_to(home/'artifacts/process') and large['stdout']['path'].startswith('@results/process/'),large)
            tail=call('fs.read',{'path':str(out),'offset':out.stat().st_size-11,'limit':11})
            check(base64.b64decode(tail['base64'])==b'1234567890\n',tail)
            chosen=run('print("chosen")',cwd='sub',stdout_path='chosen.log')
            check(Path(chosen['stdout']['native_path']).resolve()==(workspace/'sub/chosen.log').resolve(),chosen)
            marker=workspace/'must-not-run'
            failure=call('process.run',{'program':sys.executable,'args':['-c',f'from pathlib import Path;Path({str(marker)!r}).touch()'],'cwd':'sub','stdout_path':'chosen.log'},409)
            check(not marker.exists() and (workspace/'sub/chosen.log').read_text().strip()=='chosen' and not failure['details']['may_have_completed'],failure)
            for fields in ({'max_output_bytes':0},{'merge_stderr':True,'stderr_path':'bad.log'},{'args':['\0']}):
                call('process.run',{'program':sys.executable,**fields},400)
            limited=run('import sys;sys.stdout.buffer.write(b"x"*200000)',max_output_bytes=1024)
            check(not limited['output_complete'] and limited['truncated'] and limited['output_error']=='output_limit' and limited['stdout']['bytes']==200000 and limited['stdout']['saved_bytes']==1024,limited)
            merged=run('import sys;print("out",flush=True);print("err",file=sys.stderr,flush=True)',merge_stderr=True)
            check(merged['stderr_merged'] and 'out' in merged['stdout']['text'] and 'err' in merged['stdout']['text'],merged)
            timed=call('process.run',{'program':sys.executable,'args':['-u','-c','import time;print("before-timeout",flush=True);time.sleep(5)'],'timeout_ms':300},504)
            check(timed['code']=='sdk_process_timeout' and timed['details']['may_have_completed'] and 'before-timeout' in timed['details']['result']['stdout']['text'],timed)
            missing=call('process.run',{'program':'mdo-no-such-process-output-qa'},404)
            check(missing['code']=='sdk_native_failed' and 'process' in missing['details']['stage'] and missing['details']['cause'] and not missing['details']['may_have_completed'] and missing['details']['result']['wait_status']==-1,missing)
            run('print("streaming-output")',stream=True)
            events=request('GET','/api/v1/application/events')['data']
            check('process.stdout' in json.dumps(events),'stream event missing')
            # Configuration/descriptor conversion keeps historical allowlists
            # executable while the UI and new model catalog use compact names.
            catalog=request('GET','/api/v1/modules')['data']
            check('ask_user' not in json.dumps(catalog),'old tool IDs still advertised')
            if args.ownership:
                subprocess.run([sys.executable,str(ROOT/'tests/test_agent_process_ownership_native.py'),
                    '--url',url,'--home',str(home),'--project','default'],check=True,timeout=45)
            print(f'Public SDK process output: PASS ({checks} checks; current app sources, no app/runtime copy)')
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)

if __name__=='__main__':main()
