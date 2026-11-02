"""Move an owned Home, remove the old location, then reopen real HTTP readers.

Reuses the existing host and app source in place; no model or external service.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--host', type=Path)
    mode.add_argument('--packed', type=Path)
    args = parser.parse_args()
    base = ROOT / '.build/portable-home-move' / uuid.uuid4().hex[:10]
    old = base / 'before/mdo-home'
    new = base / 'after/mdo-home'
    (old / 'workspace').mkdir(parents=True)
    new.parent.mkdir(parents=True)
    (old / 'workspace/project-file.txt').write_text('move acceptance', encoding='utf-8')
    ids = [uuid.uuid4().hex for _ in range(2)]
    stamp = time.time_ns() // 1000
    for ident in ids:
        directory = old / 'sessions/default' / ident
        directory.mkdir(parents=True)
        meta = dict(schema_version=3, revision=4, id=ident, project_id='default', title='Owned migration fixture',
                    agent_id='mdo.default', model_id='ornith-1.5-35b', protocol='openai-chat-completions',
                    reasoning_effort='', permission_profile='balanced', max_output_tokens=8192,
                    workspace_root=str(old / 'workspace'), created_at_us=stamp, updated_at_us=stamp,
                    last_activated_at_us=stamp, status='active', previous_status='active', pinned=False,
                    config_revision=0, model_generation=0, module_generation=0, skill_generation=0,
                    parent_session_id='', forked_through_sequence=0)
        (directory / 'meta.json').write_text(json.dumps(meta), encoding='utf-8')
        (directory / 'meta.json.bak').write_text(json.dumps(meta), encoding='utf-8')
    config = old.parent / 'xs.json' if args.packed else ROOT / ('app/.qa-portable-move-' + uuid.uuid4().hex[:10] + '.json')
    document = json.loads((ROOT / 'app/xs.json').read_text(encoding='utf-8'))
    sock = socket.socket(); sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]; sock.close()
    document['services'][0]['port'] = port
    document['services'][0].pop('window', None)
    if args.packed:
        document['services'][0]['class'] = 'http'
        os.link(args.packed.resolve(), old.parent / 'mdo.exe')
    content = json.dumps(document).encode('utf-8')
    config.write_bytes(content)
    process = None
    checks = {}
    def request(path):
        with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/v1' + path, timeout=10) as response:
            return json.load(response)['data']
    def start(home, log_name):
        with (base / log_name).open('wb') as log:
            flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
            command = [str(home.parent / 'mdo.exe')] if args.packed else [str(args.host.resolve()), config.name, '--', '--home', str(home)]
            environment = {key: value for key, value in os.environ.items() if key.upper() != 'MDO_HOME'}
            environment['USE_WEBVIEW'] = '0'
            child = subprocess.Popen(command, cwd=home.parent if args.packed else ROOT / 'app', env=environment,
                                     stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            for _ in range(100):
                if child.poll() is not None: raise AssertionError(('host exited', child.returncode))
                try:
                    request('/bootstrap')
                    return child
                except OSError: time.sleep(.1)
            raise AssertionError('host startup timeout')
        except BaseException:
            child.terminate(); child.wait(timeout=10)
            raise
    try:
        process = start(old, 'before.log')
        for ident in ids:
            assert request('/projects/default/sessions/' + ident)['revision'] == 4
            assert json.loads((old / 'sessions/default' / ident / 'meta.json').read_text())['workspace_root'] == 'workspace'
        checks['old_metadata_upgrades_on_startup'] = True
        process.terminate(); process.wait(timeout=10); process = None
        # Preserve one new-format session and put one genuine old path back to
        # exercise both readers after the old directory physically disappears.
        for leaf in ('meta.json', 'meta.json.bak'):
            path = old / 'sessions/default' / ids[0] / leaf
            meta = json.loads(path.read_text()); meta['workspace_root'] = str(old / 'workspace')
            path.write_text(json.dumps(meta), encoding='utf-8')
        assert old.resolve().is_relative_to(base.resolve()) and new.resolve().is_relative_to(base.resolve())
        if args.packed:
            # The destination parent was created only to anchor its checked
            # location; rename the complete executable + Home directory.
            new.parent.rmdir()
            old.parent.rename(new.parent)
            config = new.parent / 'xs.json'
        else:
            old.rename(new)
        assert not old.exists()
        process = start(new, 'after.log')
        for ident in ids:
            row = request('/projects/default/sessions/' + ident)
            assert Path(row['workspace_root']) == new / 'workspace', row
            assert row['revision'] == 4 and row['updated_at'] == stamp
            found = request('/projects/default/sessions/' + ident + '/workspace/files?q=project-file')
            assert 'project-file.txt' in found['items'], found
            for leaf in ('meta.json', 'meta.json.bak'):
                meta = json.loads((new / 'sessions/default' / ident / leaf).read_text())
                assert meta['workspace_root'] == 'workspace' and meta['revision'] == 4
        checks.update(old_location_is_absent=True, legacy_and_relative_sessions_reopen=True,
                      session_file_readers_use_the_moved_workspace=True, metadata_backups_converge=True,
                      revisions_and_activity_are_preserved=True)
        if args.packed:
            checks['executable_and_home_move_together_without_home_override'] = True
        (base / 'result.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
        print('PASS', len(checks), 'physical Home move contracts; evidence:', base / 'result.json')
    finally:
        if process and process.poll() is None:
            process.terminate(); process.wait(timeout=10)
        if config.exists() and hashlib.sha256(config.read_bytes()).digest() == hashlib.sha256(content).digest():
            config.unlink()


if __name__ == '__main__':
    main()
