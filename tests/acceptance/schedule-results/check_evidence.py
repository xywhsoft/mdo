"""Check actual schedule results, locale UI, restart and packed source bytes."""
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


def history(stage):
    return read(stage + '-history.json')['data']['items']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--running-copy', type=Path, required=True)
    args = parser.parse_args()
    baseline = history('baseline')
    assert len(baseline) == 1 and baseline[0]['result'] == 'failed'
    assert 'model_error_kind' not in baseline[0]
    assert read('baseline-calls.json') == {'quota': 1}
    legacy = history('setup')[0]
    assert legacy['text'] == baseline[0]['text']
    assert (legacy['model_error_kind'], legacy['model_http_status'], legacy['model_attempts']) == ('', 0, 0)
    assert (HERE / 'final-history.jsonl').read_bytes().startswith((HERE / 'legacy.jsonl').read_bytes())
    failure = history('failure')
    assert len(failure) == 2 and failure[-1] == legacy
    assert failure[0]['result'] == 'failed'
    assert (failure[0]['model_error_kind'], failure[0]['model_http_status'], failure[0]['model_attempts']) == ('daily_token_limit', 429, 1)
    assert read('failure-calls.json') == {'quota': 1}
    assert history('restart') == failure
    assert read('failure-state.json')['pid'] != read('restart-state.json')['pid']
    lifecycle = read('lifecycle.json')
    assert [row['event'] for row in lifecycle] == ['stopped', 'restarted']
    assert lifecycle[-1]['pid'] == read('restart-state.json')['pid']
    for locale, name in (('zh-CN', 'failure-zh'), ('en-US', 'failure-en'), ('ru-RU', 'failure-ru'), ('zh-CN', 'restart-zh')):
        ui = read(name + '.json')
        translations = json.loads((ROOT / 'app/web/lang' / (locale + '.json')).read_text(encoding='utf-8'))
        assert ui['locale'] == locale
        assert ui['history'].count(translations['model.error.daily_token_limit']) == 1
        assert ui['history'].count(legacy['text']) == 1
        assert ui['draft'] == 'SCHEDULE_HISTORY_FOLLOWUP' and not ui['errors']
    success = history('final')
    assert len(success) == 3 and success[1:] == failure
    assert success[0]['result'] == 'succeeded' and success[0]['text'] == 'FIXTURE_OK: quota'
    assert (success[0]['model_error_kind'], success[0]['model_http_status'], success[0]['model_attempts']) == ('', 0, 0)
    stored = [json.loads(row) for row in (HERE / 'final-history.jsonl').read_text(encoding='utf-8').splitlines()]
    assert len(stored) == 3 and [row['result'] for row in stored] == [-1, -1, 0]
    assert not any(key in stored[0] or key in stored[2] for key in ('model_error_kind', 'model_http_status', 'model_attempts'))
    success_ui = read('success-zh.json')
    assert success_ui['history'].count('FIXTURE_OK: quota') == 1 and not success_ui['errors']
    assert read('followup-before.json')['draft'] == 'SCHEDULE_HISTORY_FOLLOWUP'
    final_ui = read('followup-final.json')
    assert final_ui['draft'] == '' and not final_ui['errors']
    assert final_ui['body'].count('FIXTURE_OK: SCHEDULE_HISTORY_FOLLOWUP') == 1
    assert read('final-calls.json') == {'quota': 2, 'SCHEDULE_HISTORY_FOLLOWUP': 1}
    assert [row['prompt'] for row in read('final-requests.json')] == ['quota', 'quota', 'SCHEDULE_HISTORY_FOLLOWUP']
    events = read('final-events.json')['data']['items']
    assert sum(row['kind'] == 'agent_start' for row in events) == 1
    assert sum(row['kind'] == 'agent_done' and row['success'] for row in events) == 1
    assert not any(row['kind'] == 'error' for row in events)
    assert read('browser-logs.json') == []
    catalog = read('final-catalog.json')['data']
    assert catalog['total'] == 1 and catalog['items'][0]['claim_count'] == 2
    assert catalog['runs_completed'] == 1 and catalog['runs_failed'] == 0 and catalog['active_runs'] == 0
    assert 'pass 981' in (HERE / 'frontend.txt').read_text(encoding='utf-8')
    assert '164 modules parsed and reachable' in (HERE / 'modules.txt').read_text(encoding='utf-8')
    assert 'Ran 124 tests' in (HERE / 'contracts.txt').read_text(encoding='utf-8')
    assert 'schedule runtime probe: PASS' in (HERE / 'runtime.txt').read_text(encoding='utf-8')
    assert 'schedule error history, reload and 8 metadata boundaries: PASS' in (HERE / 'executor.txt').read_text(encoding='utf-8')
    assert 'API runtime probe: PASS' in (HERE / 'api.txt').read_text(encoding='utf-8')
    assert '-Werror' in (HERE / 'strict.txt').read_text(encoding='utf-8')
    receipt = read('candidate.json')
    assert receipt['mdo_commit'].startswith('b9f42347') and not receipt['mdo_worktree_dirty']
    raw = args.exe.read_bytes()
    assert len(raw) == receipt['bytes'] and hashlib.sha256(raw).hexdigest() == receipt['sha256']
    assert args.running_copy.read_bytes() == raw
    helper = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
    with tempfile.TemporaryDirectory() as directory:
        pack = Path(directory) / 'app.xrtpack'
        helper['extract_pack'](args.exe, pack)
        files = helper['files_from_pack'](pack.read_bytes())
    sources = {}
    for name in ('include/mdo/schedules.h', 'src/schedules/manager.c', 'src/schedules/executor.c',
                 'src/api/schedules.c', 'web/js/features/schedules/schedule-panel.js',
                 'web/js/utils/model-errors.js', 'web/lang/en-US.json', 'web/lang/ru-RU.json', 'web/lang/zh-CN.json'):
        source = (ROOT / 'app' / name).read_bytes()
        assert files[name] == source, name
        sources[name] = hashlib.sha256(source).hexdigest()
    assert all(path.read_bytes().startswith(b'\xff\xd8\xff') for path in HERE.glob('*.jpg'))
    report = dict(passed=True, source_commit=receipt['mdo_commit'], sha256=receipt['sha256'],
        bytes=receipt['bytes'], embedded_sources=sources, frontend_pass=981, contracts_pass=124,
        native_metadata_boundaries=8, model_requests=3, deliberate_schedule_failures=1,
        successful_scheduled_runs=1, normal_chat_runs=1, normal_chat_final_errors=0,
        ui_languages=['zh-CN', 'en-US', 'ru-RU'], legacy_line_preserved=True,
        restart_verified=True, stress_or_high_load=False)
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS typed schedule final causes, three locale UIs, legacy history, restart and continued chat')


if __name__ == '__main__':
    main()
