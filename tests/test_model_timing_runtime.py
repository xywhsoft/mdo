"""Check normal/debug startup flags through the packed application's bootstrap."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_api_runtime import ROOT, free_port, request, wait_ready


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    args = parser.parse_args()
    packed = args.packed.resolve()
    with tempfile.TemporaryDirectory(dir=ROOT / '.build', prefix='timing-mode-') as raw:
        base = Path(raw)
        launcher = base / packed.name
        os.link(packed, launcher)
        port = free_port()
        (base / 'xs.json').write_text(json.dumps({'services': [{
            'enabled': True, 'class': 'http', 'name': 'timing-mode',
            'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web',
                'devlang': 'c', 'devfile': 'generated/mdo_unity.c'},
        }]}), encoding='utf-8')
        home = base / 'home'
        for flags, expected in (([], False), (['-debug'], True), (['--debug'], True)):
            with (base / 'host.log').open('wb') as log:
                process = subprocess.Popen([str(launcher), '--', '--home', str(home), *flags],
                    cwd=base, stdout=log, stderr=log,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                try:
                    wait_ready(port, process)
                    status, _, body = request(port, 'GET', '/api/v1/bootstrap')
                    assert status == 200, (status, body)
                    actual = json.loads(body)['data']['debug']
                    assert actual is expected, (flags, actual)
                    print('PASS', flags or 'normal', 'debug=', actual, flush=True)
                except Exception:
                    print((base / 'host.log').read_text(encoding='utf-8', errors='replace'))
                    raise
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


if __name__ == '__main__':
    main()
