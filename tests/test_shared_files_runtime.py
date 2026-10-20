"""Real native share import: private inbox, rooted publication and safe replay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor

from test_api_runtime import request
from test_interrupt_runtime import free_port, start_host, stop_host

ROOT = Path(__file__).resolve().parents[1]


def probe(host):
    with tempfile.TemporaryDirectory(prefix='shared-files-', dir=ROOT / '.build') as temporary:
        base = Path(temporary)
        site, inbox, workspace = base / 'site', base / 'inbox', base / 'workspace'
        shutil.copytree(ROOT / 'app', site)
        inbox.mkdir(); workspace.mkdir()
        config = site / 'xs.json'; port = free_port()
        config.write_text(json.dumps({'services': [{'enabled': True, 'class': 'http', 'name': 'shares',
            'ip': '127.0.0.1', 'port': port, 'host_default': {'enabled': True, 'name': 'mdo',
                'path': 'web', 'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}), encoding='utf-8')
        environment = dict(os.environ, USERPROFILE=str(base), HOME=str(base), MDO_ANDROID_SHARE_ROOT=str(inbox))
        process = None

        def call(method, path, body=None, expected=200):
            status, headers, raw = request(port, method, '/api/v1/'+path,
                body=json.dumps(body).encode() if body is not None else None, headers={'Content-Type': 'application/json'})
            data = json.loads(raw)
            assert status == expected, (path, status, data)
            return data.get('data', data), headers

        try:
            process = start_host(host, config, base / 'home', environment, base / 'xs.log', port)
            session, _ = call('POST', 'sessions', {'project_id': 'default', 'title': 'Share import',
                'model_id': 'ornith-1.5-35b', 'workspace_root': str(workspace)}, 201)
            path = f'projects/default/sessions/{session["id"]}'
            sid = 'a'*32; directory = inbox / sid; directory.mkdir()
            rows = []

            def source(number, name, content):
                file_id = f'{number:032x}'
                (directory / (file_id+'.bin')).write_bytes(content)
                row = {'id': file_id, 'name': name, 'mime': 'application/octet-stream',
                    'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
                rows.append(row)
                return row

            def manifest(owner=None):
                (directory / 'ready.json').write_text(json.dumps({'id': sid, 'files': rows,
                    'destination': owner or {'project_id': 'default', 'session_id': session['id']}}), encoding='utf-8')

            first = source(1, '小说 慕晴.txt', '共享文本\n'.encode())
            empty = source(2, 'empty.txt', b'')
            concurrent = source(3, 'concurrent.bin', bytes(range(256))*5000)
            manifest()
            body = {'share_id': sid, 'file_id': first['id']}
            result, _ = call('POST', path+'/shared-files', body)
            target = workspace / result['path']
            assert target.read_bytes() == '共享文本\n'.encode()
            again, _ = call('POST', path+'/shared-files', body)
            assert again == result and len(list(target.parent.iterdir())) == 1
            zero, _ = call('POST', path+'/shared-files', {'share_id': sid, 'file_id': empty['id']})
            assert (workspace/zero['path']).stat().st_size == 0
            with ThreadPoolExecutor(max_workers=2) as pool:
                replies = list(pool.map(lambda _: call('POST', path+'/shared-files',
                    {'share_id': sid, 'file_id': concurrent['id']})[0], range(2)))
            assert replies[0] == replies[1]
            target.write_bytes(b'user edit')
            call('POST', path+'/shared-files', body, 409)
            assert target.read_bytes() == b'user edit'
            call('POST', path+'/shared-files', {**body, 'share_id': '../outside'}, 422)
            call('POST', path+'/shared-files', {**body, 'source_path': '/outside'}, 422)
            malicious = source(4, '../escape.txt', b'escape')
            manifest(); call('POST', path+'/shared-files', {'share_id': sid, 'file_id': malicious['id']}, 409)
            assert not (workspace/'escape.txt').exists()
            wrong = source(5, 'bad-hash.txt', b'original')
            wrong['sha256'] = '0'*64; manifest()
            call('POST', path+'/shared-files', {'share_id': sid, 'file_id': wrong['id']}, 409)
            assert not list(workspace.rglob('*.part'))
            manifest({'project_id': 'default', 'session_id': 'other-session'})
            call('POST', path+'/shared-files', {'share_id': sid, 'file_id': empty['id']}, 409)
            link = source(6, 'linked.txt', b'original')
            filename = directory/(link['id']+'.bin')
            outside = base/'outside.txt'; outside.write_text('outside')
            filename.unlink()
            try:
                filename.symlink_to(outside)
            except OSError:
                print('Source symlink case unavailable on this host')
            else:
                manifest(); call('POST', path+'/shared-files', {'share_id': sid, 'file_id': link['id']}, 409)
                assert outside.read_text() == 'outside'
            outside_directory = base/'outside-workspace'; outside_directory.mkdir()
            junction = workspace/'shared-files'/('c'*32)
            assert junction.is_relative_to(base) and outside_directory.is_relative_to(base)
            try:
                if os.name == 'nt':
                    subprocess.run(['cmd', '/d', '/c', 'mklink', '/J', str(junction), str(outside_directory)],
                        check=True, capture_output=True)
                else:
                    junction.symlink_to(outside_directory, target_is_directory=True)
                (inbox/('c'*32)).mkdir()
                copied = json.loads((directory/'ready.json').read_text(encoding='utf-8'))
                copied['id'] = 'c'*32
                (inbox/('c'*32)/'ready.json').write_text(json.dumps(copied),encoding='utf-8')
                (inbox/('c'*32)/(empty['id']+'.bin')).write_bytes(b'')
                call('POST', path+'/shared-files', {'share_id': 'c'*32, 'file_id': empty['id']}, 409)
                assert not list(outside_directory.iterdir())
            finally:
                if os.name == 'nt' and junction.exists():
                    os.rmdir(junction)  # Remove the junction itself, never its target.
                elif junction.is_symlink():
                    junction.unlink()
            print('Shared file native runtime PASS: Unicode/empty/large files, replay, concurrent publication, '
                  'edited-target protection, traversal, hash mismatch, binding and destination links')
        except BaseException:
            print((base/'xs.log').read_text(encoding='utf-8', errors='replace')[-4000:])
            raise
        finally:
            stop_host(process)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    probe(parser.parse_args().host.resolve())
