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
from mcp_template_fixture import template
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'home/tests'))
from test_mdo_delivery import fixture,request,EXE
from test_api_runtime import free_port


def run():
    base,site,port,user,password=fixture('mdo-mcp-marketplace'+('-ui' if '--ui' in sys.argv else ''))
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
    def store(action,**body):return client('POST','/ecosystem',dict(action=action,single_kind='mcp',**body),status=201 if action=='submit' else 200)
    prefix='/api/v1/mdo/ecosystem/'
    try:
        server=start(EXE,site/'xs.json',ROOT.parent/'home');ready(server,port,prefix+'packages')
        web('POST','/api/v1/register',dict(username='mcp_author',password='Fixture-2026-password'),status=201)
        login,_=web('POST','/api/v1/login',dict(identifier='mcp_author',password='Fixture-2026-password'));auth={'Authorization':'Bearer '+login['access_token']}
        _,head=web('POST','/admin/login',dict(username=user,password=password));cookie='; '.join(c.split(';',1)[0] for c in head['_cookies'])
        inventory,_=web('GET','/admin/api/mdo/ecosystem',cookie=cookie);csrf={'X-CSRF-Token':inventory['csrf']}
        native=start(ROOT/'.build/host/windows-server-glibc/xs.exe',config,app,home);ready(native,native_port,'/api/v1/account')
        client('POST','/account/login',dict(identifier='mcp_author',password='Fixture-2026-password',remember=False))
        for _ in range(100):
            if client('GET','/account')['state']=='signed_in':break
            time.sleep(.1)
        recipe=template('shared-service')
        package=dict(format='mdo.extension.v1',manifest=dict(slug='shared-service',name='Shared service',version='1.0.0',description='Marketplace recipe',author='Upstream author',copyright='Copyright upstream',license='MIT',readme='No remote service is contacted in this fixture.',platforms=['windows-x86_64']),resources=[dict(kind='mcp',id='shared-service',content=json.dumps(recipe))])
        first=store('submit',package=package,publish=False);identity=first['id'];assert not store('catalog')['items']
        web('POST','/api/v1/mdo/creator/publish',dict(source='package',id=identity,revision=1,publish=True,review_notes='Public source and connection details supplied; no credential material.'),auth=auth)
        web('POST','/admin/api/mdo/ecosystem',dict(action='approve',id=identity,revision=2,reason='Inspected inert recipe and parameter mappings'),auth=csrf,cookie=cookie)
        listing=store('catalog',mcp_transport='streamable-http',mcp_auth='none');assert len(listing['items'])==1 and listing['items'][0]['submitter']=='mcp_author'
        detail=store('detail',id=identity);assert detail['package']==package
        plan=store('plan',id=identity);store('install',id=identity,expected_plan=plan['sha256'])
        assert not client('GET','/extensions/mcp')['items'],'template download must not create or connect runtime servers'
        one=client('POST','/mcp/connections',dict(action='save',id='home',receipt_id=identity,variant='default',values={}),revision='new')
        two=client('POST','/mcp/connections',dict(action='save',id='work',receipt_id=identity,variant='default',values={}),revision='new')
        assert one['submitter']=='mcp_author' and one['copyright']=='Copyright upstream' and not one['enabled']
        # Publishing an update keeps the older public version until approval.
        update=copy.deepcopy(package);update['manifest']['version']='1.1.0';updated_recipe=copy.deepcopy(recipe);updated_recipe['variants'][0]['config']['transport']['endpoint']='https://new.example.invalid/mcp';update['resources'][0]['content']=json.dumps(updated_recipe)
        pending=store('submit',package=update,publish=True,review_notes='Changed endpoint; operators and permissions must be checked again.');assert store('latest',id=identity)['id']==identity
        web('POST','/admin/api/mdo/ecosystem',dict(action='approve',id=pending['id'],revision=1,reason='Endpoint and source changes inspected'),auth=csrf,cookie=cookie)
        plan=store('plan',id=pending['id']);store('install',id=pending['id'],expected_plan=plan['sha256'])
        assert len(client('GET','/mcp/templates')['items'])==2
        assert json.loads(client('GET','/extensions/mcp/work')['content'])['transport']['endpoint']=='https://example.invalid/mcp'
        one=client('POST','/mcp/connections',dict(action='save',id='home',receipt_id=pending['id'],variant='default',values={}),revision=one['revision'])
        assert json.loads(one['content'])['transport']['endpoint']=='https://new.example.invalid/mcp'
        assert json.loads(client('GET','/extensions/mcp/work')['content'])['transport']['endpoint']=='https://example.invalid/mcp'
        # Removing a store receipt or withdrawing a work never remotely deletes
        # an already configured instance or its pinned immutable template.
        store('uninstall',entry=dict(id=identity));assert client('GET','/extensions/mcp/work')['valid']
        rows,_=web('GET','/api/v1/mdo/creator/works?kind=mcp',auth=auth);work=rows['rows'][0]
        if '--ui' not in sys.argv:
            web('POST','/api/v1/mdo/creator/work',dict(action='withdraw',work=work['work'],revision=work['work_revision']),auth=auth);assert not store('catalog')['items']
        assert client('GET','/extensions/mcp/home')['valid']
        old_template_file=home/one['template_receipt']['files'][0]['path']
        pinned=client('GET','/extensions/mcp/work');old_path=home/pinned['template_receipt']['files'][0]['path'];assert old_path.exists()
        client('POST','/mcp/connections',dict(id='work',action='delete'),revision=pinned['revision']);assert not old_path.exists(), 'last connection removal collects an uninstalled template'
        print('PASS real client/platform MCP submission, review, download, multi-instance pinning, independent upgrade, withdrawal and local retention')
        if '--ui' in sys.argv:
            print(json.dumps(dict(native_port=native_port,site_port=port,base=str(base),home=str(home))),flush=True)
            while True:time.sleep(.25)
    finally:
        for p in reversed(processes):
            if p.poll() is None:p.terminate();p.wait(15)
        for log in logs:log.close()


if __name__=='__main__':run()
