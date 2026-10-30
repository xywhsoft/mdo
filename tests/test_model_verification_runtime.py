"""Model onboarding evidence through the real API; in-place, local HTTP only."""
import argparse
import copy
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

from test_api_runtime import ROOT, free_port, request, wait_ready


class Supplier(BaseHTTPRequestHandler):
    mode = 'ok'
    calls = []
    entered = threading.Event()
    release = threading.Event()
    contract_errors = []

    def log_message(self, *_): pass

    def do_GET(self):
        rows = [{'id': 'gpt-5.4', 'context_length': 32000},
            {'id': 'metadata-false', 'max_input_tokens': 12345, 'max_tokens': 4096,
             'capabilities': {'thinking': {'supported': False}, 'tool_calling': False}},
            {'id': 'metadata-effort', 'context_length': 64000, 'max_tokens': 16000,
             'capabilities': {'effort': {'supported': True, 'low': True, 'high': True, 'max': False}}}]
        raw = json.dumps({'data': rows}).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw))); self.end_headers(); self.wfile.write(raw)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.calls.append(body)
        tools = bool(body.get('tools'))
        def strict_object(schema):
            if not isinstance(schema, dict): return True
            if schema.get('type') == 'object' and (schema.get('additionalProperties') is not False or
                set(schema.get('required', [])) != set(schema.get('properties', {}))): return False
            return all(strict_object(value) for value in schema.values() if isinstance(value, dict)) and all(
                strict_object(value) for row in schema.values() if isinstance(row, list) for value in row)
        definitions = [tool.get('function', tool) for tool in body.get('tools', [])]
        names = {tool.get('name') for tool in definitions}
        valid = body.get('stream') is True and (not tools or {'skill', 'read', 'exec', 'mdo_connection_probe'} <= names)
        if not self.path.endswith('/messages'):
            valid &= all((isinstance(tool.get('strict'), bool) if self.path.endswith('/responses') else
                'strict' not in tool or isinstance(tool['strict'], bool)) and
                (not tool.get('strict') or strict_object(tool.get('parameters', {}))) for tool in definitions)
        if not valid:
            self.contract_errors.append('stream or native tool schemas not exercised')
            raw = json.dumps({'error': {'code': 'invalid_function_parameters', 'type': 'invalid_request_error',
                'message': 'Invalid strict tool schema or missing runtime verification contract'}}).encode()
            self.send_response(400); self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw))); self.end_headers(); self.wfile.write(raw); return
        if self.mode == 'wait':
            self.entered.set()
            assert self.release.wait(3), 'fixture wait exceeded'
        args = '[]' if self.mode == 'invalid_tool' else '{}'
        if self.path.endswith('/responses'):
            output = [{'type': 'function_call', 'call_id': 'probe', 'name': 'mdo_connection_probe', 'arguments': args}] if tools else [
                {'type': 'message', 'role': 'assistant', 'content': [{'type': 'output_text', 'text': 'OK'}]}]
            data = {'id': 'resp_fixture', 'model': body['model'], 'status': 'completed', 'output': output}
        elif self.path.endswith('/messages'):
            content = [{'type': 'tool_use', 'id': 'probe', 'name': 'mdo_connection_probe', 'input': [] if self.mode == 'invalid_tool' else {}}] if tools else [{'type': 'text', 'text': 'OK'}]
            data = {'id': 'msg_fixture', 'type': 'message', 'role': 'assistant', 'model': body['model'],
                'content': content, 'stop_reason': 'tool_use' if tools else 'end_turn', 'usage': {'input_tokens': 2, 'output_tokens': 2}}
        else:
            message = {'role': 'assistant', 'content': 'OK'}
            if tools:
                message = {'role': 'assistant', 'content': None, 'tool_calls': [{'id': 'probe', 'type': 'function',
                    'function': {'name': 'mdo_connection_probe', 'arguments': args}}]}
            elif self.mode == 'reasoning_only':
                message = {'role': 'assistant', 'content': None, 'reasoning_content': 'internal thought only'}
            data = {'id': 'fixture', 'object': 'chat.completion', 'model': body['model'],
                'choices': [{'index': 0, 'message': message, 'finish_reason': 'tool_calls' if tools else 'stop'}]}
        events = []
        def event(kind, value):
            events.append(f'event: {kind}\ndata: {json.dumps(value)}\n\n')
        if self.path.endswith('/responses'):
            if tools:
                item = {**data['output'][0], 'id': 'fc_probe'}
                event('response.output_item.added', {'type': 'response.output_item.added', 'output_index': 0,
                    'item': {**item, 'arguments': ''}})
                event('response.function_call_arguments.delta', {'type': 'response.function_call_arguments.delta',
                    'output_index': 0, 'item_id': 'fc_probe', 'delta': args})
            else:
                event('response.output_text.delta', {'type': 'response.output_text.delta', 'output_index': 0,
                    'content_index': 0, 'delta': 'OK'})
            event('response.completed', {'type': 'response.completed', 'response': data})
        elif self.path.endswith('/messages'):
            event('message_start', {'type': 'message_start', 'message': {**data, 'content': [],
                'usage': {'input_tokens': 2, 'output_tokens': 0}}})
            block = {'type': 'tool_use', 'id': 'probe', 'name': 'mdo_connection_probe', 'input': {}} if tools else {'type': 'text', 'text': ''}
            event('content_block_start', {'type': 'content_block_start', 'index': 0, 'content_block': block})
            event('content_block_delta', {'type': 'content_block_delta', 'index': 0, 'delta':
                {'type': 'input_json_delta', 'partial_json': args} if tools else {'type': 'text_delta', 'text': 'OK'}})
            event('content_block_stop', {'type': 'content_block_stop', 'index': 0})
            event('message_delta', {'type': 'message_delta', 'delta': {'stop_reason': data['stop_reason']}, 'usage': {'output_tokens': 2}})
            event('message_stop', {'type': 'message_stop'})
        else:
            delta = data['choices'][0]['message']
            for call in delta.get('tool_calls', []): call['index'] = 0
            chunk = {**data, 'object': 'chat.completion.chunk', 'choices': [{'index': 0, 'delta': delta,
                'finish_reason': data['choices'][0]['finish_reason']}]}
            events.append(f'data: {json.dumps(chunk)}\n\ndata: [DONE]\n\n')
        raw = ''.join(events).encode()
        self.send_response(200); self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Content-Length', str(len(raw))); self.end_headers(); self.wfile.write(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--packed', type=Path, help='test an existing executable in place, without copying the app')
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT))
    from tools import build_mdo
    if not args.packed:
        build_mdo.write_if_changed(build_mdo.UNITY_PATH, build_mdo.generated_unity(
            build_mdo.load_object(build_mdo.LOCK_PATH), build_mdo.source_list()).encode())
    host_config = (args.packed.resolve().parent if args.packed else ROOT/'app')/'xs.model-verification-test.json'
    assert not host_config.exists(), 'An owned probe is already running'
    supplier = ThreadingHTTPServer(('127.0.0.1', 0), Supplier)
    threading.Thread(target=supplier.serve_forever, daemon=True).start()
    evidence = ROOT/'.build/acceptance/model-verification-20261030'
    evidence.mkdir(parents=True, exist_ok=True)
    checks = []
    with tempfile.TemporaryDirectory(prefix='model-verification-', dir=ROOT/'.build') as raw:
        base = Path(raw); port = free_port(); process = None
        if not args.packed:
            host_config.write_text(json.dumps({'engine': {'workers': 2}, 'services': [{'enabled': True, 'class': 'http',
                'name': 'model-verification-test', 'ip': '127.0.0.1', 'port': port,
                'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web', 'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}))
        def call(method, path, body=None, etag=None):
            status, headers, data = request(port, method, '/api/v1/'+path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={'Content-Type': 'application/json', **({'If-Match': etag} if etag else {})})
            return status, headers, json.loads(data)
        def get():
            status, headers, data = call('GET', 'models/config'); assert status == 200, data
            return data['data'], headers['etag']
        def save(config, etag):
            patch = {key: config[key] for key in ('providers', 'items', 'default_model')}
            status, _, data = call('PUT', 'settings/models', {'schema_version': 1, 'patch': patch}, etag)
            assert status == 200, data
        def probe(model, tools=False, expected=200):
            status, _, data = call('POST', 'models/test', {'model_id': model, 'tools': tools})
            assert status == expected, data
            return data
        def stop():
            nonlocal process
            if process and process.poll() is None:
                if os.name == 'nt': subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                else: process.terminate()
                process.wait(timeout=8)
                if args.packed: time.sleep(.5)  # WebView child handles finish closing.
            process = None
        label = 'packed-api' if args.packed else 'native-api'
        log_path = evidence/(label+'.log')
        with log_path.open('wb') as log:
            def start():
                nonlocal process, port
                executable = args.packed or args.host
                offset = log_path.stat().st_size
                command = [str(executable.resolve()), '--', '--home', str(base/'home')] if args.packed else [str(executable.resolve()), str(host_config)]
                process = subprocess.Popen(command, cwd=host_config.parent,
                    env={**os.environ, 'MDO_HOME': str(base/'home'), 'USE_WEBVIEW': '0'}, stdout=log, stderr=log,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                if args.packed:
                    until = time.monotonic()+20
                    while time.monotonic() < until and process.poll() is None:
                        text = log_path.read_bytes()[offset:].decode('utf-8', errors='replace')
                        match = re.search(r"server 'mdo' http bound on 127\.0\.0\.1:(\d+)", text)
                        if match: port = int(match[1]); break
                        time.sleep(.05)
                    else: raise RuntimeError('Packed app did not bind its embedded service')
                wait_ready(port, process)
            try:
                start(); config, etag = get()
                metadata_provider = {'endpoints': {'responses': f'http://127.0.0.1:{supplier.server_port}/v1/responses'}, 'proxy': {'kind': 'none'}}
                status, _, result = call('POST', 'models/parameters', {'provider': metadata_provider,
                    'protocol': 'openai-responses', 'models': ['gpt-5.4', 'metadata-false', 'metadata-effort', 'unknown-gpt-5.4']})
                assert status == 200, result
                rows = {row['id']: row for row in result['data']['items']}
                fields = rows['gpt-5.4']['fields']
                assert fields['context_tokens']['value'] == 32000 and fields['context_tokens']['source'] == 'api', rows
                assert fields['max_output_tokens']['source'] == 'upstream-recommendation', rows
                assert fields['reasoning']['value']['efforts'] == ['none', 'low', 'medium', 'high', 'xhigh'], rows
                assert rows['metadata-false']['fields']['tools']['value'] is False
                assert rows['metadata-false']['fields']['reasoning']['value']['kind'] == 'none'
                assert rows['metadata-effort']['fields']['reasoning']['value']['efforts'] == ['low', 'high']
                assert not rows['unknown-gpt-5.4']['fields'], rows
                metadata_provider['endpoints']['responses'] = 'https://api.openai.com/v1/responses'
                status, _, result = call('POST', 'models/parameters', {'provider': metadata_provider,
                    'protocol': 'openai-responses', 'models': ['gpt-6.1-sol']})
                assert status == 200 and result['data']['items'][0]['fields']['context_tokens']['value'] == 1050000, result
                assert 'none' not in result['data']['items'][0]['fields']['reasoning']['value']['efforts']
                status, _, _ = call('POST', 'models/parameters', {'provider': metadata_provider,
                    'protocol': 'openai-responses', 'models': ['x']*33})
                assert status == 422
                checks.append('bundled exact-ID registry; API field precedence; explicit false; unknown and batch limits')
                for name, protocol, endpoint in [('chat', 'openai-chat-completions', 'chat_completions'),
                    ('responses', 'openai-responses', 'responses'), ('messages', 'anthropic-messages', 'anthropic_messages')]:
                    provider = {'id': name, 'name': name, 'builtin': False, 'editable': True, 'removable': True,
                        'verify_peer': True, 'timeout_ms': 2000, 'proxy': {'kind': 'none'},
                        'credential': {'secret_ref': 'env:MDO_ONBOARDING_TEST_KEY'},
                        'endpoints': {endpoint: f'http://127.0.0.1:{supplier.server_port}/v1/'+
                            {'chat_completions': 'chat/completions', 'responses': 'responses', 'anthropic_messages': 'messages'}[endpoint]}}
                    model = copy.deepcopy(config['items'][0])
                    model.update(id=name, name=name, provider=name, wire_model=name, builtin=False, free=False,
                        editable=True, removable=True, protocols=[protocol], default_protocol=protocol,
                        reasoning_efforts=['none'], default_reasoning_effort='none', attachments=[],
                        proxy={'kind': 'provider'}, capabilities=['text-input', 'text-output', 'tool-result-input', 'tool-call-output', 'streaming'])
                    config['providers'].append(provider); config['items'].append(model)
                save(config, etag)
                for name in ('chat', 'responses', 'messages'):
                    result = probe(name)['data']; assert not result['verification']['verified'], result
                    result = probe(name, True)['data']; assert result['verification']['verified'], result
                    assert result['verification']['contract'] == 'runtime-tools-stream-v2'
                assert not Supplier.contract_errors
                assert not list((base/'home/sessions').rglob('snapshot.json'))
                checks.append('reply + valid dummy tool across Chat, Responses and Anthropic')
                checks.append('streaming and real native read/exec/skill schemas verified without executing tools or persisting conversations')
                config, etag = get(); assert all(config['verification'][name]['verified'] for name in ('chat', 'responses', 'messages'))
                for path in (base/'home/data/model-verification').glob('*.json'):
                    record = json.loads(path.read_text()); record['schema_version'] = 1
                    path.write_text(json.dumps(record))
                assert not get()[0]['verification']
                for name in ('chat', 'responses', 'messages'): probe(name); probe(name, True)
                checks.append('old probe-only verification records must be tested again')
                stop(); start(); config, etag = get(); assert config['verification']['chat']['verified']
                checks.append('verification survives native restart')
                Supplier.mode = 'reasoning_only'
                result = probe('chat', expected=422); assert result['error']['code'] == 'model_test_failed', result
                assert not get()[0]['verification']['chat']['verified']
                Supplier.mode = 'ok'; probe('chat')
                Supplier.mode = 'invalid_tool'
                result = probe('chat', True, 422); assert result['error']['code'] == 'model_tools_test_failed', result
                assert not get()[0]['verification']['chat']['verified']
                Supplier.mode = 'ok'; probe('chat', True)
                checks.append('reasoning-only replies and invalid tool arguments cannot verify a model')
                config, etag = get(); config['default_model'] = 'chat'; save(config, etag)
                assert get()[0]['verification']['chat']['verified']
                config, etag = get(); config['items'][-3]['window']['max_output_tokens'] -= 1; save(config, etag)
                assert 'chat' not in get()[0]['verification']
                checks.append('default selection keeps evidence; changed model limits invalidate it')
                config, etag = get(); text_only = copy.deepcopy(config['items'][-3]); text_only['id'] = 'text-only'
                text_only['capabilities'].remove('tool-call-output'); config['items'].append(text_only); save(config, etag)
                assert probe('text-only')['data']['verification']['verified']
                before = len(Supplier.calls); probe('text-only', True, 422); assert before == len(Supplier.calls)
                checks.append('reply-only profile verifies without tool probes; undeclared tools rejected locally')
                Supplier.mode = 'wait'; Supplier.entered.clear(); Supplier.release.clear()
                with ThreadPoolExecutor(max_workers=1) as pool:
                    pending = pool.submit(probe, 'chat', False, 409)
                    assert Supplier.entered.wait(2)
                    config, etag = get(); config['items'][-4]['proxy'] = {'kind': 'none'}; save(config, etag)
                    Supplier.release.set(); result = pending.result(timeout=4)
                    assert result['error']['code'] == 'model_config_changed', result
                Supplier.mode = 'ok'
                assert 'chat' not in get()[0]['verification']
                checks.append('configuration race cannot stamp a changed profile as verified')
                Supplier.mode = 'wait'; Supplier.entered.clear(); Supplier.release.clear()
                with ThreadPoolExecutor(max_workers=1) as pool:
                    pending = pool.submit(probe, 'chat', False, 409)
                    assert Supplier.entered.wait(2)
                    config, etag = get(); original_proxy = config['items'][-4]['proxy']
                    config['items'][-4]['proxy'] = {'kind': 'provider'}; save(config, etag)
                    config, etag = get(); config['items'][-4]['proxy'] = original_proxy; save(config, etag)
                    Supplier.release.set(); result = pending.result(timeout=4)
                    assert result['error']['code'] == 'model_config_changed', result
                Supplier.mode = 'ok'
                assert not get()[0]['verification'].get('chat', {}).get('verified')
                checks.append('changing and restoring the same profile during a probe remains inconclusive')
                config, etag = get(); original = copy.deepcopy(config['providers'][-2])
                for field, value in [('proxy', {'kind': 'global'}),
                    ('credential', {'secret_ref': 'env:MDO_ONBOARDING_OTHER_KEY'}),
                    ('endpoints', {'responses': f'http://127.0.0.1:{supplier.server_port}/changed/responses'})]:
                    config['providers'][-2][field] = value; save(config, etag)
                    config, etag = get(); assert 'responses' not in config['verification'], field
                    config['providers'][-2] = copy.deepcopy(original); save(config, etag)
                    config, etag = get(); assert config['verification']['responses']['verified'], field
                status, headers, _ = call('GET', 'settings'); assert status == 200
                status, _, data = call('PATCH', 'settings/settings', {'schema_version': 1,
                    'patch': {'transport': {'proxy': {'kind': 'system'}}}}, headers['etag'])
                assert status == 200, data
                assert not get()[0]['verification']
                checks.append('supplier endpoint, credential reference, proxy and global transport invalidate evidence')
                records = list((base/'home/data/model-verification').glob('*.json'))
                assert records and all('fixture-only-key' not in p.read_text() and 'http://' not in p.read_text() for p in records)
                checks.append('evidence stores only fingerprints, states, latency and safe error kinds')
                (evidence/(label+'.json')).write_text(json.dumps({'passed': True, 'checks': checks,
                    'embedded_application': bool(args.packed)}, indent=2)+'\n')
                print('PASS: '+ '; '.join(checks))
            finally:
                Supplier.release.set(); stop(); host_config.unlink(missing_ok=True)
                supplier.shutdown(); supplier.server_close()


if __name__ == '__main__':
    os.environ['MDO_ONBOARDING_TEST_KEY'] = 'fixture-only-key'
    main()
