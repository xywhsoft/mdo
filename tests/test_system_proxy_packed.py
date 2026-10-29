"""Verify the real settings API and restart, without OS changes or model calls."""
import argparse
import json
import os
from pathlib import Path
import tempfile

from test_api_runtime import request
from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    args = parser.parse_args()
    packed = args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix='system-proxy-packed-', dir=ROOT/'.build') as raw:
        base = Path(raw)
        folder, port = site(base, 'site', packed)
        home = base/'mdo-home'
        environment = {**os.environ, 'USE_WEBVIEW': '0'}
        process = start(folder, packed, home, environment)

        def settings():
            status, headers, body = request(port, 'GET', '/api/v1/settings')
            value = json.loads(body)
            assert status == 200, value
            return value['data'], headers['etag']

        def mode(value):
            _, etag = settings()
            status, _, body = request(port, 'PATCH', '/api/v1/settings/settings',
                body=json.dumps({'schema_version': 1, 'patch': {'transport': {'proxy': value}}}).encode(),
                headers={'Content-Type': 'application/json', 'If-Match': etag})
            assert status == 200, (json.loads(body), value)
            return settings()[0]['transport']['proxy']

        try:
            _, bootstrap = wait_bootstrap(process, port, folder/'packed.log')
            assert bootstrap['data']['ready'], bootstrap
            initial, _ = settings()
            assert initial['transport']['proxy']['kind'] == 'none'
            manual = {'kind': 'socks5', 'host': '127.0.0.1', 'port': 1080,
                      'user': 'fixture', 'bypass': 'localhost',
                      'credential': {'secret_ref': 'env:MDO_UNREAD_MANUAL_PROXY_SECRET'}}
            mode(manual)
            selected = mode({'kind': 'system'})
            assert selected == {'kind': 'system', 'host': '127.0.0.1', 'port': 1080,
                                'user': 'fixture', 'bypass': 'localhost', 'credential_configured': True}, selected
            assert 'MDO_UNREAD_MANUAL_PROXY_SECRET' not in json.dumps(selected)
            stop(process)
            process = start(folder, packed, home, environment)
            _, bootstrap = wait_bootstrap(process, port, folder/'packed.log')
            assert bootstrap['data']['ready'], bootstrap
            assert settings()[0]['transport']['proxy'] == selected
            selected = mode({'kind': 'none'})
            assert selected['host'] == manual['host'] and selected['credential_configured']
            selected = mode({key: value for key, value in manual.items() if key != 'credential'})
            assert selected['port'] == 1080 and selected['user'] == 'fixture'
            print('packed system proxy settings, secret redaction and restart: PASS')
        finally:
            stop(process)
            release_packed_copies(folder/packed.name)


if __name__ == '__main__':
    main()
