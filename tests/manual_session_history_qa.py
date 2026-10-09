"""Bounded real packed UI history preparation through a loopback HTTP proxy.

CUA/manual browser actions use the printed URL. The ordinary editable model
answers locally; the candidate's embedded sources and native APIs are unchanged.
Read modes: recover (two 503s), hold (one stalled body), refuse (503 until healthy).
Write modes: fork_drop (lost ACK), fork_hold (stalled ACK), fork_refuse (lost
ACK and unavailable child reads until healthy). Fork POSTs always reach native.
Create healthy/release/stop files in the printed isolated directory as needed.
"""
import argparse
from collections import Counter
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import http.client
import json
import os
from pathlib import Path
import socket
import threading
import time

from test_api_runtime import request, session_events
from test_interrupt_runtime import until
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Model(BaseHTTPRequestHandler):
    calls = 0

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        type(self).calls += 1
        raw = json.dumps({'id': 'history-fixture', 'model': body['model'],
            'choices': [{'index': 0, 'message': {'role': 'assistant',
                'content': 'Ready to continue.'}, 'finish_reason': 'stop'}],
            'usage': {'prompt_tokens': 10, 'completion_tokens': 5}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(raw)


class Proxy(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    lock = threading.Lock()
    attempts = Counter()
    proof = []
    stopping = threading.Event()

    def log_message(self, *_):
        pass

    def handle_request(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        is_history = self.command == 'GET' and self.path == self.session_path + '/history'
        is_fork = self.command == 'POST' and self.path == self.session_path + '/fork'
        is_confirmation = self.command == 'GET' and self.fork_id and self.path == '/api/v1/projects/default/sessions/' + self.fork_id
        key = self.command + ' ' + self.path
        with self.lock:
            if is_history or is_fork or is_confirmation:
                self.attempts[key] += 1
            attempt = self.attempts[key]
        fault = None
        if is_history and not (self.base / 'healthy').exists():
            if self.mode == 'refuse' or self.mode == 'recover' and attempt <= 2:
                fault = 'refusal'
            elif self.mode == 'hold' and attempt == 1:
                fault = 'stall_body'
        if is_fork and attempt == 1:
            if self.mode in ('fork_drop', 'fork_refuse'):
                fault = 'drop_ack'
            elif self.mode == 'fork_hold':
                fault = 'stall_body'
        if is_confirmation and self.mode == 'fork_refuse' and not (self.base / 'healthy').exists():
            fault = 'refusal'
        if fault == 'refusal':
            status, headers = 503, [('Content-Type', 'application/json')]
            raw = json.dumps({'ok': False, 'error': {'code': 'session_history_unavailable',
                'message': 'Fixture read unavailable; no write applied'}}).encode()
        else:
            connection = http.client.HTTPConnection('127.0.0.1', self.native_port, timeout=35)
            try:
                forwarded = {key: value for key, value in self.headers.items()
                    if key.lower() not in ('host', 'connection', 'accept-encoding')}
                forwarded['Connection'] = 'close'
                connection.request(self.command, self.path, body or None, forwarded)
                response = connection.getresponse()
                status, headers, raw = response.status, response.getheaders(), response.read()
            finally:
                connection.close()
        record = None
        if is_fork and status in (200, 201):
            type(self).fork_id = json.loads(raw)['data']['id']
        if is_history or is_fork or is_confirmation:
            record = {'method': self.command, 'path': self.path, 'attempt': attempt,
                'status': status, 'seconds': round(time.monotonic() - self.started, 3),
                'fault': fault, 'response': json.loads(raw)}
            record['kind'] = 'history' if is_history else 'fork' if is_fork else 'child_read'
            if body:
                record['body'] = json.loads(body)
            with self.lock:
                self.proof.append(record)
            # An observation marker, never a product fault seam.
            (self.base / 'observed.json').write_text(json.dumps(list(self.proof), indent=2), encoding='utf-8')
        if fault == 'drop_ack':
            assert status in (200, 201), raw
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            record['finished_seconds'] = round(time.monotonic() - self.started, 3)
            return
        self.send_response(status)
        for key, value in headers:
            if key.lower() not in ('connection', 'content-length', 'transfer-encoding'):
                self.send_header(key, value)
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.close_connection = True
        if fault == 'stall_body':
            self.wfile.flush()
            (self.base / 'holding').touch()
            end = time.monotonic() + 20
            while not (self.base / 'release').exists() and not self.stopping.is_set() and time.monotonic() < end:
                self.stopping.wait(.1)
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            if record is not None:
                record['client_closed'] = True
        if record is not None:
            record['finished_seconds'] = round(time.monotonic() - self.started, 3)

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = handle_request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', required=True, type=Path)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--mode', required=True, choices=('recover', 'hold', 'refuse',
        'fork_drop', 'fork_hold', 'fork_refuse'))
    args = parser.parse_args()
    base, packed = args.directory.resolve(), args.packed.resolve()
    assert not base.exists(), 'Choose a new isolated directory'
    base.mkdir(parents=True)
    native, port = site(base, 'native', packed)
    model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    Model.calls = 0
    model_worker = threading.Thread(target=model.serve_forever, daemon=True)
    model_worker.start()
    process = start(native, packed, base / 'home',
        dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base), MDO_QUEUE_FIXTURE_KEY='fixture-only'))
    proxy = worker = None

    def call(method, path, body=None, expected=200):
        status, _, raw = request(port, method, '/api/v1' + path,
            body=json.dumps(body).encode() if body is not None else None,
            headers={'Content-Type': 'application/json'})
        assert status == expected, raw
        return json.loads(raw)['data']

    try:
        status, bootstrap = wait_bootstrap(process, port, native / 'packed.log')
        assert status == 200 and bootstrap['data']['ready'], bootstrap
        configure_model(port, f'http://127.0.0.1:{model.server_port}/v1')
        def create(title):
            return call('POST', '/sessions', {'project_id': 'default', 'title': title,
                'model_id': 'queue-fixture', 'reasoning_effort': 'none', 'permission_profile': 'balanced',
                'workspace_root': str(base)}, 201)
        source, other = create('History recovery source'), create('History other session')
        path = '/projects/default/sessions/' + source['id']
        run = call('POST', path + '/runs', {'prompt': 'Seed the history recovery check.'}, 202)
        result = until(lambda: (value if (value := call('GET', '/runs/' + run['id']))['terminal'] else None), seconds=15)
        assert result['state'] == 'succeeded', result
        snapshot = base / 'home/sessions/default' / source['id'] / 'snapshot.json'
        before = hashlib.sha256(snapshot.read_bytes()).hexdigest()
        Proxy.base, Proxy.native_port, Proxy.mode = base, port, args.mode
        Proxy.session_path = '/api/v1' + path
        Proxy.started = time.monotonic()
        Proxy.fork_id = ''
        Proxy.proof = []; Proxy.attempts = Counter(); Proxy.stopping.clear()
        proxy = ThreadingHTTPServer(('127.0.0.1', 0), Proxy)
        worker = threading.Thread(target=proxy.serve_forever, daemon=True); worker.start()
        url = f'http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/'
        print(json.dumps({'url': url + source['id'], 'other_url': url + other['id'],
            'native_port': port, 'source_id': source['id'], 'other_id': other['id'],
            'directory': str(base), 'stop_file': str(base / 'stop')}), flush=True)
        end = time.monotonic() + 540
        while not (base / 'stop').exists() and time.monotonic() < end:
            assert process.poll() is None, 'Native candidate exited'
            time.sleep(.1)
        listing = call('GET', '/sessions')['items']
        forks = [entry for entry in listing if entry.get('parent_session_id') == source['id']]
        events = session_events(port, '/api/v1' + path)
        for fork in forks:
            events += session_events(port, '/api/v1/projects/default/sessions/' + fork['id'])
        proof = {'mode': args.mode, 'source_id': source['id'], 'other_id': other['id'],
            'model_calls': Model.calls, 'transport': list(Proxy.proof), 'forks': forks,
            'source_snapshot_unchanged': before == hashlib.sha256(snapshot.read_bytes()).hexdigest(),
            'final_errors': sum(e['kind'] == 'error' for e in events),
            'event_kinds': dict(Counter(e['kind'] for e in events))}
        (base / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'saved': str(base / 'proof.json'), 'history_attempts':
            sum(item['kind'] == 'history' for item in proof['transport']), 'forks': len(forks),
            'source_snapshot_unchanged': proof['source_snapshot_unchanged']}), flush=True)
    finally:
        Proxy.stopping.set()
        if proxy is not None:
            proxy.shutdown(); proxy.server_close(); worker.join(timeout=3)
        stop(process); model.shutdown(); model.server_close(); model_worker.join(timeout=3)


if __name__ == '__main__':
    main()
