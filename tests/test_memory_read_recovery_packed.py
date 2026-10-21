"""Replay wrong memory paths through packed mdo, including an old-chat upgrade.

Only a loopback model and disposable Home are used. The original executable
is copied for the seed chat; its running instance and data are never touched.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies
from test_responses_followup_runtime import api, setup_provider, terminal


class Provider(BaseHTTPRequestHandler):
    requests = []
    counts = {}
    failures = []
    cases = {}

    def log_message(self, *_):
        pass

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.requests.append(payload)
        user = next(x['content'] for x in reversed(payload['input']) if x.get('role') == 'user')
        label = user if isinstance(user, str) else user[0]['text']
        step = self.counts.get(label, 0)
        self.counts[label] = step + 1
        outputs = []
        for item in payload['input']:
            if item.get('type') != 'function_call_output':
                continue
            value = item.get('output', '')
            outputs.append(value if isinstance(value, str) else json.dumps(value))
        prior = '\n'.join(outputs)
        body_text = json.dumps(payload, ensure_ascii=False)
        paths = []
        if label == 'recover-memory':
            paths = ['MEMORY.md', '@memory-global/MEMORY.md',
                     '@memory-global/gpu-server-ornith.md']
            required = ['[Server](@memory-global/gpu-server-ornith.md)',
                        '@memory-project/MEMORY.md']
            if step:
                required += ['read_failure: reason=not_found', 'resolved_path:',
                             'Memory recovery:']
            if step >= 3:
                required += ['FIXTURE_SERVER_CONTENT']
            for text in required:
                if text not in (body_text if text.startswith('[Server]') else body_text + prior):
                    self.failures.append((label, step, text))
        elif label == 'invalid-read-path':
            paths = ['tmp_diag.py\n</parameter atlas="#">\n Atlas #>\n']
            if step and 'read_failure: reason=invalid_argument' not in prior:
                self.failures.append((label, step, 'invalid_argument'))
        elif label in self.cases:
            target, expected = self.cases[label]
            paths = [target]
            if step and expected not in prior:
                self.failures.append((label, step, expected))
            if label.endswith('full-access') and 'Full access: native file paths outside the workspace are allowed.' not in body_text:
                self.failures.append((label, step, 'full-access prompt'))
        output = [{'type': 'message', 'content': [{'type': 'output_text',
                   'text': 'Memory read fixture complete'}]}]
        if step < len(paths):
            output = [{'type': 'function_call', 'name': 'read',
                       'call_id': label + '-' + str(step),
                       'arguments': json.dumps({'path': paths[step]})}]
        raw = json.dumps({'id': 'memory-recovery-fixture', 'status': 'completed',
            'model': 'fixture-followup', 'output': output,
            'usage': {'input_tokens': 8, 'output_tokens': 4, 'total_tokens': 12}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def run(packed, previous):
    Provider.requests, Provider.counts, Provider.failures = [], {}, []
    model = ThreadingHTTPServer(('127.0.0.1', 0), Provider)
    thread = threading.Thread(target=model.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='memory-read-packed-', dir=ROOT / '.build') as raw, \
             tempfile.TemporaryDirectory(prefix='memory-read-external-') as external_raw:
            base = Path(raw)
            directory, port = site(base, 'site', previous or packed)
            home = base / 'mdo-home'
            memory = home / 'memory/global'
            memory.mkdir(parents=True)
            index = '# Memory\n\n- [Server](gpu-server-ornith.md)\n'
            (memory / 'MEMORY.md').write_text(index, encoding='utf-8')
            (memory / 'gpu-server-ornith.md').write_text('FIXTURE_SERVER_CONTENT\n', encoding='utf-8')
            (memory / '.legacy-imported').write_text('1\n', encoding='utf-8')
            workspace = home / 'workspace'
            workspace.mkdir()
            (workspace / 'note.txt').write_text('CORRECTED_XML_CONTENT\n', encoding='utf-8')
            external = Path(external_raw) / '慕晴.txt'
            external.write_text('PACKED_EXTERNAL_CONTENT 慕晴\n', encoding='utf-8')
            Provider.cases = {
                'recover-topic': ('gpu-server-ornith.md', 'read_recovery:'),
                'recover-legacy-topic': (str(home / 'data/memory/gpu-server-ornith.md'), 'FIXTURE_SERVER_CONTENT'),
                'recover-xml': ('note.txt\n</parameter atlas="#">\n Atlas #>\n', 'CORRECTED_XML_CONTENT'),
                'recover-quoted-path': (' "note.txt" ', 'read_recovery:'),
                'outside-full-access': (str(external), 'PACKED_EXTERNAL_CONTENT'),
                'outside-balanced': (str(external), 'reason=permission_denied'),
                'outside-read-only': (str(external), 'reason=permission_denied'),
            }
            env = dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base), HOME=str(base))
            current = previous or packed
            process = None
            try:
                process = start(directory, current, home, env)
                _, bootstrap = wait_bootstrap(process, port, directory / 'packed.log')
                assert bootstrap['data']['ready'], bootstrap
                setup_provider(port, model.server_port)
                session, _ = api(port, 'POST', '/api/v1/sessions', {
                    'project_id': 'default', 'agent_id': 'mdo.default',
                    'model_id': 'fixture-followup', 'protocol': 'openai-responses',
                    'permission_profile': 'full-access', 'max_output_tokens': 1024})
                path = '/api/v1/projects/default/sessions/' + session['id']

                def send(prompt):
                    operation, _ = api(port, 'POST', path + '/runs', {'prompt': prompt})
                    value = terminal(port, operation)
                    assert value['state'] == 'succeeded', value

                send('seed-chat')
                stop(process)
                process = None
                release_packed_copies(directory / current.name)
                shutil.copy2(packed, directory / packed.name)
                process = start(directory, packed, home, env)
                _, bootstrap = wait_bootstrap(process, port, directory / 'packed.log')
                assert bootstrap['data']['ready'], bootstrap
                send('recover-memory')
                for label in ('recover-topic', 'recover-legacy-topic', 'recover-xml',
                              'recover-quoted-path', 'outside-full-access'):
                    send(label)
                    assert Provider.counts[label] == 2, (label, Provider.counts)
                for profile in ('balanced', 'read-only'):
                    other, _ = api(port, 'POST', '/api/v1/sessions', {
                        'project_id': 'default', 'agent_id': 'mdo.default',
                        'model_id': 'fixture-followup', 'protocol': 'openai-responses',
                        'permission_profile': profile, 'max_output_tokens': 1024})
                    operation, _ = api(port, 'POST', '/api/v1/projects/default/sessions/' +
                        other['id'] + '/runs', {'prompt': 'outside-' + profile})
                    value = terminal(port, operation)
                    assert value['state'] == 'succeeded', value
                    assert Provider.counts['outside-' + profile] == 2
                if os.name == 'nt':
                    send('invalid-read-path')
                assert not Provider.failures, Provider.failures
                assert (memory / 'MEMORY.md').read_text(encoding='utf-8') == index
                assert Provider.counts['recover-memory'] == 4, Provider.counts
                print('Packed old-chat refresh, full-access external UTF-8 read, confined profiles, single-call path recovery and genuine invalid paths: PASS')
            finally:
                if process:
                    stop(process)
                release_packed_copies(directory / packed.name)
    finally:
        model.shutdown()
        model.server_close()
        thread.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    parser.add_argument('--previous', type=Path)
    args = parser.parse_args()
    run(args.packed.resolve(), args.previous.resolve() if args.previous else None)
