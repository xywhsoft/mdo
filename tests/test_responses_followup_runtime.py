"""Bounded packed/TCC checks for Responses follow-ups and failed-run recovery.

The loopback provider rejects string assistant history with an explicit message
discriminator, matching strict Responses servers. No account bypass or external
model service is used. An optional snapshot is copied into the disposable Home.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_api_runtime import request as raw_request
from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies


class Provider(BaseHTTPRequestHandler):
    requests: list[dict] = []
    fail_next = False
    lock = threading.Lock()

    def log_message(self, *_args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        with self.lock:
            self.requests.append(body)
            rejected = self.fail_next
            type(self).fail_next = False
        rejected |= any(item.get('role') == 'assistant' and
                        item.get('type') == 'message' and
                        isinstance(item.get('content'), str)
                        for item in body.get('input', []))
        status = 400 if rejected else 200
        result = {'error': {'type': 'invalid_request_error', 'message':
                           'Assistant output messages require structured content'}} if rejected else {
            'id': 'resp_followup', 'object': 'response', 'status': 'completed',
            'model': 'fixture-followup', 'output': [{
                'id': 'msg_followup', 'type': 'message', 'role': 'assistant',
                'status': 'completed', 'content': [{
                    'type': 'output_text', 'text': 'Reply complete', 'annotations': []}]}],
            'usage': {'input_tokens': 16, 'output_tokens': 4, 'total_tokens': 20}}
        payload = json.dumps(result).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def api(port, method, path, data=None, etag=None):
    headers = {'Content-Type': 'application/json'} if data is not None else {}
    if etag:
        headers['If-Match'] = etag
    status, response_headers, body = raw_request(port, method, path,
        body=json.dumps(data).encode() if data is not None else None, headers=headers)
    response = json.loads(body)
    assert status in (200, 201, 202), (status, response)
    return response['data'], response_headers


def terminal(port, run):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        value, _ = api(port, 'GET', '/api/v1/runs/' + run['id'])
        if value['terminal']:
            return value
        time.sleep(0.05)
    raise AssertionError('follow-up exceeded bounded deadline')


def setup_provider(port, provider_port):
    config, headers = api(port, 'GET', '/api/v1/models/config')
    supplier = {'id': 'fixture-followup', 'name': 'Fixture', 'builtin': False,
        'editable': True, 'removable': True, 'verify_peer': True, 'timeout_ms': 5000,
        'endpoints': {'responses': f'http://127.0.0.1:{provider_port}/v1/responses'}}
    model = copy.deepcopy(config['items'][0])
    model.update(id='fixture-followup', name='Fixture', provider='fixture-followup',
        wire_model='fixture-followup', builtin=False, free=False, editable=True, removable=True,
        protocols=['openai-responses'], default_protocol='openai-responses',
        reasoning_efforts=['medium'], default_reasoning_effort='medium', attachments=[],
        capabilities=['text-input', 'text-output', 'tool-result-input', 'tool-call-output',
                      'streaming', 'reasoning-control', 'parallel-tool-calls'])
    patch = {key: config[key] for key in ('providers', 'items', 'default_model')}
    patch['providers'].append(supplier)
    patch['items'].append(model)
    api(port, 'POST', '/api/v1/models/setup', {'patch': patch,
        'keys': [{'provider': 'fixture-followup', 'value': 'local-fixture-key'}]}, headers['etag'])


def run(packed: Path, session_copy: Path | None):
    Provider.requests = []
    Provider.fail_next = False
    server = ThreadingHTTPServer(('127.0.0.1', 0), Provider)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    process = None
    try:
        with tempfile.TemporaryDirectory(prefix='responses-followup-', dir=ROOT / '.build') as temp:
            base = Path(temp)
            directory, port = site(base, 'site', packed)
            home = base / 'mdo-home'
            env = dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base))

            def launch():
                instance = start(directory, packed, home, env)
                try:
                    _, bootstrap = wait_bootstrap(instance, port, directory / 'packed.log')
                    assert bootstrap['data']['ready'], bootstrap
                    return instance
                except BaseException:
                    stop(instance)
                    raise

            def send(path, prompt, expected='succeeded'):
                operation, _ = api(port, 'POST', path + '/runs', {'prompt': prompt})
                value = terminal(port, operation)
                if value['state'] != expected:
                    events = home / 'sessions/default' / session['id'] / 'ui-events.jsonl'
                    errors = [item.get('data', item) for line in events.read_text(encoding='utf-8').splitlines()
                              if (item := json.loads(line)).get('kind') == 11]
                    print('Provider requests:', len(Provider.requests), 'errors:', errors)
                assert value['state'] == expected, value

            def resume(path):
                recovery, _ = api(port, 'GET', path + '/recovery')
                assert recovery['resume_required'] and not recovery['items'], recovery
                operation, _ = api(port, 'POST', path + '/resume', {
                    'recovery_token': recovery['recovery_token'], 'decisions': []})
                value = terminal(port, operation)
                assert value['state'] == 'succeeded', value
                recovery, _ = api(port, 'GET', path + '/recovery')
                assert not recovery['resume_required'], recovery

            try:
                process = launch()
                setup_provider(port, server.server_port)
                session, _ = api(port, 'POST', '/api/v1/sessions', {
                    'project_id': 'default', 'agent_id': 'mdo.default',
                    'model_id': 'fixture-followup', 'protocol': 'openai-responses',
                    'reasoning_effort': 'medium', 'permission_profile': 'full-access',
                    'max_output_tokens': 1024})
                path = '/api/v1/projects/default/sessions/' + session['id']
                send(path, 'Hello, who are you?')
                send(path, 'What can you do?')
                assert sum(item.get('role') == 'assistant' for item in Provider.requests[-1]['input']) == 1
                stop(process)
                process = launch()
                send(path, 'Continue after reopening')
                Provider.fail_next = True
                send(path, 'Recover this follow-up', 'failed')
                stop(process)
                process = launch()
                resume(path)
                pending = Provider.requests[-1]['input']
                assert sum(item.get('content') == 'Recover this follow-up' for item in pending) == 1
                send(path, 'Next question after recovery')
                assert len(Provider.requests) == 6, len(Provider.requests)
                print('PASS first reply, follow-up, reopen, failed follow-up recovery, next question')
                if session_copy:
                    stop(process)
                    process = None
                    meta = json.loads((session_copy / 'meta.json').read_text(encoding='utf-8'))
                    snapshot = (session_copy / 'snapshot.json').read_bytes()
                    events = (session_copy / 'ui-events.jsonl').read_bytes()
                    target = home / 'sessions/default' / meta['id']
                    assert not target.exists()
                    target.mkdir(parents=True)
                    meta.update(project_id='default', model_id='fixture-followup',
                        protocol='openai-responses', reasoning_effort='medium', max_output_tokens=1024,
                        workspace_root=str(home / 'workspace'))
                    (target / 'meta.json').write_text(json.dumps(meta), encoding='utf-8')
                    (target / 'snapshot.json').write_bytes(snapshot)
                    (target / 'ui-events.jsonl').write_bytes(events)
                    process = launch()
                    resume('/api/v1/projects/default/sessions/' + meta['id'])
                    assert Provider.requests[-1]['input'][-1]['content'] == json.loads(snapshot)['entries'][-1]['content']
                    assert Provider.requests[-1]['input'][-2]['content'] == json.loads(snapshot)['entries'][-2]['content']
                    print('PASS affected snapshot resumes with original assistant and pending follow-up intact')
            except BaseException:
                print((directory / 'packed.log').read_text(errors='replace')[-4000:])
                raise
            finally:
                if process is not None:
                    stop(process)
                release_packed_copies(directory / packed.name)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    parser.add_argument('--session-copy', type=Path,
                        help='read-only source session; copied into disposable Home for resume')
    args = parser.parse_args()
    run(args.packed.resolve(), args.session_copy.resolve() if args.session_copy else None)
