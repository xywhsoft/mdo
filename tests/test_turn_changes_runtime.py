"""Real managed offline Agent replies, immutable Git review, no app/SDK copy."""
import argparse,json,os,socket,subprocess,tempfile,time
from pathlib import Path
from test_conversation_groups_runtime import prepare_history_home
from test_api_runtime import request,wait_ready
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');p.add_argument('--browser',action='store_true');a=p.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='turn-review-') as raw:
        base=Path(raw);workspace=base/'workspace';workspace.mkdir();home=base/'home';prepare_history_home(home)
        def git(*args):subprocess.run(['git',*args],cwd=workspace,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        git('init','-b','master')
        for name in ('tracked.txt','preexisting.txt','removed.txt'):(workspace/name).write_text('before\n',encoding='utf-8')
        git('add','--all');git('-c','user.name=QA','-c','user.email=qa@example.invalid','commit','-m','baseline')
        (workspace/'preexisting.txt').write_text('dirty before the reply\n',encoding='utf-8')
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        probe=ROOT/'app/generated/turn_changes_qa.c';config=ROOT/'app/xs.turn-changes-qa.json';assert not config.exists() and not probe.exists()
        probe.write_text((ROOT/'tests/fixtures/turn-changes.c').read_text(encoding='utf-8').replace('#include "generated/mdo_unity.c"','#include "mdo_unity.c"'),encoding='utf-8')
        config.write_text(json.dumps({'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'review-qa','ip':'127.0.0.1','port':port,'host_default':{'enabled':True,'path':'web','devlang':'c','devfile':'generated/turn_changes_qa.c'}}]}),encoding='utf-8')
        process=None
        try:
            with (base/'runtime.log').open('wb') as log:
                process=subprocess.Popen([str(a.host.resolve()),str(config)],cwd=ROOT/'app',stdout=log,stderr=log,env={**os.environ,'MDO_HOME':str(home),'MDO_CHANGES_WORKSPACE':str(workspace)},creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            end=time.monotonic()+90
            while process.poll() is None and time.monotonic()<end:
                text=(base/'runtime.log').read_text(encoding='utf-8',errors='replace')
                if 'probe_done=1' in text:break
                time.sleep(.1)
            text=(base/'runtime.log').read_text(encoding='utf-8',errors='replace');assert 'review_failed=0' in text and 'probe_done=1' in text,text[-10000:]
            receipts=home/'workbench-baselines/qa/sessions/turn-review/turns'
            assert len(list(receipts.glob('*.json')))==3
            first_turn=min(int(path.stem) for path in receipts.glob('*.json'))
            if a.browser:
                evidence=ROOT/'.build/turn-review-qa';evidence.mkdir(exist_ok=True)
                subprocess.run(['node',str(ROOT/'tests/fixtures/turn-changes-browser.cjs'),f'http://127.0.0.1:{port}',str(evidence)],cwd=ROOT,check=True,timeout=90)
            # Restart the real product entry, retaining only its private Home.
            process.terminate();process.wait(timeout=10)
            doc=json.loads(config.read_text(encoding='utf-8'));doc['services'][0]['host_default']['devfile']='generated/mdo_unity.c';config.write_text(json.dumps(doc),encoding='utf-8')
            with (base/'restart.log').open('wb') as log:
                process=subprocess.Popen([str(a.host.resolve()),str(config)],cwd=ROOT/'app',stdout=log,stderr=log,env={**os.environ,'MDO_HOME':str(home)},creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            wait_ready(port,process)
            def call(params,session='turn-review'):
                return request(port,'POST','/api/v1/application/call',headers={'Content-Type':'application/json'},body=json.dumps({'method':'changes.diff','params':{'scope':'turn','turn':first_turn,**params},'context':{'project':'qa','session':session}}).encode())
            status,_,body=call({'path':'tracked.txt'});assert status==200,body
            diff=json.loads(body)['data'];assert any(row.get('text')=='+first reply' for row in diff['rows']),diff
            status,_,body=call({'path':'../outside'});assert status==400,body
            print('PASS host restart retains exact diff and rejects traversal')
            print('PASS three managed replies: committed edits, existing dirty file excluded, add/delete/Unicode, immutable earlier diff and empty reply')
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            probe.unlink(missing_ok=True);config.unlink(missing_ok=True)
if __name__=='__main__':main()
