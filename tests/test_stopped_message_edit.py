"""Actual product UI and native stop/edit/queue, using an offline model.

Requires the generated application SDK, .build/host/xs.exe, Edge, and
Playwright (optionally provided by MDO_PLAYWRIGHT_PATH). Runs in the existing
app tree with a temporary Home/workspace; never copies the app or SDK.
"""
import argparse
import json
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path
from test_conversation_groups_runtime import prepare_history_home

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    parser.add_argument('--browser-script', type=Path, default=ROOT / 'tests/fixtures/stopped-message-edit-browser.cjs')
    parser.add_argument('--evidence-name', default='stopped-message-edit-qa')
    parser.add_argument('--expected-calls', type=int, default=3)
    parser.add_argument('--no-model-hold', action='store_true')
    args = parser.parse_args()
    evidence = ROOT / '.build' / args.evidence_name
    assert evidence.resolve().parent == (ROOT / '.build').resolve()
    evidence.mkdir(exist_ok=True)
    (evidence / 'browser.json').unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='stopped-edit-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        workspace = base / 'workspace'
        workspace.mkdir()
        home = base / 'home'
        prepare_history_home(home)
        subprocess.run(['git', 'init', '--quiet', str(workspace)], check=True)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        probe = ROOT / 'app/generated/stopped_edit_qa.c'
        config = ROOT / 'app/xs.stopped-edit-qa.json'
        assert not probe.exists() and not config.exists(), 'another edit fixture is running'
        process = None
        log = evidence / 'runtime.log'
        try:
            probe.write_text((ROOT / 'tests/fixtures/stopped-message-edit.c')
                .read_text(encoding='utf-8').replace('#include "generated/mdo_unity.c"',
                    '#include "mdo_unity.c"'), encoding='utf-8')
            config.write_text(json.dumps({'engine': {'workers': 4}, 'services': [{
                'enabled': True, 'class': 'http', 'name': 'edit-qa', 'ip': '127.0.0.1',
                'port': port, 'host_default': {'enabled': True, 'path': 'web',
                    'devlang': 'c', 'devfile': 'generated/stopped_edit_qa.c'}}]}), encoding='utf-8')
            with log.open('wb') as out:
                process = subprocess.Popen([str(args.host.resolve()), str(config)],
                    cwd=ROOT / 'app', stdout=out, stderr=out,
                    env={**os.environ, 'MDO_HOME': str(home), 'MDO_EDIT_WORKSPACE': str(workspace),
                         'MDO_EDIT_NO_HOLD': '1' if args.no_model_hold else ''},
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            end, text = time.monotonic() + 60, ''
            while process.poll() is None and time.monotonic() < end:
                text = log.read_text(encoding='utf-8', errors='replace')
                if 'EDIT_FIXTURE_READY=1' in text:
                    break
                time.sleep(.1)
            assert 'EDIT_FIXTURE_READY=1' in text, text[-8000:]
            subprocess.run(['node', str(args.browser_script.resolve()),
                f'http://127.0.0.1:{port}', str(evidence)], cwd=ROOT, check=True, timeout=120)
            text = log.read_text(encoding='utf-8', errors='replace')
            assert text.count('EDIT_MODEL_CALL=') == args.expected_calls, text[-5000:]
            (evidence / 'failure.png').unlink(missing_ok=True)
            print('PASS native stop/edit/resend, immediate bubble, metadata rebase, original history conflict fence, exactly one replacement run')
        finally:
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            probe.unlink(missing_ok=True)
            config.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
