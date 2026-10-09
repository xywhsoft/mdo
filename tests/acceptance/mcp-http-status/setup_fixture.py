"""Configure four disposable HTTP MCP targets using standard product writes."""
import argparse
import json
from pathlib import Path
import urllib.request
import urllib.error

parser = argparse.ArgumentParser()
parser.add_argument('--directory', type=Path, required=True)
args = parser.parse_args(); base = args.directory.resolve()
state = json.loads((base / 'state.json').read_text(encoding='utf-8'))
peer = json.loads((base / 'peer.json').read_text(encoding='utf-8'))
url = f"http://127.0.0.1:{state['port']}/api/v1"
with urllib.request.urlopen(url + '/project-purge-intent', timeout=5) as response:
    token = response.headers['X-Mdo-Write-Token']; response.read()
for name in ('auth', 'permission', 'limit', 'service'):
    doc = dict(schema_version=1, id=name, name=name, enabled=True,
        description='Disposable real HTTP status acceptance',
        transport=dict(type='streamable-http', endpoint=f"http://127.0.0.1:{peer['port']}/{name}", headers=[]),
        protocol_version='2025-11-25', startup_timeout_ms=60000, request_timeout_ms=5000,
        limits=dict(message_bytes=1048576, tools=8), tools=dict(allow=[], deny=[]),
        security=dict(default_effects=['read'], permission_profile='balanced', trust_read_only_annotations=False),
        auto_reconnect=True)
    request = urllib.request.Request(url + '/extensions/mcp/' + name,
        data=json.dumps(dict(content=json.dumps(doc))).encode(), method='PUT',
        headers={'Content-Type': 'application/json', 'If-Match': '"new"', 'X-Mdo-Write-Token': token})
    try:
        with urllib.request.urlopen(request, timeout=5) as response: raw = response.read()
    except urllib.error.HTTPError as error:
        raise RuntimeError(error.read().decode('utf-8')) from error
    (base / ('setup-' + name + '.json')).write_bytes(raw)
    assert json.loads(raw)['data']['valid']
print('PASS four HTTP definitions; setup did not start a connection')
