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
    delegation_payloads = []
    tool_payloads = []
    profile_payloads = []
    def log_message(self, *_): pass
    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        users = [item for item in payload.get('input', []) if item.get('role') == 'user']
        latest = json.dumps(users[-1] if users else payload)
        if 'EXTENSION profile probe' in latest:
            type(self).profile_payloads.append(payload); calls = len(type(self).profile_payloads)
            output = [{'type': 'message', 'content': [{'type': 'output_text', 'text': 'Profile probe complete'}]}]
        elif 'EXTENSION local tool probe' in latest:
            type(self).tool_payloads.append(payload)
            calls = len(type(self).tool_payloads)
            output = [{'type': 'function_call', 'call_id': 'local-tool', 'name': 'user.fixture', 'arguments': '{}'}] if calls == 1 else [
                {'type': 'message', 'content': [{'type': 'output_text', 'text': 'Tool probe complete'}]}]
        elif 'EXTENSION delegation probe' in json.dumps(payload) or 'EXTENSION reviewer child' in json.dumps(payload):
            type(self).delegation_payloads.append(payload)
            calls = len(type(self).delegation_payloads)
            if calls == 1:
                output = [{'type': 'function_call', 'call_id': 'delegate-fixture', 'name': 'agent',
                    'arguments': json.dumps({'name': 'subagent.reviewer', 'prompt': 'EXTENSION reviewer child'})}]
            else:
                output = [{'type': 'message', 'content': [{'type': 'output_text',
                    'text': 'Reviewer child report' if calls == 2 else 'Delegation probe complete'}]}]
        else:
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
    parser.add_argument('--packed', type=Path, help='verify a standalone package without external app files')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='extensions-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        site = base / 'site'
        if args.packed:
            site.mkdir(); shutil.copy2(args.packed, site / args.packed.name)
        else:
            shutil.copytree(ROOT / 'app', site)
        port = free_port()
        config = {'engine': {'workers': 1}, 'services': [{'enabled': True,
            'class': 'http', 'name': 'extension-probe', 'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
                'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}
        (site / 'xs.json').write_text(json.dumps(config), encoding='utf-8')
        model_server = ThreadingHTTPServer(('127.0.0.1', 0), SkillModel)
        thread = threading.Thread(target=model_server.serve_forever, daemon=True); thread.start()
        env = dict(os.environ, MDO_HOME=str(base / 'home'), MDO_EXTENSION_MODEL_KEY='bounded-fixture-key',
            USE_WEBVIEW='0')
        with (base / 'log.txt').open('wb') as log:
            command = [str(site / args.packed.name), '--', '--home', str(base / 'home')] if args.packed else [str(args.host.resolve()), str(site / 'xs.json')]
            process = subprocess.Popen(command,
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
                for kind in ('agents', 'subagents', 'skills', 'mcp', 'commands', 'tools'):
                    status, data = call('GET', kind)
                    assert status == 200, data
                # Startup tool preflight can persist its own notification. Reads
                # must never materialize extension files or enablement settings.
                for name in ('agents', 'subagents', 'skills', 'mcp', 'commands', 'tools', 'config/extensions.json'):
                    assert not (base / 'home' / name).exists(), 'read-only list wrote ' + name
                # Use the normal model configuration API. The built-in online
                # model requires login and must not bypass that requirement.
                status, headers, raw = request(port, 'GET', '/api/v1/models/config')
                assert status == 200, raw
                config = json.loads(raw)['data']
                config.pop('runtime_override', None)
                provider = json.loads(json.dumps(config['providers'][0]))
                provider.update(id='extension-fixture', name='Extension fixture',
                    builtin=False, editable=True, removable=True,
                    endpoints={'responses':f'http://127.0.0.1:{model_server.server_port}/v1'},
                    credential={'secret_ref':'env:MDO_EXTENSION_MODEL_KEY'})
                profile = json.loads(json.dumps(config['items'][0]))
                profile.update(id='extension-fixture', name='Extension fixture',
                    provider='extension-fixture', builtin=False, free=False,
                    editable=True, removable=True, protocols=['openai-responses'],
                    default_protocol='openai-responses')
                config['providers'].append(provider); config['items'].append(profile)
                status, _, raw = request(port,'PUT','/api/v1/settings/models',
                    body=json.dumps({'schema_version':1,'patch':config}).encode(),
                    headers={'Content-Type':'application/json','If-Match':headers['etag']})
                assert status == 200, raw
                def tool_catalog():
                    status, _, raw = request(port, 'GET', '/api/v1/tools')
                    assert status == 200, raw
                    return {item['id']: item for item in json.loads(raw)['data']['items']}
                builtins = tool_catalog()
                assert len(builtins) == 21 and 'ask_user' in builtins, builtins
                assert builtins['web_search']['member_only'] and builtins['web_search']['availability'] == 'sign_in_required'
                assert builtins['agent']['availability'] == 'no_subagents'
                tool_source = (ROOT / 'tests/fixtures/modules/local-tool.c').read_text()
                status, tool = call('PUT', 'tools/fixture', {'content': tool_source}, 'new')
                assert status == 200 and tool['data']['loaded'], tool
                tool_revision = tool['data']['revision']
                assert tool['data']['tools'][0]['id'] == 'user.fixture', tool
                assert tool_catalog()['user.fixture']['source'] == 'c'
                assert call('PUT', 'tools/fixture', {'content': tool_source}, 'new')[0] == 412
                status, rejected = call('PUT', 'tools/fixture', {'content': tool_source + '\ninvalid C source!'}, tool_revision)
                assert status == 503 and 'fixture.c' in json.dumps(rejected), rejected
                assert call('GET', 'tools/fixture')[1]['data']['content'] == tool_source
                assert 'user.fixture' in tool_catalog()
                assert call('PUT', 'tools/collision', {'content': tool_source.replace('user.fixture', 'read')}, 'new')[0] == 503
                assert not (base / 'home/tools/collision.c').exists()
                assert call('PUT', 'tools/alias-collision', {'content': tool_source.replace('user.fixture', 'Read')}, 'new')[0] == 503
                empty = tool_source.replace('return Registrar->AddTool(Registrar->Context, &Tool, Error, Capacity);',
                    '(void)Registrar; (void)Tool; (void)Error; (void)Capacity; return MDO_RESULT_OK;')
                assert call('PUT', 'tools/fixture', {'content': empty}, tool_revision)[0] == 503
                assert 'user.fixture' in tool_catalog()
                unsafe = tool_source.replace('MDO_TOOL_EFFECT_READ', 'MDO_TOOL_EFFECT_PROCESS')
                assert call('PUT', 'tools/fixture', {'content': unsafe}, tool_revision)[0] == 503
                assert call('POST', 'tools/fixture/enabled', {'enabled': False}, tool_revision)[0] == 200
                assert 'user.fixture' not in tool_catalog()
                assert call('POST', 'tools/fixture/enabled', {'enabled': True}, tool_revision)[0] == 200
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
                assert call('POST', 'subagents/reviewer/enabled', {'enabled': True}, agent_revision)[0] == 200
                badfile = {'path': '../escape.txt', 'base64': file['base64']}
                assert call('PUT', 'skills/escape', {'content': skill, 'files': [badfile]}, 'new')[0] != 200
                assert not (base / 'home/escape.txt').exists()
                status, _, raw_session = request(port, 'POST', '/api/v1/sessions',
                    body=json.dumps({'project_id': 'default', 'model_id':'extension-fixture',
                        'title': 'Extension Skill probe', 'permission_profile': 'full-access'}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 201, raw_session
                session = json.loads(raw_session)['data']['id']
                status, _, raw_run = request(port, 'POST', '/api/v1/projects/default/sessions/' + session + '/runs',
                    body=json.dumps({'prompt': 'EXTENSION local tool probe', 'timeout_ms': 5000}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 202, raw_run
                run = json.loads(raw_run)['data']; deadline = time.monotonic() + 8
                while not run['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05)
                    run = json.loads(request(port, 'GET', '/api/v1/runs/' + run['id'])[2])['data']
                assert run['state'] == 'succeeded', run
                assert 'LOCAL TOOL CALLBACK RESULT' in json.dumps(SkillModel.tool_payloads[-1]), SkillModel.tool_payloads
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
                status, _, raw_run = request(port, 'POST', '/api/v1/projects/default/sessions/' + session + '/runs',
                    body=json.dumps({'prompt': 'EXTENSION delegation probe', 'timeout_ms': 5000}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 202, raw_run
                run = json.loads(raw_run)['data']; deadline = time.monotonic() + 8
                while not run['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05)
                    status, _, raw_run = request(port, 'GET', '/api/v1/runs/' + run['id'])
                    run = json.loads(raw_run)['data']
                assert run['state'] == 'succeeded', run
                assert len(SkillModel.delegation_payloads) == 3, SkillModel.delegation_payloads
                child = SkillModel.delegation_payloads[1]
                assert 'Inspect changes and report bugs.' in json.dumps(child), child
                assert {tool['name'] for tool in child['tools']} == {'read', 'grep', 'skill'}, child['tools']
                assert 'Reviewer child report' in json.dumps(SkillModel.delegation_payloads[-1])
                assert call('DELETE', 'subagents/reviewer', revision=agent_revision)[0] == 200
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
                status, default = call('GET', 'agents/default')
                assert status == 200 and not default['data']['external'], default
                default_text = '---\nname: Default\ndescription: Configured default\nmodel: inherit\ntools: [read, grep, user.fixture]\nallow_delegation: true\ncode: false\n---\nPROFILE SYSTEM INSTRUCTIONS'
                status, edited = call('PUT', 'agents/default', {'content': default_text}, default['data']['revision'])
                assert status == 200, edited
                edited_revision = edited['data']['revision']
                assert call('DELETE', 'tools/fixture', revision=tool_revision)[0] == 409, 'dependency was removed'
                assert 'user.fixture' in tool_catalog()
                status, _, raw_profile = request(port, 'POST', '/api/v1/sessions',
                    body=json.dumps({'project_id': 'default', 'model_id':'extension-fixture',
                        'title': 'Profile probe', 'permission_profile': 'full-access'}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 201, raw_profile
                profile_session = json.loads(raw_profile)['data']['id']
                status, _, raw_run = request(port, 'POST', f'/api/v1/projects/default/sessions/{profile_session}/runs',
                    body=json.dumps({'prompt': 'EXTENSION profile probe', 'timeout_ms': 5000}).encode(), headers={'Content-Type': 'application/json'})
                assert status == 202, raw_run
                run = json.loads(raw_run)['data']; deadline = time.monotonic() + 8
                while not run['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05); run = json.loads(request(port, 'GET', '/api/v1/runs/' + run['id'])[2])['data']
                assert run['state'] == 'succeeded', run
                sent = SkillModel.profile_payloads[-1]
                assert {tool['name'] for tool in sent['tools']} == {'read', 'grep', 'user.fixture'}, sent
                assert 'PROFILE SYSTEM INSTRUCTIONS' in json.dumps(sent), sent
                assert call('POST', 'agents/default/enabled', {'enabled': False}, edited_revision)[0] == 422
                assert call('DELETE', 'agents/default', revision=edited_revision)[0] == 200
                # No C profile is required; an ordinary Markdown main Agent works.
                basic = '---\nname: Researcher\ndescription: Read projects\nmodel: inherit\ntools: [read, ask_user]\n---\n'
                status, basic_item = call('PUT', 'agents/researcher', {'content': basic}, 'new')
                assert status == 200, basic_item
                assert 'agent.researcher' in request(port, 'GET', '/api/v1/agents')[2].decode()
                bad_code = basic.replace('model: inherit', 'model: inherit\ncode: true')
                assert call('PUT', 'agents/researcher', {'content': bad_code}, basic_item['data']['revision'])[0] == 503
                code_directory = base / 'home/modules/agents'
                code_directory.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / 'tests/fixtures/modules/generated-agent.c', code_directory / 'generated.c')
                status, coded = call('PUT', 'agents/researcher', {'content': bad_code}, basic_item['data']['revision'])
                assert status == 200, coded
                assert call('POST', 'agents/researcher/enabled', {'enabled': False}, coded['data']['revision'])[0] == 200
                assert 'agent.researcher' not in request(port, 'GET', '/api/v1/agents')[2].decode()
                assert call('POST', 'agents/researcher/enabled', {'enabled': True}, coded['data']['revision'])[0] == 200
                def profile_probe(agent_id):
                    status, _, raw = request(port, 'POST', '/api/v1/sessions',
                        body=json.dumps({'project_id': 'default', 'model_id':'extension-fixture', 'agent_id': agent_id,
                            'title': 'Generated prompt probe', 'permission_profile': 'full-access'}).encode(), headers={'Content-Type': 'application/json'})
                    assert status == 201, raw
                    sid = json.loads(raw)['data']['id']
                    status, _, raw = request(port, 'POST', f'/api/v1/projects/default/sessions/{sid}/runs',
                        body=json.dumps({'prompt': 'EXTENSION profile probe', 'timeout_ms': 5000}).encode(), headers={'Content-Type': 'application/json'})
                    assert status == 202, raw
                    run = json.loads(raw)['data']; deadline = time.monotonic() + 8
                    while not run['terminal'] and time.monotonic() < deadline:
                        time.sleep(.05); run = json.loads(request(port, 'GET', '/api/v1/runs/' + run['id'])[2])['data']
                    assert run['state'] == 'succeeded', run
                    return SkillModel.profile_payloads[-1]
                generated = profile_probe('agent.researcher')
                assert 'CODE GENERATED AGENT INSTRUCTIONS' in json.dumps(generated), generated
                assert {tool['name'] for tool in generated['tools']} == {'read', 'ask_user'}, generated
                # Restoring ordinary mode works even with a C implementation present.
                status, ordinary = call('PUT', 'agents/researcher', {'content': basic}, coded['data']['revision'])
                assert status == 200, ordinary
                generated = profile_probe('agent.researcher')
                assert 'CODE GENERATED AGENT INSTRUCTIONS' not in json.dumps(generated), generated
                assert 'careful coding and general task Agent' in json.dumps(generated), generated
                (code_directory / 'generated.c').unlink()
                assert call('DELETE', 'agents/researcher', revision=basic_item['data']['revision'])[0] == 200
                c_profile = default_text.replace('code: false', 'code: true')
                status, default = call('GET', 'agents/default')
                status, c_item = call('PUT', 'agents/default', {'content': c_profile}, default['data']['revision'])
                assert status == 200, c_item
                live = json.loads(request(port, 'GET', '/api/v1/agents')[2])['data']['items']
                main_agent = next(agent for agent in live if agent['id'] == 'mdo.default')
                # The base prompt stays code-owned, while the UI tool list remains effective.
                assert main_agent['tools'] == ['read', 'grep', 'user.fixture'], main_agent
                assert call('DELETE', 'agents/default', revision=c_item['data']['revision'])[0] == 200
                assert call('DELETE', 'tools/fixture', revision=tool_revision)[0] == 200
                assert 'user.fixture' not in tool_catalog()
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
                if args.packed:
                    from test_packed_home_lease import release_packed_copies
                    release_packed_copies(site / args.packed.name)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

