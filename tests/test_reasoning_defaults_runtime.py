"""Implicit preferences adapt to model capabilities; explicit choices stay strict."""
import argparse,copy,json,os,shutil,subprocess,tempfile
from pathlib import Path
from model_fixture import model_catalog
from test_api_runtime import ROOT,free_port,request,wait_ready

def check(host):
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='reasoning-defaults-') as raw:
        base=Path(raw);site=base/'site';shutil.copytree(ROOT/'app',site);home=base/'home';port=free_port()
        catalog=model_catalog('no-thinking')
        for name,efforts,default in [('adaptive',['low','high'],'high'),('supported',['medium','high'],'high'),
                ('generic',['default','medium','high'],'default')]:
            model=copy.deepcopy(catalog['items'][0]);model.update(id=name,wire_model=name,reasoning_efforts=efforts,default_reasoning_effort=default)
            model['capabilities']+=['reasoning-output','reasoning-control'];catalog['items'].append(model)
        (home/'config').mkdir(parents=True)
        (home/'config/models.json').write_text(json.dumps(dict(schema_version=1,patch=catalog)),encoding='utf-8')
        role=home/'agents/strict';role.mkdir(parents=True)
        (role/'extension.json').write_text(json.dumps(dict(kind='agents',id='agent:qa.strict',name='Strict',alias='strict',entry='agent.json',platforms=['any'])),encoding='utf-8')
        (role/'agent.json').write_text(json.dumps(dict(schema='mdo.agent.v1',name='Strict',model='no-thinking',reasoning_effort='medium')),encoding='utf-8')
        config=site/'xs.json';config.write_text(json.dumps(dict(services=[dict(enabled=True,**{'class':'http'},name='reasoning-defaults',ip='127.0.0.1',port=port,
            host_default=dict(enabled=True,path='web',devlang='c',devfile='generated/mdo_unity.c'))])),encoding='utf-8')
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(host),str(config),'--','--home',str(home)],cwd=site,stdout=log,stderr=log,
                env=dict(os.environ,USE_WEBVIEW='0'),creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                wait_ready(port,process)
                def create(model,expected,effort=None,agent=None):
                    body=dict(project_id='default',model_id=model)
                    if effort is not None:body['reasoning_effort']=effort
                    if agent:body['agent_id']=agent
                    status,_,raw=request(port,'POST','/api/v1/sessions',body=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
                    data=json.loads(raw);assert status==expected,(status,data)
                    if status==201:return data['data']['reasoning_effort']
                    assert data['error']['code']=='model_profile_reasoning_unsupported',data
                assert create('no-thinking',201)=='none'
                assert create('adaptive',201)=='high'
                assert create('supported',201)=='high'
                assert create('generic',201)=='medium'
                create('no-thinking',422,'medium')
                assert create('adaptive',201,'low')=='low'
                create('no-thinking',422,agent='agent.strict')
                assert create('no-thinking',201,'none','agent.strict')=='none'
                print('PASS per-model default over global preference, generic global fallback, strict caller/role override and explicit precedence; no model calls')
            except Exception:
                print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-4000:]);raise
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,required=True)
    check(parser.parse_args().host.resolve())
