"""Explicit orphan cleanup: real project mutations and PTYs, private Home only."""
import argparse,json,os,subprocess,sys,tempfile,time
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,wait_ready,request

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--evidence',type=Path);args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build' if os.name=='nt' else None,prefix='workbench-terminal-cleanup-') as raw:
        base=Path(raw);home=base/'home';port=free_port();config=write_site(base,port)
        def api(method,path,body=None,expected=200,headers=None):
            status,_,data=request(port,method,path,body=json.dumps(body).encode() if body is not None else None,headers={'Content-Type':'application/json',**(headers or {})})
            value=json.loads(data);assert status==expected,(path,status,value)
            return value['data'] if status<400 else value['error']
        def call(method,params=None,project='default',owner='qa.cleanup',expected=200):
            return api('POST','/api/v1/application/call',{'method':method,'params':params or {},'context':{'project':project,'extension':owner}},expected)
        def until(check):
            end=time.monotonic()+15
            while time.monotonic()<end:
                value=check()
                if value:return value
                time.sleep(.04)
            raise AssertionError('Terminal cleanup fixture timed out')
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
            try:
                wait_ready(port,process);rows={};settings={}
                for project in ['removed','moved','valid','corrupt']:
                    directory=base/project;directory.mkdir();api('POST','/api/v1/projects',{'id':project,'name':project,'workspace_root':str(directory)},201)
                    settings[project]={'key':project,'program':sys.executable,'args':['-c','pass']}
                    row=call('terminal.create',settings[project],project);rows[project]=row
                    until(lambda project=project,row=row:call('terminal.get',{'id':row['id']},project)['drained'])
                other=call('terminal.create',{**settings['removed'],'key':'other-owner'},'removed','qa.other')
                until(lambda:call('terminal.get',{'id':other['id']},'removed','qa.other')['drained'])
                active=call('terminal.create',{**settings['removed'],'key':'still-active','args':['-c','import time;time.sleep(60)']},'removed')
                api('DELETE','/api/v1/projects/removed',headers={'If-Match':'"mdo-project-removed-1"'})
                next_directory=base/'moved-next';next_directory.mkdir()
                api('PUT','/api/v1/projects/moved',{'name':'moved','workspace_root':str(next_directory),'default_model_id':''},headers={'If-Match':'"mdo-project-moved-1"'})
                corrupt_path=home/'projects/corrupt.json';corrupt=corrupt_path.read_bytes();corrupt_path.write_text('{broken')
                receipts={p.name:p.read_bytes() for p in (home/'data/application/terminal-receipts').glob('*.json')}
                store=home/'data/application/terminals.json';backup=store.with_suffix('.qa-backup');store.rename(backup);store.mkdir()
                assert call('terminal.prune',expected=503)['code']=='terminal_store_failed'
                store.rmdir();backup.rename(store)
                assert call('terminal.prune')=={'released':2}
                stored=json.loads(store.read_text(encoding='utf-8'))['terminals'];ids={row['id'] for row in stored}
                assert rows['removed']['id'] not in ids and rows['moved']['id'] not in ids
                assert {active['id'],other['id'],rows['valid']['id'],rows['corrupt']['id']}<=ids,'cleanup removed live, other-owner, valid or uncertain records'
                assert receipts=={p.name:p.read_bytes() for p in (home/'data/application/terminal-receipts').glob('*.json')},'cleanup removed creation tombstones'
                assert call('terminal.prune')=={'released':0}
                api('POST','/api/v1/projects',{'id':'removed','name':'restored','workspace_root':str(base/'removed')},201)
                assert call('terminal.create',settings['removed'],'removed',expected=410)['code']=='terminal_intent_retired','late creation retry was replayed'
                assert call('terminal.get',{'id':other['id']},'removed',expected=403)['code']=='terminal_owner_mismatch'
                call('terminal.close',{'id':active['id'],'epoch':active['epoch']},'removed')
                until(lambda:call('terminal.get',{'id':active['id']},'removed')['drained'])
                api('DELETE','/api/v1/projects/removed',headers={'If-Match':'"mdo-project-removed-1"'})
                assert call('terminal.prune')=={'released':1}
                assert call('terminal.prune',owner='qa.other')=={'released':1}
                corrupt_path.write_bytes(corrupt)
                assert call('terminal.get',{'id':rows['corrupt']['id']},'corrupt')['drained']
                result={'passed':True,'checks':['real unregister and workspace replacement','only drained own orphan records released','active and other owners retained','corrupt definitions retained','admission receipts retained','late retry never respawns','idempotent explicit cleanup']}
                if args.evidence:args.evidence.mkdir(parents=True,exist_ok=True);(args.evidence/'result.json').write_text(json.dumps(result,indent=2))
                print('Actual project unregister/move, drained orphan cleanup, owner isolation and no replay: PASS')
            except Exception:
                log.flush()
                if args.evidence:args.evidence.mkdir(parents=True,exist_ok=True);(args.evidence/'host.log').write_bytes((base/'host.log').read_bytes())
                raise
            finally:process.terminate();process.wait(timeout=15);time.sleep(1)

if __name__=='__main__':main()
