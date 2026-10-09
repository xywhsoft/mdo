"""Reuse real packed slow-continuity QA to inspect deferred locale persistence.

The printed /__qa/locale endpoint reads only native settings and served JS
hashes. It records no credentials and never changes locale or the write gate.
Create the printed stop file to save both normal fixture and locale evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path

import manual_service_read_qa as fixture
from test_api_runtime import request


class Proxy(fixture.Proxy):
    snapshots = []

    def proxy(self):
        if self.command == "GET" and self.path == "/__qa/locale":
            status, headers, raw = request(self.server.native, "GET", "/api/v1/settings")
            assert status == 200, raw
            data = json.loads(raw)["data"]
            sources = {}
            for name in ["js/state/settings.js", "js/api/client.js", "js/app.js"]:
                status, _, raw = request(self.server.native, "GET", "/" + name)
                assert status == 200, name
                sources[name] = hashlib.sha256(raw).hexdigest()
            with self.server.lock:
                writes = [row for row in self.server.writes if row["path"] == "/api/v1/settings/settings"]
                reads = list(self.server.service_reads)
            snapshot = {"locale": data["locale"], "etag": headers.get("etag"),
                "writes": writes, "continuity_reads": reads, "sources": sources}
            self.snapshots.append(snapshot)
            return self.reply(200, snapshot)
        return super().proxy()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--directory", type=Path, required=True)
    args, _ = parser.parse_known_args()
    fixture.Proxy = Proxy
    try: fixture.main()
    finally:
        if args.directory.exists():
            (args.directory / "locale-snapshots.json").write_text(
                json.dumps(Proxy.snapshots, indent=2), encoding="utf-8")
