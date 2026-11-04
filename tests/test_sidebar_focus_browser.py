"""Bounded real-DOM sidebar regression; only local public JS and fake sessions."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/session-list-focus-browser.html'
MODULE = ROOT / 'app/web/js/features/sessions/session-list.js'


def run(browser, baseline=False, width=1280):
    old = subprocess.check_output(['git', 'show', 'HEAD:app/web/js/features/sessions/session-list.js'], cwd=ROOT) if baseline else None

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_): pass

        def do_GET(self):
            path = (ROOT / urlsplit(self.path).path.lstrip('/')).resolve()
            if path != FIXTURE and path != ROOT / 'app/web/css/app.css' and not (path.is_relative_to(ROOT / 'app/web/js') and path.suffix == '.js'):
                self.send_error(404); return
            if old is not None and path == MODULE:
                self.send_response(200); self.send_header('Content-Type', 'text/javascript')
                self.send_header('Content-Length', str(len(old))); self.end_headers(); self.wfile.write(old)
            else: super().do_GET()

    server = ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=str(ROOT)))
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='mdo-sidebar-browser-') as profile:
            command = ['node', str(ROOT / 'tests/queue_browser_driver.mjs'), str(browser), profile,
                f'http://127.0.0.1:{server.server_port}/tests/fixtures/session-list-focus-browser.html',
                str(width), str(Path(profile) / 'sidebar.png')]
            process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            assert process.returncode == 0, 'headless browser exited unsuccessfully'
            return json.loads(process.stdout.decode('utf-8', errors='replace'))
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', type=Path)
    parser.add_argument('--baseline', action='store_true', help='serve the committed source to reproduce the reported bug')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--width', type=int, default=1280)
    args = parser.parse_args()
    candidates = [args.browser, Path('C:/Program Files/Google/Chrome/Application/chrome.exe'),
        Path('C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe')]
    browser = next((path for path in candidates if path and path.is_file()), None)
    if browser is None:
        executable = shutil.which('chromium') or shutil.which('google-chrome')
        if not executable: parser.error('a local Chrome, Edge or Chromium executable is required')
        browser = Path(executable)
    report = run(browser, args.baseline, args.width)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
    raise SystemExit(0 if report['passed'] else 1)
