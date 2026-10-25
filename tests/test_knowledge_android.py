"""Bounded knowledge acceptance in an independent real Android QA APK.

ADB controls only the selected QA process and its private fixtures. All product
operations use its normal HTTP/TCC API; no production accounts or models are used.
"""
import argparse
import copy
import http.client
import json
from pathlib import Path
import re
import subprocess
import time
from urllib.parse import quote

from knowledge_fixture import metadata, file


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb',type=Path,required=True)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--package',required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--expect-preserved',action='store_true')
    parser.add_argument('--full',action='store_true')
    args=parser.parse_args()
    if not re.fullmatch(r'org\.xleaves\.mdo\.qa\.[a-z][a-z0-9_]*',args.package):
        raise ValueError('Only an independent org.xleaves.mdo.qa.* package is allowed')
    args.evidence.mkdir(parents=True,exist_ok=False)
    report={'package':args.package,'passed':False,'checks':[],'paid_model_calls':0}
    command=[str(args.adb.resolve()),'-s',args.serial]
    forwarded=None;device_port=None

    def adb(*argv,check=True):
        result=subprocess.run([*command,*argv],capture_output=True,timeout=35)
        if check and result.returncode:raise AssertionError(result.stderr.decode(errors='replace')[-1500:])
        return result.stdout.decode(errors='replace').strip()

    def mark(text):
        report['checks'].append(text);print('PASS '+text,flush=True)

    def launch():
        nonlocal forwarded,device_port
        adb('shell','am','force-stop',args.package)
        adb('shell','run-as',args.package,'rm','-f','files/mdo-home/native.log',check=False)
        adb('shell','am','start','-n',args.package+'/org.xserver.android.XsActivity')
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            log=adb('shell','run-as',args.package,'cat','files/mdo-home/native.log',check=False)
            ports=re.findall(r'server.*?bound on 127\.0\.0\.1:(\d+)',log)
            if ports and adb('shell','pidof',args.package,check=False):break
            time.sleep(.25)
        else:raise AssertionError('QA native listener did not start: '+log[-2500:])
        if forwarded is not None:adb('forward','--remove','tcp:'+str(forwarded),check=False)
        device_port=int(ports[-1]);forwarded=int(adb('forward','tcp:0','tcp:'+str(device_port)))

    def request(method,path,body=None,status=200,revision=None):
        authority='127.0.0.1:'+str(device_port)
        headers={'Host':authority,'Origin':'http://'+authority}
        if method!='GET':
            _,head,_=request('GET','/bootstrap')
            headers['X-Mdo-Write-Token']=head['x-mdo-write-token']
        if revision:headers['If-Match']='"'+revision+'"'
        if body is not None:headers['Content-Type']='application/json'
        conn=http.client.HTTPConnection('127.0.0.1',forwarded,timeout=30)
        try:
            conn.request(method,'/api/v1'+path,json.dumps(body).encode()if body is not None else None,headers)
            response=conn.getresponse();raw=response.read()
            assert response.status==status,(path,response.status,raw[:1500])
            return response.status,{k.lower():v for k,v in response.getheaders()},json.loads(raw)
        finally:conn.close()

    def call(method,path,body=None,**kwargs):return request(method,path,body,**kwargs)[2].get('data')
    def sdk(method,params=None):
        result=call('POST','/application/call',dict(method=method,params=params or {}))
        assert result.get('status',200)==200,result
        return result
    def warning_items():return [d for d in call('GET','/diagnostics')['items']if d['domain']=='knowledge']

    identity='qa-android-knowledge';path='/extensions/knowledge/'+identity
    deep_index='/'.join(['level']*9+['index.md'])
    limit=220-len('knowledge/'+identity+'/');levels=(limit-4)//2
    deep_path='a/'*levels+'x'*(limit-2*levels-3)+'.md'
    content=dict(metadata(),index=deep_index)
    documents=[file(deep_index,'# Android index'),file(deep_path,'Android preserved sentinel 中文')]
    try:
        assert 'uid='in adb('shell','run-as',args.package,'id')
        report['device']={'model':adb('shell','getprop','ro.product.model'),'android':adb('shell','getprop','ro.build.version.release'),'abi':adb('shell','getprop','ro.product.cpu.abi')}
        launch();assert call('GET','/bootstrap')['ready']
        info=sdk('runtime.info');report['runtime']=info
        if args.expect_preserved:
            assert call('GET',path+'/documents?path='+quote(deep_path,safe=''))['text']=='Android preserved sentinel 中文'
            marker=sdk('fs.read',dict(space='home',path='qa-upgrade-marker.txt'))
            assert 'keep across APK upgrade' in json.dumps(marker)
            mark('Same-package lite/full upgrade preserves knowledge and Home marker')
            current=next(v for v in call('GET','/extensions/knowledge')['items']if v['id']==identity)
            call('DELETE',path,revision=current['revision'])
        else:
            existing=next((v for v in call('GET','/extensions/knowledge')['items']if v['id']==identity),None)
            if existing:call('DELETE',path,revision=existing['revision'])
        first=call('PUT',path,dict(content=json.dumps(content),files=documents),revision='new')
        assert first['valid']and first['enabled']and first['inject_prompt']
        listed=call('GET',path+'/documents');assert {f['path']for f in listed['files']}=={deep_index,deep_path}
        assert call('GET',path+'/documents?path='+quote(deep_path,safe=''))['text']=='Android preserved sentinel 中文'
        mark('APK JNI/TCC custom knowledge, nine-level index and maximum portable document path')
        bundle=call('GET',path+'/bundle')
        first=call('PUT',path,dict(content=json.dumps(content),files=documents,bundle_revision=bundle['bundle_revision']),revision=first['revision'])
        call('PUT',path,dict(content=json.dumps(content),files=documents,bundle_revision=bundle['bundle_revision']),revision='stale',status=412)
        call('GET',path+'/documents?path=..%2F..%2Fconfig%2Faccount.json',status=400)
        bad=documents+[file('../outside.md','must not escape')]
        bundle=call('GET',path+'/bundle')
        call('PUT',path,dict(content=json.dumps(content),files=bad,bundle_revision=bundle['bundle_revision']),revision=first['revision'],status=422)
        assert len(call('GET',path+'/documents')['files'])==2
        mark('Atomic overwrite, stale-revision rejection and portable path containment')
        sdk('fs.write',dict(space='home',path='knowledge/'+identity+'/knowledge.json',text='{invalid'))
        assert any(d['subject_id']==identity and d['code']=='invalid_metadata'for d in warning_items())
        sdk('fs.write',dict(space='home',path='knowledge/'+identity+'/knowledge.json',text=json.dumps(content)))
        assert not any(d['subject_id']==identity for d in warning_items())
        mark('Broken knowledge is diagnosed and a repaired library clears its warning')
        first=next(v for v in call('GET','/extensions/knowledge')['items']if v['id']==identity)
        off=call('POST',path+'/enabled',dict(enabled=False),revision=first['revision']);assert not off['enabled']
        first=call('POST',path+'/enabled',dict(enabled=True),revision=off['revision']);assert first['enabled']
        selection=call('POST','/ecosystem',dict(action='export',single_kind='knowledge',references=[dict(kind='knowledge',id=identity)]))
        assert json.loads(selection['resources'][0]['content'])['format']=='mdo.knowledge.v1'
        exported=dict(format='mdo.extension.v2',manifest=dict(slug=identity,name='Android knowledge fixture',
            description='Inert QA documents',readme='Independent device fixture',version='1.0.0',
            license='MIT',min_build_id=60000009,platforms=['android-arm64-v8a']),resources=selection['resources'])
        incompatible=copy.deepcopy(exported);incompatible['manifest']['min_build_id']=60000002
        call('POST','/ecosystem',dict(action='plan_import',single_kind='knowledge',package=incompatible),status=409)
        call('DELETE',path,revision=first['revision'])
        plan=call('POST','/ecosystem',dict(action='plan_import',single_kind='knowledge',package=exported))
        call('POST','/ecosystem',dict(action='import',single_kind='knowledge',package=exported,expected_plan=plan['sha256']))
        assert call('GET',path+'/documents?path='+quote(deep_path,safe=''))['text']=='Android preserved sentinel 中文'
        mark('Enable state, knowledge v1 compatibility floor, export, recursive deletion and import')
        sdk('fs.write',dict(space='home',path='qa-upgrade-marker.txt',text='keep across APK upgrade'))
        before=adb('shell','pidof',args.package);launch()
        assert adb('shell','pidof',args.package)!=before
        assert call('GET',path+'/documents?path='+quote(deep_path,safe=''))['text']=='Android preserved sentinel 中文'
        assert 'keep across APK upgrade'in json.dumps(sdk('fs.read',dict(space='home',path='qa-upgrade-marker.txt')))
        mark('Force-stop and cold restart retain knowledge documents and Home data')
        if args.full:
            distribution=call('GET','/distribution');report['distribution']=distribution
            serialized=json.dumps(distribution)
            for tool in ['busybox','curl','jq','openssh','aria2','ripgrep','7zip','python','git','sqlite','poppler','ffmpeg','imagemagick']:
                assert tool in serialized,tool
            mark('Full APK retains all thirteen external tool registrations')
            programs={tool['id']:tool['path']for tool in distribution['tools']}
            probes={'busybox':['true'],'curl':['--version'],'jq':['--version'],'ssh':['-V'],
                'python':['-c','print("ANDROID_RUNTIME_OK")'],'aria2c':['--version'],'rg':['--version'],
                '7zip':['i'],'git':['--version'],'sqlite':['--version'],'poppler':['-v'],
                'ffmpeg':['-version'],'imagemagick':['-version']}
            report['tool_exits']={}
            for tool,argv in probes.items():
                result=sdk('process.run',dict(program=programs[tool],args=argv))
                assert result['exit_code']==0,(tool,result)
                report['tool_exits'][tool]=result['exit_code']
            mark('All thirteen bundled tool groups execute under the real APK UID, including the C++ dependencies')
        report['passed']=True
    finally:
        if forwarded is not None:adb('forward','--remove','tcp:'+str(forwarded),check=False)
        adb('shell','am','force-stop',args.package,check=False)
        (args.evidence/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Android knowledge acceptance: PASS',flush=True)


if __name__=='__main__':main()
