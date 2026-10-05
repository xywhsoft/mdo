#!/usr/bin/env python3
"""Bounded ordinary image delivery and lifecycle checks over HTTP and TLS.

A valid 2.36 MiB PNG and four composer thumbnails exercise a normal workflow,
not a stress or high-load test. No model, shell or live user Home is used.
Pause/deadline controls are injected into a temporary source copy only.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import time

from fixture_images import ordinary_png
from test_api_runtime import ROOT, free_port
from test_backup_download_runtime import replace

CONTROL = "/__fixture/image-download/"


class Probe:
    def __init__(self, host: Path, base: Path, secure: bool):
        self.host, self.base, self.secure = host, base, secure
        self.site, self.home = base / "site", base / "home"
        self.port = free_port()
        self.process, self.log = None, None
        shutil.copytree(ROOT / "app", self.site)
        shutil.copy2(ROOT / "tests/fixtures/image-download.c", self.site / "src/bootstrap/image-download.c")
        replace(self.site, "src/bootstrap/service.c", "void ServiceInit(XS_HostInfo* pHost)",
                '#include "image-download.c"\nvoid ServiceInit(XS_HostInfo* pHost)')
        replace(self.site, "src/bootstrap/service.c", "    (void)MdoApiInit();",
                "    (void)MdoApiInit();\n    ImageDownloadFixtureInit();")
        replace(self.site, "src/bootstrap/service.c",
                "    MdoBootstrapUnit();\n    MdoApiLiveRelease();",
                "    ImageDownloadFixtureUnit();\n    MdoBootstrapUnit();\n    MdoApiLiveRelease();")
        replace(self.site, "src/bootstrap/service.c", "    return MdoApiRequest(pRequest);",
                "    if ( ImageDownloadFixtureControl(pRequest) ) return XS_OK;\n"
                "    return MdoApiRequest(pRequest);")
        replace(self.site, "src/api/image_downloads.c", "#define MDO_IMAGE_DOWNLOAD_WORKERS 4u",
                "bool ImageDownloadFixturePause(unsigned, MdoApiContext*);\n"
                "#define MDO_IMAGE_DOWNLOAD_WORKERS 4u")
        replace(self.site, "src/api/image_downloads.c", "    Job->Context.SendCancel = Cancel;",
                "    Job->Context.SendCancel = Cancel;\n"
                "    if (!ImageDownloadFixturePause(1u, &Job->Context)) return XTASK_CANCELLED;")
        replace(self.site, "src/api/image_downloads.c",
                "    Job->Sent = MdoApiReplyImage(&Job->Context, Data, Bytes, Mime);",
                "    if (!ImageDownloadFixturePause(2u, &Job->Context)) { xrtFree(Data); return XTASK_CANCELLED; }\n"
                "    Job->Sent = MdoApiReplyImage(&Job->Context, Data, Bytes, Mime);")
        host_config = {"enabled": True, "name": "mdo", "path": "web", "devlang": "c",
                       "devfile": "generated/mdo_unity.c"}
        if secure:
            for kind in ("cert", "key"):
                shutil.copy2(ROOT / f"tests/fixtures/proxy-localhost-{kind}.pem", self.site / f"fixture-{kind}.pem")
            host_config.update(tls_cert="fixture-cert.pem", tls_key="fixture-key.pem")
        plain_port = free_port() if secure else self.port
        while secure and plain_port == self.port:
            plain_port = free_port()
        self.config = self.site / "xs.json"
        self.config.write_text(json.dumps({"engine": {"workers": 1}, "services": [{
            "enabled": True, "class": "http", "tls": secure, "name": "image-probe",
            "ip": "127.0.0.1", "ip_tls": "127.0.0.1", "port": plain_port,
            "port_tls": self.port if secure else 0, "host_default": host_config,
            "recv_limit": 8454144, "body_limit": 8388608,
        }]}), encoding="utf-8")

    def connection(self, small_window=False):
        if self.secure:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname, context.verify_mode = False, ssl.CERT_NONE  # loopback fixture only
            conn = http.client.HTTPSConnection("127.0.0.1", self.port, timeout=4, context=context)
        else:
            conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=4)
        if small_window:
            # Set the window before the TCP handshake. Changing SO_RCVBUF only
            # after connect leaves negotiated/autotuned windows platform-specific
            # and can let this entire ordinary image drain before observation.
            def connect_small(address, timeout, source_address=None):
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                try:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
                    sock.settimeout(timeout)
                    if source_address is not None:
                        sock.bind(source_address)
                    sock.connect(address)
                    return sock
                except BaseException:
                    sock.close()
                    raise
            conn._create_connection = connect_small
        conn.connect()
        return conn

    def call(self, method, path, body=None, headers=None):
        headers = dict(headers or {})
        if method not in ("GET", "HEAD", "OPTIONS") and path.startswith("/api/v1/"):
            headers["X-Mdo-Write-Token"] = self.call("GET", "/api/v1/bootstrap")[1]["x-mdo-write-token"]
        if isinstance(body, dict):
            body = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        with closing(self.connection()) as conn:
            conn.request(method, path, body=body, headers=headers)
            response = conn.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()

    def control(self, name):
        status, _, body = self.call("GET", CONTROL + name)
        assert status == 200, (status, body)
        return json.loads(body)["data"]

    def wait(self, predicate):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            value = self.control("state")
            if predicate(value):
                return value
            time.sleep(0.01)
        raise AssertionError(("fixture did not reach state", value))

    def inventory(self):
        return {str(p.relative_to(self.home)): p.read_bytes() for p in self.home.rglob("*")
                if p.is_file() and p != self.home / ".mdo.lock"}

    def start(self):
        self.log = (self.base / "xs.log").open("wb")
        self.process = subprocess.Popen([str(self.host), str(self.config)], cwd=self.site,
            env=dict(os.environ, MDO_HOME=str(self.home),
                     MDO_ORNITH_RESPONSES_URL="https://example.invalid/v1", MDO_ORNITH_API_KEY="image-fixture"),
            stdout=self.log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError((self.base / "xs.log").read_text(errors="replace"))
            try:
                status, _, body = self.call("GET", "/api/v1/bootstrap")
                if status == 200 and json.loads(body)["data"]["ready"]:
                    return
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        raise AssertionError((self.base / "xs.log").read_text(errors="replace"))

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.log:
            self.log.close()

    def exercise(self):
        payload = ordinary_png()
        assert len(payload) == 2361103
        digest = hashlib.sha256(payload).hexdigest()
        status, _, body = self.call("POST", "/api/v1/sessions", {
            "project_id": "default", "title": "Four composer thumbnails",
            "agent_id": "mdo.default", "model_id": "ornith-1.5-35b",
            "protocol": "openai-responses", "reasoning_effort": "medium", "max_output_tokens": 1024})
        assert status == 201, (status, body)
        session = json.loads(body)["data"]["id"]
        path = f"/api/v1/projects/default/sessions/{session}"
        urls = []
        for _ in range(4):
            status, _, body = self.call("POST", path + "/attachments", payload, {"Content-Type": "image/png"})
            assert status == 201, (status, body)
            urls.append(json.loads(body)["data"]["url"])
        before = self.inventory()

        def image(method, url):
            status, headers, data = self.call(method, url)
            assert status == 200, (status, data[:512])
            assert headers["content-type"] == "image/png" and headers["content-length"] == str(len(payload))
            assert headers["connection"] == "close" and headers["cache-control"] == "no-store"
            assert headers["x-content-type-options"] == "nosniff" and headers["x-request-id"]
            assert data == (b"" if method == "HEAD" else payload)
            if method == "GET":
                assert hashlib.sha256(data).hexdigest() == digest
            self.wait(lambda v: v["count"] == 0)

        image("GET", urls[0])
        image("HEAD", urls[0])
        assert self.inventory() == before
        # A normal composer can display four images together. Holding all four
        # before capture proves actual independent admission, not serial reads.
        self.control("hold-read")
        held = [self.connection() for _ in urls]
        try:
            for conn, url in zip(held, urls):
                conn.request("GET", url)
            state = self.wait(lambda v: v["entered"] == 4)
            assert state["count"] == 4 and state["storage_free"], state
            # Backup capture has its own quota and remains available.
            status, _, data = self.call("GET", path + "/backup")
            assert status == 200 and json.loads(data)["session_id"] == session, (status, data[:512])
            self.control("resume")
            for conn in held:
                response = conn.getresponse()
                assert response.status == 200 and response.read() == payload
        finally:
            for conn in held:
                conn.close()
        self.wait(lambda v: v["count"] == 0)
        before = self.inventory()  # backup checkpoint may legitimately rewrite its snapshot
        # Cancel while waiting on the real bounded transport, not only a pause.
        self.control("hold-send")
        held = self.connection(small_window=True)
        held.request("GET", urls[0])
        assert self.wait(lambda v: v["entered"] == 1)["storage_free"]
        assert self.control("small-socket")["small_socket"]
        self.control("resume")
        assert self.wait(lambda v: v["count"] == 1 and v["pending"] > 0)["storage_free"]
        start = time.monotonic()
        assert self.control("unit")["count"] == 0
        assert time.monotonic() - start < 3
        try:
            response = held.getresponse()
            assert len(response.read()) < len(payload)
        except (http.client.RemoteDisconnected, http.client.IncompleteRead,
                ConnectionResetError, ssl.SSLError):
            pass
        finally:
            held.close()
        image("GET", urls[0])
        # Peer close, deadline expiry, and Unit on the sole network worker must
        # all release their jobs and allow a later request without restarting.
        self.control("hold-send")
        held = self.connection()
        held.request("GET", urls[0])
        self.wait(lambda v: v["entered"] == 1)
        held.close()
        self.control("resume")
        self.wait(lambda v: v["count"] == 0)
        self.control("hold-send")
        self.control("short")
        held = self.connection()
        held.request("GET", urls[0])
        self.wait(lambda v: v["entered"] == 1)
        try:
            held.getresponse()
            raise AssertionError("expired image returned a response")
        except (http.client.RemoteDisconnected, ConnectionResetError):
            pass
        finally:
            held.close()
        self.wait(lambda v: v["count"] == 0)
        self.control("resume")
        self.control("hold-send")
        held = self.connection()
        held.request("GET", urls[0])
        self.wait(lambda v: v["entered"] == 1)
        start = time.monotonic()
        assert self.control("unit")["count"] == 0
        assert time.monotonic() - start < 3
        held.close()
        image("GET", urls[0])
        assert self.inventory() == before
        # The four current thumbnails plus a HEAD for one historical thumbnail
        # leave one metadata-only job queued. Unit must drop queued ownership as
        # well as cancel the four active readers, without capturing another body.
        self.control("hold-read")
        held = [self.connection() for _ in range(5)]
        try:
            for conn, url in zip(held[:4], urls):
                conn.request("GET", url)
            self.wait(lambda v: v["entered"] == 4)
            held[4].request("HEAD", urls[0])
            state = self.wait(lambda v: v["count"] == 5)
            assert state["entered"] == 4 and state["storage_free"], state
            assert self.control("unit")["count"] == 0
        finally:
            for conn in held:
                conn.close()
        image("GET", urls[0])
        assert self.inventory() == before
        assert self.call("GET", urls[0].rsplit("/", 1)[0] + "/" + "f" * 32)[0] == 404
        self.wait(lambda v: v["count"] == 0)
        # Capture has ended at the send pause: deleting an unreferenced image
        # must not corrupt the immutable response already owned by the job.
        self.control("hold-send")
        held = self.connection()
        held.request("GET", urls[3])
        self.wait(lambda v: v["entered"] == 1)
        assert self.control("state")["storage_free"]
        assert self.call("DELETE", urls[3])[0] == 200
        self.control("resume")
        assert held.getresponse().read() == payload
        held.close()
        self.wait(lambda v: v["count"] == 0)
        assert self.call("GET", urls[3])[0] == 404
        self.wait(lambda v: v["count"] == 0)
        assert self.process.poll() is None and self.call("GET", "/api/v1/bootstrap")[0] == 200
        print(f"image {'TLS' if self.secure else 'HTTP'}: valid PNG exact bytes/hash, HEAD, four thumbnails, separate backup quota, released capture, real wait cancel, peer close, deadline, active/queued Unit, immutable deleted-image response, retry PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="image-download-", dir=ROOT / ".build") as raw:
        for secure in (False, True):
            base = Path(raw) / ("tls" if secure else "http")
            probe = Probe(args.host.resolve(), base, secure)
            try:
                probe.start()
                probe.exercise()
            except BaseException:
                print((base / "xs.log").read_text(errors="replace"))
                raise
            finally:
                probe.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
