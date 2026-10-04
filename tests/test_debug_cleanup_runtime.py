#!/usr/bin/env python3
"""Check retired debug APIs and retained chat replay in an isolated pack."""
from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading

from test_interrupt_runtime import ModelHandler, ModelServer, free_port, request, stop_host, until
from test_api_runtime import request as raw_request

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.inspect_session import inspect_session


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed-path', type=Path, default=ROOT / 'mdo.exe')
    parser.add_argument('--keep', action='store_true', help='keep owned hosts for browser QA until Enter')
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix='slim-debug-qa-', dir=ROOT / '.build'))
    packed = base / 'mdo.exe'
    shutil.copy2(args.packed_path, packed)
    home = base / 'mdo-home'
    port = free_port()
    model = ModelServer(('127.0.0.1', 0), ModelHandler)
    ModelHandler.calls = 0
    threading.Thread(target=model.serve_forever, daemon=True).start()
    (base / 'xs.json').write_text(json.dumps({'services': [{
        'enabled': True, 'class': 'http', 'name': 'mdo', 'ip': '127.0.0.1',
        'port': port, 'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
            'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}), encoding='utf-8')
    env = dict(os.environ, MDO_HOME=str(home), USERPROFILE=str(base), HOME=str(base),
        MDO_ORNITH_API_KEY='bounded-local-test',
        MDO_ORNITH_RESPONSES_URL=f'http://127.0.0.1:{model.server_port}/v1/responses')
    process = None
    static = None
    try:
        with (base / 'packed.log').open('wb') as log:
            process = subprocess.Popen([str(packed)], cwd=base, env=env,
                stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)

        def ready():
            if process.poll() is not None:
                raise AssertionError('packed host exited during startup')
            try:
                status, response = request(port, 'GET', '/api/v1/bootstrap')
                return status == 200 and response['data']['ready']
            except (OSError, KeyError):
                return False
        until(ready, 20)
        for path in ('/api/v1/events', '/api/v1/tasks/1/events'):
            for method in ('GET', 'HEAD', 'OPTIONS'):
                status, _, body = raw_request(port, method, path)
                assert status == 404, (path, method, status)
                if method == 'HEAD':
                    assert body == b'', body
                else:
                    assert json.loads(body)['error']['code'] == 'route_not_found', body
        status, response = request(port, 'GET', '/api/v1/tasks')
        assert status == 200 and isinstance(response['data']['items'], list)
        status, response = request(port, 'GET', '/api/v1/models')
        assert status == 200, response
        model_id = response['data']['models'][0]['id']
        status, response = request(port, 'POST', '/api/v1/sessions', {
            'project_id': 'default', 'title': '精简界面验证', 'agent_id': 'mdo.default', 'model_id': model_id,
            'protocol': 'openai-responses', 'permission_profile': 'balanced',
        })
        assert status == 201, response
        session_id = response['data']['id']
        route = f'/api/v1/projects/default/sessions/{session_id}'
        status, response = request(port, 'POST', route + '/runs', {'prompt': '本地界面验证'})
        assert status == 202, response
        run_id = response['data']['id']
        def complete():
            status, response = request(port, 'GET', f'/api/v1/runs/{run_id}')
            assert status == 200, response
            if response['data']['terminal']:
                assert response['data']['state'] == 'succeeded', response
                return True
            return False
        until(complete, 10)
        status, response = request(port, 'GET', route + '/events?after=0&limit=32')
        assert status == 200, response
        kinds = {event['kind'] for event in response['data']['items']}
        assert {'agent_start', 'model_done', 'agent_done'} <= kinds, kinds
        status, response = request(port, 'GET', route + '/recovery')
        assert status == 200 and not response['data']['resume_required'], response
        report = inspect_session(home / 'sessions/default' / session_id)
        assert report['event_counts']['model_done'] >= 1, report
        assert report['model_usage']['input_tokens'] == 8, report
        html = (ROOT / 'app/web/index.html').read_text(encoding='utf-8')
        for removed in ('trace-panel', 'decisions-panel', 'context-panel', 'task-summary'):
            assert f'id="{removed}"' not in html
        print('Packed debug cleanup + chat replay + offline report: PASS', flush=True)
        if args.keep:
            static = ModelServer(('127.0.0.1', 0), partial(SimpleHTTPRequestHandler, directory=str(ROOT)))
            threading.Thread(target=static.serve_forever, daemon=True).start()
            state = {'port': port, 'static_port': static.server_port, 'base': str(base),
                     'session_id': session_id, 'pid': process.pid}
            (ROOT / '.build/slim-qa.json').write_text(json.dumps(state), encoding='utf-8')
            print('Browser QA: ' + json.dumps(state), flush=True)
            input('Press Enter to stop owned QA hosts: ')
    finally:
        stop_host(process)
        if static:
            static.shutdown(); static.server_close()
        model.shutdown(); model.server_close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
