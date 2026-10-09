"""Finite unified-plugin receipt tests: durable single quota/provider submission.

Uses the isolated website/JWT fixture, never public providers or live databases.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import sqlite3
import time
from test_search_xadmin_integration import run, web, WEBSITE_HOST

PATH='/api/v1/search/requests'

def scenario(ctx):
    call,policy,state,used=map(ctx.__getitem__,('call','policy','state','used'))
    bearer,other,dbpath,member=map(ctx.__getitem__,('bearer','other_bearer','dbpath','member_id'))
    policy(default_provider='bocha',max_concurrent=4)
    result={}
    def search(body,expected=200,auth=bearer):
        return call('POST',PATH,body,headers=auth,expected=expected)[0]
    def row(key):
        with sqlite3.connect(dbpath) as db:
            usage=db.execute('SELECT bytes FROM search_receipt_usage WHERE id=1').fetchone()[0]
            total=db.execute('SELECT coalesce(sum(bytes),0) FROM search_receipt').fetchone()[0]
            assert usage==total and 0<=usage<=33554432,(usage,total)
            return db.execute('SELECT status,response,bytes FROM search_receipt WHERE owner=? AND id=?',(member,key)).fetchone()
    key='a'*32;body={'query':'hello','count':2,'request_id':key}
    before=state()['calls'];before_quota=used()
    first=search(body);second=search(body)
    assert first==second and first['data']['request_id']==key
    assert state()['calls']==before+1 and used()==before_quota+1
    assert row(key)[0]==200
    result['same_body_replay']={'dispatches':1,'quota':1,'same_response':True}
    policy(daily_limit=used(),max_results=1,bocha_enabled=False)
    assert search(body)==first  # Recovery precedes changed policy and quota.
    assert state()['calls']==before+1 and used()==before_quota+1
    policy(daily_limit=100,max_results=10,bocha_enabled=True)
    assert search({**body,'query':'different'},409)['data']['error']['code']=='request_conflict'
    assert search({'request_id':key,'count':2,'query':'hello'},409)['data']['error']['code']=='request_conflict'
    assert search(body,401,auth={})['data']['error']['code']=='login_required'
    assert state()['calls']==before+1 and used()==before_quota+1
    for invalid in ({**body,'request_id':'B'*32},{**body,'request_id':True},{**body,'extra':1}):
        search(invalid,400)
    result['conflict_or_invalid']={'new_dispatches':0}
    # The same key belongs to a different member independently, never exposes
    # another member's cached result or spends the first member's quota.
    other_before=call('GET','/api/v1/search/usage',headers=other)[0]['data']['daily_used']
    other_reply=search({**body,'query':'other-member'},auth=other)
    assert other_reply['data']['request_id']==key and used()==before_quota+1
    assert call('GET','/api/v1/search/usage',headers=other)[0]['data']['daily_used']==other_before+1
    result['member_isolation']={'other_dispatches':1,'other_quota':1}
    error_body={'query':'upstream401','request_id':'c'*32}
    before=state()['calls'];before_quota=used()
    error=search(error_body,502);again=search(error_body,502)
    assert error==again and error['data']['error']['code']=='provider_auth_failed'
    assert state()['calls']==before+1 and used()==before_quota+1
    assert 'test-bocha-secret' not in json.dumps(error)
    result['cached_provider_failure']={'dispatches':1,'quota':1,'same_response':True}
    slow_body={'query':'slow','request_id':'d'*32}
    entered=state()['entered_count'];before=state()['calls'];before_quota=used()
    with ThreadPoolExecutor(max_workers=1) as pool:
        task=pool.submit(search,slow_body)
        end=time.monotonic()+2
        while state()['entered_count']==entered and time.monotonic()<end:time.sleep(.02)
        assert state()['entered_count']>entered
        pending=search(slow_body,429)['data']
        assert pending['request_id']==slow_body['request_id'] and pending['error']=={
            'code':'request_pending','retry_safe':True,'dispatched':True,'retry_after_ms':1000}
        complete=task.result();assert search(slow_body)==complete
    assert state()['calls']==before+1 and used()==before_quota+1
    result['parallel_same_key']={'dispatches':1,'quota':1,'pending_then_same_result':True}
    # A rejected quota transaction also rolls back its receipt and pool bytes.
    denied_body={'query':'hello','request_id':'e'*32}
    policy(daily_limit=used());before=state()['calls'];before_quota=used()
    assert search(denied_body,429)['data']['error']['code']=='daily_limit'
    assert row(denied_body['request_id']) is None and state()['calls']==before and used()==before_quota
    policy(daily_limit=100);assert search(denied_body)['code']==0
    assert state()['calls']==before+1 and used()==before_quota+1
    result['rejected_reservation']={'orphan_receipts':0,'later_dispatches':1,'later_quota':1}
    # Completed rows survive a service restart. Auth keys/tokens are retained
    # only inside this fixture, and the provider counter resets in its host.
    before_quota=used();ctx['restart']()
    assert state()['calls']==0 and search(body)==first and used()==before_quota
    result['restart_completed']={'dispatches':0,'same_response':True}
    orphan={'query':'hello','request_id':'f'*32}
    raw=json.dumps(orphan).encode();now=int(time.time())
    with sqlite3.connect(dbpath) as db:
        db.execute('INSERT INTO search_receipt(owner,id,fingerprint,created,pending_until,bytes) VALUES(?,?,?,?,?,?)',
            (member,orphan['request_id'],hashlib.sha256(raw).hexdigest(),now,now-1,1048576))
    ctx['restart']()
    uncertain=search(orphan,502)
    assert uncertain['data']['error']['code']=='request_uncertain' and state()['calls']==0 and used()==before_quota
    result['restart_uncertain']={'dispatches':0,'reason':'request_uncertain'}
    with sqlite3.connect(dbpath) as db:
        db.execute('UPDATE search_receipt SET finished=? WHERE owner=? AND id=?',(now-601,member,key))
    assert search(body,410)['data']['error']['code']=='request_expired'
    assert state()['calls']==0 and used()==before_quota
    result['expired_result']={'dispatches':0,'quota':0}
    cleanup={'query':'hello','request_id':'1'*32}
    assert search(cleanup)['code']==0
    assert row(orphan['request_id'])[2]==0  # Expired orphan releases pool reservation.
    assert row(key)[1] is None and row(key)[2]==0  # Body expires, key remains.
    result['bounded_cleanup']={'pending_bytes_released':True,'expired_key_retained':True}
    # Synthetic metadata reservations exercise the pool bound without sending
    # concurrent requests or allocating large result bodies (not a load test).
    now=int(time.time());pool_keys=[f'{1000+i:032x}' for i in range(31)]
    with sqlite3.connect(dbpath) as db:
        db.executemany('INSERT INTO search_receipt(owner,id,fingerprint,created,pending_until,bytes) VALUES(?,?,?,?,?,?)',
            [(member,k,'0'*64,now,now+1000,1048576) for k in pool_keys])
    full_body={'query':'hello','request_id':'2'*32}
    before=state()['calls'];before_quota=used()
    full=search(full_body,503)['data']['error']
    assert full['code']=='receipt_busy' and full['retry_safe'] and not full['dispatched']
    assert row(full_body['request_id']) is None and state()['calls']==before and used()==before_quota
    with sqlite3.connect(dbpath) as db:
        db.executemany('UPDATE search_receipt SET pending_until=? WHERE owner=? AND id=?',
            [(now-1,member,k) for k in pool_keys])
    assert search(full_body)['code']==0
    assert state()['calls']==before+1 and used()==before_quota+1
    assert row(pool_keys[0])[2]==0 and row(pool_keys[-1])[2]==0
    result['pool_capacity_metadata']={'rejected_dispatches':0,'rejected_quota':0,
        'after_expiry_dispatches':1,'after_expiry_quota':1,'old_keys_retained':True}
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=Path,default=web.ROOT/'.build/host/xs.exe')
    p.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    p.add_argument('--output',type=Path)
    args=p.parse_args();record=run(args.host,args.website_host,scenario)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(record,indent=2)+'\n')
    print('PASS durable search receipts, quota, member isolation, conflicts, pending/restart/expiry',record)

if __name__=='__main__':main()
