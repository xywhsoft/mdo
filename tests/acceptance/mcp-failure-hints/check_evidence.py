"""Check actual MCP failures, private-text isolation, continuation and pack."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MARKER = 'qa-private-credential-must-not-appear'


def read(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--running-copy', type=Path)
    args = parser.parse_args()
    operations = read('native-operations.json')['data']['items']
    assert len(operations) == 6 and len({row['id'] for row in operations}) == 6
    jobs = {row['target']: row for row in operations}
    expected = {'missing': ('mcp_start_failed', '无法启动 MCP 程序'),
                'protocol': ('mcp_protocol_version', 'MCP 协议版本不兼容'),
                'response': ('mcp_invalid_response', '服务器返回的数据不符合 MCP 协议'),
                'remote': ('mcp_remote_error', 'MCP 服务器拒绝了初始化或工具发现请求'),
                'timeout': ('mcp_timeout', 'MCP 服务器未在时限内响应')}
    for target, (code, prefix) in expected.items():
        job = jobs[target]; ui = read(target + '.json')
        assert job['state'] == 'failed' and job['terminal']
        assert job['error_code'] == code and job['http_status'] == 0
        assert job['result']['server_state'] == 'failed' and not job['result']['connected']
        assert len(ui['errors']) == 1 and ui['errors'][0].startswith(prefix)
        assert not ui['hasRecheck'] and all(not button['disabled'] for button in ui['testButtons'])
        assert '连接测试完成' not in ui['body'] and MARKER not in ui['body']
        assert ui['draft'] == 'MCP_FAILURE_HINT_FOLLOWUP'
    assert all(MARKER not in row['message'] and row['http_status'] == 0 for row in operations)
    assert jobs['working']['state'] == 'succeeded' and jobs['working']['error_code'] == ''
    assert jobs['working']['result']['connected'] and jobs['working']['result']['discovered_tools'] == 1
    assert not read('working.json')['errors']
    native = {row['id']: row for row in read('native-mcp.json')['data']['items']}
    assert native['working']['state'] == 'ready' and native['working']['connected']
    assert all(native[target]['state'] == 'failed' for target in expected)
    mutations = [json.loads(line) for line in (HERE / 'writes.jsonl').read_text(encoding='utf-8').splitlines()]
    refreshes = [row for row in mutations if row['path'].startswith('/api/v1/mcp/') and row['path'].endswith('/refresh')]
    assert Counter(row['path'].split('/')[4] for row in refreshes) == {target: 1 for target in jobs}
    assert all(row['method'] == 'POST' and row['status'] == 202 for row in refreshes)
    assert not any('/operations/' in row['path'] for row in mutations)
    assert read('followup-before.json')['draft'] == 'MCP_FAILURE_HINT_FOLLOWUP'
    ui = read('followup-final.json')
    assert ui['draft'] == '' and not ui['errors']
    assert ui['timeline'].count('FIXTURE_OK: MCP_FAILURE_HINT_FOLLOWUP') == 1
    assert read('calls.json') == {'MCP_FAILURE_HINT_FOLLOWUP': 1}
    assert read('requests.json') == [{'prompt': 'MCP_FAILURE_HINT_FOLLOWUP', 'image_parts': 0,
                                    'user_messages': 1, 'tool_results': 0}]
    events = read('followup-events.json')['data']['items']
    assert sum(row['kind'] == 'agent_done' and row['success'] for row in events) == 1
    assert not any(row['kind'] == 'error' for row in events)
    baseline = (HERE / 'baseline-observer.txt').read_text(encoding='utf-8')
    assert 'tests 4' in baseline and 'pass 0' in baseline and 'fail 4' in baseline
    receipt = read('build.json'); runtime = read('runtime.json'); gates = read('gates.json')
    assert receipt['mdo_commit'].startswith('fde30c2c') and not receipt['mdo_worktree_dirty']
    assert gates['frontend_passed'] == 950 and gates['reachable_modules'] == 161
    assert gates['contracts_passed'] == 124 and gates['operation_classification_cases'] == 16
    assert gates['strict_compile_passed'] and gates['api_runtime_passed'] and gates['fixture_exit_code'] == 0
    assert runtime['passed'] and runtime['packed_sha256'] == receipt['sha256']
    assert runtime['final_model_errors'] == 0 and sum(runtime['model_requests'].values()) == 16
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
        for name in ('include/mdo/operations.h', 'src/api/operations.c', 'src/operations/manager.c',
                     'web/js/state/resource-operation.js', 'web/lang/zh-CN.json', 'web/lang/en-US.json', 'web/lang/ru-RU.json'):
            expected_source = (ROOT / 'app' / name).read_bytes()
            assert entries[name] == expected_source
            sources[name] = hashlib.sha256(expected_source).hexdigest()
        assert not any('mcp-failure-hints.py' in name for name in entries)
    report = {'passed': True, 'mdo_commit': receipt['mdo_commit'], 'sha256': receipt['sha256'],
              'bytes': receipt['bytes'], 'embedded_sources': sources, 'native_mcp_operations': 6,
              'page_model_requests': 1, 'native_extension_model_requests': 16, 'final_model_errors': 0,
              'external_provider_requests': 0, 'real_http_mcp_failure_tested': False,
              'android_device_tested': False, 'daily_app_overwritten': False}
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS five real MCP failure classes, safe advice, one successful connection, continuation and clean pack')


if __name__ == '__main__':
    main()
