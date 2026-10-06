"""Isolated real xs/TCC settings UI with a local mock supplier; no real keys."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from test_model_setup_runtime import Supplier, ThreadingHTTPServer
from test_api_runtime import ROOT, free_port, wait_ready
from test_interrupt_runtime import stop_host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', type=Path, required=True)
parser.add_argument('--metadata-output', type=Path, required=True)
args = parser.parse_args()
mock = ThreadingHTTPServer(('127.0.0.1', 0), Supplier)
threading.Thread(target=mock.serve_forever, daemon=True).start()
with tempfile.TemporaryDirectory(prefix='mdo-model-ui-') as temporary:
    base = Path(temporary); shutil.copytree(ROOT / 'app', base, dirs_exist_ok=True)
    port = free_port()
    config = {'engine': {'workers': 1}, 'services': [{
        'enabled': True, 'class': 'http', 'name': 'model-ui', 'ip': '127.0.0.1', 'port': port,
        'host_default': {'enabled': True, 'name': 'mdo', 'path': 'web', 'devlang': 'c', 'devfile': 'generated/mdo_unity.c'}}]}
    (base / 'xs.json').write_text(json.dumps(config))
    with (base / 'host.log').open('wb') as log:
        process = subprocess.Popen([str(args.host.resolve()), str(base / 'xs.json')], cwd=base,
            env=dict(os.environ, MDO_HOME=str(base / 'mdo-home')), stdin=subprocess.PIPE, stdout=log, stderr=log)
        try:
            wait_ready(port, process)
            args.metadata_output.write_text(json.dumps({'url': f'http://127.0.0.1:{port}/#/settings/models',
                'supplier': f'http://127.0.0.1:{mock.server_port}/v1', 'log': str(base / 'host.log')}))
            print('READY. Press Enter to stop.', flush=True)
            input()
        finally: stop_host(process)
mock.shutdown(); mock.server_close()
