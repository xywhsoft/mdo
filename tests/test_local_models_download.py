"""Full installation through an owned HTTPS/proxy store; no external traffic."""
import argparse, copy, datetime, hashlib, io, json, os, select, socket, socketserver, ssl, subprocess, tempfile, threading, time, zipfile
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from current_product_host import APP, product_http_config
from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host
from local_model_fixture import recipe, manifest
from test_local_model_format import sign

PAYLOAD=b'GGUF\x03\0\0\0'+b'small controlled weight fixture\n'*32768
HASH=hashlib.sha256(PAYLOAD).hexdigest()
RUNTIME_WORK='A'*32;RUNTIME_VERSION='B'*32;MODEL_WORK='C'*32;MODEL_VERSION='D'*32

def archive(files):
    stream=io.BytesIO()
    with zipfile.ZipFile(stream,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,json.dumps(data,ensure_ascii=False,separators=(',',':')) if isinstance(data,dict) else data)
    return stream.getvalue()

def fixtures(program=None):
    runtime=dict(kind='external-tools',id='external-tool:qa.local-runtime',name='Synthetic runtime (never executed)',alias='qa-runtime',entry='tool.json',platforms=['windows-x86_64'])
    runtime_zip=archive({'extension.json':runtime,'tool.json':dict(program='server.exe',prompt='Protocol fixture for local model service tests.',commands=[dict(name='qa-llama-server',entry='server.exe')]),'server.exe':program or b'NOT AN EXECUTABLE: never run this fixture'})
    doc=recipe();doc['weights'][0]['files'][0].update(bytes=len(PAYLOAD),sha256=HASH,urls=['https://weights.example.test/model.gguf'],redirect_hosts=[])
    doc['runtimes'][0].update(version_id=RUNTIME_VERSION,sha256=hashlib.sha256(runtime_zip).hexdigest())
    profile=doc['profiles'][0];profile['requirements'].update(ram_bytes=1048576,disk_bytes=2*len(PAYLOAD),margin_bytes=0)
    model=manifest();model_zip=archive({'extension.json':model,'model.json':doc})
    def meta(m,work,version,data):return {**m,'manifest':m,'xid':work,'version_id':version,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'state':'published','serial':1}
    r=meta(runtime,RUNTIME_WORK,RUNTIME_VERSION,runtime_zip);m=meta(model,MODEL_WORK,MODEL_VERSION,model_zip);m['local_model']=doc
    remote=sign(dict(schema='mdo.local-model-plan.v1',platform='windows-x86_64',recipe=m,document=doc,profile=copy.deepcopy(profile),runtime_plan=dict(dependency_protocol=1,root=runtime['id'],platform='windows-x86_64',items=[r]),assets=copy.deepcopy(doc['weights'][0]['files']),asset_bytes=len(PAYLOAD)))
    return doc,r,m,remote,{RUNTIME_VERSION:runtime_zip,MODEL_VERSION:model_zip}

DOC,RUNTIME,MODEL,PLAN,ARCHIVES=fixtures()

class Publisher(BaseHTTPRequestHandler):
    ranges=[];hits=0;cut=True;bad=False;slow=True
    def log_message(self,*args):pass
    def do_GET(self):
        p=urlsplit(self.path);query=parse_qs(p.query)
        if p.path=='/model.gguf':
            if not self.headers.get('User-Agent','').startswith('mdo/'):return self.send_error(403)
            type(self).hits+=1;start=int(self.headers.get('Range','bytes=0-').split('=')[1].split('-')[0]);type(self).ranges.append(start)
            data=PAYLOAD[start:];data=(b'x'+data[1:]) if type(self).bad and data else data
            self.send_response(206 if start else 200);self.send_header('Content-Length',str(len(data)));self.send_header('ETag','"fixture-v1"')
            if start:self.send_header('Content-Range',f'bytes {start}-{len(PAYLOAD)-1}/{len(PAYLOAD)}')
            self.end_headers()
            if type(self).cut and not start:
                type(self).cut=False;self.wfile.write(data[:65536]);self.wfile.flush();self.close_connection=True;return
            try:
                for at in range(0,len(data),8192):
                    self.wfile.write(data[at:at+8192]);self.wfile.flush()
                    if type(self).slow:time.sleep(.01)
            except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
            return
        if p.path.endswith('/archive'):
            data=ARCHIVES[query['version'][0]];offset=int(query.get('offset',['0'])[0]);chunk=data[offset:offset+1048576];self.send_response(200);self.send_header('Content-Type','application/zip');self.send_header('Content-Length',str(len(chunk)));self.end_headers();self.wfile.write(chunk);return
        if p.path.endswith('/updates'):value=dict(items=[MODEL],platform='windows-x86_64')
        elif p.path.endswith('/catalog'):value=dict(items=[MODEL],total=1,offset=0,limit=100)
        elif p.path.endswith('/plan'):value=PLAN
        elif p.path.endswith('/detail'):value=RUNTIME if query['version'][0]==RUNTIME_VERSION else MODEL
        else:return self.send_error(404)
        data=json.dumps(dict(code=0,data=value),ensure_ascii=False,separators=(',',':')).encode();self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)

class Proxy(socketserver.BaseRequestHandler):
    def handle(self):
        client=self.request;client.settimeout(20);header=bytearray()
        try:
            while not header.endswith(b'\r\n\r\n') and len(header)<8192:
                value=client.recv(1)
                if not value:return
                header+=value
            line=header.decode().split('\r\n')[0];assert line in ('CONNECT ai.xywhsoft.com:443 HTTP/1.1','CONNECT weights.example.test:443 HTTP/1.1'),line
            self.server.targets.append(line)
            with socket.create_connection(('127.0.0.1',self.server.target),5) as upstream:
                client.sendall(b'HTTP/1.1 200 Connection Established\r\n\r\n')
                while True:
                    ready,_,_=select.select([client,upstream],[],[],10)
                    if not ready:return
                    for src in ready:
                        data=src.recv(65536)
                        if not data:return
                        (upstream if src is client else client).sendall(data)
        except (ConnectionError,OSError,ssl.SSLError):pass

class ProxyServer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True;daemon_threads=True

def certificates(base):
    ca_key=rsa.generate_private_key(public_exponent=65537,key_size=2048);key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
    name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'Owned model fixture CA')]);leaf_name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'weights.example.test')])
    start=datetime.datetime(2020,1,1,tzinfo=datetime.timezone.utc);end=datetime.datetime(2040,1,1,tzinfo=datetime.timezone.utc)
    ca_cert=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(ca_key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(start).not_valid_after(end).add_extension(x509.BasicConstraints(ca=True,path_length=0),True).add_extension(x509.KeyUsage(True,False,False,False,False,True,True,False,False),True).sign(ca_key,hashes.SHA256())
    cert=x509.CertificateBuilder().subject_name(leaf_name).issuer_name(name).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(start).not_valid_after(end).add_extension(x509.BasicConstraints(ca=False,path_length=None),True).add_extension(x509.KeyUsage(True,False,True,False,False,False,False,False,False),True).add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]),False).add_extension(x509.SubjectAlternativeName([x509.DNSName('ai.xywhsoft.com'),x509.DNSName('weights.example.test')]),False).sign(ca_key,hashes.SHA256())
    ca=base/'ca.pem';leaf=base/'cert.pem';pem=base/'key.pem';ca.write_bytes(ca_cert.public_bytes(serialization.Encoding.PEM));leaf.write_bytes(cert.public_bytes(serialization.Encoding.PEM));pem.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.TraditionalOpenSSL,serialization.NoEncryption()));return ca,leaf,pem

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');parser.add_argument('--browser',type=Path);parser.add_argument('--services',action='store_true');args=parser.parse_args()
    global DOC,RUNTIME,MODEL,PLAN,ARCHIVES
    if args.services:
        import sys
        runner=ROOT/'.build/local-model-fixture-runner.exe'
        subprocess.run(['gcc','-Os','-s','-municode','-ffunction-sections','-Wl,--gc-sections',str(ROOT/'tests/local_model_fixture_runner.c'),'-lshell32','-o',str(runner)],check=True)
        DOC,RUNTIME,MODEL,PLAN,ARCHIVES=fixtures(runner.read_bytes())
    with tempfile.TemporaryDirectory(prefix='local-model-download-',dir=ROOT/'.build') as raw,product_http_config(free_port()) as config:
        base=Path(raw);home=base/'home';(home/'config').mkdir(parents=True);(home/'certs').mkdir();ca,leaf,key=certificates(base);(home/'certs/ca.pem').write_bytes(ca.read_bytes())
        publisher=ThreadingHTTPServer(('127.0.0.1',0),Publisher);publisher.daemon_threads=True;context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(leaf,key);publisher.socket=context.wrap_socket(publisher.socket,server_side=True)
        proxy=ProxyServer(('127.0.0.1',0),Proxy);proxy.target=publisher.server_port;proxy.targets=[]
        threading.Thread(target=publisher.serve_forever,daemon=True).start();threading.Thread(target=proxy.serve_forever,daemon=True).start()
        (home/'config/settings.json').write_text(json.dumps(dict(schema_version=1,patch=dict(transport=dict(ca_pem_path='certs/ca.pem',proxy=dict(kind='http-connect',host='127.0.0.1',port=proxy.server_address[1],user='probe',bypass='',credential=dict(secret_ref='env:MDO_TEST_PROXY_PASSWORD')))))),encoding='utf-8')
        control=base/'control.json';control.write_text('{}');
        process=None;port=json.loads(config.read_text(encoding='utf-8'))['services'][0]['port'];log=base/'runtime.log';checks=0
        def check(value,detail):
            nonlocal checks
            assert value,detail;checks+=1
        def call(body=None,status=200,path='/local-models'):
            code,_,raw=request(port,'GET' if body is None else 'POST','/api/v1'+path,body=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'});data=json.loads(raw);check(code==status,(code,data));return data.get('data')
        def start():
            nonlocal process
            with log.open('ab') as out:process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=APP,env={**os.environ,'MDO_HOME':str(home),'USERPROFILE':str(base),'USE_WEBVIEW':'0','MDO_TEST_PROXY_PASSWORD':'bounded-password','MDO_QA_MODEL_PYTHON':__import__('sys').executable,'MDO_QA_MODEL_SCRIPT':str(ROOT/'tests/local_model_server_fixture.py'),'MDO_QA_MODEL_CONTROL':str(control)},stdout=out,stderr=out,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            wait_ready(port,process)
        def wait(id,skip=()):
            end=time.monotonic()+50
            while time.monotonic()<end:
                row=call()['installations'][id]
                if row['state'] not in ('queued','installing') and row['state'] not in skip:return row
                time.sleep(.1)
            raise AssertionError(row)
        def install():
            plan=call(dict(action='plan',version=MODEL_VERSION,profile='default'));check(plan['runtime_plan']['ready'],plan)
            call(dict(action='install',plan_id=plan['id'],revision=plan['revision'],trust_code=False),status=422)
            command=dict(action='install',plan_id=plan['id'],revision=plan['revision'],trust_code=True)
            row=call(command);check(call(command)['id']==row['id'],'replayed install duplicated the receipt')
            call({**command,'revision':'0'*64},status=422)
            return row['id']
        def download():
            return next(row for row in call(path='/download-tasks') if row['identity'].endswith('/model.gguf'))
        def partial():
            end=time.monotonic()+20
            while time.monotonic()<end:
                path=store/'partial'/f'{HASH}.part'
                if path.exists() and path.stat().st_size>16384:
                    row=download()
                    if row['state']=='downloading' and row['received']<len(PAYLOAD):return row
                time.sleep(.02)
            raise AssertionError('partial model file never became resumable')
        try:
            start();result=call(dict(action='catalog',limit=100));check(result['items'][0]['id']==MODEL['id'],result)
            call(dict(action='plan',version='E'*32,profile='default'),status=422)
            call(dict(action='plan',version=MODEL_VERSION,profile='unselected'),status=422)
            store=base/'weights';store.mkdir();call(dict(action='storage',path=str(store)))
            first=install();row=wait(first);check(row['state']=='installed',row);check((store/'blobs'/HASH).read_bytes()==PAYLOAD,'verified model bytes differ');check(any(Publisher.ranges),'interrupted response did not resume with Range')
            check(row['local_model']['i18n']==DOC['i18n'] and 'plan' not in row and 'runtime_plan' not in row,'snapshot lost localization or exposed its internal install plans')
            runtime=call(path='/extensions/external-tools/'+RUNTIME_WORK);call(dict(action='remove',expected_revision=runtime['revision']),path='/extensions/external-tools/'+RUNTIME_WORK+'/actions',status=422)
            hits=Publisher.hits;second=install();row=wait(second);check(row['state']=='installed',row);check(Publisher.hits==hits,'shared model weights downloaded twice')
            call(dict(action='uninstall',id=first,delete_files=True));check((store/'blobs'/HASH).exists(),'shared model file removed')
            stop_host(process);process=None;start();check(second in call()['installations'],'installed model did not recover')
            call(dict(action='uninstall',id=second,delete_files=True));check(not (store/'blobs'/HASH).exists(),'unreferenced file was not removed')
            Publisher.cut=False;Publisher.bad=True;failed=install();row=wait(failed);check(row['state']=='failed' and not (store/'blobs'/HASH).exists(),row);Publisher.bad=False
            call(dict(action='retry',id=failed));row=wait(failed);check(row['state']=='installed',row)
            completed=download();call({},path=f"/download-tasks/{completed['id']}/forget");check((store/'blobs'/HASH).read_bytes()==PAYLOAD,'forgetting download history removed installed weights')
            call(dict(action='uninstall',id=failed,delete_files=True))
            standalone=None;standalone_deployment=None
            if args.services:
                source=base/'standalone.gguf';source.write_bytes(PAYLOAD);standalone=call(dict(action='import',path=str(source),mode='reference',name='Independent model'));check(wait(standalone['id'])['state']=='installed','independent import failed')
                standalone_deployment=call(dict(action='deployment_create',installation=standalone['id'],runtime_ref=RUNTIME['id'],name='During download'))
            paused=install();task=partial();call({},path=f"/download-tasks/{task['id']}/pause");time.sleep(.25)
            if standalone_deployment:
                id=standalone_deployment['id'];call(dict(action='start',id=id,allow_unknown=True));end=time.monotonic()+15
                while time.monotonic()<end and (row:=call()['deployments'][id])['service_state'] not in ('ready','failed'):time.sleep(.05)
                check(row['service_state']=='ready','a paused large download blocked an installed model from starting: '+str(row))
                call(dict(action='stop',id=id));end=time.monotonic()+10
                while time.monotonic()<end and call()['deployments'][id]['service_state']!='stopped':time.sleep(.05)
                call(dict(action='deployment_delete',id=id,deployment_revision=standalone_deployment['revision']))

            before=(store/'partial'/f'{HASH}.part').stat().st_size;time.sleep(.25);check((store/'partial'/f'{HASH}.part').stat().st_size==before,'paused producer continued writing')
            call({},path=f"/download-tasks/{task['id']}/resume");check(wait(paused)['state']=='installed','paused download did not finish')
            call(dict(action='uninstall',id=paused,delete_files=True))
            if standalone:call(dict(action='uninstall',id=standalone['id'],delete_files=True))
            cancelled=install();partial();call(dict(action='cancel',id=cancelled));check(wait(cancelled)['state']=='interrupted','cancelled model did not retain a resumable receipt')
            call(dict(action='retry',id=cancelled));check(wait(cancelled)['state']=='installed','model-page retry did not resume cancelled weights')
            call(dict(action='uninstall',id=cancelled,delete_files=True))
            interrupted=install();task=partial();process.kill();process.wait(timeout=10);process=None;start()
            row=call()['installations'][interrupted];check(row['state']=='interrupted',row);hits=Publisher.hits;time.sleep(.25);check(Publisher.hits==hits,'restart unexpectedly resumed model downloads')
            call({},path=f"/download-tasks/{task['id']}/resume");check((resumed:=wait(interrupted,('interrupted',)))['state']=='installed',resumed);check((store/'blobs'/HASH).read_bytes()==PAYLOAD,'restart committed wrong bytes')
            check(all('example.test:443' in t or 'ai.xywhsoft.com:443' in t for t in proxy.targets),'unexpected network destination')
            if args.services:
                deployment=call(dict(action='deployment_create',installation=interrupted,name='Service QA'))
                did=deployment['id'];mid='local-model.'+did
                model_rows=call(path='/models')['models'];check(any(m['id']==mid for m in model_rows),'managed model missing from catalog')
                check(next(m for m in model_rows if m['id']==mid)['default_output_tokens']==2048,'deployment output budget lost')
                def service(state,timeout=30):
                    end=time.monotonic()+timeout
                    while time.monotonic()<end:
                        row=call()['deployments'][did]
                        if row['service_state'] in state:return row
                        time.sleep(.05)
                    raise AssertionError(row)
                call(dict(action='start',id=did));row=service(('ready','failed'));check(row['service_state']=='ready',row)
                checked=call(dict(action='verify',id=did));check(checked['verified'] and checked['reply'] and checked['tools'],checked)
                access=call(dict(action='web_access',id=did));check(access['key'] not in json.dumps(call()),'web credential leaked into snapshots')
                # A live request owns a lease; a graceful stop drains it.
                from concurrent.futures import ThreadPoolExecutor
                control.write_text('{"slow_call":true}')
                with ThreadPoolExecutor(max_workers=1) as pool:
                    task=pool.submit(call,dict(action='verify',id=did));end=time.monotonic()+5
                    while not call()['deployments'][did].get('active_requests') and time.monotonic()<end:time.sleep(.02)
                    check(call()['deployments'][did]['active_requests']>0,'verification did not own an active request lease')
                    call(dict(action='stop',id=did));check(call()['deployments'][did]['service_state']=='stopping','graceful stop did not reject new requests while draining');task.result(timeout=8)
                service(('stopped',));call(dict(action='stop',id=did));check(service(('stopped',))['service_state']=='stopped','stop was not idempotent')
                control.write_text('{}');call(dict(action='start',id=did));service(('ready',))
                check('[redacted]' in call(dict(action='logs',id=did))['text'],'runtime credential was not redacted')
                call(dict(action='deployment_update',id=did,deployment_revision=deployment['revision'],config=deployment['config']),status=422)
                call(dict(action='uninstall',id=interrupted,delete_files=True),status=422)
                call(dict(action='stop',id=did));check(service(('stopped',))['service_state']=='stopped','stop never finished')
                # A stopped deployment remains selectable; no random closed port is saved.
                check(any(m['id']==mid for m in call(path='/models')['models']),'stopped model disappeared')
                orphan_view=store/'views'/did/'model.gguf';orphan_view.parent.mkdir(parents=True,exist_ok=True);os.link(store/'blobs'/HASH,orphan_view)
                for _ in range(30):
                    try:
                        edited=call(dict(action='deployment_update',id=did,deployment_revision=deployment['revision'],config={**deployment['config'],'context_tokens':4096,'max_output_tokens':2048,'output_tokens':1024}))
                        break
                    except AssertionError:time.sleep(.05)
                else:raise AssertionError('completed deployment remained locked')
                check(not orphan_view.exists() and (store/'blobs'/HASH).exists(),'configuration edit leaked a split-file view left by a crashed runtime')
                call(dict(action='start',id=did));row=service(('ready','failed'));check(row['service_state']=='ready',row)
                call(dict(action='stop',id=did,force=True));service(('stopped',))
                call(dict(action='deployment_rollback',id=did,deployment_revision=edited['revision']))
                current=call()['deployments'][did];check(current['config']['context_tokens']==8192,'deployment rollback lost original context')
                current=call(dict(action='deployment_update',id=did,deployment_revision=current['revision'],config={**current['config'],'auto_start_on_use':True,'idle_seconds':1}))
                checked=call(dict(action='verify',id=did));check(checked['verified'],'first-use startup failed');service(('stopped',))
                current=call()['deployments'][did]
                current=call(dict(action='deployment_update',id=did,deployment_revision=current['revision'],config={**current['config'],'auto_start_on_use':False,'idle_seconds':0}))
                control.write_text('{"bad_context":true}');call(dict(action='start',id=did));row=service(('failed',));check('context' in row['service_error'],'wrong effective server context accepted')
                # Publish a new inert recipe while preserving an old deployment.
                control.write_text('{}');before_hits=Publisher.hits;new_doc=copy.deepcopy(DOC);new_doc['profiles'][0].update(context_tokens=4096,max_output_tokens=2048,output_tokens=1024)
                new_manifest=manifest();new_manifest['version']='2.0.0';new_zip=archive({'extension.json':new_manifest,'model.json':new_doc});new_version='E'*32
                MODEL={**MODEL,**new_manifest,'manifest':new_manifest,'version_id':new_version,'sha256':hashlib.sha256(new_zip).hexdigest(),'bytes':len(new_zip),'serial':2,'local_model':new_doc};ARCHIVES[new_version]=new_zip
                PLAN=sign({**PLAN,'recipe':MODEL,'document':new_doc,'profile':copy.deepcopy(new_doc['profiles'][0])})
                latest=call(dict(action='updates',id=interrupted));check(latest['items'][0]['version_id']==new_version,'update lookup did not return published version')
                new_plan=call(dict(action='plan',version=new_version,profile='default'));new_install=call(dict(action='install',plan_id=new_plan['id'],revision=new_plan['revision'],trust_code=True));new_row=wait(new_install['id']);check(new_row['state']=='installed',new_row);check(Publisher.hits==before_hits,'unchanged weight was downloaded on update')
                current=call()['deployments'][did];switched=call(dict(action='deployment_switch',id=did,deployment_revision=current['revision'],installation=new_install['id']));check(switched['config']['context_tokens']==4096 and switched['id']==did,'update changed stable identity or lost new profile')
                call(dict(action='uninstall',id=interrupted,delete_files=True),status=422)
                call(dict(action='deployment_rollback',id=did,deployment_revision=switched['revision']));check(call()['deployments'][did]['installation']==interrupted,'rollback did not restore old model installation')
                stop_host(process);process=None;start();check(call()['installations'][interrupted]['state']=='installed','updated recipe made old deployment unavailable after restart')
                current=call()['deployments'][did]
                current=call(dict(action='deployment_update',id=did,deployment_revision=current['revision'],config={**current['config'],'auto_start':True}))
                stop_host(process);process=None;start();check(service(('ready','failed'))['service_state']=='ready','automatic startup happened before the command/API services were ready')
                call(dict(action='stop',id=did));service(('stopped',));current=call()['deployments'][did]
                call(dict(action='deployment_delete',id=did,deployment_revision=current['revision']))
                check(not any(m['id']==mid for m in call(path='/models')['models']),'deleted deployment stayed in model catalog');call(dict(action='uninstall',id=new_install['id'],delete_files=True))
            if args.browser:
                source=base/'browser.gguf';source.write_bytes(PAYLOAD)
                evidence=ROOT/'.build/local-model-browser';evidence.mkdir(exist_ok=True)
                for locale,width in [('en-US',1400),('ru-RU',390),('zh-CN',1400)]:
                    with tempfile.TemporaryDirectory(prefix='chrome-',dir=base) as profile:
                        from urllib.parse import urlencode
                        result=subprocess.run(['node',str(ROOT/'tests/queue_browser_driver.mjs'),str(args.browser),profile,f'http://127.0.0.1:{port}/?'+urlencode(dict(locale=locale,qa_model_path=str(source),qa_services="1" if args.services else "0")),str(width),str(evidence/f'{locale}-{width}.png'),str(ROOT/'tests/local_model_browser_driver.js'),'desktop'],capture_output=True,timeout=60)
                        report=json.loads(result.stdout);(evidence/f'{locale}-{width}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');check(result.returncode==0 and report.get('passed'),report)
            call(dict(action='uninstall',id=interrupted,delete_files=True));check(not (home/'local-models'/MODEL_WORK).exists(),'last recipe installation was retained')
            runtime=call(path='/extensions/external-tools/'+RUNTIME_WORK);call(dict(action='remove',expected_revision=runtime['revision']),path='/extensions/external-tools/'+RUNTIME_WORK+'/actions')
            check(not (home/'external-tools'/RUNTIME_WORK).exists(),'unused runtime remained pinned')
        except BaseException as e:raise AssertionError(f'{e}\n{log.read_text(encoding="utf-8",errors="replace")[-6000:]}') from e
        finally:
            if process:stop_host(process)
            publisher.shutdown();proxy.shutdown();publisher.server_close();proxy.server_close()
        print(f'PASS {checks} HTTPS model plan, installation, resume, sharing and runtime pin checks')

if __name__=='__main__':main()
