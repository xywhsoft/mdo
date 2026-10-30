"""Draft verification commits only successful profiles; bounded loopback calls."""
import argparse,copy,http.client,json,os,subprocess,tempfile,threading,time
from concurrent.futures import ThreadPoolExecutor
from http.server import ThreadingHTTPServer
from pathlib import Path
from test_api_runtime import ROOT,free_port,request,wait_ready,write_site
from test_model_verification_runtime import Supplier

def check(host):
    Supplier.calls=[];Supplier.contract_errors=[];Supplier.mode='ok'
    supplier=ThreadingHTTPServer(('127.0.0.1',0),Supplier)
    threading.Thread(target=supplier.serve_forever,daemon=True).start()
    checks=[]
    try:
      with tempfile.TemporaryDirectory(prefix='model-onboarding-',dir=ROOT/'.build') as raw:
        base=Path(raw);port=free_port();site=base/'site';site.mkdir();config_path=write_site(site,port)
        home=base/'home'
        with (base/'host.log').open('wb') as log:
          proc=subprocess.Popen([str(host),str(config_path),'--','--home',str(home)],cwd=site,
            env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
          try:
            wait_ready(port,proc)
            def call(method,path,body=None,etag=None):
                status,headers,raw=request(port,method,'/api/v1/'+path,
                    body=json.dumps(body).encode() if body is not None else None,
                    headers={'Content-Type':'application/json',**({'If-Match':etag} if etag else {})})
                return status,headers,json.loads(raw)
            def current():
                status,headers,data=call('GET','models/config');assert status==200,data
                return data['data'],headers['etag']
            initial,etag=current()
            def draft(name,protocol='openai-chat-completions',provider=None):
                p=copy.deepcopy(provider or initial['providers'][0]);p.update(id='onboarding',name='Onboarding',builtin=False,editable=True,removable=True,
                    endpoints={dict(zip(['openai-chat-completions','openai-responses','anthropic-messages'],['chat_completions','responses','anthropic_messages']))[protocol]:
                        f'http://127.0.0.1:{supplier.server_port}/v1/'+{'openai-chat-completions':'chat/completions','openai-responses':'responses','anthropic-messages':'messages'}[protocol]},proxy={'kind':'none'})
                p.pop('credential',None)
                m=copy.deepcopy(initial['items'][0]);m.update(id=name,name=name,wire_model=name,provider=p['id'],builtin=False,free=False,editable=True,removable=True,
                    protocols=[protocol],default_protocol=protocol,reasoning_efforts=['default'],default_reasoning_effort='default',proxy={'kind':'provider'})
                return {'provider':p,'model':m,'key':'onboarding-fixture-only','make_default':False}
            for mode,name in [('reasoning_only','reply-fails'),('invalid_tool','tools-fail')]:
                Supplier.mode=mode;before,_=current();body=draft(name)
                status,_,data=call('POST','models/add',body,etag);assert status==422,data
                after,_=current();assert after==before,(mode,after)
                assert name not in after.get('verification',{})
                checks.append(mode+' leaves model, provider and verification unchanged')
            Supplier.mode='ok';body=draft('first-success');body['make_default']=True
            status,_,data=call('POST','models/add',body,etag);assert status==201,data
            assert data['data']['added'] and data['data']['verification_saved'],data
            saved,etag=current();assert saved['default_model']=='first-success'
            assert saved['verification']['first-success']['verified']
            p=next(p for p in saved['providers'] if p['id']=='onboarding')
            assert p['credential']['secret_ref'].startswith('vault:') and 'onboarding-fixture-only' not in json.dumps(saved)
            checks.append('successful reply and tools atomically add profile and sealed key with persisted evidence')
            before_calls=len(Supplier.calls)
            status,_,data=call('POST','models/add',body,etag);assert status==409,data
            assert len(Supplier.calls)==before_calls
            checks.append('duplicate submission never repeats model calls')
            body2=draft('existing-provider');body2['provider']=p;body2.pop('key')
            status,_,data=call('POST','models/add',body2,etag);assert status==201,data
            saved,etag=current();assert saved['verification']['existing-provider']['verified']
            checks.append('existing supplier credentials are reused without replacement')
            Supplier.mode='wait';Supplier.entered.clear();Supplier.release.clear()
            concurrent=draft('stale-draft');concurrent['provider']=p;concurrent.pop('key')
            with ThreadPoolExecutor(max_workers=1) as executor:
                future=executor.submit(call,'POST','models/add',concurrent,etag)
                assert Supplier.entered.wait(2)
                status,_,busy=call('POST','projects/no-such-project/purge',{})
                assert status==409 and busy['error']['code']=='write_admission_busy',busy
                now,match=current();next(p for p in now['providers'] if p['id']=='onboarding')['name']='Concurrent edit'
                patch={k:now[k] for k in ('providers','items','default_model')}
                status,_,data=call('PUT','settings/models',{'schema_version':1,'patch':patch},match);assert status==200,data
                Supplier.release.set();status,_,data=future.result(timeout=6);assert status in (409,412),data
            saved,etag=current();assert not any(m['id']=='stale-draft' for m in saved['items'])
            checks.append('configuration revision changes during verification refuse the commit')
            checks.append('accepted background verification holds the destructive-write gate')
            Supplier.entered.clear();Supplier.release.clear()
            cancelled=draft('cancelled-draft');cancelled['provider']=next(p for p in saved['providers'] if p['id']=='onboarding');cancelled.pop('key')
            _,headers,_=request(port,'GET','/api/v1/bootstrap')
            connection=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
            connection.request('POST','/api/v1/models/add',json.dumps(cancelled),{'Content-Type':'application/json','If-Match':etag,'X-Mdo-Write-Token':headers['x-mdo-write-token']})
            assert Supplier.entered.wait(2);connection.close();time.sleep(.1);Supplier.release.set();time.sleep(.2)
            saved,etag=current();assert not any(m['id']=='cancelled-draft' for m in saved['items'])
            checks.append('closing an in-flight request prevents late model installation')
            Supplier.mode='ok'
            for index,protocol in enumerate(('openai-responses','anthropic-messages')):
                body=draft('protocol-'+str(index),protocol);body['provider']['id']='onboarding-'+str(index);body['model']['provider']=body['provider']['id']
                status,_,data=call('POST','models/add',body,etag);assert status==201,(protocol,data)
                _,etag=current()
            checks.append('draft verification uses the saved transport for Chat, Responses and Anthropic')
            status,_,data=call('POST','models/discover',{'provider':draft('x')['provider']});assert status==200,data
            assert any(row['id']=='metadata-effort' and row['fields']['context_tokens']['value']==64000 for row in data['data']['parameters'])
            checks.append('discovery preserves supplier parameter metadata')
            proc.terminate();proc.wait(timeout=10)
            proc=subprocess.Popen([str(host),str(config_path),'--','--home',str(home)],cwd=site,
                env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
            wait_ready(port,proc);restarted,_=current();assert restarted['verification']['protocol-0']['verified']
            checks.append('successful verification and profile survive native restart')
            assert not Supplier.contract_errors,Supplier.contract_errors
            print(json.dumps({'passed':True,'checks':checks},ensure_ascii=False))
          except BaseException:
            log.flush();print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-3500:]);raise
          finally:
            Supplier.release.set();proc.terminate();proc.wait(timeout=10)
    finally:supplier.shutdown();supplier.server_close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True)
    check(parser.parse_args().host.resolve())
