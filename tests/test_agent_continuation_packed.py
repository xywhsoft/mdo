"""Tool failure streaks and unusable generation recovery in real packed mdo."""
import argparse
import json
import os
import hashlib
import shutil
from pathlib import Path
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies
from test_responses_followup_runtime import api, setup_provider, terminal


class Model(BaseHTTPRequestHandler):
    counts = {}
    requests = {}

    def log_message(self, *_): pass

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        user = next(x['content'] for x in reversed(payload['input']) if x.get('role') == 'user')
        label = user if isinstance(user, str) else user[0]['text']
        step = self.counts.get(label, 0)
        self.counts[label] = step + 1
        self.requests.setdefault(label, []).append(payload)

        def call(tool='read', args=None, suffix=''):
            return {'type': 'function_call', 'name': tool, 'call_id': f'{label}-{step}{suffix}',
                    'arguments': json.dumps(args or {'path': 'missing-fixture.txt'})}

        output = [{'type': 'message', 'role': 'assistant', 'content': [
            {'type': 'output_text', 'text': 'Continuation completed'}]}]
        if label == 'nine-identical-failures' and step < 9:
            output = [call()]
        elif label == 'ten-identical-failures':
            output = [call()]
        elif label == 'varying-failures' and step < 9:
            output = [call(args={'path': f'missing-{step}.txt'})]
        elif label == 'failure-reset' and step < 29:
            output = [call(args={'path': 'utf8.txt'})] if step in (9, 19) else [call()]
        elif label == 'successful-repeated-read' and step < 12:
            output = [call(args={'path': 'utf8.txt'})]
        elif label == 'mixed-tool-batches' and step < 12:
            output = [call(suffix='-failure'), call(args={'path': 'utf8.txt'}, suffix='-success')]
        elif label == 'boundary-batch':
            output = [call()] if step < 9 else [call(suffix='-failure'), call('write', {
                'path': 'boundary-marker.txt', 'content': 'EXECUTED_ONCE', 'mode': 'append'}, '-write')]
        elif label == 'utf8-midcursor' and step == 0:
            output = [call(args={'path': 'utf8.txt', 'offset': 1})]
        elif label in ('empty-then-recovery', 'tool-error-then-empty', 'write-then-empty'):
            empty_step = 0 if label == 'empty-then-recovery' else 1
            if label == 'tool-error-then-empty' and step == 0:
                output = [call()]
            elif label == 'write-then-empty' and step == 0:
                output = [call('write', {'path': 'write-once.txt', 'content': 'ONCE', 'mode': 'append'})]
            elif step == empty_step:
                output = [{'type': 'reasoning', 'id': 'rs-empty', 'summary': [
                    {'type': 'summary_text', 'text': 'Uncommitted thinking draft'}]}]
        body = {'id': f'response-{label}-{step}', 'object': 'response', 'status': 'completed',
                'model': 'fixture-followup', 'output': output,
                'usage': {'input_tokens': 8, 'output_tokens': 4, 'total_tokens': 12}}
        if label == 'write-then-empty' and step == 1:
            # Replaced streamed thinking must not survive as another reply or
            # cause the already committed write to execute again.
            events = [ {'type': 'response.created', 'response': {'id': body['id'], 'status': 'in_progress'}},
                       {'type': 'response.reasoning_summary_text.delta', 'delta': 'Uncommitted thinking draft'},
                       {'type': 'response.completed', 'response': body} ]
            raw = ''.join('data: ' + json.dumps(e) + '\n\n' for e in events).encode()
            mime = 'text/event-stream'
        else:
            raw, mime = json.dumps(body).encode(), 'application/json'
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('X-Request-Id', body['id'])
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def run(packed, session_copy=None):
    Model.counts, Model.requests = {}, {}
    model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    thread = threading.Thread(target=model.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='continuation-packed-', dir=ROOT / '.build') as raw:
            base = Path(raw)
            directory, port = site(base, 'site', packed)
            home = base / 'home'
            workspace = home / 'workspace'
            workspace.mkdir(parents=True)
            (workspace / 'utf8.txt').write_text('慕晴🙂文本\n', encoding='utf-8')
            for name in ('write-once.txt', 'boundary-marker.txt'):
                (workspace / name).write_text('', encoding='utf-8')
            process = start(directory, packed, home, dict(os.environ,
                USE_WEBVIEW='0', USERPROFILE=str(base), HOME=str(base)))
            try:
                _, boot = wait_bootstrap(process, port, directory / 'packed.log')
                assert boot['data']['ready'], boot
                setup_provider(port, model.server_port)
                cases = {'nine-identical-failures': (9, 'succeeded'),
                         'ten-identical-failures': (10, 'failed'),
                         'varying-failures': (9, 'succeeded'),
                         'failure-reset': (27, 'succeeded'),
                         'successful-repeated-read': (0, 'succeeded'),
                         'mixed-tool-batches': (12, 'succeeded'),
                         'boundary-batch': (10, 'failed'),
                         'utf8-midcursor': (0, 'succeeded'),
                         'empty-then-recovery': (0, 'succeeded'),
                         'tool-error-then-empty': (1, 'succeeded'),
                         'write-then-empty': (0, 'succeeded')}
                for label, (failure_count, state) in cases.items():
                    session, _ = api(port, 'POST', '/api/v1/sessions', {
                        'project_id': 'default', 'agent_id': 'mdo.default',
                        'model_id': 'fixture-followup', 'protocol': 'openai-responses',
                        'permission_profile': 'full-access', 'max_output_tokens': 1024})
                    path = '/api/v1/projects/default/sessions/' + session['id']
                    operation, _ = api(port, 'POST', path + '/runs', {'prompt': label})
                    value = terminal(port, operation)
                    storage = home / 'sessions/default' / session['id']
                    events = [json.loads(line) for line in (storage / 'ui-events.jsonl').read_text(encoding='utf-8').splitlines()]
                    errors = [e.get('text') for e in events if e.get('kind') == 11]
                    assert value['state'] == state, (label, value, errors)
                    tool_done = [e for e in events if e.get('kind') == 6]
                    assert sum(not e.get('success') for e in tool_done) == failure_count, (label, tool_done)
                    snap = json.loads((storage / 'snapshot.json').read_text(encoding='utf-8'))
                    assistants = [e for e in snap['entries'] if e.get('role') == 2]
                    assert all(e.get('content','').strip() or e.get('tool_calls') for e in assistants), label
                    calls = {c['id'] for e in assistants for c in e.get('tool_calls',[])}
                    results = {e['tool_call_id'] for e in snap['entries'] if e.get('role') == 3}
                    assert calls == results, (label, calls-results)
                    if state == 'succeeded': assert not errors, (label, errors)
                    else: assert errors == ['consecutive tool failure limit reached'], (label, errors)
                    for e in events:
                        if e.get('kind') == 4:
                            assert e['model_attempts'] >= 1 and e['model_http_status'] == 200, (label, e)
                            assert e['model_request_id'] and 'model_finish_reason' in e and 'model_tool_call_count' in e
                    if label in ('empty-then-recovery', 'tool-error-then-empty', 'write-then-empty'):
                        done = [e for e in events if e.get('kind') == 4]
                        assert done[-1]['model_attempts'] == 2, (label, done)
                        assert Model.counts[label] == (2 if label == 'empty-then-recovery' else 3)
                        assert 'visible answer' in str(Model.requests[label][-1].get('instructions','')), label
                    if label == 'write-then-empty':
                        assert (workspace / 'write-once.txt').read_text() == 'ONCE'
                        assert sum(e.get('tool_name') == 'write' for e in tool_done) == 1
                        assert any(e.get('kind') == 1 and e.get('text') == 'stream_restart' for e in events), events
                    if label == 'boundary-batch':
                        assert (workspace / 'boundary-marker.txt').read_text() == 'EXECUTED_ONCE'
                        assert len(tool_done) == 11
                    print(f'Packed continuation PASS: {label}; failures={failure_count}; state={state}', flush=True)
                if session_copy:
                    stop(process)
                    original = (session_copy / 'snapshot.json').read_bytes()
                    original_hash = hashlib.sha256(original).hexdigest()
                    meta = json.loads((session_copy / 'meta.json').read_text(encoding='utf-8'))
                    target = home / 'sessions/default' / meta['id']
                    target.mkdir()
                    meta.update(model_id='fixture-followup', protocol='openai-responses',
                                reasoning_effort='medium', max_output_tokens=1024,
                                workspace_root=str(workspace), permission_profile='full-access')
                    (target / 'meta.json').write_text(json.dumps(meta), encoding='utf-8')
                    (target / 'snapshot.json').write_bytes(original)
                    process = start(directory, packed, home, dict(os.environ,
                        USE_WEBVIEW='0', USERPROFILE=str(base), HOME=str(base)))
                    _, boot = wait_bootstrap(process, port, directory / 'packed.log')
                    assert boot['data']['ready'], boot
                    operation, _ = api(port, 'POST', '/api/v1/projects/default/sessions/' + meta['id'] +
                                       '/runs', {'prompt':'affected-session-continue'})
                    value = terminal(port, operation)
                    assert value['state'] == 'succeeded', value
                    es = [json.loads(line) for line in (target / 'ui-events.jsonl').read_text(encoding='utf-8').splitlines()]
                    assert not any(e.get('kind') in (5,6,11) for e in es), es
                    assert Model.counts['affected-session-continue'] == 1
                    assert hashlib.sha256((session_copy / 'snapshot.json').read_bytes()).hexdigest() == original_hash
                    print('Packed continuation PASS: original affected snapshot continues; no completed tools replayed; source unchanged')
            except BaseException:
                print((directory / 'packed.log').read_text(encoding='utf-8', errors='replace')[-3000:])
                raise
            finally:
                stop(process)
                release_packed_copies(directory / packed.name)
    finally:
        model.shutdown(); model.server_close(); thread.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    parser.add_argument('--session-copy', type=Path)
    args = parser.parse_args()
    run(args.packed.resolve(), args.session_copy.resolve() if args.session_copy else None)
