#!/usr/bin/env python3
"""Small real HTTP probe for the four file-backed extension editors."""
import argparse
import base64
import hashlib
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
SKILL_PARAMETERS = r'"C:\new folder\章节.txt" "检查 $0 与 ${MDO_SKILL_DIR}" ""'
SKILL_PARAMETER_BODY = 'ARG_MARKER=$file|$focus|$2\nRAW=$ARGUMENTS\nINDEX=$ARGUMENTS[0]'
SKILL_PARAMETER_RENDERED = ('ARG_MARKER=C:\\new folder\\章节.txt|检查 $0 与 ${MDO_SKILL_DIR}|\nRAW='
    + SKILL_PARAMETERS + '\nINDEX=C:\\new folder\\章节.txt')
BUILDER_DOCUMENT = '---\nname: builder-fixture\ndescription: Local builder regression\n---\nReturn a short checklist for $ARGUMENTS.'

class SkillModel(BaseHTTPRequestHandler):
    calls = 0
    payloads = []
    delegation_payloads = []
    tool_payloads = []
    profile_payloads = []
    mcp_payloads = []
    builder_payloads = []
    scope_payloads = []
    disabled_scope_payloads = []
    scope_server = ''
    def log_message(self, *_): pass
    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        users = [item for item in payload.get('input', []) if item.get('role') == 'user']
        latest = json.dumps(users[-1] if users else payload)
        if 'EXTENSION builder probe' in latest:
            type(self).builder_payloads.append(payload)
            calls=len(type(self).builder_payloads)
            steps=[('skill',{'name':'skill-creator'}),
                ('skill',{'action':'validate','content':'missing frontmatter'}),
                ('skill',{'action':'validate','content':BUILDER_DOCUMENT}),
                ('write',{'path':'.mdo/skills/builder-fixture/SKILL.md','content':BUILDER_DOCUMENT,'mode':'create'}),
                ('skill',{'action':'validate','name':'builder-fixture','scope':'project'})]
            output=[{'type':'function_call','call_id':f'builder-{calls}','name':steps[calls-1][0],
                'arguments':json.dumps(steps[calls-1][1])}] if calls<=len(steps) else [
                {'type':'message','content':[{'type':'output_text','text':'Builder probe complete'}]}]
        elif 'EXTENSION MCP disabled scope probe' in latest:
            type(self).disabled_scope_payloads.append(payload)
            calls=len(type(self).disabled_scope_payloads)
            output = [{'type':'function_call','call_id':'disabled-scope','name':'tool_search','arguments':'{}'}] if len(type(self).disabled_scope_payloads)==1 else [
                {'type':'message','content':[{'type':'output_text','text':'Disabled scope complete'}]}]
        elif 'EXTENSION MCP scope probe' in latest:
            type(self).scope_payloads.append(payload); calls=len(type(self).scope_payloads)
            steps=[('tool_search',{}),('tool_load',{'server':'legacy','tool':'echo'}),
                ('tool_load',{'server':type(self).scope_server,'tool':'echo'})]
            if calls<=3: name,arguments=steps[calls-1]
            elif calls==4:
                loaded=[tool for tool in payload['tools'] if tool['name'].startswith('mcp__')]
                assert len(loaded)==1,loaded
                name,arguments=loaded[0]['name'],{'text':'PROJECT_SCOPE_RESULT'}
            output=[{'type':'function_call','call_id':f'scope-{calls}','name':name,'arguments':json.dumps(arguments)}] if calls<=4 else [
                {'type':'message','content':[{'type':'output_text','text':'Project scope complete'}]}]
        elif 'EXTENSION MCP probe' in latest:
            type(self).mcp_payloads.append(payload)
            calls = len(type(self).mcp_payloads)
            if calls <= 2:
                name = 'tool_search' if calls == 1 else 'tool_load'
                arguments = {'server': 'legacy', 'query': 'echo'} if calls == 1 else {'server': 'legacy', 'tool': 'echo'}
            elif calls == 3:
                loaded = [tool for tool in payload['tools'] if tool['name'].startswith('mcp__')]
                assert len(loaded) == 1, 'only the requested MCP schema is loaded'
                name, arguments = loaded[0]['name'], {'text': 'MCP_CALLBACK_RESULT'}
            output = [{'type': 'function_call', 'call_id': f'mcp-{calls}', 'name': name,
                'arguments': json.dumps(arguments)}] if calls <= 3 else [
                {'type': 'message', 'content': [{'type': 'output_text', 'text': 'MCP probe complete'}]}]
        elif 'EXTENSION profile probe' in latest:
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
            arguments = ({'query':'RESEARCH', 'offset':0, 'limit':1},
                {'name': 'research', 'arguments': SKILL_PARAMETERS}, {'name': 'research', 'path': 'references/guide.txt'})
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
    parser.add_argument('--record', type=Path, help='save a compact successful verification receipt')
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
        foreign=base/'foreign';foreign.mkdir()
        foreign_sources={
            '.claude.json':json.dumps({'mcpServers':{'imported-stdio':{'command':sys.executable,
                'args':['-c',f'from pathlib import Path; Path({str(base / "import-executed")!r}).touch()'],
                'env':{'EXTERNAL_TOKEN':'foreign-secret-claude'}}}}),
            '.codex/config.toml':f'[mcp_servers.imported-codex]\ncommand = {json.dumps(sys.executable)}\nargs = ["-c", "pass"]\nstartup_timeout_sec = 2\ntool_timeout_sec = 12\n[mcp_servers.imported-codex.env]\nEXTERNAL_TOKEN = \'foreign-secret-codex\'\n',
            '.zcode/cli/config.json':json.dumps({'mcp':{'servers':{'imported-zcode':{
                'url':'http://127.0.0.1:9911/mcp','headers':{'Authorization':'Bearer foreign-secret-zcode'}}}}}),
            '.config/opencode/opencode.json':json.dumps({'mcp':{'imported-open':{
                'type':'local','command':[sys.executable,'-c','pass'],'environment':{'TOKEN':'foreign-secret-open'}}}}),
            '.agents/mcp.json':json.dumps({'mcpServers':{'legacy-sse':{'type':'sse','url':'https://example.invalid/sse'}}}),
        }
        for name,text in foreign_sources.items():
            file=foreign/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(text,encoding='utf-8')
        env.update(USERPROFILE=str(foreign),HOME=str(foreign))
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
                assert len(builtins) == 22 and 'ask_user' in builtins and 'mcp_read' in builtins, builtins
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
                skill = '---\nname: research\ndescription: >\n  Research a project\n  before changes.\narguments: [file, focus]\nargument-hint: "[file] [focus]"\nmetadata:\n  author: test\n---\nUse references/guide.txt.\n' + SKILL_PARAMETER_BODY
                file = {'path': 'references/guide.txt', 'base64': base64.b64encode(b'Inspect first').decode()}
                status, data = call('PUT', 'skills/research', {'content': skill, 'files': [file]}, 'new')
                assert status == 200, data
                assert (base / 'home/skills/research/references/guide.txt').read_bytes() == b'Inspect first'
                skill_revision = data['data']['revision']
                skills = json.loads(request(port, 'GET', '/api/v1/skills')[2])['data']['items']
                assert any(item['id'] == 'research' and item['resource_count'] == 1 for item in skills), skills
                parameters = next(item for item in skills if item['id'] == 'research')
                assert parameters['arguments'] == ['file', 'focus'] and parameters['argument_hint'] == '[file] [focus]', parameters
                status, _, raw = request(port, 'POST', '/api/v1/skills/invoke',
                    body=json.dumps({'id':'research','arguments':SKILL_PARAMETERS}).encode(),
                    headers={'Content-Type':'application/json'})
                assert status == 200, raw
                selected=json.loads(raw)['data']['text']
                expected_instructions = 'Use references/guide.txt.\n' + SKILL_PARAMETER_RENDERED
                assert expected_instructions in selected and selected.endswith(SKILL_PARAMETERS), selected
                status, _, raw = request(port, 'POST', '/api/v1/skills/invoke',
                    body=b'{"id":"research"}', headers={'Content-Type':'application/json'})
                assert status == 200 and 'ARG_MARKER=||$2' in json.loads(raw)['data']['text'], raw
                for invalid in (42, 'bad\0input', 'x' * 16385):
                    assert request(port, 'POST', '/api/v1/skills/invoke',
                        body=json.dumps({'id':'research','arguments':invalid}).encode(),
                        headers={'Content-Type':'application/json'})[0] == 400
                assert call('PUT', 'skills/invalid-arguments',
                    {'content': skill.replace('[file, focus]', '[file, file]')}, 'new')[0] == 422
                assert not (base / 'home/skills/invalid-arguments').exists()
                status, invalid = call('PUT', 'skills/invalid-arguments',
                    {'content': skill.replace('arguments: [file, focus]', 'arguments:\n  file: required')}, 'new')
                assert status == 422 and 'arguments' in json.dumps(invalid), invalid
                # Validation reports do not publish or activate a draft.
                def validate_skill(data, project=''):
                    status, _, raw=request(port,'POST','/api/v1/skills/validate'+project,
                        body=json.dumps(data).encode(),headers={'Content-Type':'application/json'})
                    return status,json.loads(raw)
                status, checked=validate_skill({'content':BUILDER_DOCUMENT})
                assert status==200 and checked['data']['valid'] and not checked['data']['resources_checked'],checked
                assert not checked['data']['behavior_tested'] and not (base/'home/skills/builder-fixture').exists()
                status, checked=validate_skill({'content':'invalid'})
                assert status==200 and not checked['data']['valid'] and checked['data']['errors'],checked
                assert validate_skill({'name':'../outside','scope':'user'})[0]==400
                assert validate_skill({'name':'research','scope':'project'})[0]==400
                status, checked=validate_skill({'name':'research','scope':'user'})
                assert status==200 and checked['data']['valid'] and checked['data']['resources_checked'],checked
                status, checked=validate_skill({'name':'absent','scope':'user'})
                assert status==200 and not checked['data']['valid'] and checked['data']['errors'],checked
                status, _, creator_draft=request(port,'POST','/api/v1/skills/invoke',
                    body=b'{"id":"skill-creator","arguments":"CREATOR_REQUEST_MARKER"}',headers={'Content-Type':'application/json'})
                assert status==200,creator_draft
                creator_text=json.loads(creator_draft)['data']['text']
                assert '默认使用 `$ARGUMENTS`' in creator_text and creator_text.count('CREATOR_REQUEST_MARKER')==2,creator_text
                scoped=base/'home/workspace/.mdo/skills/research/SKILL.md'
                scoped.parent.mkdir(parents=True,exist_ok=True)
                scoped.write_text(skill.replace('Use references/guide.txt.','PROJECT_SCOPE_MARKER'),encoding='utf-8')
                scoped_data=json.loads(request(port,'GET','/api/v1/skills?project=default')[2])['data']
                assert any(i['id']=='research' and i['scope']=='project' for i in scoped_data['items']),scoped_data
                status,_,raw=request(port,'POST','/api/v1/skills/invoke?project=default',body=b'{"id":"research"}',headers={'Content-Type':'application/json'})
                assert status==200 and 'PROJECT_SCOPE_MARKER' in json.loads(raw)['data']['text'],raw[:400]
                assert request(port,'GET','/api/v1/skills?project=missing')[0]==404
                assert request(port,'GET','/api/v1/skills?project=default&path=bad')[0]==400
                scoped.unlink(); scoped.parent.rmdir()
                project_api='/api/v1/skills/project'
                project_doc='---\nname: Created\ndescription: A project Skill\n---\nFirst version'
                def project_write(id,content,match='new'):
                    return request(port,'PUT',f'{project_api}/{id}?project=default',body=json.dumps({'content':content}).encode(),headers={'Content-Type':'application/json','If-Match':f'"{match}"'})
                status,_,raw=project_write('created',project_doc)
                assert status==200,raw
                project_revision=json.loads(raw)['data']['revision']
                assert project_write('created',project_doc+' changed')[0]==412
                status,_,raw=project_write('created',project_doc+' changed',project_revision)
                assert status==200,raw
                project_revision=json.loads(raw)['data']['revision']
                invalid_resources=project_doc.replace('description: A project Skill','description: A project Skill\nscripts: [scripts/missing.txt]')
                assert project_write('created',invalid_resources,project_revision)[0]==422
                status,_,raw=request(port,'GET',f'{project_api}/created?project=default')
                assert status==200 and json.loads(raw)['data']['content']==project_doc+' changed',raw
                assert request(port,'POST',f'{project_api}/created/enabled?project=default',body=b'{"enabled":false}',headers={'Content-Type':'application/json','If-Match':f'"{project_revision}"'})[0]==200
                listed=json.loads(request(port,'GET',project_api+'?project=default')[2])['data']['items']
                assert any(i['id']=='created' and i['enabled'] is False for i in listed),listed
                assert not any(i['id']=='created' for i in json.loads(request(port,'GET','/api/v1/skills?project=default')[2])['data']['items'])
                status,_,raw=request(port,'DELETE',f'{project_api}/created?project=default',headers={'If-Match':f'"{project_revision}"'})
                assert status==200,raw
                recovery=json.loads(raw)['data']['recovery_path']
                assert (base/'home/workspace'/recovery/'SKILL.md').read_text(encoding='utf-8')==project_doc+' changed'
                assert request(port,'GET',f'{project_api}/created?project=default')[0]==404
                source=base/'home/workspace/.claude/skills/portable'
                source.mkdir(parents=True)
                (source/'SKILL.md').write_text(project_doc,encoding='utf-8')
                (source/'references').mkdir()
                payload=b'resource payload'*50000
                (source/'references/large.bin').write_bytes(payload)
                bad=source.parent/'unsupported';bad.mkdir();(bad/'SKILL.md').write_text(project_doc.replace('name: Created','name: Created\ncontext: fork'),encoding='utf-8')
                status,_,raw=request(port,'GET','/api/v1/skills/sources?project=default')
                assert status==200,raw[:400]
                sources=json.loads(raw)['data']
                assert any(i['id']=='portable' and i['origin']=='project' and i['provider']=='claude' for i in sources['items']),sources
                assert any('context' in i['message'] for i in sources['diagnostics']),sources
                def migrate(id,scope):
                    return request(port,'POST','/api/v1/skills/migrate?project=default',body=json.dumps({'origin':'project','provider':'claude','id':'portable','target_id':id,'scope':scope}).encode(),headers={'Content-Type':'application/json'})
                status,_,raw=migrate('imported-project','project');assert status==200,raw
                status,_,raw=migrate('imported-user','user');assert status==200,raw
                assert migrate('imported-user','user')[0]==409
                assert migrate('../escape','user')[0]==409
                assert migrate('1-invalid','user')[0]==409
                assert (base/'home/workspace/.mdo/skills/imported-project/references/large.bin').read_bytes()==payload
                assert (base/'home/skills/imported-user/references/large.bin').read_bytes()==payload
                assert (source/'references/large.bin').read_bytes()==payload
                large='---\nname: large\ndescription: A large instruction document\n---\n'+'x'*60000
                assert call('PUT','skills/large',{'content':large},'new')[0] == 200
                status, _, raw=request(port,'POST','/api/v1/skills/invoke',body=json.dumps({'id':'large'}).encode(),headers={'Content-Type':'application/json'})
                assert status == 200 and 'x'*60000 in json.loads(raw)['data']['text'], raw[:400]
                assert call('PUT','skills/1-invalid',{'content':large},'new')[0]==422
                escaped='---\nname: escaped\ndescription: JSON escaping fixture\n---\n'+'\"'*131000
                assert call('PUT','skills/escaped',{'content':escaped},'new')[0]==200
                status,_,raw=request(port,'POST','/api/v1/skills/invoke',body=b'{"id":"escaped"}',headers={'Content-Type':'application/json'})
                assert status==200 and '\"'*131000 in json.loads(raw)['data']['text'],raw[:400]
                assert call('PUT','skills/oversized',{'content':large+'x'*80000},'new')[0] in (413,422)
                manual='---\nname: manual\ndescription: MANUAL_ONLY_MARKER\ndisable-model-invocation: true\n---\nTask: $ARGUMENTS\nFiles: ${MDO_SKILL_DIR}'
                assert call('PUT','skills/manual',{'content':manual},'new')[0] == 200
                status, _, raw=request(port,'POST','/api/v1/skills/invoke',body=json.dumps({'id':'manual','arguments':'literal $&'}).encode(),headers={'Content-Type':'application/json'})
                assert status == 200 and 'Task: literal $&' in json.loads(raw)['data']['text'] and '${MDO_SKILL_DIR}' not in json.loads(raw)['data']['text'], raw[:400]
                automatic=manual.replace('disable-model-invocation: true','user-invocable: false').replace('MANUAL_ONLY_MARKER','Automatic task')
                assert call('PUT','skills/automatic',{'content':automatic},'new')[0] == 200
                assert request(port,'POST','/api/v1/skills/invoke',body=b'{"id":"automatic"}',headers={'Content-Type':'application/json'})[0] == 403
                assert call('PUT','skills/forked',{'content':manual.replace('disable-model-invocation: true','context: fork')},'new')[0] == 422
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
                discovered = [part['output'] for part in SkillModel.payloads[1]['input'] if part.get('type') == 'function_call_output']
                assert discovered and 'status: success\ntool: skill\n{' in discovered[-1], discovered
                page = json.loads(discovered[-1][discovered[-1].index('{'):])
                assert page['total'] == 1 and page['items'][0]['name'] == 'research' and page['next_offset'] == 1, page
                assert page['items'][0]['argument_names'] == ['file', 'focus'] and page['items'][0]['argument_hint'] == '[file] [focus]', page
                loaded = [part['output'] for part in SkillModel.payloads[2]['input'] if part.get('type') == 'function_call_output'][-1]
                loaded = json.loads(loaded[loaded.index('{'):])
                assert loaded['instructions'] == expected_instructions, loaded
                assert loaded['arguments'] == SKILL_PARAMETERS and loaded['argument_names'] == ['file', 'focus'], loaded
                assert 'mdo_available_skills' in json.dumps(SkillModel.payloads[0]), 'Skill metadata missing'
                assert 'MANUAL_ONLY_MARKER' not in json.dumps(SkillModel.payloads[0]).replace('Automatic',''), 'manual-only Skill advertised'
                assert 'Use references/guide.txt.' not in json.dumps(SkillModel.payloads[0]), 'Skill body eagerly injected'
                assert 'Inspect first' in json.dumps(SkillModel.payloads[-1]), SkillModel.payloads[-1]
                status, _, raw_run=request(port,'POST','/api/v1/projects/default/sessions/'+session+'/runs',
                    body=json.dumps({'prompt':'EXTENSION builder probe','timeout_ms':5000}).encode(),
                    headers={'Content-Type':'application/json'})
                assert status==202,raw_run
                run=json.loads(raw_run)['data'];deadline=time.monotonic()+8
                while not run['terminal'] and time.monotonic()<deadline:
                    time.sleep(.05)
                    run=json.loads(request(port,'GET','/api/v1/runs/'+run['id'])[2])['data']
                assert run['state']=='succeeded' and len(SkillModel.builder_payloads)==6,run
                assert (base/'home/workspace/.mdo/skills/builder-fixture/SKILL.md').read_text(encoding='utf-8')==BUILDER_DOCUMENT
                def builder_result(index):
                    outputs=[part['output'] for part in SkillModel.builder_payloads[index]['input'] if part.get('type')=='function_call_output']
                    value=outputs[-1];return json.loads(value[value.index('{'):])
                assert 'Skill 构建器' in builder_result(1)['instructions']
                assert builder_result(2)['valid'] is False and builder_result(2)['errors']
                assert builder_result(3)['valid'] is True and builder_result(3)['behavior_tested'] is False
                assert builder_result(5)['valid'] is True and builder_result(5)['resources_checked'] is True
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
                status,_,raw=request(port,'GET','/api/v1/mcp/sources?project=default')
                assert status==200 and 'foreign-secret-' not in raw.decode() and 'vault:' not in raw.decode(),raw[:300]
                source_items=json.loads(raw)['data']['items']
                assert {'imported-stdio','imported-codex','imported-zcode','imported-open','legacy-sse'} <= {item['id'] for item in source_items},source_items
                assert not next(item for item in source_items if item['id']=='legacy-sse')['compatible']
                for source in source_items:
                    if not source['compatible']:continue
                    target_scope='project' if source['id']=='imported-open' else 'user'
                    body={key:source[key] for key in ('origin','id','source_index','revision')}
                    body['scope']=target_scope
                    status,_,raw=request(port,'POST','/api/v1/mcp/migrate?project=default',body=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
                    assert status==200,(source['id'],source,raw[:500])
                    path=(base/'home/mcp' if target_scope=='user' else base/'home/workspace/.mdo/mcp')/(source['id']+'.json')
                    saved=path.read_text(encoding='utf-8')
                    assert 'foreign-secret-' not in saved and 'vault:' in saved,saved
                    if source['id']=='imported-codex':
                        doc=json.loads(saved);assert doc['startup_timeout_ms']==2000 and doc['request_timeout_ms']==12000,doc
                    assert request(port,'POST','/api/v1/mcp/migrate?project=default',body=json.dumps(body).encode(),headers={'Content-Type':'application/json'})[0]==412
                assert not (base/'import-executed').exists(),'Preview/import started a server'
                for name,text in foreign_sources.items():assert (foreign/name).read_text(encoding='utf-8')==text,name
                stale=next(item for item in source_items if item['id']=='imported-codex')
                with (foreign/'.codex/config.toml').open('a',encoding='utf-8') as output:output.write('\n# changed\n')
                body={key:stale[key] for key in ('origin','id','source_index','revision')};body.update(scope='user',target_id='must-not-import')
                assert request(port,'POST','/api/v1/mcp/migrate?project=default',body=json.dumps(body).encode(),headers={'Content-Type':'application/json'})[0]==412
                assert not (base/'home/mcp/must-not-import.json').exists()
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
                # Exercise the default 2025 protocol through the actual model
                # tool loop, not merely discovery. Schemas must stay lazy.
                legacy_log = base / 'legacy-mcp.jsonl'
                legacy = {**mcp, 'id': 'legacy', 'protocol_version': '2025-11-25',
                    'transport': {**mcp['transport'], 'environment': [],
                        'arguments': [str(ROOT / 'tests/fixtures/mcp-2025.py'), str(legacy_log)]}}
                status, legacy_item = call('PUT', 'mcp/legacy', {'content': json.dumps(legacy)}, 'new')
                assert status == 200 and not legacy_log.exists(), legacy_item
                status, _, raw = request(port, 'POST', f'/api/v1/projects/default/sessions/{session}/runs',
                    body=json.dumps({'prompt': 'EXTENSION MCP probe', 'timeout_ms': 5000}).encode(),
                    headers={'Content-Type': 'application/json'})
                assert status == 202, raw
                run = json.loads(raw)['data']; deadline = time.monotonic() + 8
                while not run['terminal'] and time.monotonic() < deadline:
                    time.sleep(.05)
                    run = json.loads(request(port, 'GET', '/api/v1/runs/' + run['id'])[2])['data']
                assert run['state'] == 'succeeded', run
                assert len(SkillModel.mcp_payloads) == 4, 'discover, load, call, then final reply'
                for payload in SkillModel.mcp_payloads[:2]:
                    assert not any(tool['name'].startswith('mcp__') for tool in payload['tools']), 'schema loaded eagerly'
                assert 'MCP_CALLBACK_RESULT' in json.dumps(SkillModel.mcp_payloads[-1]), SkillModel.mcp_payloads[-1]['input'][-2:]
                rpc = [json.loads(line) for line in legacy_log.read_text().splitlines()]
                assert [row['method'] for row in rpc] == ['initialize', 'notifications/initialized', 'tools/list', 'tools/call'], rpc
                assert rpc[-1]['params'] == {'name': 'echo', 'arguments': {'text': 'MCP_CALLBACK_RESULT'}}, rpc
                def scoped(method, suffix='', body=None, revision=None):
                    headers={'Content-Type':'application/json'}
                    if revision is not None: headers['If-Match']='"'+revision+'"'
                    status,_,raw=request(port,method,'/api/v1/mcp/project'+suffix+'?project=default',
                        body=json.dumps(body).encode() if body is not None else None,headers=headers)
                    return status,json.loads(raw)
                project_log=base/'project-mcp.jsonl'
                project_doc={**legacy,'transport':{**legacy['transport'],
                    'arguments':[str(ROOT/'tests/fixtures/mcp-2025.py'),str(project_log)]}}
                status,project_item=scoped('PUT','/legacy',{'content':json.dumps(project_doc)},'new')
                assert status==200 and not project_log.exists(),project_item
                conflict=scoped('PUT','/legacy',{'content':json.dumps(project_doc)},'new')
                assert conflict[0]==412,conflict
                SkillModel.scope_server=project_item['data']['runtime_id']
                def scope_run(prompt):
                    status,_,raw=request(port,'POST',f'/api/v1/projects/default/sessions/{session}/runs',
                        body=json.dumps({'prompt':prompt,'timeout_ms':5000}).encode(),headers={'Content-Type':'application/json'})
                    assert status==202,raw
                    run=json.loads(raw)['data'];end=time.monotonic()+8
                    while not run['terminal'] and time.monotonic()<end:
                        time.sleep(.05);run=json.loads(request(port,'GET','/api/v1/runs/'+run['id'])[2])['data']
                    assert run['state']=='succeeded',run
                scope_run('EXTENSION MCP scope probe')
                search=SkillModel.scope_payloads[1]['input'][-1]['output'];denied=SkillModel.scope_payloads[2]['input'][-1]['output']
                assert SkillModel.scope_server in search and 'server=legacy ' not in search,(SkillModel.scope_server,search)
                assert "outside this project's scope" in denied,denied
                assert 'PROJECT_SCOPE_RESULT' in json.dumps(SkillModel.scope_payloads[-1])
                status,disabled=scoped('POST','/legacy/enabled',{'enabled':False},project_item['data']['revision'])
                assert status==200,disabled
                scope_run('EXTENSION MCP disabled scope probe')
                search=SkillModel.disabled_scope_payloads[-1]['input'][-1]['output']
                assert 'server=legacy ' not in search and 'server='+SkillModel.scope_server+' ' not in search,search
                assert scoped('DELETE','/legacy',revision=disabled['data']['revision'])[0]==200
                assert call('DELETE', 'mcp/legacy', revision=legacy_item['data']['revision'])[0] == 200
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
                events = [json.loads(line) for file in (base / 'home/sessions').rglob('ui-events.jsonl')
                    for line in file.read_text(encoding='utf-8').splitlines()]
                assert not any('model_error_kind' in event for event in events), 'unexpected final model error'
                if args.record:
                    receipt = {'passed': True, 'model_requests': {
                        'custom_tool': len(SkillModel.tool_payloads), 'skill': len(SkillModel.payloads),
                        'delegation': len(SkillModel.delegation_payloads), 'profiles': len(SkillModel.profile_payloads),
                        'mcp': len(SkillModel.mcp_payloads), 'builder': len(SkillModel.builder_payloads)}, 'final_model_errors': 0,
                        'mcp_2025_methods': [row['method'] for row in rpc],
                        'mcp_schemas_lazy': True, 'packed_sha256': hashlib.sha256(args.packed.read_bytes()).hexdigest() if args.packed else None}
                    args.record.parent.mkdir(parents=True, exist_ok=True)
                    args.record.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
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

