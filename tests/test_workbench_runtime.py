"""Real C worker / HTTP shared state, CAS, ownership, recovery and UI receipts."""
import argparse,json,os,subprocess,tempfile,time,uuid
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready
from test_application_sdk_runtime import COMMAND

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-qa-') as temporary:
        base=Path(temporary);port=free_port();config=write_site(base,port);home=base/'home';(home/'commands').mkdir(parents=True)
        csource=COMMAND.replace('mdo_result Code=mdoCall(S,Method,V->At(A,1),R,&E);','mdo_result Code=!strcmp(Method,"resource.put")?mdoResourcePut(S,V->At(A,1),R,&E):mdoCall(S,Method,V->At(A,1),R,&E);')
        (home/'commands/sdk.c').write_text('/* mdo-command {"id":"sdk","description":"Workbench fixture"} */\n#include <string.h>\n#include <mdo/workbench.h>\n'+csource,encoding='utf-8')
        (home/'commands/workbench-plan.c').write_text((ROOT/'examples/application-sdk/workbench-plan.c').read_text(encoding='utf-8'),encoding='utf-8')
        log=(base/'host.log').open('wb');process=None
        def start():
            nonlocal process
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
            try:wait_ready(port,process)
            except Exception:log.flush();raise AssertionError((base/'host.log').read_text(encoding='utf-8',errors='replace'))
        def stop():
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
        def call(method,params=None,owner='qa.workbench',session='',expected=200):
            status,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':method,'params':params or {},'context':{'project':'default','session':session,'extension':owner}}).encode(),headers={'Content-Type':'application/json'})
            value=json.loads(body);assert status==expected,(method,status,value)
            return value.get('data') if expected==200 else value['error']
        def native(method,params=None,command='sdk'):
            identity='cmd-'+uuid.uuid4().hex
            status,_,body=request(port,'POST','/api/v1/command-invocations?project=default',body=json.dumps({'invocation_id':identity,'command_id':command,'raw_arguments':json.dumps([method,params])}).encode(),headers={'Content-Type':'application/json'})
            assert status==202,(status,body)
            end=time.monotonic()+15
            while time.monotonic()<end:
                status,_,body=request(port,'GET','/api/v1/command-invocations/'+identity);value=json.loads(body)['data']
                if value['terminal']:assert value['state']=='succeeded',value;return value['result']
                time.sleep(.02)
            raise AssertionError('C invocation did not finish')
        try:
            start()
            assert native('runtime.info')['version']=='0.5.0'
            example=native('',command='workbench-plan');assert example['resource']['kind']=='plan'
            context=native('sdk.context');owner=context['extension']
            created=native('resource.put',{'title':'C plan','kind':'plan','revision':0,'value':{'items':[{'text':'Verify native SDK','done':False}]}})
            assert call('resource.get',{'id':created['id']},owner=owner)['value']==created['value']
            changed=call('resource.put',{**created,'revision':created['revision'],'value':{'items':[{'text':'JS edited C resource','done':True}]}},owner=owner)
            assert native('resource.get',{'id':created['id']})['value']==changed['value']
            assert call('resource.put',{**created,'value':{}},owner=owner,expected=409)['code']=='revision_conflict'
            call('resource.get',{'id':created['id']},owner='someone.else',expected=403)
            call('resource.put',{**changed,'value':{}},owner=owner,session='different',expected=409)
            call('resource.put',{'title':'Too large','kind':'text','revision':0,'value':'x'*262145},expected=400)
            call('panel.register',{'id':'qa.plan','title':'Plan','renderer':'plan'})
            panel=call('panel.open',{'client':'window-one','type':'qa.plan','key':'main','resource':{'id':created['id']}})
            assert call('panel.open',{'client':'window-one','type':'qa.plan','key':'main'})['id']==panel['id']
            other=call('panel.open',{'client':'window-one','type':'qa.plan','key':'main'},session='different')
            assert other['id']!=panel['id']
            call('panel.update',{'id':panel['id'],'revision':panel['revision'],'title':'Wrong context'},session='different',expected=409)
            panel=call('panel.update',{'id':panel['id'],'revision':panel['revision'],'pinned':True})
            call('panel.report',{'id':panel['id'],'client':'window-one','value':{'visible':True,'width':480}})
            assert call('panel.get',{'id':panel['id']})['runtime']['visible'] is True
            call('client.hello',{'client':'window-one','context':{}})
            call('panel.attach',{'id':panel['id'],'revision':panel['revision'],'client':'window-two'},expected=409)
            first=call('panel.layout.get',{'id':'profile-one'});assert first['revision']==0
            layout=call('panel.layout.put',{'id':'profile-one','revision':0,'value':{'order':[panel['id']],'width':480}})
            call('panel.layout.put',{'id':'profile-one','revision':0,'value':{}},expected=409)
            browser=call('browser.create',{'client':'window-one','url':'https://example.com','key':'main'})
            call('browser.attach',{'id':browser['id'],'revision':browser['revision'],'client':'window-two'},expected=409)
            receipt=call('browser.command',{'id':browser['id'],'command':'snapshot'})
            assert receipt['state']=='pending'
            call('browser.release',{'id':receipt['id']},expected=409)
            pending=call('client.poll',{'client':'window-one'});assert pending[0]['id']==receipt['id']
            call('client.ack',{'client':'window-one','id':receipt['id'],'success':True,'result':{'text':'actual browser result'}})
            result=call('browser.result',{'id':receipt['id']});assert result['state']=='succeeded' and result['result']['text']=='actual browser result'
            call('client.ack',{'client':'window-one','id':receipt['id'],'success':True,'result':{'text':'duplicate must not overwrite'}})
            assert call('browser.result',{'id':receipt['id']})['result']==result['result']
            large=call('browser.command',{'id':browser['id'],'command':'snapshot'})
            call('client.ack',{'client':'window-one','id':large['id'],'success':True,'result':{'text':'x'*300000}})
            assert len(call('browser.result',{'id':large['id']})['result']['text'])==300000
            call('browser.release',{'id':large['id']});call('browser.result',{'id':large['id']},expected=410)
            call('browser.command',{'id':browser['id'],'command':'click'},session='wrong',expected=409)
            stop();start()
            assert call('resource.get',{'id':created['id']},owner=owner)['value']==changed['value']
            assert call('panel.layout.get',{'id':'profile-one'})['revision']==layout['revision']
            assert call('browser.get',{'id':browser['id']})['state']=='suspended'
            browser=call('browser.attach',{'id':browser['id'],'revision':browser['revision'],'client':'window-two'});assert browser['client']=='window-two'
            assert 'runtime' not in call('panel.get',{'id':panel['id']})
            attached=call('panel.attach',{'id':panel['id'],'revision':panel['revision'],'client':'window-two'});assert attached['client']=='window-two'
            call('panel.unregister',{'id':'qa.plan'});assert call('panel.list')==[]
            stop();(home/'data/workbench.json').write_text('{broken',encoding='utf-8');start()
            call('panel.types',expected=503);assert (home/'data/workbench.json').read_text(encoding='utf-8')=='{broken'
            print('Workbench C/HTTP shared state, CAS, UI receipts and restart recovery: PASS')
        finally:stop();log.close()
if __name__=='__main__':main()
