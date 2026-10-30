"""Bounded general-assistant acceptance in an independent Android QA APK.

All model traffic uses a loopback fixture over one owned ADB reverse mapping.
Only org.xleaves.mdo.qa.* packages are accepted. Neither daily application data
nor system power/battery policies are changed. Lite/full share the same QA Home.
"""
from __future__ import annotations

import argparse
import hashlib
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import threading
import time
import traceback

from android_release_client import AndroidReleaseClient
from test_general_subagent_packed import Model


class AndroidModel(Model):
    exec_argv = ['/system/bin/sh', '-c', 'printf general-exec-ok']
    lifecycle_entered = threading.Event()
    lifecycle_release = threading.Event()

    def reply(self, status, data):
        value = json.loads(data)
        calls = value.get('choices', [{}])[0].get('message', {}).get('tool_calls', [])
        if calls and calls[0]['id'] == 'lifecycle-child-1':
            self.lifecycle_entered.set()
            assert self.lifecycle_release.wait(20), 'background lifecycle fixture timed out'
        super().reply(status, data)


def main():
    release = json.loads((Path(__file__).resolve().parents[1]/'app/release.json').read_text(encoding='utf-8'))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True, type=Path)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--edition', required=True, choices=('lite', 'full'))
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--expect-removed', action='store_true', help='Full upgrade must preserve the previous explicit uninstall')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=False)
    client = AndroidReleaseClient(args.adb, args.serial, args.package)
    server = ThreadingHTTPServer(('127.0.0.1', 0), AndroidModel)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    port = server.server_port
    mapping = 'tcp:' + str(port)
    owned_reverse = False
    report = dict(package=args.package, edition=args.edition, real_model_calls=0, passed=False, checks=[], runs={})

    def check(value, name):
        assert value, name
        report['checks'].append(name)
        print('PASS', name, flush=True)

    def get_role():
        return client.data('GET', '/api/v1/extensions/subagents/general')

    def agent_tool():
        return next(row for row in client.data('GET', '/api/v1/tools')['items'] if row['id'] == 'agent')

    def plan(operation):
        value = client.data('POST', '/api/v1/ecosystem', dict(action='dependency_plan',
            operation=operation, ref='subagent:general', cascade=True))
        assert value['ready'], value
        return client.data('POST', '/api/v1/ecosystem', dict(action='apply_plan', plan_id=value['id'],
            revision=value['revision'], trust_code=True))

    def wait_run(run, approve=False):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            value = client.data('GET', '/api/v1/runs/'+run)
            if value['terminal']:
                return value
            if approve:
                for approval in client.data('GET', '/api/v1/approvals')['items']:
                    assert approval['tool'] == 'agent' and approval['effects'] == ['agent_delegation'], approval
                    client.data('PUT', '/api/v1/approvals/'+str(approval['id']), dict(decision='allow'))
            time.sleep(.05)
        raise AssertionError(value)

    try:
        before_reverse = client.adb('reverse', '--list').decode().splitlines()
        assert not any(mapping in row.split() for row in before_reverse), 'Refusing to replace an existing ADB mapping'
        client.adb('reverse', mapping, mapping)
        owned_reverse = True
        worker.start()
        Model.requests.clear()
        Model.errors = []
        Model.entered.clear()
        Model.release.clear()
        AndroidModel.lifecycle_entered.clear()
        AndroidModel.lifecycle_release.clear()
        client.launch()
        info = client.call('runtime.info')
        check(info['version'] == release['version_name'], 'Actual Android host runs the candidate version')
        cli = client.data('GET', '/api/v1/cli-tools')
        check(cli['edition'] == args.edition and not cli.get('binding_error'), 'Expected edition and healthy command bindings')
        distribution = client.data('GET', '/api/v1/distribution')
        report['build_id'] = distribution['build_id']
        check(distribution['build_id'] == release['android_'+args.edition+'_build_id'],
            'Runtime uses the edition-specific Android installation number')
        marker = client.home+'/workspace/general-upgrade-marker.txt'
        if args.edition == 'lite':
            client.call('fs.write', dict(path=marker, text='general-assistant-upgrade-marker'))
        else:
            check(client.private_file(marker) == b'general-assistant-upgrade-marker', 'Lite-to-full upgrade preserves private Home data')
        if args.expect_removed:
            rows = client.data('GET', '/api/v1/extensions/subagents')['items']
            check(not any(row['directory_id'] == 'general' for row in rows) and not agent_tool()['available'],
                'Full APK upgrade respects the earlier explicit uninstall')
            source = Path(__file__).resolve().parents[1]/'app/default-home/subagents/general'
            manifest = json.loads((source/'extension.json').read_text(encoding='utf-8'))
            client.data('PUT', '/api/v1/extensions/subagents/general', dict(manifest=manifest,
                edits=[dict(path='agent.md', text=(source/'agent.md').read_text(encoding='utf-8'))],
                expected_revision='', trust_code=True))
            role = get_role()
            client.data('POST', '/api/v1/extensions/subagents/general/actions', dict(action='restore_builtin',
                expected_revision=role['revision'], trust_code=True))
            plan('enable')
        role = get_role()
        check(role['id'] == 'subagent:general' and role['source'] == 'builtin' and role['effective_enabled']
            and role['editable'] and role['builtin_restorable'] and agent_tool()['available'],
            'Ordinary builtin general assistant is loaded and editable')
        roles = client.data('GET', '/api/v1/agents')['items']
        native = next(row for row in roles if row['id'] == 'subagent.general')
        check(native['flags'] & 2 and not native['flags'] & (1|16), 'General assistant is a child role without nested delegation')
        workspace = client.home+'/workspace'
        client.call('fs.write', dict(path=workspace+'/AGENTS.md', text='GENERAL_RULE_SENTINEL: Preserve other contributors work.\n'))

        _, headers, body = client.request('GET', '/api/v1/models/config')
        config = body['data']
        provider = dict(id='android-fixture', name='Offline general assistant fixture', builtin=False,
            editable=True, removable=True, verify_peer=True, timeout_ms=15000, proxy={'kind':'none'},
            endpoints={'chat_completions':f'http://127.0.0.1:{port}/v1/chat/completions'})
        model = json.loads(json.dumps(config['items'][0]))
        model.update(id='android-fixture', name='Offline fixture', provider=provider['id'],
            wire_model='fixture-general-wire', builtin=False, free=False, editable=True, removable=True,
            protocols=['openai-chat-completions'], default_protocol='openai-chat-completions',
            capabilities=['text-input','text-output','streaming','tool-call-output','tool-result-input','reasoning-control'],
            window=dict(mode='shared-context',context_tokens=64000,max_input_tokens=60000,
                max_output_tokens=4096,output_reserve_tokens=1024,summary_tokens=1024),
            reasoning_efforts=['default','none','high'],default_reasoning_effort='high',attachments=[])
        patch = {key: config[key] for key in ('providers','items','default_model')}
        patch['providers'] = [row for row in patch['providers'] if row['id'] != provider['id']] + [provider]
        patch['items'] = [row for row in patch['items'] if row['id'] != model['id']] + [model]
        client.data('POST', '/api/v1/models/setup', dict(patch=patch,
            keys=[dict(provider=provider['id'], value='offline-fixture-only')]), extra={'If-Match':headers['etag']})

        for mode in ('foreground','background','readonly','failure','cancel','lifecycle'):
            session = client.data('POST', '/api/v1/sessions', dict(project_id='default',
                title='Android general '+mode,agent_id='mdo.default',model_id=model['id'],
                protocol='openai-chat-completions',reasoning_effort='high',max_output_tokens=1024,
                permission_profile='read-only' if mode == 'readonly' else 'full-access',workspace_root=workspace))
            started = client.data('POST', '/api/v1/projects/default/sessions/'+session['id']+'/runs',
                dict(prompt=f'QA_ROOT_{mode}: PARENT_PRIVATE_CONTEXT. Delegate the fixture task to the general assistant.'))
            run = started['id']
            if mode == 'cancel':
                assert Model.entered.wait(10), 'Child did not reach cancellation fixture'
                client.data('DELETE', '/api/v1/runs/'+run)
                Model.release.set()
            if mode == 'lifecycle':
                assert AndroidModel.lifecycle_entered.wait(10), 'Child did not reach lifecycle fixture'
                pid = client.shell('pidof',client.package)
                client.shell('am','start','-a','android.intent.action.MAIN','-c','android.intent.category.HOME')
                AndroidModel.lifecycle_release.set()
            result = wait_run(run, approve=mode == 'readonly')
            if mode == 'lifecycle':
                client.shell('am','start','-n',client.package+'/org.xserver.android.XsActivity')
                check(client.shell('pidof',client.package) == pid, 'Background-to-foreground keeps the native process')
            check(result['state'] == ('cancelled' if mode == 'cancel' else 'succeeded') and not Model.errors,
                mode+' delegation reaches the expected terminal state')
            if mode in ('foreground','background','lifecycle'):
                check(client.private_file(workspace+'/result-'+mode+'.txt') == b'delegated-ok', mode+' child actually wrote its result')
            report['runs'][mode] = dict(session=session['id'], run=run, state=result['state'],
                root_requests=len(Model.requests[mode]['root']),child_requests=len(Model.requests[mode]['child']))
        check(all(Model.requests[mode]['child'] for mode in report['runs']),
            'Every mode exercised an actual native child Agent')
        check(not Model.errors,'Child model, effort, output budget, project rules, context isolation and permission assertions pass')
        client.launch()
        check(get_role()['effective_enabled'] and agent_tool()['available'] and client.private_file(marker) == b'general-assistant-upgrade-marker',
            'Cold restart preserves data and default assistant availability')
        role = get_role()
        original = client.private_file(client.home+'/subagents/general/agent.md')
        edited = original.decode('utf-8')+'\nANDROID_USER_ROLE_CHANGE\n'
        client.data('PUT','/api/v1/extensions/subagents/general',dict(manifest=role['manifest'],
            edits=[dict(path='agent.md',text=edited)],expected_revision=role['revision'],trust_code=True))
        plan('disable')
        client.launch()
        role = get_role()
        check(role['source'] == 'manual' and not role['desired_enabled']
            and client.private_file(client.home+'/subagents/general/agent.md').decode('utf-8') == edited,
            'Edited assistant and explicit disabled state survive restart')
        status,_,_ = client.request('POST','/api/v1/extensions/subagents/general/actions',
            dict(action='restore_builtin',expected_revision='0'*64,trust_code=True))
        check(status != 200 and get_role()['revision'] == role['revision'], 'Stale restore cannot replace edited files')
        client.data('POST','/api/v1/extensions/subagents/general/actions',
            dict(action='restore_builtin',expected_revision=role['revision'],trust_code=True))
        role = get_role()
        check(role['source'] == 'builtin' and not role['desired_enabled']
            and client.private_file(client.home+'/subagents/general/agent.md') == original,
            'Explicit restore recovers exact builtin bytes and keeps the disabled state')
        plan('enable')
        if args.edition == 'lite':
            plan('remove')
            client.launch()
            rows = client.data('GET','/api/v1/extensions/subagents')['items']
            check(not any(row['directory_id'] == 'general' for row in rows) and not agent_tool()['available'],
                'Uninstall survives cold restart without a hidden fallback')
        check(not client.data('GET','/api/v1/diagnostics').get('items'), 'No pending application warnings')
        pid = client.shell('pidof',client.package)
        logs = client.adb('logcat','-d','-v','brief','--pid',pid).decode('utf-8',errors='replace')
        crashes = [row for row in logs.splitlines() if any(key in row for key in ('FATAL EXCEPTION','Fatal signal','SIGSEGV','SIGABRT','ANR in'))]
        (args.evidence/'crash-lines.txt').write_text('\n'.join(crashes),encoding='utf-8')
        check(not crashes,'Current QA process has no crash or ANR evidence')
        report['passed'] = True
    except Exception:
        (args.evidence/'failure.txt').write_text(traceback.format_exc(),encoding='utf-8')
        raise
    finally:
        Model.release.set()
        AndroidModel.lifecycle_release.set()
        client.close()
        if owned_reverse:
            client.adb('reverse','--remove',mapping,check=False)
        if worker.is_alive():
            server.shutdown()
            worker.join(timeout=3)
        server.server_close()
        report['model_fixture_errors'] = Model.errors
        (args.evidence/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
