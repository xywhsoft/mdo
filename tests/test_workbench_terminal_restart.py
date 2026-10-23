"""Retain the actual application tab across an abrupt private-host restart."""
import argparse, json, os, subprocess, tempfile, time
from pathlib import Path
from test_api_runtime import ROOT, write_site, free_port, wait_ready


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    for name in ('restart-request', 'restart-done'):
        (evidence / name).unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / '.build', prefix='terminal-restart-') as temporary:
        base = Path(temporary)
        port = free_port()
        config = write_site(base, port)
        environment = {key: value for key, value in os.environ.items() if key.lower() != 'psmodulepath'}
        with (evidence / 'host.log').open('wb') as log:
            def start():
                process = subprocess.Popen([str(args.host.resolve()), str(config), '--', '--home', str(base / 'home')],
                    cwd=base, env={**environment, 'USE_WEBVIEW': '0'}, stdout=log, stderr=log)
                wait_ready(port, process)
                return process
            host = start()
            ui = None
            try:
                ui = subprocess.Popen(['node', str(ROOT / 'tests/fixtures/workbench-terminal-restart.cjs'),
                    f'http://127.0.0.1:{port}', str(evidence)])
                restarted = False
                deadline = time.monotonic() + 120
                while ui.poll() is None and time.monotonic() < deadline:
                    if not restarted and (evidence / 'restart-request').exists():
                        # Abrupt stop affects only the host owned by this fixture.
                        host.kill()
                        host.wait(timeout=10)
                        host = start()
                        (evidence / 'restart-done').write_text('restarted', encoding='utf-8')
                        restarted = True
                    time.sleep(.1)
                if ui.poll() is None:
                    raise TimeoutError('Terminal restart frontend did not finish')
                if ui.returncode or not restarted:
                    raise RuntimeError('Terminal restart frontend failed')
                assert json.loads((evidence / 'result.json').read_text())['passed']
            finally:
                if ui and ui.poll() is None:
                    ui.terminate()
                    ui.wait(timeout=10)
                host.terminate()
                host.wait(timeout=10)
                time.sleep(1)


if __name__ == '__main__':
    main()
