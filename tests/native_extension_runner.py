"""Run an owned C contract command in an existing product, without cloning it."""
import hashlib
import json
import time
import urllib.error
import urllib.request
import uuid


def run(args, ident, source, argument, evidence):
    encoded = source.encode('utf-8')
    command = args.home / 'commands' / (ident + '.c')
    if command.exists():
        raise AssertionError('Refusing to replace an existing command')
    command.parent.mkdir(parents=True, exist_ok=True)
    command.write_bytes(encoded)
    def request(method, path, value=None):
        headers = {}
        if method == 'POST':
            with urllib.request.urlopen(args.url + '/api/v1/bootstrap', timeout=15) as r:
                headers['X-Mdo-Write-Token'] = r.headers.get('X-Mdo-Write-Token', '')
            headers['Content-Type'] = 'application/json'
        req = urllib.request.Request(args.url + path, data=json.dumps(value).encode() if value is not None else None, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=90) as r:
                return json.load(r)
        except urllib.error.HTTPError as error:
            raise AssertionError((error.code, error.read().decode('utf-8'))) from error
    def call(method, params):
        return request('POST', '/api/v1/application/call', {'method': method, 'params': params, 'context': {'project': args.project}})['data']
    try:
        invocation = 'cmd-' + uuid.uuid4().hex
        body = {'invocation_id': invocation, 'command_id': ident, 'raw_arguments': json.dumps([argument], ensure_ascii=False)}
        call('product.command_invocations.post', {'query': {'project': args.project}, 'body': body})
        for _ in range(300):
            status = call('product.command_invocations.by_invocation.get', {'path': {'invocation': invocation}})['data']
            if status.get('terminal'):
                break
            time.sleep(.2)
        evidence.write_text(json.dumps(status, ensure_ascii=False, indent=2), encoding='utf-8')
        assert status.get('state') == 'succeeded', status
        return status['result']
    finally:
        if command.exists() and hashlib.sha256(command.read_bytes()).digest() == hashlib.sha256(encoded).digest():
            command.unlink()
