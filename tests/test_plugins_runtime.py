"""Unified plugins through a real isolated XS/TCC host; no provider requests."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_api_runtime import ROOT, free_port, request, wait_ready, write_site


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='plugins-runtime-') as tmp:
        base=Path(tmp);port=free_port();config=write_site(base,port)
        with (base/'probe.log').open('wb') as log:
            child=subprocess.Popen([str(args.host.resolve()),str(config),'--'],cwd=base,stdout=log,stderr=log,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                wait_ready(port,child)
                def call(method,params=None,status=200):
                    code,_,raw=request(port,'POST','/api/v1/application/call',body=json.dumps(dict(method=method,params=params or {})).encode(),headers={'Content-Type':'application/json'})
                    value=json.loads(raw);assert code==status,(method,code,value)
                    return value['data'] if status==200 else value['error']
                def install(id,**fields):
                    doc=dict(format='mdo.plugin.v1',id=id,name=id,version='1.0.0',description='Offline fixture',
                        overrides=[dict(source='value.txt',target='web/plugin-fixture/'+id+'.txt')])
                    doc.update(fields)
                    return call('plugin.apply',dict(id=id,manifest=doc,files=[dict(path='value.txt',text=id)]))
                def plan(changes):return call('plugin.plan',dict(changes=changes))
                def commit(changes):
                    p=plan(changes);assert p['allowed'],p
                    return call('plugin.commit',dict(changes=changes,expected_revision=p['base_revision'],expected_plan=p['plan_revision']))
                install('base')
                a=install('alpha',exclusive=['mdo.agent.default-orchestrator'])
                assert all(not r['enabled'] for r in a['items'])
                install('beta',exclusive=['mdo.agent.default-orchestrator'])
                commit([dict(id='alpha',enabled=True)])
                p=plan([dict(id='beta',enabled=True)])
                assert not p['allowed'] and any(r['code']=='exclusive' for r in p['issues']),p
                assert call('plugin.enable',dict(id='beta'),409)['code']=='sdk_plugin_conflict'
                commit([dict(id='alpha',enabled=False),dict(id='beta',enabled=True)])
                assert call('vfs.read',dict(path='/app/web/plugin-fixture/beta.txt'))['text']=='beta'
                assert call('vfs.read',dict(path='/app/web/plugin-fixture/alpha.txt'),404)
                install('worker',requires=[dict(id='base',min_version='1.0.0',max_version_exclusive='2.0.0')])
                p=plan([dict(id='worker',enabled=True)]);assert not p['allowed'] and p['issues'][0]['code']=='requires',p
                commit([dict(id='base',enabled=True),dict(id='worker',enabled=True)])
                p=plan([dict(id='base',enabled=False)]);assert not p['allowed']
                call('plugin.remove',dict(id='base'),409)
                p=plan([dict(id='beta',enabled=False)])
                install('additional')
                call('plugin.commit',dict(changes=[dict(id='beta',enabled=False)],expected_revision=p['base_revision'],expected_plan=p['plan_revision']),412)
                install('incompatible',conflicts=[dict(id='beta',reason='one-sided conflict')])
                p=plan([dict(id='incompatible',enabled=True)]);assert not p['allowed'] and p['issues'][0]['code']=='conflicts',p
                install('cycle-a',requires=[dict(id='cycle-b')]);install('cycle-b',requires=[dict(id='cycle-a')])
                p=plan([dict(id='cycle-a',enabled=True),dict(id='cycle-b',enabled=True)])
                assert not p['allowed'] and any(r['code']=='dependency_cycle' for r in p['issues']),p
                previous=call('plugin.list')
                doc=dict(format='mdo.plugin.v1',id='base',name='base',version='2.0.0',description='Breaks dependency',overrides=[dict(source='value.txt',target='web/plugin-fixture/base.txt')])
                call('plugin.apply',dict(id='base',manifest=doc,files=[dict(path='value.txt',text='new')]),409)
                assert call('plugin.list')==previous,'A rejected update changed the published plugin set'
                commit([dict(id='worker',enabled=False),dict(id='base',enabled=False)])
                print('Plugins: immutable mappings, mutual exclusion, one-sided conflicts, dependency bounds/cycles, atomic switching and stale plans PASS',flush=True)
            finally:
                child.terminate();child.wait(timeout=10)


if __name__=='__main__':main()
