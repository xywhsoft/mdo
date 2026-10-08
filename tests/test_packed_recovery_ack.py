"""Bounded recovery correlation/guards against a disposable packed TCC app."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from test_api_runtime import ROOT, request


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed',required=True,type=Path)
    parser.add_argument('--record',type=Path)
    args=parser.parse_args(); packed=args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix='recovery-ack-probe-',dir=ROOT/'.build') as raw:
        base=Path(raw); fixture=base/'fixture'; log=(base/'fixture.log').open('wb')
        child=subprocess.Popen([sys.executable,str(ROOT/'tests/manual_conversation_retry_qa.py'),
            '--packed',str(packed),'--directory',str(fixture),'--duration','300','--stream-wait','60'],
            cwd=ROOT,env=dict(os.environ,USE_WEBVIEW='0'),stdout=log,stderr=log,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            deadline=time.monotonic()+40
            while not (fixture/'state.json').exists():
                assert child.poll() is None,(base/'fixture.log').read_text(errors='replace')
                assert time.monotonic()<deadline
                time.sleep(.1)
            state=json.loads((fixture/'state.json').read_text());port=state['port']
            path=f"/api/v1/projects/default/sessions/{state['session']}"
            folder=fixture/'home/sessions/default'/state['session']
            def call(method,target,body=None,headers=None):
                supplied=dict(headers or {})
                if body is not None:supplied['Content-Type']='application/json'
                status,fields,raw=request(port,method,target,body=None if body is None else json.dumps(body).encode(),headers=supplied)
                return status,fields,json.loads(raw)
            def wait_for(predicate):
                end=time.monotonic()+15
                while not predicate():
                    assert time.monotonic()<end,'bounded condition did not complete'
                    time.sleep(.02)
            def calls():
                return json.loads((fixture/'calls.json').read_text()).get('streaming',0) if (fixture/'calls.json').exists() else 0
            def cancel(run_id):
                status,_,value=call('DELETE',f'/api/v1/runs/{run_id}')
                assert status==200,(status,value)
                wait_for(lambda:call('GET',f'/api/v1/runs/{run_id}')[2]['data']['terminal'])
            def recovery():
                status,_,value=call('GET',path+'/recovery');assert status==200,(status,value)
                return value['data']
            def files():
                return {p.name:p.read_bytes() for p in folder.iterdir() if p.name in
                    ('meta.json','snapshot.json','journal.jsonl','ui-events.jsonl')}
            status,_,value=call('POST',path+'/runs',{'prompt':'streaming'})
            assert status==202,(status,value)
            wait_for(lambda:calls()==1);cancel(value['data']['id'])
            original=recovery();assert original['resume_required'] and original['items']==[]
            assert original['automatic_resume'] is False, original
            body={'recovery_token':original['recovery_token'],'decisions':[],'client_resume_id':'a'*32}
            before=files()
            for bad in ('','A'*32,'short',None,32):
                status,_,value=call('POST',path+'/resume',{**body,'client_resume_id':bad})
                assert status==422 and value['error']['code']=='recovery_resume_invalid',(status,value)
                assert files()==before and calls()==1
            status,_,value=call('POST',path+'/resume',body)
            assert status==202 and value['data']['client_resume_id']=='a'*32,(status,value)
            run_id=value['data']['id'];wait_for(lambda:calls()==2)
            status,_,value=call('GET',f'/api/v1/runs/{run_id}')
            assert status==200 and value['data']['client_resume_id']=='a'*32 and value['data']['resume']
            status,_,value=call('GET','/api/v1/runs')
            matched=[item for item in value['data']['items'] if item['client_resume_id']=='a'*32]
            assert status==200 and len(matched)==1 and matched[0]['id']==run_id
            status,_,value=call('POST',path+'/resume',body)
            assert status==409 and value['error']['code']=='recovery_state_conflict' and calls()==2
            cancel(run_id)
            current=recovery()
            status,_,value=call('POST',path+'/resume',{'recovery_token':current['recovery_token'],'decisions':[]})
            assert status==202 and value['data']['client_resume_id']=='',(status,value)
            wait_for(lambda:calls()==3);cancel(value['data']['id'])
            current=recovery();assert current['resume_required']
            ending={'revision':current['revision'],'last_sequence':current['last_sequence']}
            status,_,value=call('POST',path+'/abandon',ending)
            assert status==200 and value['data']['resume_required'] is False
            closed=recovery()
            assert closed['resume_required'] is False and closed['revision']>current['revision'] and closed['last_sequence']>current['last_sequence']
            before=files();status,_,value=call('POST',path+'/abandon',ending)
            assert status==409 and files()==before and calls()==3
            _,old_headers,_=call('GET','/api/v1/bootstrap')
            (fixture/'restart').touch();wait_for(lambda:json.loads((fixture/'state.json').read_text())['pid']!=state['pid'])
            before=files()
            status,_,value=call('POST',path+'/resume',body,{'X-Mdo-Write-Token':old_headers['x-mdo-write-token']})
            assert status==412 and value['error']['code']=='write_token_conflict' and files()==before
        finally:
            (fixture/'stop').touch()
            try:child.wait(timeout=20)
            except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=10)
            log.close()
        assert child.returncode==0,(base/'fixture.log').read_text(errors='replace')
    record={'passed':True,'model_requests':3,'packed_sha256':hashlib.sha256(packed.read_bytes()).hexdigest(),
        'checks':['invalid correlation IDs do not change history or start a model',
            'accepted resume has the same correlation in the reply, run detail and run list',
            'a busy/stale recovery is refused without another model call',
            'legacy resume without a client ID still works',
            'ending advances a closed recovery boundary; repeating its old guard changes nothing',
            'host restart fences the old page and preserves files']}
    if args.record:args.record.parent.mkdir(parents=True,exist_ok=True);args.record.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))


if __name__=='__main__':main()
