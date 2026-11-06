"""Compile actual product parameter resolution/config validation in place."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    probe = ROOT / 'app/generated/model_request_defaults_qa.c'
    config = ROOT / 'app/xs.model-request-defaults-qa.json'
    assert not probe.exists() and not config.exists()
    evidence = ROOT / '.build/model-official-defaults-20261107'
    evidence.mkdir(exist_ok=True)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    process = None
    log = evidence / 'native-client.log'
    try:
        source = (ROOT / 'tests/fixtures/model-request-defaults.c').read_text(encoding='utf-8')
        probe.write_text(source.replace('#include "generated/mdo_unity.c"', '#include "mdo_unity.c"'), encoding='utf-8')
        config.write_text(json.dumps({'services': [{'class': 'http', 'enabled': True,
            'name': 'model-defaults-qa', 'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'path': 'web', 'devlang': 'c',
                'devfile': 'generated/model_request_defaults_qa.c'}}]}), encoding='utf-8')
        with log.open('wb') as out:
            process = subprocess.Popen([str(args.host.resolve()), str(config)], cwd=ROOT / 'app',
                stdout=out, stderr=out, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        end, text = time.monotonic() + 45, ''
        while process.poll() is None and time.monotonic() < end:
            text = log.read_text(encoding='utf-8', errors='replace')
            if 'model_defaults_done=1' in text:
                break
            time.sleep(.1)
        assert 'model_defaults_failed=0' in text and 'model_defaults_done=1' in text, text[-6000:]
        print('PASS native request defaults, API priority, persisted advisory metadata and ambiguous proxy fencing')
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        probe.unlink(missing_ok=True)
        config.unlink(missing_ok=True)

if __name__ == '__main__':
    main()
