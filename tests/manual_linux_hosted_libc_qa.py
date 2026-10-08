"""Exercise TCC's shared static libc boundary with the xs acceptance fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    folder = Path(tempfile.mkdtemp(prefix='xs-hosted-libc-qa-'))
    binary = folder / 'xs'
    shutil.copy2(args.host.resolve(), binary)
    shutil.copy2(args.source.resolve(), folder / 'probe.c')
    (folder / 'xs.json').write_text(json.dumps({'services': [{
        'name': 'crt-test', 'enabled': True, 'class': 'app', 'ip': '127.0.0.1', 'port': 0,
        'host_default': {'enabled': True, 'name': 'crt-test', 'path': '.',
                         'devlang': 'c', 'devfile': 'probe.c'}}]}), encoding='utf-8')
    log_path = folder / 'run.log'
    with log_path.open('wb') as log:
        process = subprocess.Popen([str(binary)], cwd=folder, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if 'HOSTED_LIBC_PASS' in log_path.read_text(encoding='utf-8', errors='replace'):
                    break
                if process.poll() is not None:
                    raise AssertionError(log_path.read_text(encoding='utf-8', errors='replace'))
                time.sleep(.1)
            else:
                raise AssertionError('hosted libc fixture timeout')
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            process.wait(timeout=30)
        assert process.returncode == 0, log_path.read_text(encoding='utf-8', errors='replace')
    result = {'passed': True, 'host': str(args.host), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'log': str(log_path),
              'checks': ['TCC stdio', 'host errno', 'clock', 'environment', 'pthread/shared allocation', 'normal stop']}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
