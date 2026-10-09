"""Copied native fixture with the real page; serial UI recovery after editing.

POST /__probe/history clears and seeds an interrupted read, DELETE truncates
and seeds it, PATCH seeds a user stop. These routes and offline model callbacks
exist only in this disposable source fixture, never in product builds.
"""
import argparse
import json
import os
from pathlib import Path
import time

from test_recovery_history_runtime import prepare_fixture
from test_interrupt_runtime import start_host, stop_host


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--duration", type=int, default=600)
    args = parser.parse_args()
    base = args.directory.resolve()
    if base.exists():
        raise RuntimeError("Choose a new disposable directory")
    base.mkdir(parents=True)
    site, home, port, config = prepare_fixture(base)
    process = None
    try:
        process = start_host(args.host.resolve(), config, home, os.environ.copy(), base / "xs.log", port)
        url = f"http://127.0.0.1:{port}/#/projects/default/sessions/history-recovery"
        state = {"url": url, "port": port, "pid": process.pid,
                 "source_fixture": True, "provider_requests": 0}
        (base / "state.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
        print(url, flush=True)
        deadline = time.monotonic() + max(1, min(args.duration, 1800))
        while process.poll() is None and not (base / "stop").exists() and time.monotonic() < deadline:
            time.sleep(.2)
    finally:
        stop_host(process)


if __name__ == "__main__":
    main()
