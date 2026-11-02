"""Default C todo is seeded, callable, recoverable and independently managed."""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)
from test_packed_queue_start_recovery import configure_model
from test_api_runtime import session_events

PLAN = [{"text": "Inspect repository", "done": True},
        {"text": "Verify result", "done": False}]


class Model(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    counts = {}
    saw_todo = False

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        cls = type(self)
        names = {row['function']['name'] for row in body.get('tools', [])}
        assert 'todo' in names and 'mdo.todo' not in names, names
        cls.saw_todo = True
        prompt = next(row['content'] for row in reversed(body['messages'])
                      if row['role'] == 'user')
        count = cls.counts.get(prompt, 0)
        cls.counts[prompt] = count + 1
        message = {'role': 'assistant', 'content': 'Plan updated.'}
        reason = 'stop'
        if count == 0 or (prompt == 'Correct invalid plan' and count == 1):
            args = {'items': [] if prompt == 'Clear plan' else PLAN}
            if prompt == 'Correct invalid plan' and count == 0:
                args = {'items': [{'text': '', 'done': False}]}
            message = {'role': 'assistant', 'content': None, 'tool_calls': [{
                'id': 'todo-' + str(count), 'type': 'function',
                'function': {'name': 'todo', 'arguments': json.dumps(args)}}]}
            reason = 'tool_calls'
        data = json.dumps({'id': 'todo-fixture', 'model': body['model'],
            'choices': [{'index': 0, 'message': message, 'finish_reason': reason}],
            'usage': {'prompt_tokens': 20, 'completion_tokens': 10}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, default=ROOT / 'mdo.exe')
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    packed = args.packed.resolve()
    results = {}

    def check(name, condition, detail=None):
        assert condition, (name, detail)
        results[name] = True
        print('PASS', name, flush=True)

    with tempfile.TemporaryDirectory(prefix='todo-packed-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        directory, port = site(base, 'site', packed)
        home = base / 'mdo-home'
        env = dict(os.environ, USE_WEBVIEW='0', MDO_QUEUE_FIXTURE_KEY='fixture-only')
        # Simulate an existing pre-todo Home, with another bundled role already
        # seeded. The new tools:todo receipt must still be installed on upgrade.
        (home / 'data/extensions').mkdir(parents=True)
        (home / 'data/extensions/builtins.json').write_text('{}', encoding='utf-8')
        process = start(directory, packed, home, env)
        model = None
        try:
            bootstrap = wait_bootstrap(process, port, directory / 'packed.log')[1]
            check('bootstrap_ready', bootstrap['data']['ready'], bootstrap)
            status, result = request(port, 'GET', '/api/v1/modules')
            tools = {row['id'] for row in result['data']['tools']}
            check('default_c_tool_has_short_name', status == 200 and 'todo' in tools
                  and 'mdo.todo' not in tools and 'mdo.echo' not in tools, result)
            status, result = request(port, 'GET', '/api/v1/tools')
            tools = {row['id']: row for row in result['data']['items']}
            check('catalog_exposes_c_tool', status == 200 and tools['todo']['source'] == 'builtin'
                  and {'read', 'exec', 'spawn', 'status', 'wait', 'ask', 'skill', 'agent'} <= tools.keys(), result)
            check('extension_is_directory', (home / 'tools/todo/extension.json').is_file()
                  and (home / 'tools/todo/tool.c').is_file())
            if args.native:
                for script in ['test_todo_native.py', 'test_extension_builtins_native.py']:
                    subprocess.run([sys.executable, str(ROOT / 'tests' / script),
                        '--url', f'http://127.0.0.1:{port}', '--home', str(home)],
                        cwd=ROOT, check=True, timeout=90)

            model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
            thread = threading.Thread(target=model.serve_forever, daemon=True)
            thread.start()
            configure_model(port, f'http://127.0.0.1:{model.server_port}/v1/chat/completions')
            status, result = request(port, 'POST', '/api/v1/sessions', {
                'project_id': 'default', 'title': 'Bundled Todo QA',
                'agent_id': 'mdo.default', 'model_id': 'queue-fixture',
                'protocol': 'openai-chat-completions', 'reasoning_effort': 'none',
                'max_output_tokens': 1024})
            assert status == 201, result
            path = '/api/v1/projects/default/sessions/' + result['data']['id']

            def run(prompt):
                status, result = request(port, 'POST', path + '/runs', {
                    'prompt': prompt, 'timeout_ms': 10000})
                assert status == 202, result
                run = result['data']
                deadline = time.monotonic() + 15
                while not run['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05)
                    status, result = request(port, 'GET', '/api/v1/runs/' + run['id'])
                    run = result['data']
                if run['state'] != 'succeeded':
                    events = session_events(port, path)
                    raise AssertionError((run, events))

            run('Update plan')
            status, plan = request(port, 'GET', path + '/todo')
            check('model_calls_default_todo_and_projects_plan', status == 200
                  and plan['data']['items'] == PLAN and Model.saw_todo, plan)
            run('Correct invalid plan')
            status, plan = request(port, 'GET', path + '/todo')
            check('invalid_call_can_correct_and_complete', status == 200 and
                  plan['data']['items'] == PLAN and Model.counts['Correct invalid plan'] == 3, plan)
            events = session_events(port, path)
            done = [e for e in events if e['kind'] == 'tool_done' and e['tool_name'] == 'todo']
            check('success_and_failure_recorded',
                  any(e['success'] for e in done) and any(not e['success'] for e in done), done)
            run('Clear plan')
            status, empty = request(port, 'GET', path + '/todo')
            check('clear_plan', status == 200 and empty['data']['items'] == [], empty)
            stop(process)
            process = start(directory, packed, home, env)
            wait_bootstrap(process, port, directory / 'packed.log')
            status, restored = request(port, 'GET', path + '/todo')
            check('plan_survives_restart', status == 200 and restored['data'] == empty['data'], restored)

            status, row = request(port, 'GET', '/api/v1/extensions/tools/todo')
            revision = row['data']['revision']
            from test_api_runtime import request as raw_request
            status, _, raw_result = raw_request(port, 'DELETE', '/api/v1/extensions/tools/todo',
                body=json.dumps({'expected_revision': revision}).encode(),
                headers={'Content-Type': 'application/json', 'If-Match': '"' + revision + '"'})
            check('default_tool_can_be_uninstalled', status == 200, raw_result)
            stop(process)
            process = start(directory, packed, home, env)
            wait_bootstrap(process, port, directory / 'packed.log')
            status, catalog = request(port, 'GET', '/api/v1/tools')
            check('uninstall_survives_restart', status == 200 and
                  all(row['id'] != 'todo' for row in catalog['data']['items']), catalog)
            status, preserved = request(port, 'GET', path + '/todo')
            check('uninstall_preserves_history', status == 200 and preserved['data'] == empty['data'], preserved)
        finally:
            stop(process)
            if model:
                model.shutdown(); model.server_close(); thread.join(timeout=5)
            release_packed_copies(directory / packed.name)
    if args.report:
        args.report.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('PASS', len(results), 'packed todo contracts')


if __name__ == '__main__':
    main()
