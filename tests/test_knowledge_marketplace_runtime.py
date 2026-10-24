"""Two real isolated hosts: mdo -> marketplace -> review -> private binding."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from knowledge_fixture import package as knowledge_package, metadata, file
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'home/tests'))
from test_mdo_delivery import fixture,request,EXE
from test_api_runtime import free_port


def run():
    base,site,port,user,password=fixture('mdo-knowledge-marketplace'+('-ui' if '--ui' in sys.argv else ''))
    origin=f'http://127.0.0.1:{port}';(site/'db/identity.json').write_text(json.dumps(dict(public_origin=origin)))
    local=Path(tempfile.mkdtemp(prefix='client-',dir=base));app=local/'app';home=local/'home';home.mkdir();shutil.copytree(ROOT/'app',app)
    unity=app/'generated/mdo_unity.c';unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
    native_port=free_port();config=app/'xs.json';config.write_text(json.dumps(dict(services=[dict(enabled=True,name='mcp',ip='127.0.0.1',port=native_port,**{'class':'http'},host_default=dict(enabled=True,name='mdo',path='web',devlang='c',devfile='generated/mdo_unity.c'))])))
    processes=[];logs=[]
    def start(exe,config,cwd,private_home=None):
        log=(base/('native.log' if private_home else 'site.log')).open('wb');logs.append(log);env=dict(os.environ,USE_WEBVIEW='0')
        if private_home:env['MDO_HOME']=str(private_home)
        p=subprocess.Popen([str(exe),str(config)],cwd=cwd,env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));processes.append(p);return p
    def ready(proc,n,path):
        for _ in range(350):
            assert proc.poll() is None,('fixture stopped',str(base))
            try:
                if request(n,'GET',path)[0]==200:return
            except OSError:pass
            time.sleep(.1)
        raise AssertionError('fixture not ready '+str(base))
    def web(method,path,body=None,auth=None,cookie=None,status=200):
        code,head,raw=request(port,method,path,body,cookie,auth);assert code==status,(path,code,raw[:1200]);return json.loads(raw).get('data'),head
    def client(method,path,body=None,status=200,revision=None):
        headers={}
        if method!='GET':headers['X-Mdo-Write-Token']=request(native_port,'GET','/api/v1/account')[1]['X-Mdo-Write-Token']
        if revision:headers['If-Match']='"'+revision+'"'
        code,_,raw=request(native_port,method,'/api/v1'+path,body,headers=headers);assert code==status,(path,code,raw[:1600]);return json.loads(raw).get('data')
    def store(action,**body):return client('POST','/ecosystem',dict(action=action,single_kind='knowledge',**body),status=201 if action=='submit' else 200)
    prefix='/api/v1/mdo/ecosystem/'
    try:
        server=start(EXE,site/'xs.json',ROOT.parent/'home');ready(server,port,prefix+'packages')
        web('POST','/api/v1/register',dict(username='knowledge_author',password='Fixture-2026-password'),status=201)
        login,_=web('POST','/api/v1/login',dict(identifier='knowledge_author',password='Fixture-2026-password'));auth={'Authorization':'Bearer '+login['access_token']}
        _,head=web('POST','/admin/login',dict(username=user,password=password));cookie='; '.join(c.split(';',1)[0] for c in head['_cookies'])
        inventory,_=web('GET','/admin/api/mdo/ecosystem',cookie=cookie);csrf={'X-CSRF-Token':inventory['csrf']}
        native=start(ROOT/'.build/host/windows-server-glibc/xs.exe',config,app,home);ready(native,native_port,'/api/v1/account')
        client('POST','/account/login',dict(identifier='knowledge_author',password='Fixture-2026-password',remember=False))
        for _ in range(100):
            if client('GET','/account')['state']=='signed_in':break
            time.sleep(.1)
        # Author a real local folder, then export only that library through the
        # same SDK-backed publication route used by the settings page.
        recipe=knowledge_package();recipe['format']='mdo.extension.v2';recipe['manifest']['min_build_id']=30000126;resource=recipe['resources'][0];resource['files'].append(file('large.md','x'*300000))
        client('PUT','/extensions/knowledge/xrt-reference',dict(content=resource['content'],files=resource['files']),revision='new')
        exported=store('export',references=[dict(kind='knowledge',id='xrt-reference')]);recipe['resources']=exported['resources']
        assert '{$path}' in recipe['resources'][0]['content'] and str(home) not in json.dumps(recipe)
        first=store('submit',package=recipe,publish=False);identity=first['id'];assert not store('catalog')['items']
        rows,_=web('GET','/api/v1/mdo/creator/works?kind=knowledge',auth=auth);assert len(rows['rows'])==1
        web('POST','/api/v1/mdo/creator/publish',dict(source='package',id=identity,revision=1,publish=True,review_notes='Static documentation, original sources and redistribution permission supplied.'),auth=auth)
        web('POST','/admin/api/mdo/ecosystem',dict(action='approve',id=identity,revision=2,reason='Inspected reference documents, prompt and copyright'),auth=csrf,cookie=cookie)
        listing=store('catalog');assert len(listing['items'])==1 and listing['items'][0]['submitter']=='knowledge_author'
        detail=store('detail',id=identity);assert detail['package']==recipe
        original=client('GET','/extensions/knowledge/xrt-reference');client('DELETE','/extensions/knowledge/xrt-reference',revision=original['revision'])
        # Removing local authoring can leave its receipt; the explicit local
        # uninstall drops that empty receipt before installing the store copy.
        records=client('GET','/ecosystem')
        for k,r in records.items():
            if r.get('source')=='local':client('POST','/ecosystem',dict(action='uninstall',entry=dict(source='local',slug=r['slug'])))
        plan=store('plan',id=identity);store('install',id=identity,expected_plan=plan['sha256'])
        installed=client('GET','/extensions/knowledge/xrt-reference');assert installed['store_managed']and installed['copyright']=='Copyright original'
        assert client('GET','/extensions/knowledge/xrt-reference/documents?path=reference%2Fruntime.md')['text']=='XRT knowledge sentinel'
        client('PUT','/extensions/knowledge/xrt-reference',dict(content=resource['content']),revision=installed['revision'],status=403)
        client('POST','/ecosystem',dict(action='export',single_kind='knowledge',references=[dict(kind='knowledge',id='xrt-reference')]),status=403)
        disabled=client('POST','/extensions/knowledge/xrt-reference/enabled',dict(enabled=False),revision=installed['revision']);assert not disabled['enabled']
        # A new pending version leaves the old public version intact.
        update=copy.deepcopy(recipe);update['manifest']['version']='1.1.0';update['resources'][0]['files']=[file('reference/runtime.md','Updated reference sentinel')if f['path']=='reference/runtime.md'else f for f in update['resources'][0]['files']]
        pending=store('submit',package=update,publish=True,review_notes='Updated docs, original reference source and redistribution rights unchanged.');assert store('latest',id=identity)['id']==identity
        web('POST','/admin/api/mdo/ecosystem',dict(action='approve',id=pending['id'],revision=1,reason='Documentation update inspected'),auth=csrf,cookie=cookie)
        assert store('latest',id=identity)['id']==pending['id']
        plan=store('plan',id=pending['id']);store('install',id=pending['id'],expected_plan=plan['sha256'])
        assert not client('GET','/extensions/knowledge/xrt-reference')['enabled'],'update preserves local disable state'
        assert client('GET','/extensions/knowledge/xrt-reference/documents?path=reference%2Fruntime.md')['text']=='Updated reference sentinel'
        history=store('history',id=pending['id']);assert len(history['items'])==2
        rows,_=web('GET','/admin/api/mdo/creator/works?kind=knowledge&state=published',cookie=cookie);assert rows['total']==2
        own,_=web('GET','/api/v1/mdo/creator/works?kind=knowledge',auth=auth);work=own['rows'][0]
        if '--ui' not in sys.argv:
            web('POST','/api/v1/mdo/creator/work',dict(action='withdraw',work=work['work'],revision=work['work_revision']),auth=auth);assert not store('catalog')['items']
        assert client('GET','/extensions/knowledge/xrt-reference')['valid']
        if '--ui' in sys.argv:
            print(json.dumps(dict(native_port=native_port,site_port=port,base=str(base),home=str(home))),flush=True)
            while True:time.sleep(.25)
        # Preserve the complete document tree if any managed document changed.
        (home/'knowledge/xrt-reference/reference/runtime.md').write_text('locally edited reference')
        store('uninstall',entry=dict(id=pending['id']))
        assert (home/'knowledge/xrt-reference/knowledge.json').exists()
        retained=client('GET','/extensions/knowledge/xrt-reference');assert retained['valid']and not retained['store_managed']
        assert (home/'knowledge/xrt-reference/reference/runtime.md').read_text()=='locally edited reference'
        print('PASS real client/site knowledge authoring, export, review, download, independent category, update, disable preservation, withdrawal and edited-folder retention')
    finally:
        for p in reversed(processes):
            if p.poll() is None:p.terminate();p.wait(15)
        for log in logs:log.close()

if __name__=='__main__':run()
