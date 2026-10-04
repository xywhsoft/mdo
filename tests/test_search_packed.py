"""Search URL settings and legacy migration using embedded VFS only."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile
import time

from test_packed_home_lease import ROOT, release_packed_copies, site, start, stop, wait_bootstrap
from test_api_runtime import request


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path, default=ROOT / ("mdo.exe" if os.name == "nt" else "mdo"))
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()
    packed = args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix="search-packed-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        directory, port = site(base, "site", packed)
        home = base / "home"
        (home / "config").mkdir(parents=True)
        legacy = {"schema_version": 1, "patch": {"web": {"search": {
            "provider": "searxng", "endpoint": "http://127.0.0.1:8888/search",
            "secret_ref": "env:MDO_BRAVE_SEARCH_API_KEY", "max_results": 8,
        }}}}
        settings_file = home / "config/settings.json"
        original = json.dumps(legacy).encode()
        settings_file.write_bytes(original)
        process = start(directory, packed, home, dict(os.environ, USE_WEBVIEW="0"))
        try:
            assert wait_bootstrap(process, port, directory / "packed.log")[1]["data"]["ready"]
            status, headers, raw = request(port, "GET", "/api/v1/settings")
            assert status == 200
            expected = {"endpoint": "https://ai.xywhsoft.com/api/v1/search"}
            assert json.loads(raw)["data"]["web"] == expected
            assert settings_file.read_bytes() == original, "startup must not rewrite user settings"
            status, _, raw = request(port, "GET", "/")
            assert status == 200
            assert b'name="endpoint"' in raw
            assert b'name="search_provider"' not in raw
            assert b'id="search-credential-state"' not in raw
            assert b'name="max_results"' not in raw
            url = "http://127.0.0.1:9081/api/v1/search"
            status, _, raw = request(port, "PATCH", "/api/v1/settings/settings",
                body=json.dumps({"schema_version": 1, "patch": {"web": {"search": {"endpoint": url}}}}).encode(),
                headers={"Content-Type": "application/json", "If-Match": headers["etag"]})
            assert status == 200, raw
            status, _, raw = request(port, "GET", "/api/v1/settings")
            assert status == 200 and json.loads(raw)["data"]["web"] == {"endpoint": url}
            stored = json.loads(settings_file.read_text())
            assert stored["patch"]["web"]["search"] == {"endpoint": url}
            print("PASS packed VFS startup/legacy migration/address-only settings/save", flush=True)
            if args.preview:
                state = ROOT / ".build/search-ui.json"
                signal = ROOT / ".build/search-ui-stop"
                signal.unlink(missing_ok=True)
                state.write_text(json.dumps({"url": f"http://127.0.0.1:{port}/#/settings/web",
                                             "pid": process.pid, "home": str(home)}))
                print(f"PREVIEW http://127.0.0.1:{port}/#/settings/web", flush=True)
                deadline = time.monotonic() + 180
                while time.monotonic() < deadline and not signal.exists():
                    time.sleep(.1)
        finally:
            stop(process)
            release_packed_copies(directory / packed.name)


if __name__ == "__main__":
    main()
