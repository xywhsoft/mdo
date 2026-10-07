"""Real mdo/xs/account/gateway composition with a bounded local upstream.

All identities and databases are disposable. No external provider or key.
"""
import http.client
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import ThreadingHTTPServer

ROOT=Path(__file__).resolve().parents[1];HOME=ROOT.parent/'home'
sys.path.insert(0,str(HOME/'tests'))
from test_mdo_delivery import fixture, request
from model_gateway_fixture import Upstream

HOOK=r'''
static bool OnlineTestRoute(MdoApiContext* Context) {
    MdoModelCatalog* Catalog=MdoModelCatalogSnapshot();
    MdoModelClientOptions Options;MdoModelClientOptionsInit(&Options);
    Options.ModelId=Context->Target.Query.Size==3?"mdo-online.glm-test":"ornith-1.5-35b";
    Options.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;Options.MaxOutputTokens=64;
    xllm_request Request;xllmRequestInit(&Request);xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,"normal");Request.bStream=false;
    xllm_response* Response=NULL;xllm_error Error;xllmErrorInit(&Error);
    xllm_result Result=MdoModelOnlineComplete(Catalog,&Options,&Request,NULL,&Response,&Error);
    xvalue* Out=xrtValueObject();MdoApiValueSetBool(Out,"success",Result==XLLM_RESULT_OK);
    MdoApiValueSetUInt(Out,"error_code",Error.eCode);MdoApiValueSetString(Out,"error",Error.sMessage);
    xllmResponseDestroy(Response);xllmRequestUnit(&Request);MdoModelCatalogRelease(Catalog);
    return MdoApiReplySuccessTake(Context,200,Out,NULL);
}
'''

def run(host):
    base,website,port,user,password=fixture('online-model-client-test')
    config=json.loads((website/'xs.json').read_text());config['services'][0]['host_default']['devfile']='main.c';(website/'xs.json').write_text(json.dumps(config))
    origin=f'http://127.0.0.1:{port}';(website/'db/identity.json').write_text(json.dumps({'public_origin':origin}))
    upstream=ThreadingHTTPServer(('127.0.0.1',0),Upstream);threading.Thread(target=upstream.serve_forever,daemon=True).start();Upstream.calls=[]
    processes=[];logs=[]
    def launch(exe,site,home=None):
        log=(site/'test.log').open('wb');logs.append(log);env=dict(os.environ)
        if home:env['MDO_HOME']=str(home)
        p=subprocess.Popen([str(exe),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        processes.append(p);return p
    def call(p,method,path,body=None,cookie=None,headers=None,status=200):
        actual,h,raw=request(p,method,path,body,cookie,headers);assert actual==status,(path,actual,raw[:300]);return json.loads(raw),h
    def ready(p,proc,path):
        end=time.monotonic()+40
        while time.monotonic()<end:
            assert proc.poll() is None,'native process exited'
            try:
                if request(p,'GET',path)[0]==200:return
            except OSError:pass
            time.sleep(.1)
        raise AssertionError('readiness timeout')
    try:
        web=launch(HOME/'xs.exe',website);ready(port,web,'/mdo/catalog')
        _,h=call(port,'POST','/admin/login',dict(username=user,password=password));cookie='; '.join(c.split(';')[0] for c in h['_cookies'])
        state=call(port,'GET','/admin/model-gateway/state',cookie=cookie)[0]['data'];ah={'X-CSRF-Token':state['csrf_token'],'Origin':origin}
        def mutate(path,body):return call(port,'POST',path,body,cookie,ah)[0]
        channel=dict(id='fixture',title='Fixture',provider='fixture',protocol='chat',url=f'http://127.0.0.1:{upstream.server_port}/chat',
            auth='bearer',anthropic_version='2023-06-01',timeout_ms=10000,first_byte_ms=5000,idle_ms=2000,max_concurrent=2,enabled=True,allow_http=True)
        mutate('/admin/model-gateway/channel',channel);mutate('/admin/model-gateway/credentials',dict(channel_id='fixture',api_key='fixture-only-secret'))
        rates={k:0 for k in ('input','cache_read','cache_write_5m','cache_write_1h','output')}
        model=dict(id='ornith-1.5-35b',title='Ornith',description='Local fixture',context_window=240128,max_output=16384,enabled=True,member_only=False,
            tool_calling=True,vision=False,reasoning_efforts='',output_limit_field='max_tokens',default_protocol='chat',sale_rates=rates,cost_rates=None,
            daily_tokens=100000000,member_daily_tokens=0,routes=[dict(protocol='chat',channel_id='fixture',wire_model='ornith-wire',priority=0)])
        mutate('/admin/model-gateway/model',model)
        mutate('/admin/model-gateway/model',dict(model,id='glm-test',title='GLM test · VIP',context_window=1000000,max_output=65536,member_only=True,member_daily_tokens=100000000,token_pool='glm',token_weight_bps=10000,token_peak_multiplier_bps=10000,reasoning_efforts='low,high,max',
            routes=[dict(protocol='chat',channel_id='fixture',wire_model='glm-test',priority=0)]))
        mutate('/admin/model-gateway/model',dict(model,id='glm-flash-test',title='Flash test',member_only=True,member_daily_tokens=100000000,token_pool='glm',token_weight_bps=4000,token_peak_multiplier_bps=10000,routes=[dict(protocol='chat',channel_id='fixture',wire_model='glm-flash-test',priority=0)]))
        mutate('/admin/billing/plan',dict(id='vip',title='VIP',duration_days=30,period_days=30,credit_micros=0,discount_bps=10000,concurrency_limit=2,model_ids='ornith-1.5-35b,glm-test,glm-flash-test',enabled=True))
        owner=call(port,'POST','/api/v1/register',dict(username='online_model_user',password='Fixture-only-2026'),status=201)[0]['data']['id']
        with tempfile.TemporaryDirectory(prefix='online-model-',dir=ROOT/'.build',ignore_cleanup_errors=True) as directory:
            site=Path(directory);shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
            with socket.socket() as sock:sock.bind(('127.0.0.1',0));app_port=sock.getsockname()[1]
            config=json.loads((site/'xs.json').read_text());config['services'][0]['port']=app_port
            config['services'][0]['class']='http';config['services'][0].pop('window',None);(site/'xs.json').write_text(json.dumps(config))
            unity=site/'generated/mdo_unity.c';unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
            router=site/'src/api/router.c';source=router.read_text();source=source.replace('static const MdoApiRoute g_MdoApiRoutes[]',HOOK+'\nstatic const MdoApiRoute g_MdoApiRoutes[]').replace('static const MdoApiRoute g_MdoApiRoutes[] = {','static const MdoApiRoute g_MdoApiRoutes[] = {\n {"/api/v1/test-model",XHTTP_METHOD_GET,"GET",OnlineTestRoute,false},');router.write_text(source)
            native=launch(host,site,site/'mdo-home');ready(app_port,native,'/api/v1/account')
            def app(method,path='/account',body=None,status=200,headers=None):
                headers=dict(headers or {})
                if method!='GET':headers['X-Mdo-Write-Token']=request(app_port,'GET','/api/v1/account')[1]['X-Mdo-Write-Token']
                reply=call(app_port,method,'/api/v1'+path,body,headers=headers,status=status)[0]
                return reply.get('data',reply)
            def wait(predicate):
                end=time.monotonic()+25
                while time.monotonic()<end:
                    value=app('GET')
                    if predicate(value):return value
                    time.sleep(.1)
                raise AssertionError(value)
            assert not app('GET','/test-model')['success'] and not Upstream.calls
            app('POST','/account/login',dict(identifier='online_model_user',password='Fixture-only-2026',remember=False))
            state=wait(lambda v:v.get('model_allowance',{}).get('group_id')=='free' and not v['busy'])
            assert state['balance_status']==200 and state['balance']['available_micros']==0,state
            mutate('/admin/billing/adjust',dict(member_id=owner,amount_micros=12345000,operation_id='fixture-cash',reason='Fixture balance'))
            mutate('/admin/billing/grant',dict(member_id=owner,amount_micros=5000000,expires_at=0,operation_id='fixture-credit',reason='Fixture credit'))
            app('POST','/account/refresh',{})
            state=wait(lambda v:v.get('balance',{}).get('available_micros')==17345000 and not v['busy'])
            assert state['balance']['cash_micros']==12345000 and state['balance']['credit_micros']==5000000
            assert state['balance']['member_id']==owner and state['balance']['amount_scale']==1000000
            assert 'plan_id' not in state['balance']
            assert 'fixture-only-secret' not in json.dumps(state) and 'access_token' not in json.dumps(state)
            assert app('GET','/models')['models'][0]['provider_id']=='mdo-online'
            result=app('GET','/test-model');assert result['success'],result
            state=wait(lambda v:v.get('model_allowance',{}).get('daily_quotas',[{}])[0].get('used_tokens')==120 and not v['busy'])
            assert state['model_allowance']['daily_quotas'][0]['limit_tokens']==100000000
            mutate('/admin/billing/subscribe',dict(member_id=owner,plan_id='vip',operation_id='fixture-subscribe'))
            app('POST','/account/refresh',{})
            state=wait(lambda v:v.get('model_allowance',{}).get('is_vip') and not v['busy'])
            assert any(q['unlimited'] for q in state['model_allowance']['daily_quotas'])
            pooled=[q for q in state['model_allowance']['daily_quotas'] if q.get('token_pool')=='glm']
            assert len(pooled)==2 and sorted(q['token_weight_bps'] for q in pooled)==[4000,10000]
            assert next(q for q in pooled if q['model_id']=='glm-flash-test')['available_model_tokens']==250000000
            assert any(m['id']=='mdo-online.glm-test' for m in app('GET','/models')['models'])
            result=app('GET','/test-model?glm');assert result['success'],result
            # Exercise the real Agent callback route and streamed completion,
            # rather than only the direct model adapter above.
            Upstream.agent_mode=True
            session=app('POST','/sessions',dict(project_id='default',title='Online agent fixture',agent_id='mdo.default',
                model_id='mdo-online.glm-test',protocol='openai-chat-completions',reasoning_effort='high',max_output_tokens=128),status=201)
            route='/projects/default/sessions/'+session['id']
            run=app('POST',route+'/runs',dict(prompt='Return the fixture reply'),status=202)
            end=time.monotonic()+10
            while time.monotonic()<end:
                result=app('GET','/runs/'+str(run['id']))
                if result['terminal']:break
                time.sleep(.1)
            assert result['state']=='succeeded',(result,app('GET',route+'/events?after=0&limit=32'))
            run=app('POST',route+'/runs',dict(prompt='Continue the fixture conversation'),status=202)
            end=time.monotonic()+10
            while time.monotonic()<end:
                result=app('GET','/runs/'+str(run['id']))
                if result['terminal']:break
                time.sleep(.1)
            assert result['state']=='succeeded',(result,app('GET',route+'/events?after=0&limit=32'))
            glm_requests=[body for _,body,_ in Upstream.calls if body.get('model')=='glm-test']
            assert all(body.get('thinking',{}).get('type')=='enabled' and body.get('reasoning_effort')=='high' for body in glm_requests)
            assert any(m.get('role')=='assistant' and m.get('reasoning_content')=='agent fixture thought' for m in glm_requests[-1]['messages'])
            assert any(m.get('role')=='tool' and m.get('tool_call_id')=='fixture-ls' for m in glm_requests[-1]['messages'])
            assert any(body.get('tool_stream') is True for body in glm_requests)
            # The composer sends an empty effort for a non-reasoning model.
            # Switching must retain the completed conversation and reopen it.
            current=app('GET',route)
            profile=dict(model_id='mdo-online.glm-flash-test',reasoning_effort='',permission_profile=current['permission_profile'])
            changed=app('PUT',route+'/profile',profile,headers={'If-Match':f'"mdo-session-{current["id"]}-{current["revision"]}"'})
            assert changed['model_id']==profile['model_id'] and changed['reasoning_effort']==''
            assert app('GET',route)['model_id']==profile['model_id']
            # Metadata owns model selection even before a new checkpoint is
            # saved. Restart between switching and the next model dispatch.
            native.terminate();native.wait(timeout=10);logs[-1].close()
            native=launch(host,site,site/'mdo-home');ready(app_port,native,'/api/v1/account')
            app('POST','/account/login',dict(identifier='online_model_user',password='Fixture-only-2026',remember=False))
            wait(lambda v:v.get('model_allowance',{}).get('is_vip') and not v['busy'])
            assert app('GET',route)['reasoning_effort']==''
            run=app('POST',route+'/runs',dict(prompt='Continue after switching to Flash'),status=202)
            end=time.monotonic()+10
            while time.monotonic()<end:
                result=app('GET','/runs/'+str(run['id']))
                if result['terminal']:break
                time.sleep(.1)
            assert result['state']=='succeeded',(result,[(e['kind'],e['text']) for e in app('GET',route+'/events?after=0&limit=32')['items'] if e['kind'] in ('error','agent_error','agent_done')])
            flash_requests=[body for _,body,_ in Upstream.calls if body.get('model')=='glm-flash-test']
            assert flash_requests and any(m.get('content')=='Continue the fixture conversation' for m in flash_requests[-1]['messages'])
            assert all('reasoning_effort' not in body for body in flash_requests)
            for model_id,effort,wire in [('mdo-online.glm-test','max','glm-test'),('ornith-1.5-35b','','ornith-wire')]:
                current=app('GET',route)
                headers={'If-Match':f'"mdo-session-{current["id"]}-{current["revision"]}"'}
                profile=dict(model_id=model_id,reasoning_effort=effort,permission_profile=current['permission_profile'])
                changed=app('PUT',route+'/profile',profile,headers=headers)
                assert changed['model_id']==model_id and changed['reasoning_effort']==effort
                run=app('POST',route+'/runs',dict(prompt='Continue with '+wire),status=202)
                end=time.monotonic()+10
                while time.monotonic()<end:
                    result=app('GET','/runs/'+str(run['id']))
                    if result['terminal']:break
                    time.sleep(.1)
                assert result['state']=='succeeded',result
                body=Upstream.calls[-1][1];assert body['model']==wire
                assert any(m.get('content')=='Continue after switching to Flash' for m in body['messages'])
                if effort:assert body['reasoning_effort']==effort
                else:assert 'reasoning_effort' not in body
            # Invalid identity/effort values must still reject without changing
            # the accepted model/revision or losing the completed conversation.
            current=app('GET',route)
            for model_id,effort in [('', ''),('missing-model', ''),('mdo-online.glm-test','invalid-effort')]:
                app('PUT',route+'/profile',dict(model_id=model_id,reasoning_effort=effort,permission_profile=current['permission_profile']),
                    status=422,headers={'If-Match':f'"mdo-session-{current["id"]}-{current["revision"]}"'})
                after=app('GET',route);assert (after['model_id'],after['revision'])==(current['model_id'],current['revision'])
            Upstream.agent_mode=False
            app('POST','/account/logout',{});state=wait(lambda v:v['state']=='signed_out' and not v['busy'])
            assert 'model_allowance' not in state
            assert 'balance' not in state and state['balance_status']==0
            assert not any(m['id']=='mdo-online.glm-test' for m in app('GET','/models')['models'])
            before=len(Upstream.calls);assert not app('GET','/test-model')['success'] and len(Upstream.calls)==before
            assert all(headers['Authorization']=='Bearer fixture-only-secret' for _,_,headers in Upstream.calls)
            native.terminate();native.wait(timeout=10);logs[-1].close()
        print('PASS native login, billing balance, token allowance, VIP catalog, model switching with history, logout and secret isolation')
    finally:
        for proc in reversed(processes):
            if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
        upstream.shutdown();upstream.server_close()
        for log in logs:log.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args().host.resolve())
