"""Local packed host for Markdown probes; hardlink binary and use a tiny test Home."""
from contextlib import contextmanager
import json, os, subprocess, tempfile
from pathlib import Path
from test_api_runtime import ROOT, free_port, request, wait_ready

@contextmanager
def markdown_host(packed):
    with tempfile.TemporaryDirectory(dir=ROOT/'.build', prefix='markdown-runtime-') as raw:
        base=Path(raw); launcher=base/'mdo.exe'; os.link(Path(packed).resolve(),launcher)
        port=free_port(); home=base/'home'
        (base/'xs.json').write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'markdown-test','ip':'127.0.0.1','port':port,
            'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}),encoding='utf-8')
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(launcher),'--','--home',str(home)],cwd=base,stdout=log,stderr=log,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                wait_ready(port,process)
                def call(method,params,context=None):
                    status,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':method,'params':params,'context':context or {}}).encode(),headers={'Content-Type':'application/json'})
                    return status,json.loads(body)
                yield port,call,home
            except Exception:
                print((base/'host.log').read_text(encoding='utf-8',errors='replace'));raise
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)

def walk(nodes):
    for node in nodes:
        yield node
        yield from walk(node.get('c',[]))
