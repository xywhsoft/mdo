"""Deterministic steering/recovery races and immutable cancelled-turn changes.

Uses the existing xs/SDK directly in the project, with an offline model callback.
No provider calls, credentials, SDK copies or generated runtime dependencies.
"""
import argparse,json,os,socket,subprocess,tempfile,time
from pathlib import Path
from test_conversation_groups_runtime import prepare_history_home

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');a=p.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='steering-handoff-') as raw:
        base=Path(raw);workspace=base/'workspace';workspace.mkdir();home=base/'home';prepare_history_home(home)
        subprocess.run(['git','init','-b','master'],cwd=workspace,check=True,capture_output=True)
        (workspace/'tracked.txt').write_text('before\n',encoding='utf-8')
        subprocess.run(['git','add','--all'],cwd=workspace,check=True,capture_output=True)
        subprocess.run(['git','-c','user.name=QA','-c','user.email=qa@example.invalid','commit','-m','baseline'],cwd=workspace,check=True,capture_output=True)
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        probe=ROOT/'app/generated/steering_handoff_qa.c';config=ROOT/'app/xs.steering-handoff-qa.json';assert not probe.exists() and not config.exists()
        probe.write_text((ROOT/'tests/fixtures/steering-handoff.c').read_text(encoding='utf-8').replace('#include "generated/mdo_unity.c"','#include "mdo_unity.c"'),encoding='utf-8')
        config.write_text(json.dumps({'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'steering-qa','ip':'127.0.0.1','port':port,'host_default':{'enabled':True,'path':'web','devlang':'c','devfile':'generated/steering_handoff_qa.c'}}]}),encoding='utf-8')
        process=None
        try:
            with (base/'runtime.log').open('wb') as log:
                process=subprocess.Popen([str(a.host.resolve()),str(config)],cwd=ROOT/'app',stdout=log,stderr=log,env={**os.environ,'MDO_HOME':str(home),'MDO_CHANGES_WORKSPACE':str(workspace),'GIT_CEILING_DIRECTORIES':str(base)},creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            end=time.monotonic()+80
            while process.poll() is None and time.monotonic()<end:
                text=(base/'runtime.log').read_text(encoding='utf-8',errors='replace')
                if 'probe_done=1' in text:break
                time.sleep(.1)
            text=(base/'runtime.log').read_text(encoding='utf-8',errors='replace')
            proof=ROOT/'.build/steering-handoff-qa';proof.mkdir(exist_ok=True);(proof/'runtime.log').write_text(text,encoding='utf-8')
            assert 'steering_failed=0' in text and 'probe_done=1' in text,text[-8000:]
            print('PASS steering handoff, stale recovery actions rejected, cancelled/new turn changes and real manual-stop recovery')
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            probe.unlink(missing_ok=True);config.unlink(missing_ok=True)
if __name__=='__main__':main()
