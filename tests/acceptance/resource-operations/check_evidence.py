"""Check bounded UI observations, native job identity and clean packed sources."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def read(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def rows(name):
    return [json.loads(line) for line in (HERE / name).read_text(encoding='utf-8').splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--running-copy', type=Path)
    args = parser.parse_args()
    baseline = rows('baseline-read-records.jsonl')
    assert len(baseline) == 1 and baseline[0]['status'] == 503
    native = read('baseline-native-operation.json')['data']
    assert native['id'] == baseline[0]['path'].split('/')[-1]
    assert native['state'] == 'succeeded' and native['result']['connected']
    assert 'Temporary extension read unavailable' in (HERE / 'baseline-error.ax.txt').read_text(encoding='utf-8')
    assert sum(row['path'] == '/api/v1/mcp/local/refresh' for row in rows('baseline-writes.jsonl')) == 1
    baseline_log = (HERE / 'baseline-bounded-node.txt').read_text(encoding='utf-8')
    assert all(text in baseline_log for text in ('tests 10', 'pass 1', 'fail 8', 'cancelled 1'))

    grouped = {}
    for row in rows('read-records.jsonl'):
        grouped.setdefault(row['phase'], []).append(row)
    operations = read('native-operations.json')
    for phase in ('operation-retry', 'module-retry'):
        observations = grouped[phase]
        assert len(observations) == 3
        assert [row.get('status') for row in observations[:2]] == [503, 503]
        assert observations[2]['forwarded']
        assert len({row['path'] for row in observations}) == 1
        assert operations[observations[0]['path']]['data']['state'] == 'succeeded'
        assert observations[1]['started'] - observations[0]['started'] >= .5
        assert observations[2]['started'] - observations[1]['started'] >= 1
    cancel = grouped['operation-cancel']
    assert len(cancel) == 1 and cancel[0]['held'] and cancel[0]['peer_closed']
    assert cancel[0]['response']['data']['state'] == 'succeeded'
    recheck = grouped['operation-recheck-after-cancel']
    assert len(recheck) == 1 and recheck[0]['path'] == cancel[0]['path'] and recheck[0]['forwarded']
    exhausted = grouped['operation-exhausted']
    assert len(exhausted) == 7 and all(row['status'] == 503 for row in exhausted[:6])
    assert exhausted[6]['forwarded'] and len({row['path'] for row in exhausted}) == 1
    assert all(exhausted[i + 1]['started'] - exhausted[i]['started'] >= delay
               for i, delay in enumerate((.5, 1, 2, 4, 8)))
    assert operations[exhausted[0]['path']]['data']['state'] == 'succeeded'
    failure = grouped['operation-native-failure']
    assert len(failure) == 1 and failure[0]['forwarded']
    assert operations[failure[0]['path']]['data']['state'] == 'failed'
    mutations = rows('writes.jsonl')
    posts = [row for row in mutations if row['method'] == 'POST' and
             (row['path'].endswith('/refresh') or row['path'] == '/api/v1/modules/reload')]
    assert Counter(row['path'] for row in posts) == {
        '/api/v1/mcp/local/refresh': 3, '/api/v1/mcp/missing/refresh': 1, '/api/v1/modules/reload': 1}
    assert all(row['status'] == 202 for row in posts)
    assert not any(row['method'] == 'DELETE' and '/operations/' in row['path'] for row in mutations)
    assert not any('/extensions/subagents/' in row['path'] for row in mutations)
    for name in ('quiet-retry', 'cancelled-chat', 'recheck-available', 'rechecked',
                 'exhausted-recovered', 'module-recovered'):
        assert not read(name + '.json')['errors']
    assert read('cancelled-chat.json')['draft'] == 'RESOURCE_OPERATION_FOLLOWUP'
    assert read('recheck-available.json')['recheck'] == [{'disabled': False}]
    assert not read('rechecked.json')['recheck']
    final = read('exhausted.json')
    assert len(final['errors']) == 1 and '状态暂未确认' in final['errors'][0]
    assert final['recheck'] == [{'disabled': False}]
    assert not read('exhausted-recovered.json')['recheck']
    final = read('native-failure.json')
    assert len(final['errors']) == 1 and final['errors'][0].startswith('连接测试失败')
    assert not final['recheck'] and '连接测试完成' not in final['body']
    hint = read('inline-details.json')
    assert hint['hintCount'] == 1 and hint['hint'] == '请填写说明。'
    assert hint['visible'] and hint['adjacent'] and hint['status'] == ''
    assert hint['role'] == 'alert' and hint['describedby'] == 'extension-field-error'
    assert hint['invalid'] == 'true' and hint['focus'] == 'description'
    assert {'name': 'extension_id', 'value': 'qa-inline-hint'} in hint['fields']
    assert {'name': 'prompt', 'value': 'Inspect files and return a concise result.'} in hint['fields']
    assert read('inline-cleared.json') == {'hints': 0, 'invalid': None, 'describedby': None}
    assert read('followup-before.json')['draft'] == 'RESOURCE_OPERATION_FOLLOWUP'
    ui = read('followup-final.json')
    assert ui['dialogs'] == 0 and ui['draft'] == '' and not ui['errorToasts']
    assert ui['timeline'].count('FIXTURE_OK: RESOURCE_OPERATION_FOLLOWUP') == 1
    assert ui['url'].startswith('http://localhost:')
    assert read('calls.json') == {'RESOURCE_OPERATION_FOLLOWUP': 1}
    assert read('requests.json') == [{'prompt': 'RESOURCE_OPERATION_FOLLOWUP', 'image_parts': 0,
                                    'user_messages': 1, 'tool_results': 0}]
    events = read('followup-events.json')
    assert sum(event['kind'] == 'agent_done' and event['success'] for event in events) == 1
    assert not any(event['kind'] == 'error' for event in events)
    methods = [json.loads(line)['method'] for line in (HERE / 'mcp-methods.jsonl').read_text(encoding='utf-8').splitlines()]
    assert methods[0:3] == ['initialize', 'notifications/initialized', 'tools/list']
    receipt = read('build.json'); runtime = read('runtime.json')
    assert not receipt['mdo_worktree_dirty'] and receipt['mdo_commit'].startswith('3940904a')
    assert runtime['passed'] and runtime['packed_sha256'] == receipt['sha256']
    assert runtime['final_model_errors'] == 0 and sum(runtime['model_requests'].values()) == 16
    assert read('fixture-completion.json') == {'baseline_exit_code': 0, 'candidate_exit_code': 0}
    sources = {}
    if args.exe:
        raw = args.exe.read_bytes()
        assert len(raw) == receipt['bytes'] and hashlib.sha256(raw).hexdigest() == receipt['sha256']
        if args.running_copy:
            assert args.running_copy.read_bytes() == raw
        helpers = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'app.xrtpack'
            helpers['extract_pack'](args.exe, archive)
            entries = helpers['files_from_pack'](archive.read_bytes())
        for name in ('web/js/state/resource-operation.js', 'web/js/state/resources.js',
                     'web/js/features/settings/extension-panels.js', 'web/js/features/settings/resource-panels.js',
                     'web/css/extensions.css', 'web/lang/zh-CN.json', 'web/lang/en-US.json', 'web/lang/ru-RU.json'):
            expected = (ROOT / 'app' / name).read_bytes()
            assert entries[name] == expected
            sources[name] = hashlib.sha256(expected).hexdigest()
        assert not any('manual_extension_' in name for name in entries)
    report = {'passed': True, 'mdo_commit': receipt['mdo_commit'], 'sha256': receipt['sha256'],
              'bytes': receipt['bytes'], 'embedded_sources': sources,
              'native_jobs_started': 5, 'page_model_requests': 1, 'native_extension_model_requests': 16,
              'final_model_errors': 0, 'external_provider_requests': 0,
              'android_device_tested': False, 'daily_app_overwritten': False,
              'native_confirm_automation_verified': False}
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS native identity, quiet recovery, cancellation, pure recheck, inline hint, actual continuation and clean pack')


if __name__ == '__main__':
    main()
