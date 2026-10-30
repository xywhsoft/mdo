"""Bundled general assistant: real packed startup, delegation and opt-out.

All model requests go to the loopback fixture. Windows fixtures stay under the
project build directory. Linux uses native temporary storage: WSL's Windows
9p mounts do not support the atomic no-replace rename required by installation.
Only the executable and small disposable Homes are copied, without a new runtime
or repository checkout. No user installation is changed.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import threading
import time

from test_api_runtime import request as raw_request
from test_packed_home_lease import ROOT, request, site, start, stop, wait_bootstrap, release_packed_copies


def message_text(message):
    content = message.get('content')
    return content if isinstance(content, str) else '\n'.join(
        part.get('text', '') for part in content or [] if isinstance(part, dict))


class Model(BaseHTTPRequestHandler):
    requests = defaultdict(lambda: {'root': [], 'child': []})
    entered = threading.Event()
    release = threading.Event()
    errors = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        try:
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            text = '\n'.join(message_text(m) for m in body['messages'])
            child = 'You are the general-purpose assistant working on a task delegated' in text
            mode = re.search(r'QA_(?:ROOT|ASSIGN)_([a-z]+)', text)[1]
            side = 'child' if child else 'root'
            calls = self.requests[mode][side]
            calls.append(body)
            index = len(calls)
            tools = {t.get('function', t)['name'] for t in body.get('tools', [])}
            def call(name, arguments):
                return {'role': 'assistant', 'tool_calls': [{'id': f'{mode}-{side}-{index}',
                    'type': 'function', 'function': {'name': name, 'arguments': json.dumps(arguments)}}]}
            if child:
                assert 'PARENT_PRIVATE_CONTEXT' not in text, 'parent chat copied into independent task'
                assert 'GENERAL_RULE_SENTINEL' in text, 'project rules missing'
                assert 'agent' not in tools, 'default assistant delegated recursively'
                assert {'read', 'ls', 'glob', 'grep', 'skill'} <= tools, tools
                assert body['model'] == 'fixture-general-wire'
                assert body.get('max_tokens', body.get('max_completion_tokens')) == 1024, body.keys()
                assert body.get('reasoning_effort') == 'high'
                if mode == 'readonly':
                    assert not {'write', 'edit', 'exec', 'spawn'} & tools, tools
                    message = call('read', {'path': 'AGENTS.md'}) if index == 1 else {'role': 'assistant', 'content': 'GENERAL_CHILD_DONE'}
                elif mode == 'cancel':
                    self.entered.set()
                    assert self.release.wait(10), 'cancel fixture timed out'
                    message = {'role': 'assistant', 'content': 'cancelled fixture response'}
                elif mode == 'failure':
                    data = json.dumps({'error': {'type': 'invalid_request_error', 'code': 'fixture_child_failure', 'message': 'Offline child failure'}}).encode()
                    self.reply(400, data)
                    return
                else:
                    assert {'write', 'exec'} <= tools, tools
                    path = f'result-{mode}.txt'
                    if index == 1:
                        message = call('write', {'path': path, 'content': 'delegated-ok'})
                    elif index == 2:
                        argv = ['cmd.exe', '/D', '/C', 'echo general-exec-ok'] if os.name == 'nt' else ['/bin/sh', '-c', 'printf general-exec-ok']
                        message = call('exec', {'argv': argv, 'timeout_ms': 5000})
                    elif index == 3:
                        message = call('read', {'path': path})
                    else:
                        assert 'delegated-ok' in text and 'general-exec-ok' in text, 'tool results missing'
                        message = {'role': 'assistant', 'content': 'GENERAL_CHILD_DONE; write, process and read verified.'}
            else:
                assert 'agent' in tools, 'general assistant tool missing from root'
                if index == 1:
                    message = call('agent', {'name': 'subagent.general', 'prompt': f'QA_ASSIGN_{mode}: Complete the assigned fixture task.', 'background': mode == 'background'})
                elif mode == 'background' and 'GENERAL_CHILD_DONE' not in text:
                    task = re.search(r'task_id["\s]*:\s*(\d+)', text)
                    assert task, 'background delegation returned no task ID'
                    assert index < 5, 'background wait did not finish'
                    message = call('wait', {'task_ids': [int(task[1])], 'timeout_ms': 5000})
                else:
                    if mode != 'failure':
                        assert 'GENERAL_CHILD_DONE' in text, 'parent received no child result'
                    message = {'role': 'assistant', 'content': 'GENERAL_ROOT_DONE'}
            document = {'id': 'general-fixture', 'model': body['model'], 'choices': [{'index': 0,
                'message': message, 'finish_reason': 'tool_calls' if 'tool_calls' in message else 'stop'}],
                'usage': {'prompt_tokens': 32, 'completion_tokens': 12}}
            self.reply(200, json.dumps(document).encode())
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as error:
            self.errors.append(str(error))
            try:
                self.reply(400, json.dumps({'error': {'message': str(error)}}).encode())
            except OSError:
                pass

    def reply(self, status, data):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(data)


def configure(port, endpoint):
    status, headers, raw = raw_request(port, 'GET', '/api/v1/models/config')
    assert status == 200, raw
    config = json.loads(raw)['data']
    patch = {k: config[k] for k in ('providers', 'items', 'default_model')}
    provider = dict(id='general-fixture', name='Offline fixture', builtin=False, editable=True,
        removable=True, verify_peer=True, timeout_ms=15000, proxy={'kind': 'none'},
        endpoints={'chat_completions': endpoint}, credential={'secret_ref': 'env:MDO_GENERAL_FIXTURE_KEY'})
    model = json.loads(json.dumps(config['items'][0]))
    model.update(id='general-fixture', name='Offline fixture', provider=provider['id'],
        wire_model='fixture-general-wire', builtin=False, free=False, editable=True, removable=True,
        protocols=['openai-chat-completions'], default_protocol='openai-chat-completions',
        capabilities=['text-input', 'text-output', 'streaming', 'tool-call-output', 'tool-result-input', 'reasoning-control'],
        window=dict(mode='shared-context', context_tokens=64000, max_input_tokens=60000,
            max_output_tokens=4096, output_reserve_tokens=1024, summary_tokens=1024),
        reasoning_efforts=['default', 'none', 'high'], default_reasoning_effort='high', attachments=[])
    patch['providers'].append(provider)
    patch['items'].append(model)
    status, _, raw = raw_request(port, 'PUT', '/api/v1/settings/models',
        body=json.dumps(dict(schema_version=1, patch=patch)).encode(),
        headers={'Content-Type': 'application/json', 'If-Match': headers['etag']})
    assert status == 200, raw


def wait_run(port, run, timeout=18, approve_agent=False):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        status, body = request(port, 'GET', '/api/v1/runs/' + run)
        assert status == 200, body
        if body['data']['terminal']:
            return body['data']
        if approve_agent:
            status, pending = request(port, 'GET', '/api/v1/approvals')
            assert status == 200, pending
            for item in pending['data']['items']:
                assert item['tool'] == 'agent' and item['effects'] == ['agent_delegation'], item
                status, decision = request(port, 'PUT', '/api/v1/approvals/'+str(item['id']), {'decision': 'allow'})
                assert status == 200, decision
        time.sleep(.025)
    raise AssertionError(body)


def check(packed, previous=None, report=None):
    scratch = ROOT/'.build' if os.name == 'nt' else Path(tempfile.gettempdir())
    base = Path(tempfile.mkdtemp(prefix='general-subagent-', dir=scratch))
    home = base/'home'
    workspace = base/'workspace'
    workspace.mkdir()
    (workspace/'AGENTS.md').write_text('GENERAL_RULE_SENTINEL: Preserve other contributors\' work.\n', encoding='utf-8')
    env = dict(os.environ, USE_WEBVIEW='0', USERPROFILE=str(base), MDO_GENERAL_FIXTURE_KEY='offline-fixture-only')
    server = ThreadingHTTPServer(('127.0.0.1', 0), Model)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    Model.requests.clear()
    Model.errors = []
    Model.entered.clear()
    Model.release.clear()
    evidence = {}
    copies = []
    process = None
    passed = False
    try:
        if previous:
            old_site, old_port = site(base, 'old-site', previous)
            copies.append(old_site/previous.name)
            process = start(old_site, previous, home, env)
            _, boot = wait_bootstrap(process, old_port, old_site/'packed.log')
            assert boot['data']['ready']
            _, tools = request(old_port, 'GET', '/api/v1/tools')
            assert next(t for t in tools['data']['items'] if t['id'] == 'agent')['availability'] == 'no_subagents'
            stop(process)
            evidence['previous_version_missing_subagent_reproduced'] = True
        directory, port = site(base, 'new-site', packed)
        copies.append(directory/packed.name)
        def boot():
            nonlocal process
            process = start(directory, packed, home, env)
            _, body = wait_bootstrap(process, port, directory/'packed.log')
            assert body['data']['ready'], body
        def read_general():
            status, body = request(port, 'GET', '/api/v1/extensions/subagents/general')
            assert status == 200, body
            return body['data']
        def agent_tool():
            status, body = request(port, 'GET', '/api/v1/tools')
            assert status == 200, body
            return next(t for t in body['data']['items'] if t['id'] == 'agent')
        def plan(operation):
            status, body = request(port, 'POST', '/api/v1/ecosystem',
                dict(action='dependency_plan', operation=operation, ref='subagent:general', cascade=True))
            assert status == 200 and body['data']['ready'], body
            value = body['data']
            status, body = request(port, 'POST', '/api/v1/ecosystem',
                dict(action='apply_plan', plan_id=value['id'], revision=value['revision'], trust_code=True))
            assert status == 200, body
            return body['data']
        boot()
        general = read_general()
        assert general['id'] == 'subagent:general' and general['source'] == 'builtin' and general['effective_enabled'], general
        assert general['editable'] and general['builtin_restorable'] and not general['shareable'], general
        assert agent_tool()['available']
        _, agents = request(port, 'GET', '/api/v1/agents')
        role = next(a for a in agents['data']['items'] if a['id'] == 'subagent.general')
        assert role['flags'] & 2 and not role['flags'] & 1 and not role['flags'] & 16, role
        evidence['ordinary_builtin_directory_registered_and_tool_available'] = True
        configure(port, f'http://127.0.0.1:{server.server_port}/v1/chat/completions')
        for mode in ('foreground', 'background', 'readonly', 'failure', 'cancel'):
            permission = 'read-only' if mode == 'readonly' else 'full-access'
            status, body = request(port, 'POST', '/api/v1/sessions', dict(project_id='default',
                title='General assistant '+mode, agent_id='mdo.default', model_id='general-fixture',
                protocol='openai-chat-completions', reasoning_effort='high', max_output_tokens=1024,
                permission_profile=permission, workspace_root=str(workspace)))
            assert status == 201, body
            session = body['data']['id']
            path = '/api/v1/projects/default/sessions/'+session
            status, body = request(port, 'POST', path+'/runs',
                dict(prompt=f'QA_ROOT_{mode}: PARENT_PRIVATE_CONTEXT. Delegate the fixture task to the general assistant.'))
            assert status == 202, body
            run = body['data']['id']
            if mode == 'cancel':
                assert Model.entered.wait(5), 'child did not start'
                status, body = request(port, 'DELETE', '/api/v1/runs/'+run)
                assert status == 200, body
                Model.release.set()
            result = wait_run(port, run, approve_agent=mode == 'readonly')
            assert result['state'] == ('cancelled' if mode == 'cancel' else 'succeeded'), (mode, result, Model.errors)
            assert not Model.errors, Model.errors
            assert Model.requests[mode]['child'], mode
            if mode in ('foreground', 'background'):
                assert (workspace/('result-'+mode+'.txt')).read_text() == 'delegated-ok'
            evidence[mode] = dict(state=result['state'], root_requests=len(Model.requests[mode]['root']),
                child_requests=len(Model.requests[mode]['child']))
        evidence['child_inherits_model_effort_output_rules_and_permission_ceiling'] = True
        evidence['child_has_independent_context_and_no_nested_delegation'] = True
        evidence['child_failure_does_not_abort_parent_conversation'] = True
        stop(process)
        boot()
        assert read_general()['effective_enabled'] and agent_tool()['available']
        evidence['restart_keeps_available_default'] = True
        plan('disable')
        stop(process)
        boot()
        assert not read_general()['desired_enabled'] and agent_tool()['availability'] == 'no_subagents'
        evidence['explicit_disable_survives_restart'] = True
        plan('enable')
        assert read_general()['effective_enabled'] and agent_tool()['available']
        plan('remove')
        stop(process)
        boot()
        _, rows = request(port, 'GET', '/api/v1/extensions/subagents')
        assert not any(row['directory_id'] == 'general' for row in rows['data']['items']), rows
        assert agent_tool()['availability'] == 'no_subagents'
        evidence['explicit_uninstall_survives_restart'] = True
        stop(process)
        home = base/'edited-home'
        boot()
        general = read_general()
        original = (home/'subagents/general/agent.md').read_bytes()
        edited = original.decode('utf-8')+'\nUSER_ROLE_CHANGE_SENTINEL\n'
        status, body = request(port, 'PUT', '/api/v1/extensions/subagents/general', dict(
            manifest=general['manifest'], edits=[dict(path='agent.md', text=edited)],
            expected_revision=general['revision'], trust_code=True))
        assert status == 200, body
        assert read_general()['source'] == 'manual'
        plan('disable')
        stop(process)
        boot()
        assert (home/'subagents/general/agent.md').read_text(encoding='utf-8') == edited
        general = read_general()
        assert general['builtin_restorable'] and not general['desired_enabled']
        evidence['local_edits_and_disabled_state_survive_restart'] = True
        path = '/api/v1/extensions/subagents/general/actions'
        status, body = request(port, 'POST', path, dict(action='restore_builtin',
            expected_revision='0'*64, trust_code=True))
        assert status != 200, body
        assert (home/'subagents/general/agent.md').read_text(encoding='utf-8') == edited
        status, body = request(port, 'POST', path, dict(action='restore_builtin',
            expected_revision=general['revision'], trust_code=True))
        assert status == 200, body
        assert (home/'subagents/general/agent.md').read_bytes() == original
        assert read_general()['source'] == 'builtin' and not read_general()['desired_enabled']
        evidence['explicit_restore_checks_revision_and_preserves_disabled_state'] = True
        plan('enable')
        general = read_general()
        status, body = request(port, 'POST', path, dict(action='file.mkdir',
            path='notes', expected_revision=general['revision']))
        assert status == 200, body
        assert read_general()['source'] == 'manual' and read_general()['effective_enabled']
        evidence['builtin_directory_file_edits_become_local_customizations'] = True
        stop(process)
        home = base/'custom-home'
        custom = home/'subagents/general'
        custom.mkdir(parents=True)
        manifest = json.loads((ROOT/'app/default-home/subagents/general/extension.json').read_text(encoding='utf-8'))
        manifest['id'] = 'subagent:user.general'
        manifest['name'] = 'User assistant'
        text = json.dumps(manifest, ensure_ascii=False)+'\n'
        (custom/'extension.json').write_text(text, encoding='utf-8')
        (custom/'agent.md').write_bytes(original)
        boot()
        assert (custom/'extension.json').read_text(encoding='utf-8') == text
        general = read_general()
        assert general['id'] == manifest['id'] and general['source'] == 'manual' and not general['builtin_restorable'], general
        evidence['preexisting_same_name_custom_directory_is_preserved'] = True
        passed = True
    finally:
        Model.release.set()
        if process:
            stop(process)
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)
        for copy in copies:
            if copy.exists():
                release_packed_copies(copy)
        if passed and base.resolve().parent == scratch.resolve() and base.name.startswith('general-subagent-'):
            shutil.rmtree(base)
        elif not passed:
            print('Failure fixture preserved:', base, flush=True)
    if report:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(evidence, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(evidence, ensure_ascii=False), flush=True)
    return evidence


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', required=True, type=Path)
    parser.add_argument('--previous', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    check(args.packed.resolve(), args.previous.resolve() if args.previous else None, args.report)
