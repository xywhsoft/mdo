"""Check packed schedule recovery against recorded HTTP, UI and native results."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def read(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def lines(name):
    return [json.loads(row) for row in (HERE / name).read_text(encoding='utf-8').splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--running-copy', type=Path, required=True)
    args = parser.parse_args()
    rows = defaultdict(list)
    for row in lines('read-records.jsonl'):
        rows[row['phase']].append(row)
    for phase in ('list-recovery', 'editor-recovery', 'history-recovery'):
        assert [row.get('status') for row in rows[phase]] == [503, 503, None], phase
        assert rows[phase][-1]['forwarded']
        gaps = [rows[phase][i + 1]['started'] - rows[phase][i]['started'] for i in range(2)]
        assert .45 <= gaps[0] < 1.2 and .9 <= gaps[1] < 2, (phase, gaps)
    for phase in ('editor-cancel', 'history-cancel', 'list-cancel'):
        assert len(rows[phase]) == 1, phase
        assert rows[phase][0]['held'] and rows[phase][0]['peer_closed'], phase
        assert rows[phase][0]['status'] == 200
    exhaustion = rows['history-exhaustion']
    assert len(exhaustion) == 7
    assert [row['status'] for row in exhaustion[:6]] == [503] * 6
    assert exhaustion[-1]['forwarded']
    elapsed = exhaustion[5]['started'] - exhaustion[0]['started']
    assert 15 <= elapsed < 30, elapsed
    for name in ('list-pending', 'list-restored', 'editor-pending', 'editor-restored',
                 'history-pending', 'history-restored', 'editor-held', 'new-editor-retained',
                 'history-held', 'history-cancelled-chat', 'list-held', 'list-cancelled-chat',
                 'exhaustion-pending', 'history-retried', 'followup-before'):
        ui = read(name + '.json')
        assert ui['draft'] == 'SCHEDULE_READ_FOLLOWUP' and not ui['errors'], name
    assert read('list-restored.json')['list'] == '1 项计划'
    assert read('editor-pending.json')['editorOpen'] and read('editor-pending.json')['saveDisabled']
    restored = read('editor-restored.json')
    assert restored['label'] == '读取恢复验收' and restored['input'] == 'SCHEDULE_READ_RUN'
    assert not restored['saveDisabled']
    assert read('new-editor-retained.json')['label'] == '不应被旧读取覆盖'
    for name in ('history-cancelled-chat', 'list-cancelled-chat'):
        ui = read(name + '.json')
        assert '/sessions/' in ui['url'] and not ui['historyOpen'] and not ui['editorOpen']
    assert len(read('history-exhausted.json')['errors']) == 1
    assert read('history-retried.json')['history'] == '1 条记录'
    ui = read('followup-final.json')
    assert ui['draft'] == '' and not ui['errors']
    assert ui['body'].count('FIXTURE_OK: SCHEDULE_READ_FOLLOWUP') == 1
    assert read('browser-logs.json') == []
    writes = lines('writes.jsonl')
    scheduled = [row for row in writes if row['path'].startswith('/api/v1/schedules')]
    assert [(row['method'], row['path'], row['status']) for row in scheduled] == [
        ('POST', '/api/v1/schedules/qa-schedule-read/run', 202)]
    assert len([row for row in writes if row['method'] == 'POST' and row['path'].endswith('/runs')]) == 1
    catalog = read('native-schedules.json')['data']
    assert catalog['total'] == 1 and catalog['runs_completed'] == 1 and catalog['runs_failed'] == 0
    assert catalog['items'][0]['claim_count'] == 1 and catalog['items'][0]['active_runs'] == 0
    history = read('native-history.json')['data']['items']
    assert len(history) == 1 and history[0]['result'] == 'succeeded'
    assert history[0]['text'] == 'FIXTURE_OK: SCHEDULE_READ_RUN'
    assert read('calls.json') == {'SCHEDULE_READ_RUN': 1, 'SCHEDULE_READ_FOLLOWUP': 1}
    events = read('followup-events.json')['data']['items']
    assert sum(row['kind'] == 'agent_start' for row in events) == 1
    assert sum(row['kind'] == 'agent_done' and row['success'] for row in events) == 1
    assert not any(row['kind'] == 'error' for row in events)
    baseline = lines('baseline-read-records.jsonl')
    assert baseline[0]['status'] == 503 and baseline[0]['phase'] == 'baseline-list'
    assert '读取失败' in (HERE / 'baseline-list.ax.txt').read_text(encoding='utf-8')
    assert '还没有计划任务' in (HERE / 'baseline-list.ax.txt').read_text(encoding='utf-8')
    assert 'Temporary extension read unavailable' in (HERE / 'baseline-editor.ax.txt').read_text(encoding='utf-8')
    assert 'fail 5' in (HERE / 'baseline.txt').read_text(encoding='utf-8')
    assert 'pass 965' in (HERE / 'frontend.txt').read_text(encoding='utf-8')
    assert 'Ran 124 tests' in (HERE / 'contracts.txt').read_text(encoding='utf-8')
    receipt = read('candidate.json')
    assert receipt['mdo_commit'].startswith('471f21e') and not receipt['mdo_worktree_dirty']
    raw = args.exe.read_bytes()
    assert len(raw) == receipt['bytes'] and hashlib.sha256(raw).hexdigest() == receipt['sha256']
    assert args.running_copy.read_bytes() == raw
    helpers = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
    with tempfile.TemporaryDirectory() as directory:
        pack = Path(directory) / 'app.xrtpack'
        helpers['extract_pack'](args.exe, pack)
        files = helpers['files_from_pack'](pack.read_bytes())
    sources = {}
    for name in ('web/js/api/read-intents.js', 'web/js/state/schedules.js',
                 'web/js/features/schedules/schedule-panel.js', 'web/js/features/settings/extension-reads.js'):
        source = (ROOT / 'app' / name).read_bytes()
        assert files[name] == source, name
        sources[name] = hashlib.sha256(source).hexdigest()
    assert not any('manual_extension_read_qa' in name for name in files)
    assert all(file.read_bytes().startswith(b'\xff\xd8\xff') for file in HERE.glob('*.jpg'))
    report = dict(passed=True, source_commit=receipt['mdo_commit'], sha256=receipt['sha256'],
        bytes=receipt['bytes'], embedded_sources=sources, frontend_pass=965, contracts_pass=124,
        native_scheduled_runs=1, model_requests=2, final_model_errors=0,
        cancelled_native_reads=3, final_read_notices=1,
        retry_exhaustion_seconds=round(elapsed, 3), stress_or_high_load=False)
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS schedule list/editor/history recovery, cancellation, one run, preserved draft and one continuation')


if __name__ == '__main__':
    main()
