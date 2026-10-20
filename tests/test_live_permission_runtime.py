"""Live permission transitions on real xs/TCC and a bounded loopback model."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_api_runtime import request
from test_interrupt_runtime import free_port, start_host, stop_host, until

ROOT = Path(__file__).resolve().parents[1]


class Model(BaseHTTPRequestHandler):
    calls = {}
    arrivals = {3: threading.Event(), 5: threading.Event()}
    resume = {3: threading.Event(), 5: threading.Event()}
    child_arrivals = {1: threading.Event(), 3: threading.Event()}
    child_resume = {1: threading.Event(), 3: threading.Event()}
    failures = []
    external = None

    def log_message(self, *_args):
        pass

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        inputs = payload['input']
        content = next(item['content'] for item in reversed(inputs) if item.get('role') == 'user')
        prompt = content if isinstance(content, str) else content[0]['text']
        count = Model.calls.get(prompt, 0) + 1
        Model.calls[prompt] = count
        if prompt == 'live' and count in Model.arrivals:
            Model.arrivals[count].set()
            if not Model.resume[count].wait(15):
                Model.failures.append('model fixture wait expired')
        if prompt == 'child' and count in Model.child_arrivals:
            Model.child_arrivals[count].set()
            if not Model.child_resume[count].wait(15):
                Model.failures.append('child model fixture wait expired')
        if prompt in ('live', 'child') and count in (3, 4):
            results = [item.get('output', '') for item in inputs if item.get('type') == 'function_call_output']
            wanted = 'EXTERNAL_LIVE_CONTENT' if count == 3 else 'reason=permission_denied'
            if wanted not in str(results[-1]):
                Model.failures.append((count, wanted, results[-1]))
        if count == 1 or count in (4, 5):
            name, args = 'write', {'path': f'{prompt}-{count}.txt', 'content': 'live permission write'}
            if prompt == 'large':
                args['content'] = 'x' * 20000
        elif count in (2, 3):
            name, args = 'read', {'path': str(Model.external)}
        else:
            name = None
        if prompt == 'delegate':
            name, args = ('agent', {'name': 'live.child', 'prompt': 'child'}) if count == 1 else (None, {})
        if prompt == 'child' and count >= 4:
            name = None
        output = [{'type': 'function_call', 'name': name, 'call_id': f'{prompt}-{count}',
                   'arguments': json.dumps(args)}] if name else [
            {'type': 'message', 'content': [{'type': 'output_text', 'text': 'Live permission probe complete'}]}]
        body = json.dumps({'id': 'live-response', 'status': 'completed', 'model': 'ornith-1.5-35b',
            'output': output, 'usage': {'input_tokens': 8, 'output_tokens': 3, 'total_tokens': 11}}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def probe(host):
    with tempfile.TemporaryDirectory(prefix='live-permission-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        site, home, workspace = base / 'site', base / 'home', base / 'workspace'
        shutil.copytree(ROOT / 'app', site)
        agent = site / 'default-home/modules/agents/builtin_default.c'
        child = agent.read_text(encoding='utf-8').replace('"mdo.default"', '"live.child"')
        child = child.replace('"mdo.default-agent"', '"live.child-module"')
        child = child.replace('MDO_AGENT_MAIN |', 'MDO_AGENT_SUBAGENT |')
        agent.with_name('live_child.c').write_text(child, encoding='utf-8')
        workspace.mkdir()
        Model.calls, Model.failures = {}, []
        Model.arrivals = {n: threading.Event() for n in (3, 5)}
        Model.resume = {n: threading.Event() for n in (3, 5)}
        Model.child_arrivals = {n: threading.Event() for n in (1, 3)}
        Model.child_resume = {n: threading.Event() for n in (1, 3)}
        Model.external = base / 'external.txt'
        Model.external.write_text('EXTERNAL_LIVE_CONTENT', encoding='utf-8')
        model = ThreadingHTTPServer(('127.0.0.1', 0), Model)
        model.daemon_threads = True
        threading.Thread(target=model.serve_forever, daemon=True).start()
        port = free_port()
        config = site / 'xs.json'
        config.write_text(json.dumps({'services': [{'class': 'http', 'enabled': True,
            'name': 'live-permissions', 'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
                'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}), encoding='utf-8')
        env = dict(os.environ, USERPROFILE=str(base), HOME=str(base),
            MDO_ORNITH_RESPONSES_URL=f'http://127.0.0.1:{model.server_port}/v1',
            MDO_ORNITH_API_KEY='fixture-only-live-permission')
        process = None

        def call(method, path, body=None, expected=200, tag=None):
            code, headers, data = request(port, method, '/api/v1/' + path,
                body=json.dumps(body).encode() if body is not None else None,
                headers={'Content-Type': 'application/json', **({'If-Match': tag} if tag else {})})
            data = json.loads(data)
            assert code == expected, (path, code, data)
            return data.get('data', data), headers

        def create(title):
            data, _ = call('POST', 'sessions', {'project_id': 'default', 'title': title,
                'model_id': 'ornith-1.5-35b', 'protocol': 'openai-responses',
                'permission_profile': 'balanced', 'workspace_root': str(workspace)}, 201)
            return 'projects/default/sessions/' + data['id']

        def pending():
            return call('GET', 'approvals')[0]['items']

        def permission(path, mode, expected=200, tag=None, extra=None):
            if tag is None:
                _, headers = call('GET', path)
                tag = headers['etag']
            return call('PUT', path + '/permission', {'permission_profile': mode, **(extra or {})}, expected, tag)

        try:
            process = start_host(host, config, home, env, base / 'xs.log', port)
            path, other = create('Live transitions'), create('Isolated permissions')
            _, stale_headers = call('GET', path)
            run, _ = call('POST', path + '/runs', {'prompt': 'live'}, 202)
            other_run, _ = call('POST', other + '/runs', {'prompt': 'isolated'}, 202)
            until(lambda: len(pending()) == 2)
            permission(path, 'full-access')
            until(lambda: (workspace / 'live-1.txt').exists())
            assert Model.arrivals[3].wait(8), 'full access read did not complete'
            assert len(pending()) == 1 and not (workspace / 'isolated-1.txt').exists()
            permission(path, 'balanced')
            permission(path, 'full-access', 412, stale_headers['etag'])
            permission(path, 'read-only', 422)
            permission(path, 'full-access', 422, extra={'model_id': 'must-not-change'})
            _, headers = permission(path, 'balanced')
            _, same_headers = permission(path, 'balanced', tag=headers['etag'])
            assert headers['etag'] == same_headers['etag'], 'no-op changed revision'
            Model.resume[3].set()
            until(lambda: len(pending()) == 2)
            live = next(item for item in pending() if item['tool_call_id'] == 'live-4')
            call('PUT', 'approvals/' + str(live['id']), {'decision': 'allow_run'})
            assert Model.arrivals[5].wait(8)
            permission(path, 'full-access')
            permission(path, 'balanced')
            Model.resume[5].set()
            until(lambda: any(item['tool_call_id'] == 'live-5' for item in pending()))
            assert not (workspace / 'live-5.txt').exists(), 'downgrade retained allow-run grant'
            permission(path, 'full-access')
            until(lambda: call('GET', 'runs/' + run['id'])[0]['terminal'])
            final, _ = call('GET', 'runs/' + run['id'])
            assert final['state'] == 'succeeded', final
            assert (workspace / 'live-5.txt').exists()
            assert len(pending()) == 1, 'other session was accidentally granted full access'
            call('DELETE', 'runs/' + other_run['id'])
            until(lambda: call('GET', 'runs/' + other_run['id'])[0]['terminal'])
            assert not Model.failures, Model.failures
            large = create('Large full-access write')
            permission(large, 'full-access')
            large_run, _ = call('POST', large + '/runs', {'prompt': 'large'}, 202)
            until(lambda: call('GET', 'runs/' + large_run['id'])[0]['terminal'])
            assert (workspace / 'large-1.txt').read_text() == 'x' * 20000
            parent = create('Live child inheritance')
            permission(parent, 'full-access')
            parent_run, _ = call('POST', parent + '/runs', {'prompt': 'delegate'}, 202)
            assert Model.child_arrivals[1].wait(8), 'child did not start'
            permission(parent, 'balanced')
            Model.child_resume[1].set()
            until(lambda: len(pending()) == 1)
            assert not (workspace / 'child-1.txt').exists(), 'existing child missed downgrade'
            permission(parent, 'full-access')
            assert Model.child_arrivals[3].wait(8)
            permission(parent, 'balanced')
            Model.child_resume[3].set()
            until(lambda: call('GET', 'runs/' + parent_run['id'])[0]['terminal'])
            assert call('GET', 'runs/' + parent_run['id'])[0]['state'] == 'succeeded'
            assert (workspace / 'child-1.txt').exists()
            assert not Model.failures, Model.failures
            permission(path, 'balanced')
            stop_host(process)
            process = start_host(host, config, home, env, base / 'xs.log', port)
            saved, _ = call('GET', path)
            assert saved['permission_profile'] == 'balanced'
            # A stale queued snapshot cannot restore full access at run start.
            queue_id = 'a' * 32
            call('POST', path + '/queue', {'id': queue_id, 'text': 'queued', 'first': False,
                'stage': True, 'profile': {
                'model_id': saved['model_id'], 'reasoning_effort': saved['reasoning_effort'],
                'permission_profile': 'full-access'}}, 201)
            for state in ('pending', 'sending'):
                call('PUT', path + '/queue/' + queue_id, {'state': state})
            run, _ = call('POST', path + '/runs', {'prompt': 'queued', 'queue_item_id': queue_id}, 202)
            until(lambda: len(pending()) == 1)
            assert not (workspace / 'queued-1.txt').exists()
            permission(path, 'full-access')
            until(lambda: call('GET', 'runs/' + run['id'])[0]['terminal'])
            print('Live permissions PASS: waiting approval, active model, file scope both ways, '
                  'grant reset, isolation, stale ETag, validation, persistence, queued snapshot')
        except BaseException:
            if (base / 'xs.log').exists():
                print((base / 'xs.log').read_text(encoding='utf-8', errors='replace')[-5000:])
            raise
        finally:
            for event in [*Model.resume.values(), *Model.child_resume.values()]:
                event.set()
            stop_host(process)
            model.shutdown()
            model.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    probe(parser.parse_args().host.resolve())
