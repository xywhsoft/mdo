"""Real pinned llama.cpp + Qwen3 acceptance through current mdo, in place.

Prerequisite: tools/runtime/build/prepare_local_models.py --download.
No app/SDK copies, production changes or model-side tool execution.
"""
import argparse,copy,hashlib,json,os,ssl,subprocess,tempfile,threading,time,zipfile
from pathlib import Path
from http.server import ThreadingHTTPServer
from urllib.parse import urlsplit
import test_local_models_download as fixture
from current_product_host import APP,product_http_config
from test_api_runtime import ROOT,free_port,request,wait_ready
from test_interrupt_runtime import stop_host
from test_local_model_format import sign

class Publisher(fixture.Publisher):
    cut=False;slow=False
    def do_GET(self):
        if urlsplit(self.path).path!='/model.gguf':return super().do_GET()
        start=int(self.headers.get('Range','bytes=0-').split('=')[1].split('-')[0]);size=self.server.weight.stat().st_size
        self.send_response(206 if start else 200);self.send_header('Content-Length',str(size-start));self.send_header('ETag','"'+self.server.sha+'"')
        if start:self.send_header('Content-Range',f'bytes {start}-{size-1}/{size}')
        self.end_headers()
        try:
            with self.server.weight.open('rb') as stream:
                stream.seek(start)
                for chunk in iter(lambda:stream.read(1024*1024),b''):self.wfile.write(chunk)
        except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend',choices=['cpu','vulkan'],default='cpu');p.add_argument('--assets',type=Path,default=ROOT/'.build/local-model-real');p.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');a=p.parse_args()
    base_assets=a.assets.resolve();sources=json.loads((base_assets/'sources.json').read_text());weight=base_assets/'Qwen3-0.6B-Q8_0.gguf';record=next(s for s in sources['sources'] if s['name']==weight.name)
    assert weight.stat().st_size==record['bytes'];runtime=base_assets/f'llama-b12477-bin-win-{a.backend}-x64.zip'
    # Build a runtime-only extension from verified upstream binaries, keeping
    # DLLs together. The archive is not installed by copying a running app.
    m=dict(kind='external-tools',id='external-tool:qa.local-runtime',name='Real llama.cpp b12477 acceptance',alias='qa-llama-b12477-'+a.backend,entry='tool.json',platforms=['windows-x86_64'],version='b12477')
    tool=dict(program='llama-server.exe',prompt='Owned llama.cpp inference runtime. mdo manages its process.',commands=[dict(name=m['alias'],entry='llama-server.exe')])
    files={'extension.json':m,'tool.json':tool}
    with zipfile.ZipFile(runtime) as z:
        for n in z.namelist():
            if n.endswith('.dll') or n in ('llama-server.exe','LICENSE-LLVM-OpenMP'):files[n]=z.read(n)
    packed=fixture.archive(files);doc=fixture.recipe();f=doc['weights'][0]['files'][0];f.update(bytes=record['bytes'],sha256=record['sha256'],urls=['https://weights.example.test/model.gguf'],redirect_hosts=[])
    doc['weights'][0]['quantization']='Q8_0';r=doc['runtimes'][0];r.update(build='b12477',backend=a.backend,sha256=hashlib.sha256(packed).hexdigest(),version_id=fixture.RUNTIME_VERSION)
    profile=doc['profiles'][0];profile.update(context_tokens=16384,max_output_tokens=4096,output_tokens=1024,gpu_layers=0 if a.backend=='cpu' else -1);profile['requirements'].update(ram_bytes=3*1024**3,vram_bytes=0 if a.backend=='cpu' else 3*1024**3,disk_bytes=weight.stat().st_size,margin_bytes=256*1024**2)
    model=fixture.manifest();model['name']='Qwen3 0.6B real acceptance';model_zip=fixture.archive({'extension.json':model,'model.json':doc})
    def meta(m,work,version,b):return {**m,'manifest':m,'xid':work,'version_id':version,'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'state':'published','serial':1}
    rm=meta(m,fixture.RUNTIME_WORK,fixture.RUNTIME_VERSION,packed);mm=meta(model,fixture.MODEL_WORK,fixture.MODEL_VERSION,model_zip);mm['local_model']=doc
    fixture.ARCHIVES={fixture.RUNTIME_VERSION:packed,fixture.MODEL_VERSION:model_zip};fixture.RUNTIME=rm;fixture.MODEL=mm
    fixture.PLAN=sign(dict(schema='mdo.local-model-plan.v1',platform='windows-x86_64',recipe=mm,document=doc,profile=copy.deepcopy(profile),runtime_plan=dict(dependency_protocol=1,root=m['id'],platform='windows-x86_64',items=[rm]),assets=copy.deepcopy(doc['weights'][0]['files']),asset_bytes=weight.stat().st_size))
    with tempfile.TemporaryDirectory(prefix='local-model-real-',dir=ROOT/'.build') as raw,product_http_config(free_port()) as config:
        base=Path(raw);home=base/'home';(home/'config').mkdir(parents=True);(home/'certs').mkdir();ca,leaf,key=fixture.certificates(base);(home/'certs/ca.pem').write_bytes(ca.read_bytes())
        publisher=ThreadingHTTPServer(('127.0.0.1',0),Publisher);publisher.daemon_threads=True;publisher.weight=weight;publisher.sha=record['sha256'];ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(leaf,key);publisher.socket=ctx.wrap_socket(publisher.socket,server_side=True)
        proxy=fixture.ProxyServer(('127.0.0.1',0),fixture.Proxy);proxy.target=publisher.server_port;proxy.targets=[]
        threading.Thread(target=publisher.serve_forever,daemon=True).start();threading.Thread(target=proxy.serve_forever,daemon=True).start()
        (home/'config/settings.json').write_text(json.dumps(dict(schema_version=1,patch=dict(transport=dict(ca_pem_path='certs/ca.pem',proxy=dict(kind='http-connect',host='127.0.0.1',port=proxy.server_address[1],user='probe',bypass='',credential=dict(secret_ref='env:MDO_TEST_PROXY_PASSWORD')))))),encoding='utf-8')
        process=None;port=json.loads(config.read_text(encoding='utf-8'))['services'][0]['port'];log=base/'runtime.log';checks=[];did=None;report=dict(backend=a.backend,build='b12477',model='Qwen3-0.6B-Q8_0',weight_sha256=record['sha256'],passed=False,checks=checks)
        def check(v,text):
            assert v,text;checks.append(text);print('PASS',text,flush=True)
        def call(body=None,path='/local-models',timeout=300):
            # Test helper defaults to 5s socket timeouts; real verification has
            # its own bounded native model deadline, so use a direct connection.
            import http.client
            if body is None:status,_,b=request(port,'GET','/api/v1'+path)
            else:
                _,headers,_=request(port,'GET','/api/v1/bootstrap');token=next(v for k,v in headers.items() if k.lower()=='x-mdo-write-token')
                c=http.client.HTTPConnection('127.0.0.1',port,timeout=timeout)
                try:c.request('POST','/api/v1'+path,json.dumps(body).encode(),{'Content-Type':'application/json','X-Mdo-Write-Token':token});r=c.getresponse();status,b=r.status,r.read()
                finally:c.close()
            value=json.loads(b);assert status==200,(status,value);return value['data']
        def wait(kind,id,states,timeout=240):
            end=time.monotonic()+timeout
            while time.monotonic()<end:
                row=call()[kind][id];state=row['service_state'] if kind=='deployments' else row['state']
                if state in states:return row
                time.sleep(.2)
            raise AssertionError(row)
        try:
            with log.open('wb') as output:process=subprocess.Popen([str(a.host.resolve()),str(config),'--','--home',str(home)],cwd=APP,env={**os.environ,'MDO_HOME':str(home),'USERPROFILE':str(base),'USE_WEBVIEW':'0','MDO_TEST_PROXY_PASSWORD':'bounded-password'},stdout=output,stderr=output,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            wait_ready(port,process);plan=call(dict(action='plan',version=fixture.MODEL_VERSION,profile='default'));check(plan['runtime_plan']['ready'],'real runtime dependency plan ready')
            installed=call(dict(action='install',plan_id=plan['id'],revision=plan['revision'],trust_code=True));row=wait('installations',installed['id'],('installed','failed'));report['installation']=row;check(row['state']=='installed','real model installed with full size and SHA-256: '+str(row.get('error','')))
            deployment=call(dict(action='deployment_create',installation=installed['id'],name='Qwen3 real '+a.backend));did=deployment['id'];config=dict(deployment['config'],reasoning_mode='off',threads=6)
            deployment=call(dict(action='deployment_update',id=did,deployment_revision=deployment['revision'],config=config));start=time.monotonic();call(dict(action='start',id=did,allow_unknown=a.backend!='cpu'));row=wait('deployments',did,('ready','failed'));logs=call(dict(action='logs',id=did))['text'];report['runtime_log']=logs
            check(row['service_state']=='ready','real runtime service ready' if row['service_state']=='ready' else str(row)+'\n'+logs);report['load_seconds']=round(time.monotonic()-start,2)
            result=call(dict(action='verify',id=did));report['verification']=result;check(result['verified'] and result['reply'] and result['tools'],'real streaming reply and tool/result/follow-up verified')
            import urllib.request
            with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(urllib.request.Request(row['web_url'],headers={'Accept':'text/html','Accept-Encoding':'gzip'}),timeout=10) as page:check(page.status==200,'real llama.cpp chat page available')
            models=call(path='/models')['models'];model_row=next(x for x in models if x['id']=='local-model.'+did);check(model_row['context_window_tokens']==16384 and model_row['local_verified'],'effective context and verification published in mdo model list')
            call(dict(action='stop',id=did));wait('deployments',did,('stopped',));call(dict(action='start',id=did,allow_unknown=a.backend!='cpu'));row=wait('deployments',did,('ready','failed'));check(row['service_state']=='ready','real model restarts with the same stable model identity')
            call(dict(action='stop',id=did));wait('deployments',did,('stopped',));current=call()['deployments'][did];call(dict(action='deployment_delete',id=did,deployment_revision=current['revision']));call(dict(action='uninstall',id=installed['id'],delete_files=True));check(not call()['installations'],'real installation and owned model resources removed')
            report['passed']=True
        except BaseException as e:
            if did:
                try:report['runtime_log']=call(dict(action='logs',id=did))['text']
                except Exception:pass
            report['error']=str(e);raise AssertionError(str(e)+'\n'+report.get('runtime_log','')+'\n'+log.read_text(encoding='utf-8',errors='replace')[-2500:]) from e
        finally:
            (base_assets/f'acceptance-{a.backend}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            if process:stop_host(process)
            publisher.shutdown();proxy.shutdown();publisher.server_close();proxy.server_close()
    print('PASS real local model acceptance',a.backend,flush=True)
if __name__=='__main__':main()
