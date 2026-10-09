"""Cross-check real transport failures, wait budget, continuation and pack."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MARKER = 'qa-private-http-marker'


def read(name):
    return json.loads((HERE / name).read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--running-copy', type=Path)
    args = parser.parse_args()
    operations = read('native-operations.json')['data']['items']
    assert len(operations) == 5 and len({row['id'] for row in operations}) == 5
    expected = dict(auth=(401, 'MCP 服务器未接受登录凭据'),
        permission=(403, '当前凭据没有访问 MCP 服务器的权限'),
        limit=(429, 'MCP 服务器暂时限流'), service=(503, 'MCP 服务器暂不可用'))
    failed = {row['target']: row for row in operations if row['state'] == 'failed'}
    assert set(failed) == set(expected)
    for target, (status, prefix) in expected.items():
        job = failed[target]; ui = read(target + '.json')
        assert job['terminal'] and job['error_code'] == 'mcp_http_error'
        assert job['http_status'] == status and job['message'] == 'MCP connection test failed'
        assert len(ui['errors']) == 1 and ui['errors'][0].startswith(prefix)
        assert not ui['hasRecheck'] and all(not row['disabled'] for row in ui['testButtons'])
        assert MARKER not in ui['body'] and '连接测试完成' not in ui['body']
        assert ui['draft'] == 'MCP_HTTP_STATUS_FOLLOWUP'
    recovered = [row for row in operations if row['state'] == 'succeeded']
    assert len(recovered) == 1 and recovered[0]['target'] == 'auth'
    job = recovered[0]
    assert job['error_code'] == '' and job['http_status'] == 0 and job['result']['connected']
    duration = (job['ended_at'] - job['started_at']) / 1e6
    assert 5 < duration < 65, duration
    pending = read('auth-pending.json')
    assert not pending['errors'] and not pending['hasRecheck'] and pending['testButtons']
    assert all(row['disabled'] for row in pending['testButtons'])
    assert not read('auth-recovered.json')['errors']
    assert '就绪' in read('auth-recovered.json')['body']
    native = {row['id']: row for row in read('native-mcp.json')['data']['items']}
    assert native['auth']['state'] == 'ready' and native['auth']['connected']
    assert all(native[target]['state'] == 'failed' for target in ('permission', 'limit', 'service'))
    writes = [json.loads(line) for line in (HERE / 'writes.jsonl').read_text(encoding='utf-8').splitlines()]
    refreshes = [row for row in writes if row['path'].startswith('/api/v1/mcp/') and row['path'].endswith('/refresh')]
    assert Counter(row['path'].split('/')[4] for row in refreshes) == dict(auth=2, permission=1, limit=1, service=1)
    assert all(row['method'] == 'POST' and row['status'] == 202 for row in refreshes)
    assert not any('/operations/' in row['path'] for row in writes)
    peer = [json.loads(line) for line in (HERE / 'peer-methods.jsonl').read_text(encoding='utf-8').splitlines()]
    assert Counter((row['target'], row['method'], row['status']) for row in peer) == Counter({
        ('auth', 'initialize', 401): 1, ('permission', 'initialize', 403): 1,
        ('limit', 'initialize', 429): 1, ('service', 'initialize', 503): 1,
        ('auth', 'initialize', 200): 1, ('auth', 'notifications/initialized', 202): 1,
        ('auth', 'tools/list', 200): 1})
    before = [row for row in read('startup-before-operations.json')['data']['items']
              if row['target'] == 'auth' and row['error_code'] == 'mcp_timeout']
    assert len(before) == 1 and 4.5 < (before[0]['ended_at'] - before[0]['started_at']) / 1e6 < 6
    assert 'MCP 服务器未在时限内响应' in read('startup-before-ui.json')['errors'][0]
    assert read('followup-before.json')['draft'] == 'MCP_HTTP_STATUS_FOLLOWUP'
    ui = read('followup-final.json')
    assert ui['draft'] == '' and not ui['errors']
    assert ui['body'].count('FIXTURE_OK: MCP_HTTP_STATUS_FOLLOWUP') == 1
    assert read('calls.json') == {'MCP_HTTP_STATUS_FOLLOWUP': 1}
    events = read('followup-events.json')['data']['items']
    assert sum(row['kind'] == 'agent_done' and row['success'] for row in events) == 1
    assert not any(row['kind'] == 'error' for row in events)
    assert 'xwork MCP HTTP errors: FAIL' in (HERE / 'http-native-baseline.txt').read_text(encoding='utf-8')
    assert 'PASS 12 HTTP failures, 12 explicit recoveries, 3 rejected replay signals' in (HERE / 'http-native-fixed.txt').read_text(encoding='utf-8')
    assert 'xwork MCP Streamable HTTP: PASS (0 failures)' in (HERE / 'http-protocol.txt').read_text(encoding='utf-8')
    assert 'fail 2' in (HERE / 'loopback-baseline.txt').read_text(encoding='utf-8')
    assert 'mcp_success=state:3' in (HERE / 'startup-budget-baseline.txt').read_text(encoding='utf-8')
    assert 'operation runtime probe: PASS' in (HERE / 'startup-budget-fixed.txt').read_text(encoding='utf-8')
    receipt = read('build.json'); runtime = read('runtime.json'); report = read('report.json')
    assert receipt['mdo_commit'].startswith('ae74a153') and not receipt['mdo_worktree_dirty']
    assert receipt['xs_commit'] == '93ef1f61e3203cfd71868ae1c4d8a4743d96d9c8'
    assert report['frontend_pass'] == 952 and report['contracts_pass'] == 124
    assert runtime['passed'] and runtime['packed_sha256'] == receipt['sha256']
    assert runtime['final_model_errors'] == 0 and sum(runtime['model_requests'].values()) == 16
    assert report['host_sha256'] == receipt['host']['binary_sha256']
    sources = {}
    if args.exe:
        raw = args.exe.read_bytes()
        assert len(raw) == receipt['bytes'] == report['size']
        assert hashlib.sha256(raw).hexdigest() == receipt['sha256'] == report['sha256']
        if args.running_copy: assert args.running_copy.read_bytes() == raw
        helpers = runpy.run_path(str(HERE.parent / 'decision-write-recovery/check_candidate.py'))
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'app.xrtpack'
            helpers['extract_pack'](args.exe, archive)
            entries = helpers['files_from_pack'](archive.read_bytes())
        for name in ('src/operations/manager.c', 'src/mcp/manager.c',
                     'web/js/features/settings/extension-panels.js',
                     'web/js/features/settings/extension-formats.js',
                     'web/js/features/settings/extension-validation.js',
                     'web/js/state/resource-operation.js', 'web/lang/zh-CN.json',
                     'web/lang/en-US.json', 'web/lang/ru-RU.json'):
            expected_source = (ROOT / 'app' / name).read_bytes()
            assert entries[name] == expected_source, name
            sources[name] = hashlib.sha256(expected_source).hexdigest()
        assert not any('mcp-http-status-peer.py' in name for name in entries)
    report.update(passed=True, initialization_wait_seconds=round(duration, 3),
                  embedded_sources=sources, owned_fixture_shutdown='exit 0',
                  page_model_requests=1, page_model_errors=0)
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS four real HTTP failures, quiet delayed recovery, one continued reply and clean pack')


if __name__ == '__main__': main()
