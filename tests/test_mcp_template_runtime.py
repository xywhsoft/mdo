"""Real native template installation/configuration, kept cold and isolated."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import uuid
import tempfile
import time
import urllib.error
import urllib.request
from mcp_template_fixture import template

ROOT = Path(__file__).resolve().parents[1]


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/windows-server-glibc/xs.exe')
    parser.add_argument('--packed', action='store_true')
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix='mcp-template-', dir=ROOT / '.build'))
    app, home = base / 'app', base / 'home'
    if args.packed: app=base
    else: shutil.copytree(ROOT / 'app', app)
    home.mkdir()
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
    config = app / 'xs.json'
    config.write_text(json.dumps(dict(services=[dict(enabled=True, name='fixture', ip='127.0.0.1', port=port,
        **{'class': 'http'}, host_default=dict(enabled=True, name='mdo', path='web', devlang='c', devfile='generated/mdo_unity.c'))])))
    origin = f'http://127.0.0.1:{port}'
    token = ''
    def api(method, path, body=None, status=200, revision=None):
        headers = {'Content-Type': 'application/json'}
        if method != 'GET': headers['X-Mdo-Write-Token'] = token
        if revision: headers['If-Match'] = '"' + revision + '"'
        req = urllib.request.Request(origin + '/api/v1' + path, data=json.dumps(body).encode() if body is not None else None, method=method, headers=headers)
        try: response = urllib.request.urlopen(req, timeout=40)
        except urllib.error.HTTPError as e: response = e
        raw = response.read(); assert response.status == status, (path, response.status, raw[:2000], str(base))
        return json.loads(raw).get('data'), response.headers
    host=args.host.resolve()
    if args.packed: shutil.copy2(host,base/'mdo.exe');host=base/'mdo.exe'
    with (base / 'runtime.log').open('wb') as log:
        child = subprocess.Popen([str(host), *([] if args.packed else [str(config)]), "--", "--home", str(home)], cwd=app, env=dict(os.environ, MDO_HOME=str(home), USE_WEBVIEW='0', PRIVATE_CLIENT_SECRET='private-fixture', PRIVATE_HEADER='private-fixture'), stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            for _ in range(400):
                if child.poll() is not None: break
                try:
                    _, h = api('GET', '/account'); token = h['X-Mdo-Write-Token']; break
                except (OSError, AssertionError): time.sleep(.1)
            assert token, (base / 'runtime.log').read_text(errors='replace')[-6000:]
            t = template('service')
            package = dict(format='mdo.extension.v1', manifest=dict(slug='mcp-fixture', name='Fixture', version='1.0.0', description='Private template', readme='Inspect the endpoint.', license='MIT', platforms=['windows-x86_64']), resources=[dict(kind='mcp', id='service', content=json.dumps(t))])
            def store(action, **extra): return api('POST', '/ecosystem', dict(action=action, single_kind='mcp', **extra))[0]
            plan = store('plan_import', package=package)
            store('import', package=package, expected_plan=plan['sha256'])
            sdk=api('POST','/application/call',dict(method='mcp.templates.list',params={}))[0];assert sdk['status']==200 and len(sdk['data']['items'])==1
            installed = api('GET', '/mcp/templates')[0]['items']; assert len(installed) == 1 and installed[0]['template'] == t
            # Populate a store receipt as an isolated download stand-in. Every
            # installed byte still passes the ordinary receipt hash checks.
            receipts = home / 'data/extensions/installed.json'
            records = json.loads(receipts.read_text())
            receipt = next(iter(records.values())); receipt.update(id=71, source='store', owner=7, submitter='fixture_author')
            receipts.write_text(json.dumps({'71': receipt}))
            body = dict(id='personal', action='preview', receipt_id=71, variant='default', values={})
            preview = api('POST', '/mcp/connections', body, revision='new')[0]
            assert preview['config']['enabled'] is False
            assert not (home / 'mcp/personal.json').exists()
            body['action'] = 'save'
            first = api('POST', '/mcp/connections', body, revision='new')[0]
            assert first['template_connection'] and first['store_managed'] and first['submitter'] == 'fixture_author'
            raw = json.loads((home / 'mcp/personal.json').read_text()); assert raw['enabled'] is False
            second = api('POST', '/mcp/connections', dict(body, id='company'), revision='new')[0]
            assert second['id'] == 'company'
            api('PUT', '/extensions/mcp/personal', dict(content=json.dumps(raw)), status=403, revision=first['revision'])
            api('POST', '/ecosystem', dict(action='export', single_kind='mcp', references=[dict(kind='mcp', id='personal')]), status=403)
            api('POST', '/mcp/connections', dict(body, name='Changed'), status=412, revision='0'*64)
            saved = api('POST', '/mcp/connections', dict(body, name='Changed'), revision=first['revision'])[0]
            assert saved['name'] == 'Changed'
            api('POST', '/mcp/connections', dict(id='personal', action='delete'), revision=saved['revision'])
            assert not (home / 'mcp/personal.json').exists() and (home / 'mcp/company.json').exists()
            # Project scope uses its registered root and does not shadow other
            # projects or implicitly connect on save.
            project = api('POST', '/mcp/connections?project=default', dict(body, id='project-instance'), revision='new')[0]
            assert project['scope'] == 'project' and project['template_connection']
            api('PUT', '/mcp/project/project-instance?project=default', dict(content=project['content']), status=403, revision=project['revision'])
            api('POST', '/mcp/connections?project=default', dict(id='project-instance', action='delete'), revision=project['revision'])
            # API credentials are sealed once, never returned by preview/export.
            key_package = copy.deepcopy(package); key_package['manifest']['slug'] = 'key-template'; key_package['resources'][0]['id'] = 'keyservice'
            key_package['resources'][0]['content'] = json.dumps(template('keyservice', 'key'))
            store('import', package=key_package)
            records = json.loads(receipts.read_text()); key_receipt = records.pop('local:key-template'); key_receipt.update(id=72, source='store', owner=7); records['72'] = key_receipt; receipts.write_text(json.dumps(records))
            secret = 'Private-fixture-credential-2026'
            key_body = dict(id='private-key', action='save', receipt_id=72, variant='default', values=dict(token=secret))
            key_preview = api('POST', '/mcp/connections', dict(key_body,action='preview'), revision='new')[0]
            assert secret not in json.dumps(key_preview)
            sealed = api('POST', '/mcp/connections', key_body, revision='new')[0]
            assert secret not in json.dumps(sealed) and secret not in (home / 'mcp/private-key.json').read_text()
            retained = api('POST', '/mcp/connections', dict(key_body, values={}), revision=sealed['revision'])[0]
            assert json.loads(retained['content'])['transport']['headers'] == json.loads(sealed['content'])['transport']['headers']
            # A target change must not forward a previously bound key.
            changed = copy.deepcopy(key_package); changed['manifest']['slug'] = 'changed-target'; changed['resources'][0]['content'] = changed['resources'][0]['content'].replace('example.invalid', 'other.invalid')
            store('import', package=changed)
            records = json.loads(receipts.read_text()); r = records.pop('local:changed-target'); r.update(id=73, source='store', owner=7); records['73'] = r; receipts.write_text(json.dumps(records))
            api('POST', '/mcp/connections', dict(key_body, receipt_id=73, values={}), revision=retained['revision'], status=409)
            assert 'other.invalid' not in (home / 'mcp/private-key.json').read_text()
            ref=json.loads(retained['content'])['mdo_template']['values']['token'];vault_file=home/('config/secrets/models/'+ref[6:]+'.key');assert vault_file.exists()
            api('POST','/mcp/connections',dict(id='private-key',action='delete'),revision=retained['revision']);assert not vault_file.exists()
            # Custom exports remove URL/argument/OAuth data and declare blank
            # parameters rather than copying any original device values.
            custom = copy.deepcopy(t['variants'][0]['config']); custom.update(id='custom-export', enabled=False, oauth=dict(enabled=True, client_id=secret, client_secret_ref='env:PRIVATE_CLIENT_SECRET'))
            custom['transport']['endpoint'] += '?token=' + secret
            custom['transport']['headers'] = [dict(name='X-Custom', secret_ref='env:PRIVATE_HEADER')]
            api('PUT', '/extensions/mcp/custom-export', dict(content=json.dumps(custom)), revision='new')
            exported = store('export', references=[dict(kind='mcp', id='custom-export')])
            public = json.dumps(exported); assert secret not in public and 'PRIVATE_HEADER' not in public and 'PRIVATE_CLIENT_SECRET' not in public
            assert all('default' not in i for i in json.loads(exported['resources'][0]['content'])['inputs'])
            optional=copy.deepcopy(key_package);optional['manifest']['slug']='optional-key';ot=template('optional-key','key');ot['inputs'][0]['required']=False;optional['resources'][0].update(id='optional-key',content=json.dumps(ot));store('import',package=optional)
            empty=api('POST','/mcp/connections',dict(action='save',id='optional',receipt_key='local:optional-key',variant='default',values={}),revision='new')[0];assert not json.loads(empty['content'])['transport']['headers']
            bound=api('POST','/mcp/connections',dict(action='save',id='optional',values=dict(token=secret)),revision=empty['revision'])[0]
            cleared=api('POST','/mcp/connections',dict(action='save',id='optional',values={},clear_secrets=['token']),revision=bound['revision'])[0];assert not json.loads(cleared['content'])['transport']['headers']
            # Local stdio recipes require explicit command consent. Saving and
            # enabling remain cold; only an explicit test starts the peer.
            stdio_package=copy.deepcopy(package);stdio_package['manifest']['slug']='stdio-template';stdio_package['resources'][0]['id']='cli-service'
            cli=template('cli-service','stdio');record=base/'stdio-record.jsonl';cli['variants'][0]['config']['transport']['arguments']=[str(ROOT/'tests/fixtures/mcp-2025.py'),str(record)];stdio_package['resources'][0]['content']=json.dumps(cli)
            store('import',package=stdio_package)
            cli_body=dict(action='save',id='cli',receipt_key='local:stdio-template',variant='default',values=dict(program=sys.executable))
            api('POST','/mcp/connections',cli_body,revision='new',status=409)
            saved_cli=api('POST','/mcp/connections',dict(cli_body,confirm_launch=True),revision='new')[0];assert not record.exists()
            api('POST','/extensions/mcp/cli/enabled',dict(enabled=True),revision=saved_cli['revision']);assert not record.exists()
            api('POST','/mcp/cli/refresh',dict(operation_id='op-c-'+uuid.uuid4().hex),status=202)
            for _ in range(80):
                status=api('GET','/extensions/mcp/cli')[0]
                if status.get('connected'):break
                time.sleep(.1)
            assert status['connected'] and status['tool_count']==1 and record.exists(),status
            api('POST','/mcp/connections',dict(action='delete',id='cli'),revision=status['revision'])
            # Invalid public shapes fail before any writes.
            for mutate in [lambda x: x.update(token='secret'), lambda x: x['variants'][0]['config']['transport'].update(endpoint='https://user:secret@example.invalid/mcp')]:
                invalid = copy.deepcopy(package); d = json.loads(invalid['resources'][0]['content']); mutate(d); invalid['resources'][0]['content'] = json.dumps(d)
                api('POST', '/ecosystem', dict(action='plan_import', single_kind='mcp', package=invalid), status=409)
            print('PASS native template install, independent connections, cold preview/save, ownership, CAS, deletion and rejection')
        finally:
            child.terminate(); child.wait(timeout=10)


if __name__ == '__main__': run()
