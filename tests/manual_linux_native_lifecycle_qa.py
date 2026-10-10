"""Bounded lifecycle checks of a packed Linux GUI and its actual WebKit child.

Run as an ordinary user under dbus-run-session + xvfb-run. The real application
is unmodified. One owned renderer is interrupted; no UI input, screenshot,
clipboard/file-dialog simulation, model requests, or sandbox override is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

from manual_linux_products_qa import request


def process_identity(pid):
    """Read identity and ancestry without exposing command-line credentials."""
    entry = Path('/proc') / str(pid)
    try:
        fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
        return {'pid': pid, 'parent': int(fields[1]), 'start': int(fields[19]),
                'state': fields[0], 'executable': os.readlink(entry / 'exe')}
    except (OSError, ValueError, IndexError):
        return None


def processes():
    result = {}
    for entry in Path('/proc').iterdir():
        if entry.name.isdigit():
            item = process_identity(int(entry.name))
            if item is not None:
                result[item['pid']] = item
    return result


def descendants(items, parent):
    owned = set()
    for pid in items:
        current, seen = pid, set()
        while current in items and current not in seen:
            seen.add(current)
            current = items[current]['parent']
            if current == parent:
                owned.add(pid)
                break
    return [items[pid] for pid in owned if items[pid]['state'] != 'Z']


def alive(identity):
    item = process_identity(identity['pid'])
    return item is not None and item['start'] == identity['start'] and item['state'] != 'Z'


def terminate_owned(identity, sig):
    # The start time prevents accidentally signalling a reused process ID.
    if alive(identity):
        os.kill(identity['pid'], sig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert sys.platform == 'linux' and os.geteuid() != 0, 'Use an ordinary Linux user'
    assert os.environ.get('DISPLAY'), 'Use an isolated X display'
    source = args.binary.resolve()
    folder = Path(tempfile.mkdtemp(prefix='mdo-native-lifecycle-qa-'))
    executable = folder / 'mdo'
    executable.write_bytes(source.read_bytes())
    executable.chmod(0o755)
    home = folder / 'mdo-home'
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    log_path = folder / 'native.log'
    environment = dict(os.environ, MDO_HOME=str(home))
    assert not any(environment.get(key) for key in ('WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS',
        'WEBKIT_FORCE_SANDBOX', 'WEBKIT_DISABLE_SANDBOX')), 'Do not override the WebKit sandbox'
    identities = {}
    child = None
    proof = {'passed': False, 'binary_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
        'home': str(home), 'log': str(log_path), 'ordinary_user': True,
        'sandbox_override': False, 'model_requests_submitted': False, 'native_input_or_pixels_tested': False}

    def observe():
        items = descendants(processes(), child.pid)
        for item in items:
            identities[(item['pid'], item['start'])] = item
        return items

    def ready():
        end = time.monotonic() + 30
        while time.monotonic() < end:
            assert child.poll() is None, log_path.read_text(errors='replace')
            try:
                status, headers, body = request(port, 'GET', '/api/v1/bootstrap')
                if status == 200 and body['data']['ready']:
                    return headers
                if status == 200 and body['data'].get('stage') == 'failed':
                    raise AssertionError(body)
            except OSError:
                pass
            time.sleep(.1)
        raise AssertionError('Native application initialization timed out')

    def renderers(previous=None):
        end = time.monotonic() + 15
        while time.monotonic() < end:
            assert child.poll() is None, log_path.read_text(errors='replace')
            found = [item for item in observe() if Path(item['executable']).name == 'WebKitWebProcess'
                and (item['pid'],item['start']) != previous]
            if found:
                return found
            time.sleep(.1)
        raise AssertionError('No live WebKit renderer belonging to the native application')

    def stop():
        child.terminate()
        child.wait(timeout=20)
        assert child.returncode == 0, log_path.read_text(errors='replace')
        end = time.monotonic() + 5
        while time.monotonic() < end and any(alive(item) for item in identities.values()):
            time.sleep(.1)
        assert not any(alive(item) for item in identities.values()), 'Native descendants survived shutdown'

    with log_path.open('ab') as log:
        def start():
            return subprocess.Popen([str(executable), '--port', str(port)], cwd=folder,
                env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            child = start()
            ready()
            first_renderer = renderers()[0]
            helpers = list((home / 'data/cache/webkit').glob('xs-window-*'))
            assert len(helpers) == 1 and helpers[0].is_file(), helpers
            helper = helpers[0].resolve()
            assert any(item['executable'] == str(helper) for item in observe()), 'No live portable window helper'
            proof['helper_sha256'] = hashlib.sha256(helper.read_bytes()).hexdigest()
            # Cover the packed startup interval that previously exposed reload
            # controller failures, without model calls or a load loop.
            end = time.monotonic() + 12
            while time.monotonic() < end:
                assert child.poll() is None and alive(first_renderer), 'Native startup did not remain live'
                time.sleep(.2)
            ready()
            terminate_owned(first_renderer, signal.SIGKILL)
            recovered_renderer = renderers((first_renderer['pid'], first_renderer['start']))[0]
            ready()
            proof['renderer_before'] = first_renderer
            proof['renderer_after'] = recovered_renderer
            stop()
            child = start()
            ready()
            renderers()
            assert hashlib.sha256(helper.read_bytes()).hexdigest() == proof['helper_sha256']
            assert any(item['executable'] == str(helper) for item in observe()), 'Restart did not use portable helper'
            stop()
            proof.update(passed=True, checks=['packed native initialization', 'actual WebKit renderer',
                'portable window cache', 'bounded startup interval', 'single renderer failure/recovery',
                'service remains available after renderer recovery', 'normal SIGTERM shutdown',
                'no surviving owned descendants', 'same Home restart'])
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait(timeout=5)
            for item in identities.values():
                terminate_owned(item, signal.SIGTERM)
            end = time.monotonic() + 2
            while time.monotonic() < end and any(alive(item) for item in identities.values()):
                time.sleep(.1)
            for item in identities.values():
                terminate_owned(item, signal.SIGKILL)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            # WSL may clear /tmp when its last process exits. Preserve the
            # native log alongside the result before leaving the session.
            log.flush()
            saved_log = args.output.with_suffix('.native.log')
            saved_log.write_bytes(log_path.read_bytes())
            proof['native_log_filename'] = saved_log.name
            args.output.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(proof, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
