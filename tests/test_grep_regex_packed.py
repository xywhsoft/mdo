"""Real packed grep regression for DFA acceptance and restart recovery.

Uses a loopback model and disposable source/session files. No user Home,
credentials, external providers or real project contents are accessed.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies
from test_responses_followup_runtime import api, setup_provider, terminal


class Model(BaseHTTPRequestHandler):
    results = []

    def log_message(self, *_args):
        pass

    def do_POST(self):
        assert self.path == '/v1/responses'
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        last_user = max(index for index, item in enumerate(payload['input']) if item.get('role') == 'user')
        results = [item['output'] for item in payload['input'][last_user + 1:]
                   if item.get('type') == 'function_call_output']
        if not results:
            output = [{'type': 'function_call', 'name': 'grep', 'call_id': 'grep-fixture',
                       'arguments': json.dumps({'pattern': 'match .*', 'path': 'examples',
                                                'max_results': 15})}]
        else:
            Model.results.append(results[-1])
            output = [{'type': 'message', 'role': 'assistant', 'content': [
                {'type': 'output_text', 'text': 'Grep completed without a crash.'}]}]
        raw = json.dumps({'id': 'grep-response', 'object': 'response', 'status': 'completed',
                          'model': 'fixture-followup', 'output': output,
                          'usage': {'input_tokens': 10, 'output_tokens': 4, 'total_tokens': 14}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def probe(packed: Path) -> dict:
    Model.results = []
    server = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    process = None
    try:
        with tempfile.TemporaryDirectory(prefix='grep-regex-', dir=ROOT / '.build') as raw:
            base = Path(raw)
            directory, port = site(base, 'site', packed)
            home = base / 'home'
            env = dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base),
                       MDO_GREP_FIXTURE_KEY='local-fixture-key')

            def launch():
                instance = start(directory, packed, home, env)
                try:
                    _, doc = wait_bootstrap(instance, port, directory / 'packed.log')
                    assert doc['data']['ready'], doc
                    return instance
                except BaseException:
                    stop(instance)
                    raise

            try:
                process = launch()
                # This probe tests grep and follow-up recovery; Linux has no
                # platform credential vault. Keep its synthetic key in memory.
                setup_provider(port, server.server_port, secret_ref='env:MDO_GREP_FIXTURE_KEY')
                session, _ = api(port, 'POST', '/api/v1/sessions', {
                    'project_id': 'default', 'agent_id': 'mdo.default',
                    'model_id': 'fixture-followup', 'protocol': 'openai-responses',
                    'permission_profile': 'full-access', 'max_output_tokens': 1024})
                path = '/api/v1/projects/default/sessions/' + session['id']
                workspace = home / 'workspace/examples'
                workspace.mkdir(parents=True, exist_ok=True)
                (workspace / 'fixture.c').write_text(
                    'unrelated\n\t\txrtahomatch Match;\nxmatch x\n中文match 正常\n', encoding='utf-8')
                for cycle in range(3):
                    if cycle:
                        stop(process)
                        process = launch()
                        api(port, 'GET', path)
                        api(port, 'GET', path + '/conversation?limit=1')
                    operation, _ = api(port, 'POST', path + '/runs', {'prompt': f'grep cycle {cycle}'})
                    result = terminal(port, operation)
                    assert result['state'] == 'succeeded', result
                    assert process.poll() is None, 'native grep crashed the host'
                events = [json.loads(line) for line in
                          (home / 'sessions/default' / session['id'] / 'ui-events.jsonl')
                          .read_text(encoding='utf-8').splitlines()]
                completed = [event for event in events
                             if event.get('kind') == 6 and event.get('tool_name') == 'grep']
                assert len(completed) == 3 and all(event['success'] for event in completed), completed
                assert len(Model.results) == 3, Model.results
                for output in Model.results:
                    for fragment in ('fixture.c:2:', 'fixture.c:3:', 'fixture.c:4:', 'xrtahomatch Match;'):
                        assert fragment in output, (fragment, output)
                assert not list(directory.glob('crash_*.dmp'))
            finally:
                if process is not None:
                    stop(process)
                    process = None
                release_packed_copies(directory / packed.name)
        return {'passed': True, 'grep_completions': 3, 'restarts': 2,
                'external_model_calls': 0, 'original_user_home_changed': False}
    finally:
        if process is not None:
            stop(process)
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, default=ROOT / 'mdo.exe')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    proof = probe(args.packed.resolve())
    if args.report:
        args.report.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print('PASS packed regex grep, model follow-up and two restarts')
