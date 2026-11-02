"""Exercise the Markdown editor contract against a running acceptance product."""
import argparse
import json
from pathlib import Path
import urllib.error
import urllib.request
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--home', required=True, type=Path)
    args = parser.parse_args()

    def request(method, path, value=None, tag=None):
        headers = {}
        if method != 'GET':
            with urllib.request.urlopen(args.url + '/api/v1/bootstrap', timeout=15) as reply:
                headers['X-Mdo-Write-Token'] = reply.headers['X-Mdo-Write-Token']
            headers['Content-Type'] = 'application/json'
        if tag:
            headers['If-Match'] = tag
        req = urllib.request.Request(args.url + '/api/v1' + path, method=method,
            data=json.dumps(value).encode() if value is not None else None, headers=headers)
        try:
            reply = urllib.request.urlopen(req, timeout=20)
        except urllib.error.HTTPError as error:
            reply = error
        with reply:
            return reply.code, reply.headers, json.load(reply)

    prefix = 'qa-memory-' + uuid.uuid4().hex[:10]
    for scope in ('global', 'projects/' + prefix):
        path = '/memory/' + scope
        status, headers, result = request('GET', path)
        assert status == 200, result
        original = headers['ETag']
        ident = prefix if scope == 'global' else 'note'
        content = '---\nname: Unicode note\n---\n\n保持 Markdown 原文。\n'
        payload = {'id': ident, 'content': content}
        assert request('PUT', path, payload)[0] == 428
        for invalid in ({'id': ident}, {'id': ident, 'content': 12},
                        {**payload, 'title': 'Unused metadata'},
                        {'id': '../outside', 'content': 'invalid'},
                        {'id': ident, 'content': 'x' * 65537}):
            status, _, result = request('PUT', path, invalid, original)
            assert status == 422, (invalid.keys(), status, result)
        status, headers, result = request('PUT', path, payload, original)
        assert status == 200, result
        saved = headers['ETag']
        assert saved != original
        status, _, result = request('GET', path + '/' + ident)
        assert status == 200 and result['data']['content'] == content, result
        directory = args.home / 'memory' / scope
        assert (directory / (ident + '.md')).read_text(encoding='utf-8') == content
        assert request('PUT', path, payload, original)[0] == 412
        status, _, result = request('DELETE', path + '/' + ident, tag=saved)
        assert status == 200, result
        assert request('GET', path + '/' + ident)[0] == 404
        assert not (directory / (ident + '.md')).exists()
    print('Markdown memory API: exact content, minimal fields, limits, ETags and scoped deletion PASS')


if __name__ == '__main__':
    main()
