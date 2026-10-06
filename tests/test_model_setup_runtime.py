"""Bounded settings integration through xs/TCC and a local model API."""
from __future__ import annotations
import argparse
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host


class Supplier(BaseHTTPRequestHandler):
    calls = []
    def log_message(self, *_args): pass
    def reply(self, data, status=200):
        payload = json.dumps(data).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def do_GET(self):
        Supplier.calls.append((self.path, self.headers.get('Authorization')))
        if self.path.startswith('/denied/'):
            return self.reply({'error': 'fixture-secret-must-not-be-reflected'}, 401)
        if self.path.startswith('/missing/'):
            return self.reply({}, 404)
        if self.path.startswith('/redirect/'):
            self.send_response(302)
            self.send_header('Location', '/v1/models')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        self.reply({'data': [{'id': 'fixture-agent'}, {'id': 'fixture-other'}]})
    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        Supplier.calls.append((data['model'], self.headers.get('Authorization')))
        if self.headers.get('Authorization') != 'Bearer fixture-model-key':
            return self.reply({}, 401)
        if data.get('tools'):
            return self.reply({'id': 'fixture', 'object': 'chat.completion', 'model': data['model'],
                'choices': [{'index': 0, 'message': {'role': 'assistant', 'content': None,
                    'tool_calls': [{'id': 'probe', 'type': 'function', 'function': {
                        'name': 'mdo_connection_probe', 'arguments': '{}'}}]}, 'finish_reason': 'tool_calls'}]})
        self.reply({'id': 'fixture', 'object': 'chat.completion', 'model': data['model'],
                    'choices': [{'index': 0, 'message': {'role': 'assistant', 'content': 'OK'},
                                 'finish_reason': 'stop'}],
                    'usage': {'prompt_tokens': 6, 'completion_tokens': 1, 'total_tokens': 7}})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', 0), Supplier)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='mdo-model-setup-') as temporary:
        base = Path(temporary)
        shutil.copytree(ROOT / 'app', base, dirs_exist_ok=True)
        port = free_port()
        (base / 'xs.json').write_text(json.dumps({'engine': {'workers': 1}, 'services': [{
            'enabled': True, 'class': 'http', 'name': 'model-setup', 'ip': '127.0.0.1',
            'port': port, 'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
                                          'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}))
        env = dict(os.environ, MDO_HOME=str(base / 'mdo-home'))
        process = None
        def get():
            status, headers, body = request(port, 'GET', '/api/v1/models/config')
            assert status == 200, body
            return json.loads(body)['data'], headers['etag']
        def post(path, data, etag=None):
            headers = {'Content-Type': 'application/json'}
            if etag: headers['If-Match'] = etag
            status, _, raw = request(port, 'POST', '/api/v1/models/' + path,
                                     body=json.dumps(data).encode(), headers=headers)
            return status, json.loads(raw)
        with (base / 'host.log').open('wb') as log:
            try:
                process = subprocess.Popen([str(args.host.resolve()), str(base / 'xs.json')],
                    cwd=base, env=env, stdin=subprocess.PIPE, stdout=log, stderr=log)
                wait_ready(port, process)
                config, etag = get()
                supplier = {'id': 'fixture', 'name': 'Fixture', 'builtin': False, 'editable': True,
                    'removable': True, 'verify_peer': True, 'timeout_ms': 5000,
                    'endpoints': {'chat_completions': f'http://127.0.0.1:{server.server_port}/v1/chat/completions'}}
                status, result = post('discover', {'provider': supplier, 'key': 'fixture-model-key'})
                assert status == 200 and result['data']['items'] == ['fixture-agent', 'fixture-other'], result
                assert not (base / 'mdo-home/config/secrets/models').exists()
                for prefix, code in [('denied', 'authentication_failed'), ('missing', 'catalog_unsupported'), ('redirect', 'connection_failed')]:
                    draft = copy.deepcopy(supplier)
                    draft['endpoints']['chat_completions'] = supplier['endpoints']['chat_completions'].replace('/v1/', f'/{prefix}/')
                    before = len(Supplier.calls)
                    status, result = post('discover', {'provider': draft, 'key': 'fixture-model-key'})
                    assert status >= 400 and result['error']['code'] == code, result
                    assert len(Supplier.calls) == before + 1 and 'fixture-secret' not in json.dumps(result)
                model = copy.deepcopy(config['items'][0])
                model.update(id='fixture-agent', name='Fixture agent', provider='fixture', wire_model='fixture-agent',
                    builtin=False, free=False, editable=True, removable=True,
                    protocols=['openai-chat-completions'], default_protocol='openai-chat-completions',
                    reasoning_efforts=['none'], default_reasoning_effort='none', attachments=[],
                    capabilities=['text-input', 'text-output', 'tool-result-input', 'tool-call-output', 'streaming'])
                patch = {key: config[key] for key in ('providers', 'items', 'default_model')}
                patch['providers'].append(supplier); patch['items'].append(model)
                body = {'patch': patch, 'keys': [{'provider': 'fixture', 'value': 'fixture-model-key'}]}
                assert post('setup', body)[0] == 428
                assert post('setup', body, '"mdo-config-999999"')[0] == 412
                invalid = copy.deepcopy(body); invalid['patch']['items'][-1]['provider'] = 'nonexistent'
                assert post('setup', invalid, etag)[0] == 422
                assert not (base / 'mdo-home/config/secrets/models').exists()
                status, result = post('setup', body, etag)
                assert status == 200, result
                saved, current_etag = get()
                assert saved['default_model'] == config['default_model']
                reference = saved['providers'][-1]['credential']['secret_ref']
                assert reference.startswith('vault:') and len(reference) == 70
                files = list((base / 'mdo-home/config/secrets/models').glob('*.key'))
                assert len(files) == 1 and b'fixture-model-key' not in files[0].read_bytes()
                assert 'fixture-model-key' not in json.dumps(saved)
                assert post('setup', body, etag)[0] == 412
                assert len(list(files[0].parent.glob('*.key'))) == 1
                status, result = post('test', {'model_id': 'fixture-agent'})
                assert status == 200 and result['data']['verified'], result
                assert Supplier.calls[-1] == ('fixture-agent', 'Bearer fixture-model-key')
                status, result = post('test', {'model_id': 'fixture-agent', 'tools': True})
                assert status == 200 and result['data']['verified'], result
                before = len(Supplier.calls)
                assert post('test', {'model_id': 'fixture-agent', 'tools': 'true'})[0] == 422
                assert len(Supplier.calls) == before
                stop_host(process); process = None
                process = subprocess.Popen([str(args.host.resolve()), str(base / 'xs.json')],
                    cwd=base, env=env, stdin=subprocess.PIPE, stdout=log, stderr=log)
                wait_ready(port, process)
                status, result = post('test', {'model_id': 'fixture-agent'})
                assert status == 200, result
                saved, current_etag = get()
                disabled = {key: saved[key] for key in ('providers', 'items', 'default_model')}
                disabled['items'][-1]['enabled'] = False
                assert post('setup', {'patch': disabled, 'keys': []}, current_etag)[0] == 200
                assert post('test', {'model_id': 'fixture-agent'})[0] == 404
                saved, current_etag = get()
                invalid_default = copy.deepcopy(disabled); invalid_default['default_model'] = 'fixture-agent'
                assert post('setup', {'patch': invalid_default, 'keys': []}, current_etag)[0] == 422
                disabled['items'][-1]['enabled'] = True
                assert post('setup', {'patch': disabled, 'keys': []}, current_etag)[0] == 200
                assert post('test', {'model_id': 'fixture-agent'})[0] == 200
                print('PASS model discovery, credential transaction, conflict, encrypted restart and actual request')
            except BaseException:
                log.flush()
                print((base / 'host.log').read_text(errors='replace')[-8000:])
                raise
            finally:
                stop_host(process)
    server.shutdown(); server.server_close()


if __name__ == '__main__': main()
