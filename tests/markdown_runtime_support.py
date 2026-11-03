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
                env=dict(os.environ,MDO_QUEUE_FIXTURE_KEY='fixture-only'),
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

def markdown_session(port,home):
    from test_packed_queue_start_recovery import configure_model
    configure_model(port,'http://127.0.0.1:1/v1/chat/completions')
    workspace=home/'workspace';workspace.mkdir(parents=True,exist_ok=True)
    source=workspace/'中文 file.txt';source.write_text('first\nsecond\nthird\nfourth\n',encoding='utf-8')
    import base64
    image=workspace/'中文 图片.png'
    image.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/Tz8AAAAASUVORK5CYII='))
    status,_,body=request(port,'POST','/api/v1/sessions',headers={'Content-Type':'application/json'},body=json.dumps({
        'project_id':'default','title':'Markdown fixture','model_id':'queue-fixture','workspace_root':str(workspace),'reasoning_effort':'none'}).encode())
    assert status==201,body
    session=json.loads(body)['data']
    return {'projectId':'default','sessionId':session['id'],'file':str(source).replace('\\','/'),
        'image':str(image).replace('\\','/'),'workspace':str(workspace).replace('\\','/')}
