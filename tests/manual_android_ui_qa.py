"""Bounded native-phone UI fixture using only an independent debuggable QA APK.

ADB manages lifecycle and port forwarding. Input, clipboard, image picking and
export are performed in the real phone UI; this script never simulates them.
Use a fresh evidence directory. A stop file ends the fixture early.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import threading
import time

REPLY = ('中文：像素画笔与橡皮。\nEnglish: keep the original text.\n'
         'Русский: сохранить исходный текст.\n\n- Color: #34a1ff\n- File: pixel.png\n')


class Model(BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert self.headers.get('Authorization') == 'Bearer android-fixture-key'
        content = next(row['content'] for row in reversed(body['messages']) if row['role'] == 'user')
        parts = content if isinstance(content, list) else [{'type': 'text', 'text': content}]
        images = [part['image_url']['url'] for part in parts if part.get('type') == 'image_url']
        image_hashes = [hashlib.sha256(base64.b64decode(url.split(',', 1)[1], validate=True)).hexdigest()
                        for url in images if url.startswith('data:')]
        self.requests.append({'model': body['model'], 'prompt': ''.join(p.get('text', '') for p in parts),
                              'images': len(images), 'image_sha256': image_hashes, 'at': time.monotonic()})
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.end_headers()
        chunks = [
            {'choices': [{'index': 0, 'delta': {'role': 'assistant', 'content': REPLY}}]},
            {'choices': [{'index': 0, 'delta': {}, 'finish_reason': 'stop'}],
             'usage': {'prompt_tokens': 50, 'completion_tokens': 40, 'total_tokens': 90}},
        ]
        try:
            for chunk in chunks:
                self.wfile.write(('data: ' + json.dumps(chunk) + '\n\n').encode())
            self.wfile.write(b'data: [DONE]\n\n')
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=900, choices=range(1, 1801), metavar='1..1800')
    args = parser.parse_args()
    if not re.fullmatch(r'org\.xleaves\.mdo\.qa\.[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*', args.package):
        raise ValueError('Use an independent org.xleaves.mdo.qa.* package, never the daily app')
    evidence = args.evidence.resolve()
    assert not evidence.exists(), 'Choose a fresh evidence directory'
    evidence.mkdir(parents=True)
    command = [str(args.adb.resolve()), '-s', args.serial]

    def adb(*parts, check=True):
        result = subprocess.run([*command, *parts], capture_output=True, timeout=30)
        if check and result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace')[-1500:])
        return result.stdout.decode(errors='replace').strip()

    assert 'uid=' in adb('shell', 'run-as', args.package, 'id'), 'A debuggable QA APK is required'
    server = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    model_port, local_port, device_port = server.server_port, None, None
    proof = {'scope': 'real QA APK and native UI; synthetic model', 'package': args.package,
             'stress_or_high_load': False, 'expected_reply': REPLY}

    def launch():
        before = adb('shell', 'run-as', args.package, 'cat', 'files/mdo-home/native.log', check=False)
        adb('shell', 'am', 'start', '-n', args.package + '/org.xserver.android.XsActivity')
        end = time.monotonic() + 45
        while time.monotonic() < end:
            log = adb('shell', 'run-as', args.package, 'cat', 'files/mdo-home/native.log', check=False)
            matches = re.findall(r'http bound on 127\.0\.0\.1:(\d+)', log[len(before):])
            if matches and adb('shell', 'pidof', args.package, check=False):
                return int(matches[-1])
            time.sleep(.2)
        raise AssertionError('QA host did not publish a loopback listener')

    def reconnect(port):
        nonlocal local_port, device_port
        if local_port is not None:
            adb('forward', '--remove', 'tcp:' + str(local_port))
        local_port = int(adb('forward', 'tcp:0', 'tcp:' + str(port)))
        device_port = port

    def request(method, path, body=None, headers=None):
        # ADB forwards to another PC port; the public API still validates the
        # phone listener's authority. Keep its Host and Origin together.
        authority = f'127.0.0.1:{device_port}'
        headers = {'Host': authority, 'Origin': 'http://' + authority, **(headers or {})}
        if body is not None:
            headers['Content-Type'] = 'application/json'
        if method not in ('GET', 'HEAD', 'OPTIONS'):
            headers['X-Mdo-Write-Token'] = request('GET', '/api/v1/bootstrap')[1]['x-mdo-write-token']
        connection = http.client.HTTPConnection('127.0.0.1', local_port, timeout=8)
        try:
            connection.request(method, path, None if body is None else json.dumps(body).encode(), headers)
            response = connection.getresponse()
            status, response_headers = response.status, {k.lower(): v for k, v in response.getheaders()}
            payload = json.loads(response.read())
            assert status in (200, 201, 202), (method, path, status, payload)
            return payload['data'], response_headers
        finally:
            connection.close()

    try:
        adb('reverse', 'tcp:' + str(model_port), 'tcp:' + str(model_port))
        # An already-open Activity does not append a fresh listener to the log.
        # Start the owned QA process anew, preserving its application data.
        adb('shell', 'am', 'force-stop', args.package)
        reconnect(launch())
        config, headers = request('GET', '/api/v1/models/config')
        provider = {'id': 'android-native', 'name': 'Android native UI fixture', 'builtin': False,
                    'editable': True, 'removable': True, 'verify_peer': True, 'timeout_ms': 15000,
                    'endpoints': {'chat_completions': f'http://127.0.0.1:{model_port}/v1/chat/completions'}}
        model = dict(config['items'][0])
        model.update(id='android-native-text', name='QA Text', provider='android-native',
                     wire_model='android-native-text', builtin=False, free=False, editable=True, removable=True,
                     protocols=['openai-chat-completions'], default_protocol='openai-chat-completions',
                     reasoning_efforts=['none'], default_reasoning_effort='none', attachments=[],
                     capabilities=['text-input', 'text-output', 'tool-result-input', 'tool-call-output',
                                   'parallel-tool-calls', 'reasoning-control', 'streaming'])
        # The public schema calls multimodal input media-input. Both its
        # capability and the supported attachment kinds must be declared.
        vision = dict(model, id='android-native-image', name='QA Image', wire_model='android-native-image',
                      attachments=['image'], capabilities=model['capabilities'] + ['media-input'])
        patch = {'providers': [p for p in config['providers'] if p['id'] != provider['id']] + [provider],
                 'items': [m for m in config['items'] if m['id'] not in (model['id'], vision['id'])] + [model, vision],
                 'default_model': config['default_model']}
        request('POST', '/api/v1/models/setup', {'patch': patch,
            'keys': [{'provider': provider['id'], 'value': 'android-fixture-key'}]}, {'If-Match': headers['etag']})
        session, _ = request('POST', '/api/v1/sessions', {'project_id': 'default', 'title': '手机系统接口验收',
            'model_id': model['id'], 'reasoning_effort': 'none', 'permission_profile': 'read-only',
            'max_output_tokens': 2048})
        path = '/api/v1/projects/default/sessions/' + session['id']
        run, _ = request('POST', path + '/runs', {'prompt': 'UI base turn', 'timeout_ms': 45000})
        end = time.monotonic() + 45
        while time.monotonic() < end:
            result, _ = request('GET', '/api/v1/runs/' + run['id'])
            if result['terminal']:
                assert result['state'] == 'succeeded', result
                break
            time.sleep(.1)
        else:
            raise AssertionError('Initial synthetic turn did not finish')
        adb('shell', 'am', 'force-stop', args.package)
        reconnect(launch())
        proof.update(session_path=path, url=f'http://127.0.0.1:{local_port}/#/projects/default/sessions/' + session['id'])
        print(json.dumps({'session_title': '手机系统接口验收', 'url': proof['url'],
                          'stop_file': str(evidence / 'stop')}, ensure_ascii=False), flush=True)
        end = time.monotonic() + args.seconds
        while not (evidence / 'stop').exists() and time.monotonic() < end:
            time.sleep(.2)
        proof['requests'] = Model.requests
        proof['session'], _ = request('GET', path)
        proof['draft'], _ = request('GET', path + '/draft')
        proof['events'] = []
        after = 0
        for _ in range(16):
            page, _ = request('GET', path + f'/events?after={after}&limit=32')
            rows = page['items']
            proof['events'].extend(rows)
            if len(rows) < 32:
                break
            after = rows[-1]['event_id']
        else:
            raise AssertionError('Ordinary fixture exceeded its 512-event bound')
    finally:
        proof['requests'] = Model.requests
        (evidence / 'runtime.json').write_bytes((json.dumps(proof, ensure_ascii=False, indent=2)+'\n').encode())
        adb('shell', 'am', 'force-stop', args.package, check=False)
        if local_port is not None:
            adb('forward', '--remove', 'tcp:' + str(local_port), check=False)
        adb('reverse', '--remove', 'tcp:' + str(model_port), check=False)
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
