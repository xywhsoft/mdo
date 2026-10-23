"""Actual Git/TCC comparisons; private repositories and unchanged real index."""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-git-') as raw:
        base=Path(raw);repo=base/'repo';repo.mkdir();port=free_port();config=write_site(base,port)
        def git(*argv,cwd=repo,check=True):
            return subprocess.run(['git','-c','user.name=Workbench QA','-c','user.email=workbench@example.invalid',*argv],cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=check)
        git('init','-b','master');(repo/'tracked.txt').write_text('one\ntwo\n',encoding='utf-8')
        (repo/'old name.txt').write_text('old name\n',encoding='utf-8');(repo/'binary.dat').write_bytes(b'a\0b')
        (repo/'magic[1].txt').write_text('literal old\n',encoding='utf-8');(repo/'--output=bad.txt').write_text('old\n',encoding='utf-8')
        (repo/'large.txt').write_text(''.join(f'old line {i}\n' for i in range(3000)),encoding='utf-8')
        git('add','--all');git('commit','-m','baseline');git('branch','baseline')
        (repo/'branch.txt').write_text('branch commit\n',encoding='utf-8');git('add','--all');git('commit','-m','branch change')
        (repo/'tracked.txt').write_text('one\nstaged\n',encoding='utf-8');git('add','tracked.txt');(repo/'tracked.txt').write_text('one\nworking\n',encoding='utf-8')
        git('mv','old name.txt','new name.txt');(repo/'new name.txt').write_text('old name\nnew content\n',encoding='utf-8')
        (repo/'binary.dat').write_bytes(b'changed\0bytes')
        (repo/'magic[1].txt').write_text('literal new\n',encoding='utf-8');(repo/'--output=bad.txt').write_text('new\n',encoding='utf-8')
        (repo/'large.txt').write_text(''.join(f'new line {i}\n' for i in range(3000)),encoding='utf-8')
        (repo/'小说 untracked.txt').write_text('第一行\nsecond\n',encoding='utf-8')
        index_before=hashlib.sha256((repo/'.git/index').read_bytes()).hexdigest()
        unborn=base/'unborn';unborn.mkdir();git('init','-b','master',cwd=unborn);(unborn/'new.txt').write_text('first\nsecond\n',encoding='utf-8')
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
            def call(method,params=None,status=200):
                code,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':method,'params':{'repository':str(repo),**(params or {})},'context':{'project':'default','extension':'qa.git'}}).encode(),headers={'Content-Type':'application/json'})
                value=json.loads(body);assert code==status,(method,code,value)
                return value['data'] if status==200 else value['error']
            try:
                wait_ready(port,process)
                state=call('changes.list');files={file['path']:file for file in state['files']}
                assert files['tracked.txt']['status']=='MM',state
                assert files['new name.txt']['old_path']=='old name.txt',files
                assert files['binary.dat']['binary'] is True
                assert files['小说 untracked.txt']['status']=='??'
                assert files['小说 untracked.txt']['stats_available'] and files['小说 untracked.txt']['additions']==2
                assert state['branch']=='master' and state['scope']=='workspace'
                refs=call('changes.branches');assert {'master','baseline'}<={row['name'] for row in refs}
                diff=call('changes.diff',{'path':'tracked.txt','revision':state['revision']})
                assert any(row.get('text')=='+working' and row.get('new_line')==2 for row in diff['rows']),diff
                assert any(row.get('text')=='-two' and row.get('old_line')==2 for row in diff['rows'])
                staged=call('changes.diff',{'scope':'staged','path':'tracked.txt'});assert any(row.get('text')=='+staged' for row in staged['rows'])
                unstaged=call('changes.diff',{'scope':'unstaged','path':'tracked.txt'});assert any(row.get('text')=='-staged' for row in unstaged['rows'])
                literal=call('changes.diff',{'path':'magic[1].txt'});assert any(row.get('text')=='+literal new' for row in literal['rows'])
                literal=call('changes.diff',{'path':'--output=bad.txt'});assert any(row.get('text')=='+new' for row in literal['rows']);assert not (repo/'bad.txt').exists()
                binary=call('changes.diff',{'path':'binary.dat'});assert binary['binary'] and any(row['kind']=='binary' for row in binary['rows'])
                new=call('changes.diff',{'path':'小说 untracked.txt'});assert new['additions']==2 and any(row.get('text')=='+第一行' for row in new['rows'])
                page=call('changes.diff',{'path':'large.txt','offset':0,'limit':30});assert page['more'] and len(page['rows'])==30 and page['total']>6000
                middle=call('changes.diff',{'path':'large.txt','offset':page['next'],'limit':2000});assert middle['offset']==page['next'] and any(row.get('old_line',0)>100 for row in middle['rows'])
                tail=call('changes.diff',{'path':'large.txt','offset':page['total']-10,'limit':20});assert not tail['more'] and tail['next']==tail['total'] and any(row.get('new_line',0)>2900 for row in tail['rows'])
                branch=call('changes.list',{'scope':'branch','base':'baseline'});assert [row['path'] for row in branch['files']]==['branch.txt']
                branch_diff=call('changes.diff',{'scope':'branch','base':'baseline','path':'branch.txt'});assert branch_diff['additions']==1
                call('changes.list',{'scope':'branch','base':'--output=bad'},422)
                call('changes.diff',{'path':'../outside'},400)
                call('changes.diff',{'path':'tracked.txt','offset':100000},416)
                call('changes.list',{'scope':'session'},409)
                (repo/'tracked.txt').write_text('later\n',encoding='utf-8');call('changes.diff',{'path':'tracked.txt','revision':state['revision']},409)
                fresh=call('changes.list',{'repository':str(unborn)});assert fresh['unborn'] and fresh['files'][0]['path']=='new.txt'
                fresh_diff=call('changes.diff',{'repository':str(unborn),'path':'new.txt'});assert fresh_diff['additions']==2
                assert hashlib.sha256((repo/'.git/index').read_bytes()).hexdigest()==index_before,'read services modified the real index'
                print('Git workbench actual status/scopes, renames, binary, Unicode/untracked/literal paths, paged large diffs, stale revisions and unborn repository: PASS')
            except Exception:
                log.flush();print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-3500:]);raise
            finally:process.terminate();process.wait(timeout=10)
if __name__=='__main__':main()
