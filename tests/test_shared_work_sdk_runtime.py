"""Atomic shared task claims through the actual TCC/HTTP SDK."""
import argparse,json,tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from test_api_runtime import ROOT,request
from test_orchestration_sdk_runtime import Fixture

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='shared-work-') as tmp:
        f=Fixture(a.host.resolve(),Path(tmp));f.start()
        try:
            def call(action,**params):return f.call('work.'+action,dict(namespace='qa.work',**params))
            first=call('create',id='research',title='Research the problem');assert first['ready']
            assert call('create',id='research',title='Research the problem')==first
            f.call('work.create',dict(namespace='qa.work',id='research',title='Different work'),409)
            second=call('create',id='implement',title='Implement',depends_on=['research']);assert not second['ready']
            f.call('work.create',dict(namespace='qa.work',id='bad',title='Bad',depends_on=['unknown']),400)
            f.call('work.create',dict(namespace='qa.work',id='bad',title='Bad',depends_on=['research','research']),400)
            f.call('work.claim',dict(namespace='qa.work',id='implement',expected_revision=second['revision'],worker='bob',claim_id='first'),409)
            def claim(i):
                status,_,body=request(f.port,'POST','/api/v1/application/call',body=json.dumps({'method':'work.claim','context':{'project':'default'},'params':dict(namespace='qa.work',id='research',expected_revision=first['revision'],worker=f'worker{i}',claim_id=f'claim{i}')}).encode(),headers={'Content-Type':'application/json'})
                assert status in (200,409),(status,body);return status,json.loads(body)
            with ThreadPoolExecutor(max_workers=8) as pool:results=list(pool.map(claim,range(8)))
            assert [r[0] for r in results].count(200)==1,results
            claimed=next(r[1]['data'] for r in results if r[0]==200);token=claimed['claim_token']
            assert 'claim_token' not in call('get',id='research')
            assert call('claim',id='research',expected_revision=first['revision'],worker=claimed['worker'],claim_id=claimed['claim_id'])==claimed
            released=call('release',id='research',claim_token=token);assert released['ready']
            f.call('work.complete',dict(namespace='qa.work',id='research',claim_token=token,result='late result'),409)
            next_claim=call('claim',id='research',expected_revision=released['revision'],worker='next',claim_id='next-claim');assert next_claim['claim_token']!=token
            f.call('work.release',dict(namespace='qa.work',id='research',claim_token=token),409)
            f.stop();f.start()
            restored=call('claim',id='research',expected_revision=0,worker='next',claim_id='next-claim');assert restored==next_claim
            complete=call('complete',id='research',claim_token=restored['claim_token'],result={'answer':42,'ok':True});assert complete['state']=='completed'
            assert call('complete',id='research',claim_token=restored['claim_token'],result={'ok':True,'answer':42})==complete
            f.call('work.complete',dict(namespace='qa.work',id='research',claim_token=restored['claim_token'],result={'answer':0}),409)
            assert call('get',id='implement')['ready']
            assert [i['id'] for i in call('list',ready_only=True)['items']]==['implement']
            next_claim=call('claim',id='implement',expected_revision=second['revision'],worker='builder',claim_id='build')
            f.call('work.complete',dict(namespace='qa.work',id='implement',claim_token=next_claim['claim_token'],result='x'*16385),413)
            cancelled=call('cancel',id='implement',expected_revision=next_claim['revision'],reason='No longer needed');assert cancelled['state']=='cancelled'
            f.call('work.complete',dict(namespace='qa.work',id='implement',claim_token=next_claim['claim_token'],result='late result'),409)
            for i in range(15):call('create',id=f'page{i}',title='Pagination',description='x'*4000)
            page=call('list',limit=64);assert page['has_more'] and len(json.dumps(page))<40000
            seen=[i['id'] for i in page['items']];revision=page['revision']
            while page['has_more']:
                page=call('list',limit=64,offset=page['next'],expected_revision=revision);seen.extend(i['id'] for i in page['items'])
            assert len(seen)==17 and len(set(seen))==17,seen
            call('create',id='changed',title='New task')
            f.call('work.list',dict(namespace='qa.work',expected_revision=revision),409)
        finally:f.stop()
    print('PASS shared tasks: dependencies, single concurrent claimant, stale result rejection, claim recovery, idempotent completion, cancellation and bounded pagination')
if __name__=='__main__':main()
