"""Opt-in Application SDK regression in an independent, debuggable Android APK.

The QA package must already be installed. All mutations stay inside its Home;
the daily mdo package is never opened or changed. No real model is called.
"""
import argparse
import http.client
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import threading
import time
import uuid

from test_application_sdk_runtime import COMMAND, MODULE, ModelHandler
from test_api_runtime import ROOT

ANDROID_MODULE = MODULE.replace('#include "mdo/native/config.h"', '#include "mdo/native/config.h"\n#include "mdo/native/sessions.h"').replace('static mdo_result Register(', r'''
static mdo_result Diagnose(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 (void)U;(void)P;(void)E;MdoSessionCreateOptions O;MdoSessionCreateOptionsInit(&O);O.ProjectId="default";
 xwork_error X;MdoSession* S=MdoSessionCreate(&O,&X);const mdo_values_v1* V=C->Api->Values;
 *R=V->Object();xvalue* B=V->Bool(S!=NULL);V->Set(*R,"ok",B);V->Release(B);
 if(!S){B=V->String(X.sMessage);V->Set(*R,"cause",B);V->Release(B);
 xvalue* A=V->Array();char* Path=xrtStrDup(C->Workspace);
 for(size_t i=0;Path&&i<32;i++){xrtClearError();xroot Root=xrtRootOpen(Path);xfileinfo I={0};bool Found=Root&&xrtRootStat(Root,".git",false,&I);
 xvalue* Row=V->Object();B=V->String(Path);V->Set(Row,"path",B);V->Release(B);B=V->UInt(xrtGetError()?xrtErrorKind(xrtGetError()):0);V->Set(Row,"kind",B);V->Release(B);V->Append(A,Row);V->Release(Row);if(Root)xrtRootClose(Root);
 char* Parent=xrtPathParent(Path);bool Stop=Found||!Parent||!strcmp(Path,Parent);xrtFree(Path);Path=Parent;if(Stop)break;}
 xrtFree(Path);V->Set(*R,"ancestors",A);V->Release(A);}
 MdoSessionRelease(S);return MDO_RESULT_OK;
}
static mdo_result Register(''').replace('return R->AddHook(R->Context,&HookSpec,Error,N);', '''S.Id="qa.diagnose";S.Execute=Diagnose;
 if(R->AddService(R->Context,&S,Error,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 return R->AddHook(R->Context,&HookSpec,Error,N);''')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb',type=Path,required=True);p.add_argument('--adb-port',type=int,default=5037)
    p.add_argument('--serial',required=True);p.add_argument('--package',required=True)
    p.add_argument('--evidence',type=Path,required=True)
    a=p.parse_args()
    if not re.fullmatch(r'org\.xleaves\.mdo\.qa\.[a-z][a-z0-9_]*',a.package):
        raise ValueError('Use an independent org.xleaves.mdo.qa.* package')
    a.evidence.mkdir(parents=True,exist_ok=False)
    report={'package':a.package,'checks':[],'paid_model_calls':0,'passed':False}
    command=[str(a.adb.resolve()),'-P',str(a.adb_port),'-s',a.serial]
    local_port=None;device_port=None;model=None;model_port=None;model_thread=None
    def adb(*args,check=True):
        r=subprocess.run([*command,*args],capture_output=True,timeout=30)
        if check and r.returncode:raise AssertionError(r.stderr.decode(errors='replace')[-1000:])
        return r.stdout.decode(errors='replace').strip()
    def launch():
        adb('shell','am','force-stop',a.package)
        adb('shell','run-as',a.package,'rm','-f','files/sdk-qa-home/native.log',check=False)
        adb('shell','am','start','-n',a.package+'/org.xserver.android.XsActivity')
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            log=adb('shell','run-as',a.package,'cat','files/sdk-qa-home/native.log',check=False)
            matches=re.findall(r'http bound on 127\.0\.0\.1:(\d+)',log)
            if matches and adb('shell','pidof',a.package,check=False):return int(matches[-1])
            time.sleep(.25)
        raise AssertionError('QA APK did not publish a native listener')
    def connect(port):
        nonlocal local_port,device_port
        if local_port is not None:adb('forward','--remove','tcp:'+str(local_port),check=False)
        local_port=int(adb('forward','tcp:0','tcp:'+str(port)));device_port=port
    def request(method,path,body=None):
        authority='127.0.0.1:'+str(device_port)
        headers={'Host':authority,'Origin':'http://'+authority}
        data=json.dumps(body).encode() if body is not None else None
        if data is not None:headers['Content-Type']='application/json'
        if method not in ('GET','HEAD','OPTIONS'):
            _,h,_=request('GET','/api/v1/bootstrap');headers['X-Mdo-Write-Token']=h['x-mdo-write-token']
        conn=http.client.HTTPConnection('127.0.0.1',local_port,timeout=25)
        try:
            conn.request(method,path,data,headers);r=conn.getresponse()
            return r.status,{k.lower():v for k,v in r.getheaders()},json.loads(r.read())
        finally:conn.close()
    def call(method,params=None,extension='android-sdk-qa',allow_error=False):
        status,_,value=request('POST','/api/v1/application/call',{'method':method,'params':params or {},'context':{'project':'default','extension':extension}})
        if allow_error:return status,value
        assert status==200,(method,status,value)
        return value['data']
    def mark(name):report['checks'].append(name);print('PASS '+name,flush=True)
    def invocation(method,params=None):
        identity='cmd-'+uuid.uuid4().hex
        status,_,v=request('POST','/api/v1/command-invocations?project=default',{'invocation_id':identity,'command_id':'sdk','raw_arguments':json.dumps([method,params or {}])})
        assert status==202,(status,v)
        end=time.monotonic()+25
        while time.monotonic()<end:
            _,_,v=request('GET','/api/v1/command-invocations/'+identity);r=v['data']
            if r['terminal']:assert r['state']=='succeeded',r;return r['result']
            time.sleep(.05)
        raise AssertionError('Android command worker timed out')
    try:
        assert 'uid=' in adb('shell','run-as',a.package,'id')
        connect(launch())
        status,_,bootstrap=request('GET','/api/v1/bootstrap');assert status==200 and bootstrap['data']['ready']
        info=call('runtime.info');report['version']=info['version'];report['sdk_version']=info['sdk_version']
        assert info['version']==json.loads((ROOT/'app/release.json').read_text())['version_name']
        directory=call('sdk.services');native=call('sdk.native');assert len(directory)>250 and len(native)>500
        contract=call('sdk.describe',{'id':'project.create'})['input_schema']
        assert 'workspace_root' in contract['$defs']['ProjectCreateInput']['properties']
        mark('JNI host, TCC directory, typed product contracts and native exports')
        for action in ('close','read','stat','release'):
            assert call('stream.'+action,{'handle':''},allow_error=True)[0]==400
        stream=call('stream.open',{'capacity':4})['handle']
        call('stream.write',{'handle':stream,'text':'abcd'})
        assert call('stream.read',{'handle':stream,'limit':2})['text']=='ab'
        call('stream.write',{'handle':stream,'text':'ef'});call('stream.close',{'handle':stream})
        end=call('stream.read',{'handle':stream});assert end['text']=='cdef' and end['eof']
        call('stream.release',{'handle':stream});assert call('stream.stat',{'handle':stream},allow_error=True)[0]==410
        mark('Invalid handles, bounded stream wraparound, EOF and stale handles')
        call('fs.write',{'path':'sdk-qa/file.txt','text':'SDK 中文'})
        assert call('fs.read',{'path':'sdk-qa/file.txt'})['text']=='SDK 中文'
        process=call('process.run',{'program':'/system/bin/sh','args':['-c','printf "%s|%s|%s" "$#" "$1" "$2"; exit 7','qa','','x']})
        assert process['exit_code']==7 and process['stdout']['text']=='2||x',process
        mark('APK UID Unicode filesystem and exact empty process arguments')
        call('fs.write',{'space':'home','path':'commands/sdk.c','text':'/* mdo-command {"id":"sdk","description":"Android SDK fixture"} */\n#include <string.h>\n'+COMMAND})
        call('fs.write',{'space':'home','path':'modules/tools/sdk.c','text':ANDROID_MODULE})
        call('runtime.reload',{'domain':'modules'})
        assert invocation('sdk.context')['project']=='default'
        assert invocation('qa.echo',{'android':True})=={'android':True,'hooked':True}
        mark('ARM64 native module exports, service/hook registration and isolated C command RPC')
        diagnostic=call('qa.diagnose');assert diagnostic['ok'],diagnostic
        session=call('session.create',{'body':{'project_id':'default','title':'SDK Android fixture','permission_profile':'full-access'}})['data']['id']
        tool=call('tool.call',{'project':'default','session':session,'tool':'qa.context','arguments':{}})
        assert tool['success'],tool
        context=json.loads(tool['text'][tool['text'].index('{'):tool['text'].rindex('}')+1])
        assert context['project']=='default' and context['session']==session,context
        mark('Managed tool executor and product identity in native tool SDK callbacks')
        model=ThreadingHTTPServer(('127.0.0.1',0),ModelHandler);model_port=model.server_port
        model_thread=threading.Thread(target=model.serve_forever,daemon=True);model_thread.start()
        adb('reverse','tcp:'+str(model_port),'tcp:'+str(model_port))
        models=call('product.models.config.get');patch={k:v for k,v in models['data'].items() if k!='runtime_override'}
        patch['providers']=[v for v in patch['providers'] if v['id']!='qa-local']
        patch['items']=[v for v in patch['items'] if v['id']!='qa-local-model']
        patch['providers'].append({'id':'qa-local','name':'QA Local','builtin':False,'editable':True,'removable':True,'verify_peer':True,'timeout_ms':5000,'endpoints':{'chat_completions':f'http://127.0.0.1:{model_port}/v1/chat/completions'}})
        item=patch['items'][0].copy();item.update({'id':'qa-local-model','name':'QA Local','provider':'qa-local','wire_model':'qa-local-model','builtin':False,'free':False,'editable':True,'removable':True,'protocols':['openai-chat-completions'],'default_protocol':'openai-chat-completions'})
        patch['items'].append(item)
        call('product.settings.by_domain.put',{'path':{'domain':'models'},'revision':models['revision'],'body':{'schema_version':1,'patch':patch}})
        stream=call('stream.open',{'capacity':65536})['handle']
        response=call('model.complete',{'model':'qa-local-model','messages':[{'role':'user','content':'offline Android SDK test'}],'stream':stream})
        assert response['text']=='sdk-model-ok' and response['usage']['input_tokens']==11,response
        assert 'sdk-model-ok' in call('stream.read',{'handle':stream})['text'];call('stream.release',{'handle':stream})
        mark('Native model boundary, local synthetic SSE stream and token usage')
        operation='sdk-'+uuid.uuid4().hex;once_path='sdk-qa/'+uuid.uuid4().hex+'/once.txt'
        call('operation.start',{'id':operation,'method':'fs.append','params':{'path':once_path,'text':'once'}})
        assert call('operation.wait',{'id':operation,'timeout_ms':3000})['state']=='succeeded'
        call('operation.start',{'id':operation,'method':'fs.append','params':{'text':'once','path':once_path}})
        assert call('fs.read',{'path':once_path})['text']=='once'
        call('operation.release',{'id':operation})
        sleeping=call('operation.start',{'method':'runtime.sleep','params':{'milliseconds':10000}})['id']
        assert call('qa.exclusive') is False
        call('operation.cancel',{'id':sleeping});assert call('operation.wait',{'id':sleeping,'timeout_ms':3000})['state']=='cancelled'
        call('operation.release',{'id':sleeping});assert call('qa.exclusive') is True
        mark('Canonical operation deduplication, cancellation and project lease lifetime')
        plugin_path='web/plugins/sdk-qa/value.txt'
        for value in ('one','two'):call('plugin.apply',{'id':'sdk-qa','files':[{'path':plugin_path,'text':value}]})
        call('plugin.disable',{'id':'sdk-qa'});call('plugin.enable',{'id':'sdk-qa'});call('plugin.rollback',{'id':'sdk-qa'})
        assert call('vfs.read',{'path':'/app/'+plugin_path})['text']=='one'
        mark('Android VFS publication, activation toggles and retained rollback')
        reference=call('secret.store',{'value':'sdk-test-only'})['reference']
        assert call('secret.read',{'reference':reference})['value']=='sdk-test-only';call('secret.remove',{'reference':reference})
        mark('APK credential vault store/read/remove')
        frontend='web/plugins/sdk-qa/panel.js'
        ui_path='ui-result-'+uuid.uuid4().hex+'.txt'
        script='''export function activate(sdk){sdk.ui.registerPanel("sdk.qa.panel",{title:"SDK QA",async render(root,{sdk}){await sdk.call("storage.write",{path:PATH,text:"scoped-webview-ok"});root.textContent="SDK QA OK";}});sdk.ui.registerMenu("sdk.qa.menu",{label:"SDK QA",execute:({sdk})=>sdk.ui.openPanel("sdk.qa.panel")});}'''.replace('PATH',json.dumps(ui_path))
        call('plugin.apply',{'id':'sdk-qa','frontend':frontend,'files':[{'path':frontend,'text':script}]})
        adb('shell','am','force-stop',a.package);connect(launch())
        end=time.monotonic()+25;clients=[]
        while time.monotonic()<end:
            clients=call('ui.clients')
            if clients:break
            time.sleep(.2)
        assert len(clients)==1,clients
        action=call('ui.invoke',{'client':clients[0]['id'],'action':'ui.panel','params':{'id':'sdk.qa.panel'}})
        end=time.monotonic()+15;receipt=None
        while time.monotonic()<end:
            receipt=call('ui.result',{'id':action['id']})
            if receipt['state']!='pending':break
            time.sleep(.2)
        assert receipt['state']=='succeeded',receipt
        assert call('storage.read',{'path':ui_path},extension='plugin.sdk-qa')['text']=='scoped-webview-ok'
        assert call('storage.read',{'path':ui_path},extension='local-client',allow_error=True)[0]==404
        mark('Real WebView plugin activation, panel render, client ACK and plugin storage namespace')
        call('plugin.remove',{'id':'sdk-qa'})
        mark('Independent QA cleanup; daily mdo installation untouched')
        report['passed']=True
    finally:
        if local_port is not None:adb('forward','--remove','tcp:'+str(local_port),check=False)
        if model_port is not None:adb('reverse','--remove','tcp:'+str(model_port),check=False)
        if model is not None:model.shutdown();model.server_close();model_thread.join(timeout=5)
        adb('shell','am','force-stop',a.package,check=False)
        (a.evidence/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Android Application SDK: PASS',flush=True)

if __name__=='__main__':main()
