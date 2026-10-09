"""Bounded local publication and read-only memory/migration API regression.

No model calls, tool downloads or public network dependencies. Every Home is
an isolated fixture; existing user data and browser-cache bytes must survive.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site


class Catalog(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    revision = 1

    def log_message(self, *args: object) -> None:
        pass

    def do_GET(self) -> None:
        if not self.path.startswith("/mdo/catalog?"):
            self.send_error(404)
            return
        data = json.dumps({"revision": type(self).revision, "notices": [],
                           "toolpacks": []}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def stop(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def run(host: Path) -> list[dict]:
    publisher = ThreadingHTTPServer(("127.0.0.1", 0), Catalog)
    thread = threading.Thread(target=publisher.serve_forever, daemon=True)
    thread.start()
    observations = []
    try:
        for mode in ("fresh", "browser-cache", "inside-source", "user-data"):
            with tempfile.TemporaryDirectory(prefix="passive-home-", dir=ROOT / ".build") as raw:
                base = Path(raw)
                port = free_port()
                site = write_site(base, port)
                home = base / "home"
                source = base / ".mdo"
                source.mkdir()
                source_bytes = b'{"models":[],"settings":{"theme":"dark"}}'
                (source / "config.json").write_bytes(source_bytes)
                if mode == "inside-source":
                    home = source / "must-not-be-created"
                if mode == "browser-cache":
                    cache = home / "data/cache/webview2/cache.bin"
                    cache.parent.mkdir(parents=True)
                    cache.write_bytes(b"browser-owned-cache")
                if mode == "user-data":
                    home.mkdir()
                    (home / "user-note.txt").write_bytes(b"existing portable user data")
                unity = base / "generated/mdo_unity.c"
                text = unity.read_text(encoding="utf-8").replace(
                    '"http://127.0.0.1:9"', f'"http://127.0.0.1:{publisher.server_port}"')
                text = text.replace("#define MDO_DISTRIBUTION_PORT 9\n",
                    f"#define MDO_DISTRIBUTION_PORT {publisher.server_port}\n")
                unity.write_text(text, encoding="utf-8")
                env = {**os.environ, "USERPROFILE": str(base), "HOME": str(base),
                       "MDO_HOME": str(home)}
                log_path = base / "xs.log"
                Catalog.revision = 1
                with log_path.open("wb") as log:
                    process = subprocess.Popen([str(host), str(site)], cwd=base,
                        env=env, stdout=log, stderr=subprocess.STDOUT,
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                    try:
                        wait_ready(port, process)

                        def published(revision: int) -> dict:
                            deadline = time.monotonic() + 8
                            while time.monotonic() < deadline:
                                status, _, body = request(port, "GET", "/api/v1/distribution")
                                data = json.loads(body)["data"]
                                if status == 200 and data.get("revision") == revision and not data["busy"]:
                                    return data
                                time.sleep(.02)
                            raise AssertionError("local catalog was not published")

                        assert published(1)["stage"] == "complete"
                        Catalog.revision = 2
                        status, _, body = request(port, "POST", "/api/v1/distribution",
                            body=b'{"action":"check"}', headers={"Content-Type": "application/json"})
                        assert status == 200, (status, body)
                        assert published(2)["stage"] == "complete"

                        def preview() -> dict:
                            status, _, body = request(port, "GET", "/api/v1/migrations/legacy")
                            assert status == 200, body
                            return next(item for item in json.loads(body)["data"]["items"]
                                        if item["source_id"] == "user-home")

                        before = preview()
                        status, headers, body = request(port, "GET", "/api/v1/memory/global")
                        assert status == 200 and json.loads(body)["data"]["items"] == [], body
                        head_status, head_headers, head_body = request(port, "HEAD", "/api/v1/memory/global")
                        assert head_status == 200 and head_body == b"" and head_headers["etag"] == headers["etag"]
                        payload = b'{"id":"note","title":"Note","content":"stale","tags":[],"pinned":false}'
                        status, _, body = request(port, "PUT", "/api/v1/memory/global",
                            body=payload, headers={"Content-Type": "application/json"})
                        assert status == 428, body
                        status, _, body = request(port, "PUT", "/api/v1/memory/global",
                            body=payload, headers={"Content-Type": "application/json",
                                                  "If-Match": '"mdo-memory-0"'})
                        assert status == 412, body
                        after = preview()
                        assert before == after and after["valid"], (before, after)
                        assert after["file_count"] == 1 and after["unsupported_count"] == 0, after
                        if mode == "inside-source":
                            assert not after["importable"] and after["conflict_count"] == 1, after
                            status, _, body = request(port, "POST", "/api/v1/migrations/legacy",
                                body=json.dumps({"source_id": "user-home",
                                    "preview_token": after["preview_token"]}).encode(),
                                headers={"Content-Type": "application/json"})
                            assert status == 409 and not home.exists(), body
                        elif mode in ("fresh", "browser-cache"):
                            assert after["importable"], after
                        catalog_path = home / "data/notifications/catalog.json"
                        if mode == "user-data":
                            assert not after["importable"]
                            assert json.loads(catalog_path.read_bytes())["revision"] == 2
                            assert (home / "user-note.txt").read_bytes() == b"existing portable user data"
                        else:
                            assert not catalog_path.exists()
                        assert not (home / "memory").exists()
                        if mode == "browser-cache":
                            assert cache.read_bytes() == b"browser-owned-cache"
                            assert {str(f.relative_to(home)).replace("\\", "/")
                                for f in home.rglob("*") if f.is_file()} == {
                                    ".mdo.lock", "data/cache/webview2/cache.bin"}
                        if mode == "fresh":
                            assert not home.exists()
                        assert (source / "config.json").read_bytes() == source_bytes
                        observations.append({"mode": mode, "catalog_revision": 2,
                            "source_file_count": after["file_count"], "source_valid": after["valid"],
                            "importable": after["importable"], "catalog_persisted": catalog_path.exists(),
                            "memory_directory_created": False, "source_bytes_unchanged": True})
                    except BaseException as error:
                        log.flush()
                        raise RuntimeError(f"{mode}: {error}\n{log_path.read_text(encoding='utf-8', errors='replace')[-4000:]}") from error
                    finally:
                        stop(process)
    finally:
        publisher.shutdown()
        publisher.server_close()
        thread.join(timeout=3)
    return observations


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    result = run(args.host.resolve())
    if args.evidence:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("passive Home runtime: PASS (four local publication/read/migration cases)")


if __name__ == "__main__":
    main()
