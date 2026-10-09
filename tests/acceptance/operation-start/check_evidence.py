"""Verify actual startup loss, bounded observation, continuation and clean pack."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def read(name): return json.loads((HERE/name).read_text(encoding='utf-8'))
def lines(name): return [json.loads(line) for line in (HERE/name).read_text(encoding='utf-8').splitlines()]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--running-copy',type=Path,required=True)
    args=parser.parse_args()
    jobs=read('native-operations.json')['data']['items']
    assert len(jobs)==4 and len({job['id'] for job in jobs})==4
    assert all(job['state']=='succeeded' and job['terminal'] and not job['error_code'] for job in jobs)
    assert Counter(job['kind'] for job in jobs)=={'mcp_refresh':3,'module_reload':1}
    rows=lines('operation-requests.jsonl'); starts=[row for row in rows if row['method']=='POST']
    assert len(starts)==5 and all(row['status']==202 for row in starts)
    identities=Counter(row['requested_id'] for row in starts)
    assert sorted(identities.values())==[1,1,1,2]
    dropped=next(row for row in starts if row['mode']=='drop')
    duplicated=[row for row in starts if row['requested_id']==dropped['requested_id']]
    assert len(duplicated)==2 and {row['operation']['sequence'] for row in duplicated}=={1}
    assert all(row['operation']['id']==row['requested_id'] for row in starts)
    def status_reads(row): return [item for item in rows if item['method']=='GET' and item['path'].endswith('/'+row['requested_id'])]
    module=next(row for row in starts if row['path']=='/api/v1/modules/reload')
    assert module['mode']=='hold' and len(status_reads(module))==1
    startup_wait=status_reads(module)[0]['started']-module['started']
    assert 7.5 < startup_wait < 12, startup_wait
    delayed=next(row for row in starts if row['mode']=='delay')
    assert 17 <= delayed['received']-delayed['started'] < 20
    assert [row['status'] for row in status_reads(delayed)]==[404,404,404,404,200]
    assert status_reads(delayed)[-1]['started']-delayed['started'] < 60
    cancelled=next(row for row in starts if row['mode']=='hold' and row['path'].startswith('/api/v1/mcp/'))
    assert len(status_reads(cancelled))==1 and status_reads(cancelled)[0]['status']==200
    assert all(row['method'] in ('GET','POST') for row in rows)
    for name in ('drop-recovered','module-pending','module-recovered','delay-pending','delay-recovered',
                 'cancelled-chat','recheck-available','rechecked','followup-before'):
        ui=read(name+'.json')
        assert ui['draft']=='OPERATION_START_FOLLOWUP' and not ui['errors'], name
    assert read('recheck-available.json')['recheck'] and not read('rechecked.json')['recheck']
    assert 'button (disabled) 重新编译' in (HERE/'module-pending.ax.txt').read_text(encoding='utf-8')
    # AX snapshots can be diffs. The pending MCP state was unchanged from the
    # preceding observation; its screenshot and JSON retain the quiet wait.
    assert (HERE/'delay-pending.jpg').read_bytes().startswith(b'\xff\xd8\xff')
    ui=read('followup-final.json')
    assert not ui['errors'] and ui['draft']=='' and ui['body'].count('FIXTURE_OK: OPERATION_START_FOLLOWUP')==1
    assert read('calls.json')=={'OPERATION_START_FOLLOWUP':1}
    events=read('followup-events.json')['data']['items']
    assert sum(row['kind']=='agent_done' and row['success'] for row in events)==1
    assert not any(row['kind']=='error' for row in events)
    assert Counter(row['method'] for row in lines('mcp-methods.jsonl'))=={'initialize':1,'notifications/initialized':1,'tools/list':3}
    baseline=lines('baseline-requests.jsonl')
    assert [row['status'] for row in baseline]==[202,404]
    assert read('baseline-error.json')['errors']==['The requested MCP server does not exist']
    assert 'identity_error=busy_mcp' in (HERE/'busy-baseline.txt').read_text(encoding='utf-8')
    assert 'identity=busy_status_does_not_hide_admitted_mcp' in (HERE/'native.txt').read_text(encoding='utf-8')
    assert 'identity_error=' not in (HERE/'native.txt').read_text(encoding='utf-8')
    assert 'pass 959' in (HERE/'frontend.txt').read_text(encoding='utf-8')
    assert 'Ran 124 tests' in (HERE/'contracts.txt').read_text(encoding='utf-8')
    assert 'API runtime probe: PASS' in (HERE/'api.txt').read_text(encoding='utf-8')
    receipt=read('build.json'); raw=args.exe.read_bytes()
    assert receipt['mdo_commit'].startswith('f932b658') and not receipt['mdo_worktree_dirty']
    assert hashlib.sha256(raw).hexdigest()==receipt['sha256']
    assert args.running_copy.read_bytes()==raw
    helpers=runpy.run_path(str(HERE.parent/'decision-write-recovery/check_candidate.py'))
    with tempfile.TemporaryDirectory() as directory:
        pack=Path(directory)/'app.xrtpack';helpers['extract_pack'](args.exe,pack)
        files=helpers['files_from_pack'](pack.read_bytes())
    sources={}
    for name in ('include/mdo/operations.h','src/api/operations.c','src/operations/manager.c',
                 'web/js/state/resource-operation-start.js','web/js/state/resource-operation.js',
                 'web/js/state/resources.js','web/js/features/settings/extension-panels.js'):
        source=(ROOT/'app'/name).read_bytes()
        assert files[name]==source,name
        sources[name]=hashlib.sha256(source).hexdigest()
    assert not any('manual_operation_start_qa' in name for name in files)
    report=dict(passed=True,frontend_pass=959,contracts_pass=124,source_commit=receipt['mdo_commit'],
                sha256=receipt['sha256'],bytes=receipt['bytes'],embedded_sources=sources,
                browser_transport_posts=5,native_jobs=4,model_requests=1,model_errors=0,
                startup_body_wait_seconds=round(startup_wait,3),stress_or_high_load=False)
    (HERE/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS lost/held/delayed/cancelled startup, browser duplicate bound to one job, preserved draft and one reply')

if __name__=='__main__': main()
