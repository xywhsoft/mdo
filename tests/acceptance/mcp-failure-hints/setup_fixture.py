"""Create disposable MCP definitions through the actual product API."""
import argparse
import json
from pathlib import Path
import sys
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--directory', type=Path, required=True)
args = parser.parse_args()
base = args.directory.resolve()
state = json.loads((base / 'state.json').read_text(encoding='utf-8'))
root = Path.cwd()
url = f"http://127.0.0.1:{state['port']}/api/v1"
with urllib.request.urlopen(url + '/project-purge-intent', timeout=5) as response:
    token = response.headers['X-Mdo-Write-Token']
    response.read()
for identifier, mode in [('missing', None), ('protocol', 'version'), ('response', 'invalid'),
                         ('remote', 'remote'), ('timeout', 'timeout'), ('working', 'valid')]:
    doc = dict(schema_version=1, id=identifier, name=identifier,
        description='Disposable MCP failure advice acceptance', enabled=True,
        transport=dict(type='stdio', program=sys.executable if mode else '__mdo_missing_mcp_fixture__',
            arguments=[str(root / 'tests/fixtures/mcp-failure-hints.py'), mode] if mode else [],
            working_directory=str(root), inherit_environment=True, environment=[]),
        protocol_version='2025-11-25', startup_timeout_ms=400, request_timeout_ms=800,
        limits=dict(message_bytes=1048576, tools=8), tools=dict(allow=[], deny=[]),
        security=dict(default_effects=['read'], permission_profile='balanced', trust_read_only_annotations=False),
        auto_reconnect=True)
    request = urllib.request.Request(url + '/extensions/mcp/' + identifier,
        data=json.dumps(dict(content=json.dumps(doc))).encode(), method='PUT',
        headers={'Content-Type': 'application/json', 'If-Match': '"new"', 'X-Mdo-Write-Token': token})
    with urllib.request.urlopen(request, timeout=5) as response:
        raw = response.read()
    (base / ('setup-' + identifier + '.json')).write_bytes(raw)
    assert json.loads(raw)['data']['valid']
print('PASS six native MCP fixture configurations; no server started during setup')
