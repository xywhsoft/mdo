"""Bounded HTTP/TCC checks for one recoverable image upload identity."""
import argparse
import binascii
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from fixture_images import ordinary_png
from test_api_runtime import ROOT, free_port, request, wait_ready, write_site


def png(red):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff))
    pixels = b''.join(b'\0' + bytes([red, 0, 255 - red, 255]) * 4 for _ in range(4))
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(pixels, 0)) + chunk(b'IEND', b''))


def probe(host):
    with tempfile.TemporaryDirectory(prefix='mdo-upload-identity-') as temp:
        base = Path(temp); home = base / 'home'; port = free_port()
        config = write_site(base / 'site', port)
        config_data = json.loads(config.read_text(encoding='utf-8'))
        production = json.loads((ROOT / 'app/xs.json').read_text(encoding='utf-8'))
        config_data['services'][0]['recv_limit'] = production['services'][0]['recv_limit']
        config.write_text(json.dumps(config_data), encoding='utf-8')
        env = dict(os.environ, USE_WEBVIEW='0')
        log = (base / 'host.log').open('wb')
        process = None
        def start():
            child = subprocess.Popen([str(host), str(config), '--', '--home', str(home)],
                cwd=base, env=env, stdout=log, stderr=log,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            wait_ready(port, child); return child
        def stop(child):
            if child and child.poll() is None:
                child.terminate()
                try: child.wait(timeout=10)
                except subprocess.TimeoutExpired: child.kill(); child.wait()
        def call(method, path, data=None, headers=None):
            status, _, raw = request(port, method, '/api/v1' + path,
                body=data, headers=headers or {})
            return status, json.loads(raw) if raw else {}
        def session():
            status, result = call('POST', '/sessions', json.dumps({
                'project_id': 'default', 'title': 'Upload identity',
                'model_id': 'ornith-1.5-35b'}).encode(), {'Content-Type': 'application/json'})
            assert status == 201, (status, result)
            return result['data']['id']
        try:
            process = start(); first = session(); second = session()
            path = f'/projects/default/sessions/{first}/attachments'
            image, changed = png(240), png(20)
            assert len(image) == len(changed) and image != changed
            headers = {'Content-Type': 'image/png', 'X-Mdo-File-Name': 'pixel.png'}
            identity = 'a' * 32; keyed = path + '/' + identity
            status, result = call('PUT', keyed, image, headers)
            assert status == 201 and result['data']['id'] == identity, (status, result)
            directory = home / 'sessions/default' / first / 'attachments'
            original_meta = (directory / (identity + '.json')).read_bytes()
            status, replay = call('PUT', keyed, image, headers)
            assert status == 200 and replay['data'] == result['data'], (status, replay)
            assert (directory / (identity + '.json')).read_bytes() == original_meta
            assert len(list(directory.glob('*.bin'))) == 1
            for body, names in [(changed, headers), (image, {**headers, 'X-Mdo-File-Name': 'other.png'})]:
                status, conflict = call('PUT', keyed, body, names)
                assert status == 409 and conflict['error']['code'] == 'image_upload_conflict', (status, conflict)
                assert (directory / (identity + '.bin')).read_bytes() == image
                assert (directory / (identity + '.json')).read_bytes() == original_meta
            for bad in ['a' * 31, 'A' * 32, 'g' * 32]:
                status, error = call('PUT', path + '/' + bad, image, headers)
                assert status == 400 and error['error']['code'] == 'image_upload_id_invalid', (status, error)
            # Either half of an interrupted stored pair reserves the identity;
            # recovery must report a conflict instead of silently overwriting it.
            for number, suffix, content in [(13, '.bin', changed), (14, '.json', original_meta)]:
                partial_id = f'{number:032x}'
                partial = directory / (partial_id + suffix)
                partial.write_bytes(content)
                status, error = call('PUT', path + '/' + partial_id, image, headers)
                assert status == 409 and error['error']['code'] == 'image_upload_conflict'
                assert partial.read_bytes() == content
                assert not (directory / (partial_id + ('.json' if suffix == '.bin' else '.bin'))).exists()
                partial.unlink()
            with ThreadPoolExecutor(max_workers=2) as pool:
                responses = list(pool.map(lambda _: call('PUT', path + '/' + 'b' * 32, image, headers), range(2)))
            assert sorted(status for status, _ in responses) == [200, 201], responses
            assert len(list(directory.glob('*.bin'))) == 2
            status, other = call('PUT', f'/projects/default/sessions/{second}/attachments/{identity}', changed, headers)
            assert status == 201 and other['data']['id'] == identity
            # Fill the documented 16-image boundary with tiny ordinary files;
            # this checks quota admission, never throughput or high load.
            for number in range(14):
                status, result = call('PUT', path + '/' + f'{number:032x}', image, headers)
                assert status == 201, (number, status, result)
            status, error = call('PUT', path + '/' + 'c' * 32, image, headers)
            assert status == 507 and error['error']['code'] == 'attachment_storage_full'
            status, _ = call('PUT', keyed, image, headers)
            assert status == 200, 'an accepted upload replays even at the quota boundary'
            status, _ = call('POST', f'/projects/default/sessions/{second}/attachments', ordinary_png(), headers)
            assert status == 201, 'legacy POST and ordinary files above the JSON bound stay usable'
            stop(process); process = start()
            status, _ = call('PUT', keyed, image, headers)
            assert status == 200 and (directory / (identity + '.json')).read_bytes() == original_meta
            assert len(list(directory.glob('*.bin'))) == 16
            status, info = call('GET', keyed + '/info')
            assert status == 200 and info['data']['id'] == identity
            status, _, raw = request(port, 'GET', '/api/v1/runs')
            assert status == 200 and json.loads(raw)['data']['runs_started'] == 0
            print('image upload identity runtime: PASS (replay, content/name conflicts, namespace, quota, restart; no model calls)')
        except BaseException:
            print((base / 'host.log').read_text(encoding='utf-8', errors='replace')[-3000:])
            raise
        finally:
            stop(process); log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    probe(parser.parse_args().host.resolve())
