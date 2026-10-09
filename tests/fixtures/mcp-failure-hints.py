"""Single-request MCP diagnostic cases; only synthetic data, no networking."""
import json
import sys
import time

mode = sys.argv[1]
for line in sys.stdin:
    request = json.loads(line)
    method = request.get('method')
    identifier = request.get('id')
    if identifier is None:
        continue
    if method == 'initialize':
        if mode == 'timeout':
            time.sleep(1.2)
        if mode == 'invalid':
            print('[]', flush=True)
            continue
        if mode == 'remote':
            print(json.dumps({'jsonrpc': '2.0', 'id': identifier, 'error': {
                'code': -32000, 'message': 'qa-private-credential-must-not-appear'}}), flush=True)
            continue
        result = {'protocolVersion': '2024-11-05' if mode == 'version' else '2025-11-25',
                  'capabilities': {'tools': {}}, 'serverInfo': {'name': 'diagnostic-fixture', 'version': '1'}}
    elif method == 'tools/list':
        result = {'tools': [{'name': 'echo', 'description': 'Synthetic echo',
                            'inputSchema': {'type': 'object', 'properties': {}}}]}
    else:
        continue
    print(json.dumps({'jsonrpc': '2.0', 'id': identifier, 'result': result}), flush=True)
