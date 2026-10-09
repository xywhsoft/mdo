"""Finite packed editor trace while real draft reads wait for explicit release.

The loopback proxy observes original assignments and editor events without
changing setters or generating input. Start one cold page, compose while its
session draft GET is held, then release the response through the wrapper.
"""
import argparse
import json
from pathlib import Path
import threading
import time
from urllib.parse import parse_qs, urlsplit

import manual_input_origin_qa as input_fixture
from test_api_runtime import request


class Proxy(input_fixture.Proxy):
    records = []

    def proxy(self):
        path = urlsplit(self.path).path
        if self.command == "GET" and path == "/__qa/service":
            route = parse_qs(urlsplit(self.path).query)["route"][0]
            self.server.composer_path = "/api/v1/projects/default/sessions/" + route.rsplit("/", 1)[-1] + "/draft"
            self.server.composer_gate = threading.Event()
            self.server.composer_gate.set()
            text = Path(__file__).with_name("packed-composer-draft-browser.html").read_text(encoding="utf-8")
            return self.send_bytes(text.encode(), "text/html; charset=utf-8")
        if self.command == "GET" and path.startswith("/__qa/composer-arm/"):
            mode = path.rsplit("/", 1)[-1]
            assert mode in ("hold", "normal")
            if mode == "hold": self.server.composer_gate.clear()
            else: self.server.composer_gate.set()
            return self.reply(200, {"mode": mode})
        if self.command == "GET" and path == "/__qa/composer-release":
            self.server.composer_gate.set()
            return self.reply(200, {"released": True})
        if self.command == "GET" and path == "/__qa/composer-proof":
            status, _, raw = request(self.server.native, "GET", self.server.composer_path)
            assert status == 200
            return self.reply(200, {"reads": self.records, "draft": json.loads(raw)["data"]})
        if self.command == "GET" and path == getattr(self.server, "composer_path", None):
            record = {"started": time.monotonic(), "held": not self.server.composer_gate.is_set()}
            self.records.append(record)
            status, headers, raw = request(self.server.native, "GET", self.path)
            record["native_status"] = status
            # The product retains its eight-second attempt / minute budget.
            # Only these disposable HTTP bodies wait, never native mutations.
            while not self.server.composer_gate.wait(.1):
                if self.server.stopped.is_set() or time.monotonic() - record["started"] > 90: break
            record["released"] = time.monotonic()
            return self.send_bytes(raw, "application/json; charset=utf-8", headers, status)
        return super().proxy()

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--directory", type=Path, required=True)
    args, _ = parser.parse_known_args()
    input_fixture.fixture.Proxy = Proxy
    try: input_fixture.fixture.main()
    finally:
        if args.directory.exists():
            (args.directory / "composer-reads.json").write_text(json.dumps(Proxy.records, indent=2), encoding="utf-8")
