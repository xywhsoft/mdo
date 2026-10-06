#!/usr/bin/env python3
"""Small real HTTP probe for the four file-backed extension editors."""
import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from test_api_runtime import free_port, request, wait_ready, MCP_MOCK_SERVER

ROOT = Path(__file__).resolve().parent.parent

class SkillModel(BaseHTTPRequestHandler):
    calls = 0
    payloads = []
    def log_message(self, *_): pass
    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        type(self).payloads.append(payload)
        type(self).calls += 1
        calls = type(self).calls
        arguments = ({}, {'name': 'research'}, {'name': 'research', 'path': 'references/guide.txt'})
        output = [{'type': 'function_call', 'call_id': 'skill-' + str(calls),
            'name': 'skill', 'arguments': json.dumps(arguments[calls-1])}] if calls <= 3 else [
                {'type': 'message', 'content': [{'type': 'output_text', 'text': 'Skill probe complete'}]}]
        response = json.dumps({'id': 'resp_skill_' + str(calls), 'model': 'ornith-1.5-35b',
            'status': 'completed', 'output': output, 'usage': {'input_tokens': 10, 'output_tokens': 5, 'total_tokens': 15}}).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(response))); self.end_headers(); self.wfile.write(response)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='extensions-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        site = base / 'site'
        shutil.copytree(ROOT / 'app', site)
        port = free_port()
        config = {'engine': {'workers': 1}, 'services': [{'enabled': True,
            'class': 'http', 'name': 'extension-probe', 'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
                'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}
        (site / 'xs.json').write_text(json.dumps(config), encoding='utf-8')
        model_server = ThreadingHTTPServer(('127.0.0.1', 0), SkillModel)
        thread = threading.Thread(target=model_server.serve_forever, daemon=True); thread.start()
        env = dict(os.environ, MDO_HOME=str(base / 'home'), MDO_ORNITH_API_KEY='bounded-fixture-key',
            MDO_ORNITH_RESPONSES_URL=f'http://127.0.0.1:{model_server.server_port}/v1/responses')
        with (base / 'log.txt').open('wb') as log:
            process = subprocess.Popen([str(args.host.resolve()), str(site / 'xs.json')],
                cwd=site, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            try:
                wait_ready(port, process)
                def call(method, path, body=None, revision=None):
                    headers = {'Content-Type': 'application/json'}
                    if revision is not None: headers['If-Match'] = '"' + revision + '"'
                    status, _, data = request(port, method, '/api/v1/extensions/' + path,
                        body=None if body is None else json.dumps(body).encode(), headers=headers)
                    return status, json.loads(data)
                for kind in ('subagents', 'skills', 'mcp', 'commands'):
                    status, data = call('GET', kind)
                    assert status == 200, data
                assert not (base / 'home').exists(), 'read-only lists created Home'
                command = '---\ndescription: Review changes\nargument-hint: <path>\n---\nReview $ARGUMENTS carefully.'
                status, data = call('PUT', 'commands/review', {'content': command}, 'new')
                assert status == 200, data
                revision = data['data']['revision']
                assert data['data']['prompt'] == 'Review $ARGUMENTS carefully.'
                assert call('PUT', 'commands/review', {'content': command}, 'new')[0] == 412
                assert call('PUT', 'commands/stop', {'content': command}, 'new')[0] == 422
                assert call('PUT', 'commands/con.txt', {'content': command}, 'new')[0] == 400
                assert call('POST', 'commands/review/enabled', {'enabled': False}, revision)[0] == 200
                assert call('GET', 'commands/review')[1]['data']['enabled'] is False
                assert call('POST', 'commands/review/enabled', {'enabled': True}, revision)[0] == 200
                skill = '---\nname: research\ndescription: >\n  Research a project\n  before changes.\nmetadata:\n  author: test\n---\nUse references/guide.txt.'
                file = {'path': 'references/guide.txt', 'base64': base64.b64encode(b'Inspect first').decode()}
                status, data = call('PUT', 'skills/research', {'content': skill, 'files': [file]}, 'new')
                assert status == 200, data
                assert (base / 'home/skills/research/references/guide.txt').read_bytes() == b'Inspect first'
                skill_revision = data['data']['revision']
                skills = json.loads(request(port, 'GET', '/api/v1/skills')[2])['data']['items']
                assert any(item['id'] == 'research' and item['resource_count'] == 1 for item in skills), skills
                status, bundle = call('GET', 'skills/research/bundle')
                assert status == 200 and bundle['data']['files'] == [file], bundle
                assert call('POST', 'skills/research/enabled', {'enabled': False}, skill_revision)[0] == 200
                assert not any(item['id'] == 'research' for item in json.loads(request(port, 'GET', '/api/v1/skills')[2])['data']['items'])
                assert call('POST', 'skills/research/enabled', {'enabled': True}, skill_revision)[0] == 200
                agent = '---\nname: Reviewer\ndescription: Review code independently\nmodel: inherit\ntools: [read, "grep", Skill]\nread_only: true\n---\nInspect changes and report bugs.'
                status, data = call('PUT', 'subagents/reviewer', {'content': agent}, 'new')
                assert status == 200, data
                agent_revision = data['data']['revision']
                agents = json.loads(request(port, 'GET', '/api/v1/agents')[2])['data']
                assert 'subagent.reviewer' in json.dumps(agents), agents
                status, bad = call('PUT', 'subagents/reviewer', {'content': agent.replace('"grep"', 'nonexistent_tool')}, agent_revision)
                assert status == 503, bad
                assert call('GET', 'subagents/reviewer')[1]['data']['content'] == agent
                assert 'subagent.reviewer' in request(port, 'GET', '/api/v1/agents')[2].decode()
                assert call('POST', 'subagents/reviewer/enabled', {'enabled': False}, agent_revision)[0] == 200
                assert 'subagent.reviewer' not in request(port, 'GET', '/api/v1/agents')[2].decode()
                assert call('DELETE', 'subagents/reviewer', revision=agent_revision)[0] == 200
                badfile = {'path': '../escape.txt', 'base64': file['base64']}
                assert call('PUT', 'skills/escape', {'content': skill, 'files': [badfile]}, 'new')[0] != 200
                assert not (base / 'home/escape.txt').exists()
                status, _, raw_session = request(port, 'POST', '/api/v1/sessions',
                    body=json.dumps({'project_id': 'default', 'title': 'Extension Skill probe'}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 201, raw_session
                session = json.loads(raw_session)['data']['id']
                status, _, raw_run = request(port, 'POST', '/api/v1/projects/default/sessions/' + session + '/runs',
                    body=json.dumps({'prompt': 'EXTENSION skill probe', 'timeout_ms': 5000}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 202, raw_run
                run = json.loads(raw_run)['data']; deadline = time.monotonic() + 8
                while not run['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05)
                    status, _, raw_run = request(port, 'GET', '/api/v1/runs/' + run['id'])
                    run = json.loads(raw_run)['data']
                assert run['state'] == 'succeeded', run
                assert SkillModel.calls == 4, SkillModel.payloads
                assert 'Use references/guide.txt.' not in json.dumps(SkillModel.payloads[0]), 'Skill body eagerly injected'
                assert 'Inspect first' in json.dumps(SkillModel.payloads[-1]), SkillModel.payloads[-1]
                assert call('DELETE', 'skills/research', revision=skill_revision)[0] == 200
                assert not (base / 'home/skills/research').exists()
                assert call('DELETE', 'commands/review', revision=revision)[0] == 200
                marker = base / 'mcp-started.txt'
                mock = base / 'mcp_mock.py'
                credential = 'fixture MCP key with spaces 中文'
                mock.write_text('import os\nfrom pathlib import Path\nassert os.environ["MOCK_KEY"] == ' + repr(credential) + '\nPath(' + repr(str(marker)) + ").write_text('started')\n" + MCP_MOCK_SERVER, encoding='utf-8')
                mcp = {'schema_version': 1, 'id': 'mock', 'name': 'Local mock',
                    'description': 'Bounded connection test', 'enabled': True,
                    'transport': {'type': 'stdio', 'program': sys.executable,
                        'arguments': [str(mock)], 'working_directory': None,
                        'inherit_environment': True, 'environment': [{'name': 'MOCK_KEY', 'secret_ref': 'input:0'}]},
                    'protocol_version': '2026-07-28', 'startup_timeout_ms': 5000,
                    'request_timeout_ms': 5000, 'limits': {'message_bytes': 1048576, 'tools': 16},
                    'tools': {'allow': [], 'deny': []}, 'security': {
                        'default_effects': ['external-service'], 'permission_profile': 'balanced',
                        'trust_read_only_annotations': False}, 'auto_reconnect': True}
                status, data = call('PUT', 'mcp/mock', {'content': json.dumps(mcp), 'secrets': [credential]}, 'new')
                assert status == 200, data
                mcp_revision = data['data']['revision']
                assert not marker.exists(), 'Saving launched an MCP program'
                stored = (base / 'home/mcp/mock.json').read_text()
                assert 'fixture MCP' not in stored and 'vault:' in stored, stored
                http_mcp = {**mcp, 'id': 'http-mock', 'transport': {
                    'type': 'streamable-http', 'endpoint': 'https://example.com/mcp',
                    'headers': [{'name': 'Authorization', 'secret_ref': 'input:0'}]}}
                status, data = call('PUT', 'mcp/http-mock', {'content': json.dumps(http_mcp), 'secrets': ['Bearer fixture token']}, 'new')
                assert status == 200, data
                http_revision = data['data']['revision']
                assert 'Bearer fixture' not in (base / 'home/mcp/http-mock.json').read_text()
                assert call('DELETE', 'mcp/http-mock', revision=http_revision)[0] == 200
                status, _, raw_op = request(port, 'POST', '/api/v1/mcp/mock/refresh')
                assert status == 202, raw_op
                op = json.loads(raw_op)['data']
                deadline = time.monotonic() + 8
                while not op['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05)
                    status, _, raw_op = request(port, 'GET', '/api/v1/operations/' + op['id'])
                    op = json.loads(raw_op)['data']
                assert op['state'] == 'succeeded' and marker.exists(), op
                status, state = call('GET', 'mcp/mock')
                assert status == 200 and state['data']['connected'] and state['data']['tool_count'] == 1, state
                status, state = call('POST', 'mcp/mock/enabled', {'enabled': False}, mcp_revision)
                assert status == 200 and not state['data']['enabled'], state
                mcp_revision = state['data']['revision']
                assert json.loads((base / 'home/mcp/mock.json').read_text())['enabled'] is False
                status, state = call('POST', 'mcp/mock/enabled', {'enabled': True}, mcp_revision)
                assert status == 200 and state['data']['enabled'], state
                assert call('DELETE', 'mcp/mock', revision=state['data']['revision'])[0] == 200
                print('extension HTTP probe: PASS')
            except BaseException:
                log.flush()
                print((base / 'log.txt').read_text(encoding='utf-8', errors='replace')[-6000:], file=sys.stderr)
                raise
            finally:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
                model_server.shutdown(); model_server.server_close(); thread.join(timeout=2)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

