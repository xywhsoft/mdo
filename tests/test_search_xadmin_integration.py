"""Finite real mdo -> unified website/JWT/search/quota tests, local only.

No live site, database, provider secret or public API request is used. The
website's fixture transport records actual dispatches, independent of retries.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import re
from pathlib import Path
import shutil
import sqlite3
import subprocess
import threading
import time

from remote_website_fixture import remote_website_fixture, HOME, WEBSITE_HOST
from test_mdo_delivery import request
from test_search_api_runtime import invoke, web


def run(host: Path, website_host: Path) -> dict:
    site, port, admin_user, admin_password = remote_website_fixture()
    origin = f"http://127.0.0.1:{port}"
    (site/'tests').mkdir()
    shutil.copy2(HOME/'tests/search_recovery_host.c',site/'tests/search_recovery_host.c')
    config = json.loads((site/'xs.json').read_text())
    config['services'][0]['host_default']['devfile'] = str(site/'tests/search_recovery_host.c')
    (site/'xs.json').write_text(json.dumps(config))
    env = os.environ.copy()
    env.pop('BOCHA_API_KEY',None); env.pop('ZAI_API_KEY',None)
    log = (site/'search-test.log').open('wb')
    process = subprocess.Popen([str(website_host.resolve()),str(site/'xs.json')],cwd=site,
        env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)

    def call(method, path, body=None, *, cookie=None, headers=None, expected=200):
        status, head, raw = request(port,method,path,body,cookie,headers)
        assert status == expected,(path,status,raw)
        return json.loads(raw),head

    def state():
        value = call('GET','/__test/search-state')[0]
        assert not value['contract_failed'],value
        return value

    records = {'requests':[]}

    def probe(query,success,marker,minimum_posts=1):
        before = state()['calls']
        output = invoke(host,origin+'/api/v1/search',[(json.dumps({'query':query,'count':2}),success)],token,external=True)
        assert marker in output,output
        assert 'test-bocha-secret' not in output and 'test-zai-secret' not in output,output
        assert output.count('execute_web_search=') == 1,output
        posts=int(re.search(r'case_fetches=0:(\d+)',output).group(1))
        assert posts >= minimum_posts,output
        dispatches=state()['calls']-before
        records['requests'].append({'query':query,'posts':posts,'provider_dispatches':dispatches,'success':success})
        return dispatches

    def policy(**changes):
        search.update(changes)
        reply,_ = call('POST','/admin/plugin/settings',{'name':'mdo','config':settings},cookie=cookie)
        assert reply['result'],reply

    def used():
        data,_ = call('GET','/api/v1/search/usage',headers=bearer)
        return data['data']['daily_used']

    def minute_used():
        data,_ = call('GET','/api/v1/search/usage',headers=bearer)
        return data['data']['minute_used']

    def budget(kind,amount,global_owner=False):
        now=int(time.time()); window=now//(60 if kind==0 else 86400)
        with sqlite3.connect(dbpath) as db:
            db.execute('INSERT OR REPLACE INTO search_budget(owner,kind,window,used) VALUES(?,?,?,?)',
                (0 if global_owner else member_id,kind,window,amount))

    try:
        end=time.monotonic()+35
        while time.monotonic()<end:
            assert process.poll() is None,'website fixture exited: '+str(site/'search-test.log')
            try:
                if call('GET','/__test/search-state')[0]['calls']==0: break
            except OSError: pass
            time.sleep(.1)
        else: raise AssertionError('website startup timed out: '+str(site/'search-test.log'))
        reply,head = call('POST','/admin/login',{'username':admin_user,'password':admin_password})
        assert reply['result'],reply
        cookie='; '.join(c.split(';',1)[0] for c in head['_cookies'])
        name='search_recovery_test'; password='Fixture-only-2026'
        call('POST','/api/v1/register',{'username':name,'password':password},expected=201)
        login,_=call('POST','/api/v1/login',{'identifier':name,'password':password})
        token=login['data']['access_token']; bearer={'Authorization':'Bearer '+token}
        with sqlite3.connect(site/'db/main.db') as db:
            member_id=db.execute('SELECT id FROM member WHERE username=?',(name,)).fetchone()[0]
        other_name='search_recovery_two'
        call('POST','/api/v1/register',{'username':other_name,'password':password},expected=201)
        other_login,_=call('POST','/api/v1/login',{'identifier':other_name,'password':password})
        other_bearer={'Authorization':'Bearer '+other_login['data']['access_token']}
        with sqlite3.connect(site/'db/main.db') as db:
            db.execute('UPDATE member SET phone=?,phone_key=?,phone_verified_at=1 WHERE username=?',
                ('+8613800138001','+8613800138001',other_name))
        settings=json.loads((site/'options/plugin/mdo.json').read_text())
        search=settings['search']
        policy(verification='phone',minute_limit=60,daily_limit=100,global_daily_limit=10000,
               max_concurrent=4,zai_region='global',bocha_enabled=True,zai_enabled=True)
        records['contact_required']=probe('hello',False,'phone/email verification')
        assert records['contact_required']==0
        with sqlite3.connect(site/'db/main.db') as db:
            db.execute('UPDATE member SET phone=?,phone_key=?,phone_verified_at=1 WHERE id=?',
                ('+8613800138000','+8613800138000',member_id))
        records['provider_unconfigured']=probe('hello',False,'administrator must configure')
        assert records['provider_unconfigured']==0
        credentials,_=call('GET','/admin/web-search/credentials',cookie=cookie)
        call('POST','/admin/web-search/credentials',{'bocha':'test-bocha-secret','zai':'test-zai-secret'},cookie=cookie,
            headers={'Origin':origin,'X-CSRF-Token':credentials['data']['csrf_token']})
        dbpath=site/'db/plugin/web-search/plugin.db'
        assert dbpath.is_file(),list((site/'db').rglob('*.db'))
        for provider in ('bocha','zai'):
            policy(default_provider=provider)
            records[provider]=probe('hello',True,f'"source":"{provider}"')
            assert records[provider]==1
        policy(default_provider='bocha')
        for query,marker in [('upstream401','rejected the service credentials'),('upstream429','provider rate limit'),
                             ('malformed','invalid response'),('transport','temporarily unavailable')]:
            before=used(); records[query]=probe(query,False,marker)
            assert records[query]==1 and used()==before+1
        # Two controlled overlapping requests exercise one busy account, not load.
        policy(max_concurrent=1)
        with ThreadPoolExecutor(max_workers=1) as pool:
            slow=pool.submit(call,'POST','/api/v1/search',{'query':'slow','count':2},headers=bearer)
            end=time.monotonic()+2
            while not state()['entered'] and time.monotonic()<end:time.sleep(.02)
            assert state()['entered']
            blocked,_=call('POST','/api/v1/search',{'query':'hello'},headers=other_bearer,expected=429)
            assert blocked['data']['error']['code']=='server_busy' and not blocked['data']['error']['dispatched']
            other_usage,_=call('GET','/api/v1/search/usage',headers=other_bearer)
            assert other_usage['data']['daily_used']==0
            records['server_busy']=0
            before=used(); records['member_busy_recovered']=probe('hello',True,'"type":"web_search_results"',2)
            assert slow.result()[0]['code']==0
            assert records['member_busy_recovered']==1 and used()==before+1
        entered=threading.Event()
        def hold_budget():
            with sqlite3.connect(dbpath) as db:
                db.execute('BEGIN IMMEDIATE'); entered.set(); time.sleep(2); db.rollback()
        thread=threading.Thread(target=hold_budget);thread.start();assert entered.wait(2)
        before=used(); records['budget_busy_recovered']=probe('hello',True,'"type":"web_search_results"',2)
        thread.join(3); assert not thread.is_alive()
        assert records['budget_busy_recovered']==1 and used()==before+1
        policy(daily_limit=used())
        before=used(); minute_before=minute_used()
        records['daily_limit']=probe('hello',False,'Your daily search quota is exhausted')
        assert records['daily_limit']==0 and used()==before
        assert minute_used()==minute_before or minute_used()==0  # UTC window may roll over.
        policy(daily_limit=100)
        budget(1,10000,True)
        minute_before=minute_used()
        records['global_daily_limit']=probe('hello',False,"service's daily search quota is exhausted")
        assert records['global_daily_limit']==0 and used()==before
        assert minute_used()==minute_before or minute_used()==0
        budget(1,0,True); budget(0,60)
        value,_=call('POST','/api/v1/search',{'query':'hello'},headers=bearer,expected=429)
        error=value['data']['error']
        assert error['code']=='minute_limit' and error['retry_safe'] and not error['dispatched']
        assert 1000 <= error['retry_after_ms'] <= 61000 and used()==before
        records['minute_limit']=0
        records['provider_dispatches']=state()['calls']
        return records
    finally:
        if process.poll() is None:
            process.terminate()
            try: process.wait(5)
            except subprocess.TimeoutExpired: process.kill();process.wait(5)
        log.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=web.ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=WEBSITE_HOST)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args(); records=run(args.host,args.website_host)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(records,indent=2)+'\n')
    print('PASS unified website/JWT/search reasons, quiet recovery, exact provider dispatch/quota',records)


if __name__=='__main__':main()
