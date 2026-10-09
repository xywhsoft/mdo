"""Unmodified packed mdo through a bounded loopback ACK-loss proxy.

Browser actions are manual/CUA. The first answer's confirmed response is dropped;
the first approval's confirmed body is withheld for 12 seconds. A later
blocked-decision question receives explicit pre-application refusals until the
printed resume file is created. Create the stop file to finish and save proof.
No fault seams, provider credentials or external requests enter the candidate.
"""
import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import http.client
import json
import os
from pathlib import Path
import socket
import threading
import time

from test_api_runtime import request, session_events
from test_decision_retry_runtime import Model
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Proxy(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    lock = threading.Lock()
    attempts = Counter()
    proof = []
    native_port = None
    base = None
    stopping = threading.Event()

    def log_message(self, *_):
        pass

    def handle_request(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        is_ask = self.command == 'PUT' and '/asks/' in self.path
        is_approval = self.command == 'PUT' and self.path.startswith('/api/v1/approvals/')
        with self.lock:
            if is_ask or is_approval:
                self.attempts[self.path] += 1
            attempt = self.attempts[self.path]
        refusal = is_ask and (self.base / 'block').exists() and not (self.base / 'resume').exists()
        if refusal:
            status, headers = 503, [('Content-Type', 'application/json')]
            raw = json.dumps({'ok': False, 'error': {'code': 'decisions_unavailable',
                'message': 'Fixture pre-application refusal; no new decision was applied'}}).encode()
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
        if is_ask or is_approval:
            with self.lock:
                self.proof.append({'path': self.path, 'attempt': attempt,
                    'body': json.loads(body), 'native_status': None if refusal else status,
                    'status': status, 'seconds': round(time.monotonic() - self.started, 3),
                    'fault': 'refusal' if refusal else 'drop_ack' if is_ask and attempt == 1
                        else 'stall_body' if is_approval and attempt == 1 else None})
        if is_ask and attempt == 1 and not refusal:
            assert status == 200, raw
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            return
        self.send_response(status)
        for key, value in headers:
            if key.lower() not in ('connection', 'content-length', 'transfer-encoding'):
                self.send_header(key, value)
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.close_connection = True
        if is_approval and attempt == 1:
            assert status == 200, raw
            self.wfile.flush()
            self.stopping.wait(12)
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError):
            pass

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = handle_request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', required=True, type=Path)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args()
    base = args.directory.resolve()
    assert not base.exists(), 'Choose a new isolated directory'
    base.mkdir(parents=True)
    packed = args.packed.resolve()
    native, port = site(base, 'native', packed)
    model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    Model.calls = Counter(); Model.workspace = base
    model_worker = threading.Thread(target=model.serve_forever, daemon=True); model_worker.start()
    Proxy.base, Proxy.native_port = base, port
    Proxy.started = time.monotonic(); Proxy.proof = []; Proxy.attempts = Counter(); Proxy.stopping.clear()
    proxy = ThreadingHTTPServer(('127.0.0.1', 0), Proxy)
    worker = threading.Thread(target=proxy.serve_forever, daemon=True); worker.start()
    process = start(native, packed, base / 'home',
        dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base), MDO_QUEUE_FIXTURE_KEY='fixture-only'))
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
        session = call('POST', '/sessions', {'project_id': 'default', 'title': 'Decision write recovery',
            'model_id': 'queue-fixture', 'reasoning_effort': 'none', 'permission_profile': 'balanced',
            'workspace_root': str(base)}, 201)
        path = '/projects/default/sessions/' + session['id']
        print(json.dumps({'url': f'http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/' + session['id'],
            'native_port': port, 'stop_file': str(base / 'stop'), 'block_file': str(base / 'block'),
            'resume_file': str(base / 'resume')}), flush=True)
        end = time.monotonic() + 360
        while not (base / 'stop').exists() and time.monotonic() < end:
            assert process.poll() is None, 'Native candidate exited'
            time.sleep(.1)
        events = session_events(port, '/api/v1' + path)
        proof = {'model_calls': dict(Model.calls), 'transport': list(Proxy.proof),
            'exec_invocations': len((base / 'executions.txt').read_text().splitlines())
                if (base / 'executions.txt').exists() else 0,
            'final_errors': sum(e['kind'] == 'error' for e in events),
            'event_kinds': dict(Counter(e['kind'] for e in events)),
            'tool_results': [e for e in events if e['kind'] == 'tool_done']}
        assert proof['model_calls']['interactive-decision'] == 3, proof
        assert proof['exec_invocations'] == 1 and proof['final_errors'] == 0, proof
        assert all(e['success'] for e in proof['tool_results']), proof
        (base / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    finally:
        Proxy.stopping.set(); proxy.shutdown(); proxy.server_close(); worker.join(timeout=3)
        stop(process); model.shutdown(); model.server_close(); model_worker.join(timeout=3)


if __name__ == '__main__':
    main()
