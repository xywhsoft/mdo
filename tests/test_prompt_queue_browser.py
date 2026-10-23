"""Real DOM/pointer queue regression in isolated headless Chromium profiles."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
from test_sidebar_focus_browser import ROOT


def run(browser, width, fixture="prompt-queue-actions-browser.html"):
    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(ROOT)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix="mdo-queue-browser-") as profile:
            image = ROOT / f".build/queue-ui/{Path(fixture).stem}-{width}.png"
            image.parent.mkdir(exist_ok=True, parents=True)
            result = subprocess.run(["node", str(ROOT / "tests/queue_browser_driver.mjs"), str(browser), profile,
                f"http://127.0.0.1:{server.server_port}/tests/fixtures/{fixture}", str(width), str(image)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            assert result.returncode == 0, result.stderr.decode(errors="replace")[-2000:]
            return json.loads(result.stdout.decode("utf-8", errors="replace"))
    finally:
        server.shutdown(); server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", type=Path)
    args = parser.parse_args()
    browser = next(path for path in [args.browser, Path("C:/Program Files/Google/Chrome/Application/chrome.exe"),
        Path("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")] if path and path.is_file())
    reports = {f"{fixture}:{width}": run(browser, width, fixture)
        for fixture in ("prompt-queue-actions-browser.html", "queue-remove-focus-browser.html",
                        "queue-scroll-browser.html", "queue-session-lock-browser.html")
        for width in (1280, 520)}
    print(json.dumps(reports, ensure_ascii=False))
    assert all(report["passed"] for report in reports.values()), reports
