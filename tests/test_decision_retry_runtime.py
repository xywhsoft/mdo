"""Bounded real xs/TCC decision replays, conflicts, cancellation and restart.

One editable loopback model and a disposable Home; no external requests.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading

from test_interrupt_runtime import ROOT, free_port, request, start_host, stop_host, until
from test_api_runtime import request as raw_request, session_events
from test_packed_queue_start_recovery import configure_model

CACHE_CLOCK = r'''
#include "decision_cache.h"
#include "write_admission.h"
static bool g_DecisionCacheTestEnabled;
static uint64 g_DecisionCacheTestNow;
static const char* g_DecisionCacheTestToken;
static uint64 DecisionCacheClock(void)
{ return g_DecisionCacheTestEnabled ? g_DecisionCacheTestNow : MdoMonotonicUs(); }
static bool DecisionCacheExpired(uint64 At)
{ return g_DecisionCacheTestEnabled ? g_DecisionCacheTestNow >= At : MdoDeadlineExpired(At); }
static bool DecisionCacheToken(char Token[MDO_API_WRITE_TOKEN_CAPACITY])
{
    if (!g_DecisionCacheTestEnabled) return MdoApiWriteToken(Token);
    strcpy(Token, g_DecisionCacheTestToken); return true;
}
#define MdoMonotonicUs DecisionCacheClock
#define MdoDeadlineExpired DecisionCacheExpired
#define MdoApiWriteToken DecisionCacheToken
'''


class Model(BaseHTTPRequestHandler):
    calls = Counter()
    workspace = None

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        prompt = next(m['content'] for m in reversed(body['messages']) if m['role'] == 'user')
        self.calls[prompt] += 1
        count = self.calls[prompt]
        message = {'role': 'assistant', 'content': 'Decision fixture complete.'}
        finish = 'stop'
        if count == 1:
            name, arguments = 'ask_user', {'question': 'Continue the isolated decision check?', 'options': ['Continue']}
        elif count == 2 and prompt == 'interactive-decision':
            counter = str(self.workspace / 'executions.txt')
            name, arguments = 'exec', {'argv': [sys.executable, '-c',
                "from pathlib import Path; p=Path(" + repr(counter) + "); "
                "p.write_text((p.read_text() if p.exists() else '')+'once\\n'); print('DECISION_VERIFIED')"],
                'timeout_ms': 5000}
        else:
            name = None
        if name:
            message['tool_calls'] = [{'id': f'decision-{count}', 'type': 'function',
                'function': {'name': name, 'arguments': json.dumps(arguments)}}]
            finish = 'tool_calls'
        raw = json.dumps({'id': 'decision-retry', 'model': body['model'], 'choices': [
            {'index': 0, 'message': message, 'finish_reason': finish}],
            'usage': {'prompt_tokens': 10, 'completion_tokens': 5}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def probe(args):
    proof = {'baseline_commit': args.baseline_commit, 'cases': [], 'passed': False}
    with tempfile.TemporaryDirectory(prefix='decision-retry-', dir=ROOT / '.build') as raw:
        base = Path(raw); site = base / 'site'
        if args.baseline_commit:
            archive = subprocess.check_output(['git', 'archive', args.baseline_commit, 'app'], cwd=ROOT)
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                tar.extractall(base, filter='data')
            (base / 'app').rename(site)
            shutil.copytree(ROOT / 'app/generated', site / 'generated', dirs_exist_ok=True)
            manifest = json.loads((site / 'sources.json').read_text())
            generated = (site / 'generated/mdo_unity.c').read_text()
            prefix = generated[:generated.index('#include "../src/')]
            (site / 'generated/mdo_unity.c').write_text(prefix + ''.join(
                '#include "../' + source + '"\n' for source in manifest['sources']), encoding='utf-8')
        else:
            shutil.copytree(ROOT / 'app', site)
        if not args.baseline_commit:
            cache = site / 'src/api/decision_cache.c'
            cache.write_text(CACHE_CLOCK + cache.read_text() + '\n#undef MdoMonotonicUs\n'
                '#undef MdoDeadlineExpired\n#undef MdoApiWriteToken\n', encoding='utf-8')
            shutil.copy2(ROOT / 'tests/fixtures/decision-cache-check.c', site / 'src/bootstrap')
            service = site / 'src/bootstrap/service.c'
            source = service.read_text()
            marker = '    (void)MdoApiInit();\n'
            assert source.count(marker) == 1
            source = source.replace('void ServiceInit(XS_HostInfo* pHost)',
                '#include "decision-cache-check.c"\nvoid ServiceInit(XS_HostInfo* pHost)', 1)
            service.write_text(source.replace(marker, marker + '    DecisionCacheCheck(pHost);\n', 1),
                encoding='utf-8')
        port = free_port()
        config = site / 'xs.json'
        config.write_text(json.dumps({'services': [{'enabled': True, 'class': 'http', 'name': 'mdo',
            'ip': '127.0.0.1', 'port': port, 'host_default': {'enabled': True, 'name': 'mdo',
            'path': 'web', 'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}), encoding='utf-8')
        model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
        Model.calls = Counter(); Model.workspace = base
        thread = threading.Thread(target=model.serve_forever, daemon=True); thread.start()
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base), MDO_QUEUE_FIXTURE_KEY='fixture-only')
        process = None

        def call(method, path, body=None, expected=200):
            status, data = request(port, method, '/api/v1' + path, body)
            assert status == expected, (path, status, data)
            return data.get('data') if status == 200 or status in (201, 202) else data

        def first(path):
            return until(lambda: call('GET', path).get('items'), 8)[0]

        def session(label):
            return call('POST', '/sessions', {'project_id': 'default', 'title': label,
                'model_id': 'queue-fixture', 'reasoning_effort': 'none', 'permission_profile': 'balanced',
                'workspace_root': str(base)}, 201)

        try:
            process = start_host(args.host.resolve(), config, base / 'home', environment, base / 'xs.log', port)
            if not args.baseline_commit:
                checks = re.findall(r'decision_cache_checks=(\d+) passed=(\d+)', (base / 'xs.log').read_text())
                assert checks and checks[-1][1] == '1', (base / 'xs.log').read_text()
                proof['cases'].append({'kind': 'cache_boundaries', 'checks': int(checks[-1][0]),
                    'ttl': True, 'capacity': True, 'invalid_inputs': True, 'epoch': True, 'reset': True})
            configure_model(port, f'http://127.0.0.1:{model.server_port}/v1')
            current = session('Decision transport retry')
            path = '/projects/default/sessions/' + current['id']
            run = call('POST', path + '/runs', {'prompt': 'interactive-decision'}, 202)
            question = first(path + '/asks')
            ask_path = path + '/asks/' + str(question['id'])
            answer = {'answer': 'Continue'}
            with ThreadPoolExecutor(max_workers=2) as pool:
                attempts = list(pool.map(lambda _: request(port, 'PUT', '/api/v1' + ask_path, answer), range(2)))
            proof['cases'].append({'kind': 'concurrent_same_answer', 'statuses': [s for s, _ in attempts]})
            assert all(s == 200 for s, _ in attempts), attempts
            approval = first('/approvals')
            assert not call('GET', path + '/asks')['items']
            call('PUT', ask_path, answer)
            conflict = call('PUT', ask_path, {'answer': 'Different'}, 409)
            assert conflict['error']['code'] == 'decision_conflict', conflict
            approval_path = '/approvals/' + str(approval['id'])
            with ThreadPoolExecutor(max_workers=2) as pool:
                attempts = list(pool.map(lambda _: request(port, 'PUT', '/api/v1' + approval_path,
                    {'decision': 'allow'}), range(2)))
            assert all(s == 200 for s, _ in attempts), attempts
            until(lambda: call('GET', '/runs/' + run['id'])['terminal'], 8)
            call('PUT', approval_path, {'decision': 'allow'})
            conflict = call('PUT', approval_path, {'decision': 'allow_run'}, 409)
            assert conflict['error']['code'] == 'decision_conflict', conflict
            events = session_events(port, '/api/v1' + path)
            assert not [e for e in events if e['kind'] == 'error'], events
            tools = [e for e in events if e['kind'] == 'tool_done']
            assert len(tools) == 2 and all(e['success'] for e in tools), tools
            assert (base / 'executions.txt').read_text() == 'once\n'
            assert Model.calls['interactive-decision'] == 3
            proof['cases'].append({'kind': 'interactive_replay_after_completion', 'answer': True,
                'approval': True, 'conflict': True, 'model_requests': 3, 'tool_completions': 2,
                'exec_invocations': 1, 'final_errors': 0})

            definition = {'id': 'decision-task', 'label': 'Task decision replay', 'notify': 'none',
                'project_id': 'default', 'agent_id': 'mdo.default', 'model_id': 'queue-fixture',
                'protocol': 'openai-chat-completions', 'reasoning_effort': 'none', 'max_output_tokens': 1024,
                'workspace_root': str(base), 'input': 'scheduled-decision', 'frequency': 'once',
                'interval': 1, 'start_at': 4102444800000000, 'weekday_mask': 0, 'timezone': 'utc',
                'utc_offset_seconds': 0, 'fold_policy': 'earlier', 'misfire_policy': 'run_once',
                'misfire_grace_seconds': 60, 'max_catch_up': 1, 'overlap_policy': 'skip',
                'max_concurrent_runs': 1, 'enabled': False}
            status, headers, raw = raw_request(port, 'POST', '/api/v1/schedules',
                body=json.dumps(definition).encode(), headers={'Content-Type': 'application/json'})
            assert status == 201, raw
            status, _, raw = raw_request(port, 'POST', '/api/v1/schedules/decision-task/run',
                headers={'If-Match': headers['etag']})
            assert status == 202, raw
            task = json.loads(raw)['data']['task_id']
            task_path = '/tasks/' + str(task)
            question = first(task_path + '/asks')
            task_answer = task_path + '/asks/' + str(question['id'])
            call('PUT', task_answer, answer)
            until(lambda: call('GET', task_path)['terminal'], 8)
            call('PUT', task_answer, answer)
            conflict = call('PUT', task_answer, {'answer': 'Different'}, 409)
            assert conflict['error']['code'] == 'decision_conflict', conflict
            proof['cases'].append({'kind': 'task_answer_replay_after_completion', 'replayed': True, 'conflict': True})

            cancelled = session('Cancellation is not acknowledgement')
            cancelled_path = '/projects/default/sessions/' + cancelled['id']
            run = call('POST', cancelled_path + '/runs', {'prompt': 'cancelled-decision'}, 202)
            question = first(cancelled_path + '/asks')
            call('DELETE', '/runs/' + run['id'])
            until(lambda: call('GET', '/runs/' + run['id'])['terminal'], 8)
            refused = call('PUT', cancelled_path + '/asks/' + str(question['id']), answer, 404)
            assert refused['error']['code'] == 'ask_not_found', refused
            proof['cases'].append({'kind': 'cancelled_question', 'falsely_acknowledged': False})
            _, headers, _ = raw_request(port, 'GET', '/api/v1/bootstrap')
            old_token = headers['x-mdo-write-token']
            stop_host(process); process = None
            process = start_host(args.host.resolve(), config, base / 'home', environment, base / 'xs.log', port)
            status, _, raw = raw_request(port, 'PUT', '/api/v1' + ask_path, body=json.dumps(answer).encode(),
                headers={'Content-Type': 'application/json', 'X-Mdo-Write-Token': old_token})
            assert status == 412 and json.loads(raw)['error']['code'] == 'write_token_conflict', (status, raw)
            refused = call('PUT', ask_path, answer, 404)
            assert refused['error']['code'] == 'ask_not_found', refused
            proof['cases'].append({'kind': 'restart', 'old_token_rejected': True, 'receipt_cleared': True})
            proof['passed'] = True
            print(json.dumps(proof, ensure_ascii=False), flush=True)
        except BaseException as error:
            proof['failure'] = str(error)
            print(json.dumps(proof, ensure_ascii=False), flush=True)
            raise
        finally:
            if args.evidence:
                args.evidence.parent.mkdir(parents=True, exist_ok=True)
                args.evidence.write_text(json.dumps(proof, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            stop_host(process); model.shutdown(); model.server_close(); thread.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--baseline-commit')
    parser.add_argument('--evidence', type=Path)
    probe(parser.parse_args())
