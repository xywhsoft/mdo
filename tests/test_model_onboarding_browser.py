"""Actual settings modules + native API + local supplier, no paid model calls."""
import argparse,io,json,os,subprocess,sys,threading
from http.server import ThreadingHTTPServer
from pathlib import Path
from test_api_runtime import ROOT,free_port,wait_ready
from test_model_verification_runtime import Supplier

class BrowserSupplier(Supplier):
    def send_json(self,value):
        raw=json.dumps(value).encode();self.send_response(200)
        self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)))
        self.send_header('Access-Control-Allow-Origin','*');self.end_headers();self.wfile.write(raw)
    def do_GET(self):
        if self.path=='/allow-beta':self.server.fail_beta=False;return self.send_json({'ok':True})
        if self.path=='/stats':
            return self.send_json({'models':self.server.model_reads,'calls':[{'model':c['model'],'tools':bool(c.get('tools'))} for c in self.calls],
                'catalog_credentials':any('authorization' in {k.lower() for k in h} for h in self.catalog_headers)})
        if self.path=='/v1/models':
            self.server.model_reads+=1
            return self.send_json({'data':[{'id':'alpha','context_length':64000,'max_tokens':16000,
                'capabilities':{'thinking':{'supported':False}}},{'id':'beta'}]})
        return super().do_GET()
    def do_POST(self):
        raw=self.rfile.read(int(self.headers['Content-Length']));body=json.loads(raw)
        self.rfile=io.BytesIO(raw);self.mode='invalid_tool' if body['model']=='beta' and self.server.fail_beta else 'ok'
        return super().do_POST()

def check(host,node,browser,evidence):
    evidence.mkdir(parents=True,exist_ok=True)
    Supplier.calls=[];Supplier.contract_errors=[];Supplier.catalog_headers=[]
    server=ThreadingHTTPServer(('127.0.0.1',0),BrowserSupplier);server.model_reads=0;server.fail_beta=True
    threading.Thread(target=server.serve_forever,daemon=True).start()
    origin=f'http://127.0.0.1:{server.server_port}'
    Supplier.catalog={'schema_version':1,'version':'wizard-browser-fixture','providers':[{'id':'fixture','name':'Fixture','variants':[
        {'id':'api','label':'regular','base':origin+'/v1','protocol':'openai-chat-completions','models':['alpha','beta']}]}],
        'entries':[{'bases':[origin+'/v1'],'ids':['beta'],'protocols':['openai-chat-completions'],'upstream':True,
            'source_url':'https://example.invalid/model-spec','checked_at':'2026-10-30',
            'fields':{'context_tokens':32000,'max_output_tokens':8000,'reasoning':{'kind':'none'}}}]}
    wrapper=ROOT/'app/generated/model_wizard_browser_test.c';config=ROOT/'app/xs.model-wizard-browser-test.json';page=ROOT/'app/web/model-wizard-qa.html'
    assert not any(p.exists() for p in (wrapper,config,page)),'Owned browser probe is already running'
    process=None;port=free_port()
    try:
        wrapper.write_text(f'#define MDO_ACCOUNT_SERVICE_ORIGIN "{origin}"\n#include "mdo_unity.c"\n',encoding='utf-8')
        config.write_text(json.dumps({'engine':{'workers':2},'services':[{'enabled':True,'class':'http','name':'wizard-browser-test','ip':'127.0.0.1','port':port,
            'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/model_wizard_browser_test.c'}}]}),encoding='utf-8')
        page.write_text('''<!doctype html><html lang="zh-CN" data-theme="dark"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="/css/app.css"><style>body{height:auto;overflow:auto;margin:0;padding:24px;background:var(--bg);color:var(--ink)} main{max-width:1120px;margin:auto;container-type:inline-size;container-name:model-config}#result{display:none}</style>
<main><h2>模型管理</h2><div id="panel"></div></main><pre id="result"></pre><script type="module">
import {api} from '/js/api/client.js';import {loadLocale} from '/js/i18n.js';import {createModelConfigPanel} from '/js/features/settings/model-config-panel.js';
await loadLocale('zh-CN');await api.get('/project-purge-intent');const panel=createModelConfigPanel(document.querySelector('#panel'));await panel.ensureLoaded();
window.__qaReady=true;window.__qaSupplier='''+json.dumps(origin)+''';</script></html>''',encoding='utf-8')
        with (evidence/'browser-host.log').open('wb') as log:
            process=subprocess.Popen([str(host),str(config)],cwd=ROOT/'app',env={**os.environ,'USE_WEBVIEW':'0','MDO_HOME':str(evidence/'home')},
                stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            wait_ready(port,process)
            for width in (1320,390):
                # A fresh user home for each width is required for the same add flow.
                if width==390:
                    # Re-run against the existing supplier via a fresh settings home.
                    process.terminate();process.wait(timeout=8)
                    server.fail_beta=True;server.model_reads=0;Supplier.calls=[];Supplier.catalog_headers=[]
                    process=subprocess.Popen([str(host),str(config)],cwd=ROOT/'app',env={**os.environ,'USE_WEBVIEW':'0','MDO_HOME':str(evidence/'home-narrow')},
                        stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));wait_ready(port,process)
                preview=subprocess.run([str(node),str(ROOT/'tests/queue_browser_driver.mjs'),str(browser),str(evidence/f'edge-{width}-preview'),
                    f'http://127.0.0.1:{port}/model-wizard-qa.html?preview=parameters',str(width),str(evidence/f'parameters-{width}.png'),str(ROOT/'tests/model_onboarding_browser_driver.js')],
                    capture_output=True,text=True,encoding='utf-8',timeout=55)
                assert preview.returncode==0 and json.loads(preview.stdout)['passed'],(preview.stdout,preview.stderr)
                server.model_reads=0;Supplier.calls=[];Supplier.catalog_headers=[]
                result=subprocess.run([str(node),str(ROOT/'tests/queue_browser_driver.mjs'),str(browser),str(evidence/f'edge-{width}'),
                    f'http://127.0.0.1:{port}/model-wizard-qa.html',str(width),str(evidence/f'wizard-{width}.png'),str(ROOT/'tests/model_onboarding_browser_driver.js')],
                    capture_output=True,text=True,encoding='utf-8',timeout=55)
                (evidence/f'browser-{width}.json').write_text(result.stdout,encoding='utf-8')
                assert result.returncode==0,(result.stdout,result.stderr)
                report=json.loads(result.stdout);assert report['passed'],report
                assert not Supplier.contract_errors,Supplier.contract_errors
                print(json.dumps({'width':width,**report},ensure_ascii=False),flush=True)
    finally:
        if process and process.poll() is None:process.terminate();process.wait(timeout=8)
        for path in (wrapper,config,page):path.unlink(missing_ok=True)
        server.shutdown();server.server_close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--node',type=Path,required=True)
    parser.add_argument('--browser',type=Path,default=Path(r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'))
    parser.add_argument('--evidence',type=Path,required=True);args=parser.parse_args()
    check(args.host.resolve(),args.node.resolve(),args.browser.resolve(),args.evidence.resolve())
