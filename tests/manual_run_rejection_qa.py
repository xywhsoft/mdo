"""Bounded packed UI check: real image rejection, configuration correction, retry.

The real API seeds one synthetic draft with an image. Send from the UI, inspect
the rejection, create allow-image to correct the same model, then retry from
the queue and send a follow-up. Create stop to save evidence. No proxy faults,
production credentials, browser mocks, or writes to the ordinary portable Home.
"""
import argparse
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
import time

from test_api_runtime import request, session_events
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


class Model(BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        content = next(row['content'] for row in reversed(body['messages']) if row['role'] == 'user')
        if isinstance(content, list):
            prompt = '\n'.join(part['text'] for part in content if part['type'] == 'text')
            images = [hashlib.sha256(base64.b64decode(part['image_url']['url'].split(',', 1)[1])).hexdigest()
                      for part in content if part['type'] == 'image_url']
        else:
            prompt, images = content, []
        self.requests.append({'prompt': prompt, 'image_sha256': images})
        raw = json.dumps({'id': 'rejected-admission-fixture', 'model': body['model'],
                         'choices': [{'index': 0, 'message': {'role': 'assistant',
                             'content': 'REJECTION_RECOVERED: ' + prompt}, 'finish_reason': 'stop'}],
                         'usage': {'prompt_tokens': 40, 'completion_tokens': 20}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--seconds', type=int, choices=range(1, 601), metavar='1..600', default=600)
    args = parser.parse_args()
    base = args.evidence.resolve()
    assert not base.exists(), 'Use a fresh evidence directory'
    base.mkdir(parents=True)
    packed = args.packed.resolve()
    native, port = site(base, 'native', packed)
    model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    threading.Thread(target=model.serve_forever, daemon=True).start()
    process = start(native, packed, base / 'home', dict(os.environ,
        USE_WEBVIEW='0', MDO_QUEUE_FIXTURE_KEY='rejected-admission-fixture-only'))

    def call(method, path, body=None, expected=200, headers=None):
        metadata = dict(headers or {})
        if body is not None:
            metadata['Content-Type'] = 'application/json'
        status, reply_headers, raw = request(port, method, path,
            body=None if body is None else json.dumps(body).encode(), headers=metadata)
        assert status == expected, (status, raw)
        return json.loads(raw)['data'], reply_headers

    path = ''
    proof = {'packed_sha256': hashlib.sha256(packed.read_bytes()).hexdigest(),
             'native_api': True, 'browser_mock': False, 'stress_or_high_load': False}

    def snapshot():
        return {'queue': call('GET', path + '/queue')[0], 'draft': call('GET', path + '/draft')[0],
                'runs': call('GET', '/api/v1/runs')[0], 'events': session_events(port, path),
                'model_requests': list(Model.requests)}

    def configure(allow_image):
        config, headers = call('GET', '/api/v1/models/config')
        item = next(row for row in config['items'] if row['id'] == 'queue-fixture')
        item.update(name='QA Image', attachments=['image'], reasoning_efforts=['none'],
                    default_reasoning_effort='none')
        item['capabilities'] = ['text-input', 'text-output', 'tool-result-input', 'tool-call-output',
                                'parallel-tool-calls', 'reasoning-control', 'streaming']
        if allow_image:
            item['capabilities'].append('media-input')
        config.pop('runtime_override', None)
        call('PUT', '/api/v1/settings/models', {'schema_version': 1, 'patch': config},
             headers={'If-Match': headers['etag']})

    try:
        _, bootstrap = wait_bootstrap(process, port, native / 'packed.log')
        assert bootstrap['data']['ready'], bootstrap
        configure_model(port, f'http://127.0.0.1:{model.server_port}/v1/chat/completions')
        configure(False)
        session, _ = call('POST', '/api/v1/sessions', {'project_id': 'default',
            'title': '明确拒绝后重试验收', 'model_id': 'queue-fixture', 'reasoning_effort': 'none',
            'permission_profile': 'read-only'}, expected=201)
        path = '/api/v1/projects/default/sessions/' + session['id']
        image = (Path(__file__).parent / 'acceptance/xge-pixel-editor/smoke_window.png').read_bytes()
        status, _, raw = request(port, 'POST', path + '/attachments', body=image,
            headers={'Content-Type': 'image/png', 'X-Mdo-File-Name': 'pixel.png'})
        assert status == 201, (status, raw)
        attachment = json.loads(raw)['data']['id']
        call('PUT', path + '/draft', {'revision': 0, 'text': 'image-rejection-test',
                                     'attachments': [attachment]})
        proof.update(session_path=path, image_sha256=hashlib.sha256(image).hexdigest())
        print(json.dumps({'url': f'http://127.0.0.1:{port}/#/projects/default/sessions/' + session['id'],
                          'allow_image_file': str(base / 'allow-image'), 'stop_file': str(base / 'stop')}), flush=True)
        deadline = time.monotonic() + args.seconds
        corrected = False
        while not (base / 'stop').exists() and time.monotonic() < deadline:
            assert process.poll() is None, 'Owned packed fixture exited'
            if not corrected and (base / 'allow-image').exists():
                proof['before_correction'] = snapshot()
                configure(True)
                corrected = True
                (base / 'corrected').write_text('Model now supports image input\n')
            time.sleep(.2)
        proof.update(corrected=corrected, final=snapshot())
    finally:
        proof['model_requests'] = Model.requests
        (base / 'proof.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        stop(process)
        model.shutdown()
        model.server_close()


if __name__ == '__main__':
    main()
