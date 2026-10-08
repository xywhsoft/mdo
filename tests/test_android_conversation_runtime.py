"""Opt-in, bounded ARM64 conversation acceptance against an isolated QA APK.

Only application IDs below org.xleaves.mdo.qa. are accepted. The fixture uses
normal model settings, Android's credential store and the installed C/TCC host.
ADB only handles developer lifecycle, logs and port forwarding; no UI gestures.
No production account, package or Home is read, changed or uninstalled.
"""
from __future__ import annotations

import argparse
from collections import Counter
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import socket
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


class Model(BaseHTTPRequestHandler):
    calls = Counter()
    requests = []
    lock = threading.Lock()
    resume_stop = False

    def log_message(self, *_args):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # Normal cancellation closes the fixture's socket.

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        prompt = next(row['content'] for row in reversed(body['messages'])
                      if row['role'] == 'user')
        if isinstance(prompt, list):
            prompt = ''.join(part.get('text', '') for part in prompt)
        assert self.headers.get('Authorization') == 'Bearer android-fixture-key'
        with self.lock:
            self.calls[prompt] += 1
            attempt = self.calls[prompt]
            self.requests.append({'prompt': prompt, 'attempt': attempt,
                                  'at': time.monotonic(),
                                  'users': [row['content'] for row in body['messages']
                                            if row['role'] == 'user']})
        if prompt == 'network' and attempt == 1:
            self.connection.shutdown(socket.SHUT_RDWR)
            self.close_connection = True
            return
        if (prompt == 'retry' and attempt <= 2 or prompt == 'quota' or
                prompt == 'stop' and not self.resume_stop):
            raw = json.dumps({'error': {'code': 'daily_token_limit' if prompt == 'quota'
                                       else 'rate_limit_exceeded',
                                       'message': 'fixture provider prose'}}).encode()
            self.send_response(429)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Retry-After', '10' if prompt == 'stop' else '1')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        partial = prompt == 'partial' and attempt == 1
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.end_headers()
        text = 'ANDROID_DISCARDED_DRAFT' if partial else 'ANDROID_OK: ' + prompt
        chunk = {'choices': [{'index': 0, 'delta': {'role': 'assistant', 'content': text}}]}
        self.wfile.write(('data: ' + json.dumps(chunk) + '\n\n').encode())
        self.wfile.flush()
        if partial:
            self.connection.shutdown(socket.SHUT_RDWR)
            self.close_connection = True
            return
        done = {'choices': [{'index': 0, 'delta': {}, 'finish_reason': 'stop'}],
                'usage': {'prompt_tokens': 10, 'completion_tokens': 4, 'total_tokens': 14}}
        self.wfile.write(('data: ' + json.dumps(done) + '\n\ndata: [DONE]\n\n').encode())


def run(args):
    if not re.fullmatch(r'org\.xleaves\.mdo\.qa\.[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*', args.package):
        raise ValueError('Use an independent org.xleaves.mdo.qa.* package, never the daily app')
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    prefix = [str(args.adb.resolve()), '-s', args.serial]

    def adb(*command, check=True):
        result = subprocess.run([*prefix, *command], capture_output=True, timeout=30)
        if check and result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace')[-1500:])
        return result.stdout.decode(errors='replace').strip()

    assert 'uid=' in adb('shell', 'run-as', args.package, 'id'), 'A debuggable QA APK is required'

    def ready_port(previous_log):
        end = time.monotonic() + 45
        while time.monotonic() < end:
            log = adb('shell', 'run-as', args.package, 'cat', 'files/mdo-home/native.log', check=False)
            ports = re.findall(r'http bound on 127\.0\.0\.1:(\d+)', log[len(previous_log):])
            if ports and adb('shell', 'pidof', args.package, check=False):
                return int(ports[-1])
            time.sleep(.25)
        raise AssertionError('QA host did not publish a loopback listener: ' + log[-2500:])

    def launch():
        previous_log = adb('shell', 'run-as', args.package, 'cat', 'files/mdo-home/native.log', check=False)
        adb('shell', 'am', 'start', '-n', args.package + '/org.xserver.android.XsActivity')
        return ready_port(previous_log)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    model_port = server.server_port
    local_port = None
    proof = {'scope': 'real ARM64 app C/TCC + HTTP API; browser/native UI separately',
             'package': args.package, 'stress_or_high_load': False, 'cases': []}
    try:
        adb('reverse', 'tcp:' + str(model_port), 'tcp:' + str(model_port))

        def reconnect(device_port):
            nonlocal local_port
            if local_port is not None:
                adb('forward', '--remove', 'tcp:' + str(local_port))
            local_port = int(adb('forward', 'tcp:0', 'tcp:' + str(device_port)))

        def request(method, path, data=None, extra=None):
            headers = dict(extra or {})
            if data is not None:
                headers['Content-Type'] = 'application/json'
            if method not in ('GET', 'HEAD', 'OPTIONS'):
                _, admission, _ = request('GET', '/api/v1/bootstrap')
                headers['X-Mdo-Write-Token'] = admission['x-mdo-write-token']
            connection = http.client.HTTPConnection('127.0.0.1', local_port, timeout=8)
            try:
                connection.request(method, path, None if data is None else json.dumps(data).encode(), headers)
                response = connection.getresponse()
                return response.status, {key.lower(): value for key, value in response.getheaders()}, json.loads(response.read())
            finally:
                connection.close()

        def data(method, path, body=None, extra=None):
            status, _, result = request(method, path, body, extra)
            assert status in (200, 201, 202), (method, path, status, result)
            return result['data']

        def wait(predicate, seconds=45):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                value = predicate()
                if value:
                    return value
                time.sleep(.08)
            raise AssertionError('Bounded Android fixture condition timed out')

        reconnect(launch())
        wait(lambda: data('GET', '/api/v1/bootstrap')['stage'] == 'ready')
        status, headers, response = request('GET', '/api/v1/models/config')
        assert status == 200
        config = response['data']
        provider = {'id': 'android-fixture', 'name': 'Android fixture', 'builtin': False,
                    'editable': True, 'removable': True, 'verify_peer': True, 'timeout_ms': 15000,
                    'endpoints': {'chat_completions': f'http://127.0.0.1:{model_port}/v1/chat/completions'}}
        model = dict(config['items'][0])
        model.update(id='android-fixture', name='Android fixture', provider='android-fixture',
                     wire_model='android-fixture', builtin=False, free=False, editable=True, removable=True,
                     protocols=['openai-chat-completions'], default_protocol='openai-chat-completions',
                     reasoning_efforts=['none'], default_reasoning_effort='none', attachments=[],
                     capabilities=['text-input', 'text-output', 'tool-result-input', 'tool-call-output',
                                   'parallel-tool-calls', 'reasoning-control', 'streaming'])
        patch = {key: config[key] for key in ('providers', 'items', 'default_model')}
        # A failed fixture run may have saved its own provider before failing.
        # Update that QA provider for the new peer without clearing any Home.
        patch['providers'] = [row for row in patch['providers'] if row['id'] != 'android-fixture']
        patch['items'] = [row for row in patch['items'] if row['id'] != 'android-fixture']
        patch['providers'].append(provider)
        patch['items'].append(model)
        data('POST', '/api/v1/models/setup', {'patch': patch,
             'keys': [{'provider': 'android-fixture', 'value': 'android-fixture-key'}]}, {'If-Match': headers['etag']})
        saved = data('GET', '/api/v1/models/config')
        secret_ref = saved['providers'][-1]['credential']['secret_ref']
        assert secret_ref.startswith('vault:') and 'android-fixture-key' not in json.dumps(saved)

        def session(title):
            value = data('POST', '/api/v1/sessions', {'project_id': 'default', 'title': title,
                         'model_id': 'android-fixture', 'reasoning_effort': 'none',
                         'permission_profile': 'read-only', 'max_output_tokens': 2048})
            return '/api/v1/projects/default/sessions/' + value['id']

        def start(path, prompt):
            return data('POST', path + '/runs', {'prompt': prompt, 'timeout_ms': 45000})

        def terminal(run):
            return wait(lambda: value if (value := data('GET', '/api/v1/runs/' + run['id']))['terminal'] else None)

        def events(path):
            result = []
            after = 0
            for _ in range(16):
                page = data('GET', path + f'/events?after={after}&limit=32')['items']
                if not page:
                    return result
                result.extend(page)
                after = page[-1]['event_id']
            raise AssertionError('Ordinary fixture exceeded its 512-event bound')

        path = session('安卓对话恢复验收')
        for prompt, count in [('retry', 3), ('network', 2), ('partial', 2)]:
            run = start(path, prompt)
            result = terminal(run)
            records = events(path)
            assert result['state'] == 'succeeded', result
            assert Model.calls[prompt] == count, (prompt, Model.calls)
            assert not [event for event in records if event['kind'] == 'error'], records
            proof['cases'].append({'name': prompt, 'requests': count, 'state': result['state'], 'errors': 0})
        quota = session('安卓额度提示验收')
        result = terminal(start(quota, 'quota'))
        errors = [event for event in events(quota) if event['kind'] == 'error']
        assert result['state'] == 'failed' and Model.calls['quota'] == 1 and len(errors) == 1, (result, errors)
        assert errors[0]['model_error_kind'] == 'daily_token_limit', errors
        proof['cases'].append({'name': 'quota', 'requests': 1, 'state': 'failed',
                               'errors': 1, 'error_kind': errors[0]['model_error_kind']})
        run = start(path, 'stop')
        wait(lambda: Model.calls['stop'] == 1)
        began = time.monotonic()
        data('DELETE', '/api/v1/runs/' + run['id'])
        result = terminal(run)
        assert result['state'] == 'cancelled', result
        time.sleep(1.2)
        assert Model.calls['stop'] == 1, 'Retry continued after cancellation'
        proof['cases'].append({'name': 'cancel_backoff', 'requests_before_resume': 1,
                               'state': result['state'], 'cancel_ms': round((time.monotonic()-began-1.2)*1000)})
        Model.resume_stop = True
        recovery = data('GET', path + '/recovery')
        assert recovery['resume_required'], recovery
        resumed = data('POST', path + '/resume', {'recovery_token': recovery['recovery_token'], 'decisions': []})
        assert terminal(resumed)['state'] == 'succeeded'
        assert not data('GET', path + '/recovery')['resume_required']
        assert terminal(start(path, 'continue'))['state'] == 'succeeded'
        draft = data('GET', path + '/draft')
        written = data('PUT', path + '/draft', {'revision': draft['revision'],
                       'text': '安卓重启后保留的草稿\nsecond line', 'attachments': []})
        old_pid = adb('shell', 'pidof', args.package)
        adb('shell', 'am', 'force-stop', args.package)
        reconnect(launch())
        wait(lambda: data('GET', '/api/v1/bootstrap')['stage'] == 'ready')
        assert adb('shell', 'pidof', args.package) != old_pid
        assert data('GET', path + '/draft') == written, 'Restart changed the saved draft'
        assert data('GET', '/api/v1/models/config')['providers'][-1]['credential']['secret_ref'] == secret_ref
        assert terminal(start(path, 'after-restart'))['state'] == 'succeeded'
        records = events(path)
        assert not [event for event in records if event['kind'] == 'error'], records
        proof['cases'].append({'name': 'resume_continue_restart', 'resumed': True,
                               'credential_retained': True, 'draft_retained': True, 'errors': 0})
        proof['calls'] = dict(Model.calls)
        proof['requests'] = Model.requests
        proof['url'] = f'http://127.0.0.1:{local_port}/#/projects/default/sessions/{path.rsplit("/", 1)[-1]}'
        (evidence / 'runtime.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        print('PASS Android native retry, disconnect, stream recovery, quota, cancel, resume, restart and credential/draft persistence', flush=True)
        print(proof['url'], flush=True)
        if args.hold_seconds:
            print('Browser fixture ready; create ' + str(evidence / 'stop') + ' to close', flush=True)
            end = time.monotonic() + args.hold_seconds
            while not (evidence / 'stop').exists() and time.monotonic() < end:
                time.sleep(.25)
            proof['calls_after_browser'] = dict(Model.calls)
            proof['requests_after_browser'] = Model.requests
            (evidence / 'runtime.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    finally:
        if 'calls' not in proof:
            proof['calls_at_failure'] = dict(Model.calls)
            proof['requests_at_failure'] = Model.requests
            (evidence / 'failure.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        adb('shell', 'am', 'force-stop', args.package, check=False)
        if local_port is not None:
            adb('forward', '--remove', 'tcp:' + str(local_port), check=False)
        adb('reverse', '--remove', 'tcp:' + str(model_port), check=False)
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--hold-seconds', type=int, default=0, choices=range(0, 1801), metavar='0..1800')
    run(parser.parse_args())
