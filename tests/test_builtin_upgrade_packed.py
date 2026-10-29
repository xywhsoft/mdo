"""Upgrade the shipped 0.7.0 builtin files and receipts through real startup/API."""
import argparse,hashlib,http.client,json,os,shutil,tempfile,zipfile
from pathlib import Path
from test_packed_home_lease import ROOT,site,start,stop,wait_bootstrap,release_packed_copies

FIXTURE=ROOT/'tests/fixtures/legacy-builtin-home-0.7.0.zip'

def check(packed,report=None,expect_legacy_failure=False):
    base=Path(tempfile.mkdtemp(prefix='builtin-upgrade-',dir=ROOT/'.build'))
    evidence={};passed=False
    try:
        for scenario in (('unchanged',) if expect_legacy_failure else ('unchanged','optouts','edited')):
            home=base/scenario;home.mkdir()
            with zipfile.ZipFile(FIXTURE) as z:z.extractall(home)
            state=home/'data/extensions/state.json'
            if scenario=='optouts':
                values=json.loads(state.read_text(encoding='utf-8'))
                values['commands:help']['enabled']=False;values['agents:default']['enabled']=False
                state.write_text(json.dumps(values),encoding='utf-8')
                shutil.rmtree(home/'commands/new')
            if scenario=='edited':
                code=home/'agents/default/agent.c';code.write_bytes(code.read_bytes()+b'\n/* retained local change */\n')
                edited=code.read_bytes()
            preserved=home/'data/upgrade-sentinel.txt';preserved.write_bytes(b'Existing private state stays in this Home.\n')
            directory,port=site(base,'site-'+scenario,packed)
            env={**os.environ,'USE_WEBVIEW':'0','MDO_UPGRADE_TEST_KEY':'upgrade-test-never-sent','MDO_ORNITH_API_KEY':'upgrade-test-never-sent',
                 'MDO_ORNITH_CHAT_COMPLETIONS_URL':'http://127.0.0.1:1/no-model-call',
                 'MDO_ORNITH_RESPONSES_URL':'http://127.0.0.1:1/no-model-call'}
            process=start(directory,packed,home,env)
            token='';etag=''
            def request(method,path,body=None,extra=None):
                nonlocal token,etag
                c=http.client.HTTPConnection('127.0.0.1',port,timeout=20)
                try:
                    headers={'Content-Type':'application/json','X-Mdo-Write-Token':token} if body is not None else {}
                    headers.update(extra or {})
                    c.request(method,path,json.dumps(body).encode() if body is not None else None,headers)
                    r=c.getresponse();token=r.getheader('x-mdo-write-token') or token
                    etag=r.getheader('etag') or etag
                    return r.status,json.loads(r.read())
                finally:c.close()
            def create(agent='mdo.default',model='upgrade-local'):
                return request('POST','/api/v1/sessions',dict(project_id='default',title='Upgrade regression',agent_id=agent,model_id=model,reasoning_effort='high',permission_profile='full-access'))
            try:
                _,boot=wait_bootstrap(process,port,directory/'packed.log');assert boot['data']['ready'],boot
                request('GET','/api/v1/bootstrap')
                rows={}
                for kind in ('agents','commands','skills'):
                    status,body=request('GET','/api/v1/extensions/'+kind);assert status==200,body
                    rows[kind]=body['data']['items']
                if expect_legacy_failure:
                    assert rows['agents'] and not rows['agents'][0]['valid']
                    status,body=create(model='ornith-1.5-35b')
                    assert status==422 and body['error']['code']=='session_profile_invalid',body
                    evidence['original_0_8_reproduces_missing_agent_as_invalid_profile']=True
                    continue
                if scenario=='unchanged':
                    assert sum(len(v) for v in rows.values())==14
                    assert all(row['valid'] and row['id'] and row['effective_enabled'] for values in rows.values() for row in values),rows
                    status,body=request('GET','/api/v1/models/config');assert status==200
                    config={k:v for k,v in body['data'].items() if k!='runtime_override'};tag=etag
                    config['providers'].append(dict(id='upgrade-local',name='Offline upgrade fixture',builtin=False,editable=True,removable=True,verify_peer=True,timeout_ms=30000,endpoints={'responses':'https://127.0.0.1:1/no-model-call'},credential={'secret_ref':'env:MDO_UPGRADE_TEST_KEY'}))
                    config['items'].append(dict(id='upgrade-local',name='Offline upgrade fixture',provider='upgrade-local',wire_model='upgrade-local',builtin=False,free=False,editable=True,removable=True,protocols=['openai-responses'],default_protocol='openai-responses',capabilities=['text-input','text-output','streaming'],window=dict(mode='shared-context',context_tokens=32000,max_input_tokens=30000,max_output_tokens=2000,output_reserve_tokens=1000,summary_tokens=500),reasoning_efforts=['none','high'],default_reasoning_effort='high',attachments=[]))
                    status,body=request('PUT','/api/v1/settings/models',dict(schema_version=1,patch=config),{'If-Match':tag});assert status==200,body
                    status,body=create();assert status in (200,201),body
                    evidence['real_0_7_home_creates_session_after_automatic_upgrade']=True
                    status,body=create(agent='missing-agent');assert status==422 and body['error']['code']=='agent_unavailable',body
                    status,body=create(model='missing-model');assert status==422 and body['error']['code']=='model_profile_model_missing',body
                    evidence['agent_and_model_errors_are_distinct']=True
                elif scenario=='optouts':
                    assert not any(row['directory_id']=='new' for row in rows['commands'])
                    assert not next(row for row in rows['commands'] if row['directory_id']=='help')['desired_enabled']
                    assert rows['agents'][0]['valid'] and not rows['agents'][0]['desired_enabled']
                    status,body=create();assert status==422 and body['error']['code']=='agent_unavailable',body
                    evidence['disabled_agent_and_command_and_removed_command_are_preserved']=True
                else:
                    assert (home/'agents/default/agent.c').read_bytes()==edited
                    assert not rows['agents'][0]['valid']
                    assert all(row['valid'] for row in rows['commands']+rows['skills'])
                    status,body=request('GET','/api/v1/diagnostics');assert status==200 and any(row.get('code')=='builtin_update_preserved' and row.get('path')=='agents/default' for row in body['data']['items']),body
                    status,body=create();assert status==422 and body['error']['code']=='agent_unavailable',body
                    evidence['edited_builtin_is_preserved_warned_and_does_not_block_other_upgrades']=True
                assert preserved.read_bytes()==b'Existing private state stays in this Home.\n'
                hashes={p.relative_to(home).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for kind in ('agents','commands','skills') for p in (home/kind).rglob('*') if p.is_file()}
                stop(process);process=start(directory,packed,home,env)
                _,boot=wait_bootstrap(process,port,directory/'packed.log');assert boot['data']['ready']
                again={p.relative_to(home).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for kind in ('agents','commands','skills') for p in (home/kind).rglob('*') if p.is_file()}
                assert hashes==again
                evidence[scenario+'_restart_is_idempotent']=True
                if scenario=='unchanged':
                    stop(process);code=home/'agents/default/agent.c';edited_current=code.read_bytes()+b'\n/* preserved current-version edit */\n';code.write_bytes(edited_current)
                    process=start(directory,packed,home,env);wait_bootstrap(process,port,directory/'packed.log');request('GET','/api/v1/bootstrap')
                    assert code.read_bytes()==edited_current
                    status,body=request('GET','/api/v1/diagnostics');assert status==200 and any(row.get('code')=='builtin_update_preserved' and row.get('path')=='agents/default' for row in body['data']['items']),body
                    status,body=create();assert status==422 and body['error']['code']=='agent_unavailable',body
                    evidence['modified_current_builtin_is_preserved_and_warned']=True
            finally:
                stop(process);release_packed_copies(directory/packed.name)
        passed=True
    finally:
        if passed and base.resolve().parent==(ROOT/'.build').resolve() and base.name.startswith('builtin-upgrade-'):shutil.rmtree(base)
        elif not passed:print('Failure evidence:',base)
    if report:Path(report).write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    print('PASS',len(evidence),'historical Home upgrade contracts');return evidence

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--packed',type=Path,required=True);p.add_argument('--report',type=Path);p.add_argument('--expect-legacy-failure',action='store_true');a=p.parse_args();check(a.packed.resolve(),a.report,a.expect_legacy_failure)
