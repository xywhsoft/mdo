"""Validate bounded schedule ACK-loss records, real UI and candidate bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def read(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))

def records(name):
    return [json.loads(row) for row in (HERE / name).read_text(encoding='utf-8').splitlines()]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--running-copy', type=Path, required=True)
    args = parser.parse_args()
    old = records('baseline-requests.jsonl')
    old_posts = [r for r in old if r['method'] == 'POST' and r['path'] == '/api/v1/schedules']
    assert len(old_posts) == 2 and all(r['status'] == 201 for r in old_posts)
    assert len({r['response']['data']['id'] for r in old_posts}) == 2
    assert read('baseline-native.json')['data']['total'] == 2
    new = records('schedule-requests.jsonl')
    creates = [r for r in new if r['method'] == 'POST' and r['path'] == '/api/v1/schedules']
    first = [r for r in creates if r['request']['label'] == '确认丢失后的新计划']
    assert [r['status'] for r in first] == [201, 409]
    assert first[0]['request']['id'] == first[1]['request']['id']
    first_id = first[0]['request']['id']
    second = [r for r in creates if r['request']['label'] == '核对失败后只读取原计划']
    assert len(second) == 1 and second[0]['status'] == 201
    second_id = second[0]['request']['id']
    assert first_id != second_id
    edits = [r for r in new if r['method'] == 'PUT' and r['path'] == f'/api/v1/schedules/{first_id}']
    assert len(edits) == 1 and edits[0]['mode'] == 'hold' and edits[0]['status'] == 200
    confirms = [r for r in new if r['method'] == 'GET' and r['path'] == edits[0]['path']
        and r['started'] > edits[0]['started']]
    assert any(7.5 <= r['started'] - edits[0]['started'] < 12 for r in confirms)
    pause = [r for r in new if r['method'] == 'PUT' and r['path'].endswith('/enabled')]
    assert pause and pause[0]['mode'] == 'drop' and pause[0]['status'] == 200
    assert all(r['request'] == {'enabled': False} for r in pause)
    runs = [r for r in new if r['method'] == 'POST' and r['path'].endswith('/run')]
    assert len(runs) == 1 and runs[0]['mode'] == 'hold' and runs[0]['status'] == 202
    exhausted = [r for r in new if r.get('phase') == 'confirmation-exhaustion']
    assert len(exhausted) == 6 and all(r['method'] == 'GET' and r['status'] == 503 for r in exhausted)
    assert all(r['path'] == f'/api/v1/schedules/{second_id}' for r in exhausted)
    gaps = [exhausted[i + 1]['started'] - exhausted[i]['started'] for i in range(5)]
    for actual, minimum in zip(gaps, [.5, 1, 2, 4, 8]):
        assert minimum * .9 <= actual < minimum * 1.5 + .2, gaps
    uncertain_edit = [r for r in new if r['method'] == 'PUT' and r['path'] == f'/api/v1/schedules/{second_id}']
    assert len(uncertain_edit) == 1 and uncertain_edit[0]['mode'] == 'hold'
    assert uncertain_edit[0]['request']['notify'] == '保存确认测试'
    assert 7.5 <= exhausted[0]['started'] - uncertain_edit[0]['started'] < 12
    for name in ('create-recovered', 'edit-pending', 'edit-recovered', 'run-pending',
                 'pause-recovered', 'confirmation-pending', 'retry-pending', 'confirmation-retried', 'followup-before'):
        ui = read(name + '.json')
        assert ui['draft'] == 'SCHEDULE_ACK_FOLLOWUP' and not ui['errors'], name
    assert read('create-recovered.json')['list'] == '1 项计划'
    assert read('edit-pending.json')['saveDisabled']
    assert not read('edit-recovered.json')['editorOpen']
    assert '已暂停' in read('pause-recovered.json')['body']
    run_ui = read('run-unconfirmed.json')
    assert run_ui['body'].count('暂时无法确认计划是否已运行，请先查看执行历史，勿重复启动。') == 1
    assert read('run-history.json')['history'] == '1 条记录'
    final = read('confirmation-exhausted.json')
    assert final['editorOpen'] and not final['saveDisabled']
    assert final['label'] == '核对失败后只读取原计划' and final['input'] == 'SCHEDULE_ACK_NOT_RUN'
    assert len(final['errors']) == 1 and '内容仍保留' in final['formNotice']
    assert read('confirmation-retried.json')['list'] == '2 项计划'
    assert read('confirmation-retried.json')['formNotice'] == ''
    assert read('retry-pending.json')['formNotice'] == '' and read('retry-pending.json')['saveDisabled']
    followup = read('followup-final.json')
    assert followup['draft'] == '' and not followup['errors']
    assert followup['body'].count('FIXTURE_OK: SCHEDULE_ACK_FOLLOWUP') == 1
    assert read('calls.json') == {'SCHEDULE_ACK_RUN': 1, 'SCHEDULE_ACK_FOLLOWUP': 1}
    history = read('native-history.json')['data']['items']
    assert len(history) == 1 and history[0]['result'] == 'succeeded'
    assert history[0]['text'] == 'FIXTURE_OK: SCHEDULE_ACK_RUN'
    catalog = read('native-schedules-before-delete.json')['data']
    assert catalog['total'] == 2 and catalog['runs_completed'] == 1 and catalog['runs_failed'] == 0
    assert read('native-schedules-after-delete.json')['data']['total'] == 0
    deleted = read('delete-probe.json')
    assert deleted['result']['data'] == {'id': first_id, 'removed': True}
    assert deleted['calls'] == ['DELETE', 'GET', 'GET']
    events = read('followup-events.json')['data']['items']
    assert sum(r['kind'] == 'agent_start' for r in events) == 1
    assert sum(r['kind'] == 'agent_done' and r['success'] for r in events) == 1
    assert not any(r['kind'] == 'error' for r in events)
    assert read('browser-logs.json') == []
    assert 'pass 979' in (HERE / 'frontend.txt').read_text(encoding='utf-8')
    assert 'pass 21' in (HERE / 'scoped.txt').read_text(encoding='utf-8')
    assert 'Ran 29 tests' in (HERE / 'contracts.txt').read_text(encoding='utf-8')
    assert '164 modules parsed and reachable' in (HERE / 'modules.txt').read_text(encoding='utf-8')
    receipt = read('candidate.json')
    assert receipt['mdo_commit'].startswith('b481fbdf') and not receipt['mdo_worktree_dirty']
    raw = args.exe.read_bytes()
    assert len(raw) == receipt['bytes'] and hashlib.sha256(raw).hexdigest() == receipt['sha256']
    assert args.running_copy.read_bytes() == raw
    helper = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
    with tempfile.TemporaryDirectory() as directory:
        pack = Path(directory) / 'app.xrtpack'
        helper['extract_pack'](args.exe, pack)
        files = helper['files_from_pack'](pack.read_bytes())
    sources = {}
    for name in ('web/js/state/schedule-write.js', 'web/js/state/schedules.js',
                 'web/js/features/schedules/schedule-panel.js', 'web/js/utils/dom.js',
                 'web/lang/en-US.json', 'web/lang/ru-RU.json', 'web/lang/zh-CN.json'):
        source = (ROOT / 'app' / name).read_bytes()
        assert files[name] == source, name
        sources[name] = hashlib.sha256(source).hexdigest()
    assert all(p.read_bytes().startswith(b'\xff\xd8\xff') for p in HERE.glob('*.jpg'))
    report = dict(passed=True, source_commit=receipt['mdo_commit'], sha256=receipt['sha256'],
        bytes=receipt['bytes'], embedded_sources=sources, frontend_pass=979, final_scoped_pass=21,
        contracts_pass=29, native_scheduled_runs=1, model_requests=2, final_model_errors=0,
        logical_creates=2, transport_create_posts=3, final_save_notices=1,
        final_run_notices=1, read_backoff_gaps=[round(v, 3) for v in gaps],
        deletion_evidence='production writer using API adapter, not UI delete confirmation',
        stress_or_high_load=False)
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS unique schedules, quiet ACK recovery, one run, read-only retry and continued chat')

if __name__ == '__main__':
    main()
